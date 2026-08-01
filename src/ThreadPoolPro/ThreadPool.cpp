/**
 * @file ThreadPool.cpp
 * @brief ThreadPool implementation.
 *
 * Contains the implementation of ThreadPool's construction and
 * destruction, execution control, runtime statistics, state queries,
 * the worker loop, task retrieval, internal task submission, and
 * worker wakeup.
 */

// ============================================================
// Implementation for ThreadPoolPro::ThreadPool.
// ============================================================
//
//  Sections:
//   1. Static Thread State
//   2. Constructors & Destructor
//   3. Execution Control
//   4. Runtime Statistics
//   5. State Queries
//   6. Worker Execution
//   7. Task Retrieval
//   8. Internal Task Submission
//   9. Worker Wakeup
//
// ============================================================

// clang-format off
#include <ThreadPoolPro/ThreadPool.h> // ThreadPool — the class this file implements

#include <chrono>     // std::chrono::microseconds — the poll interval in the FinishTasks shutdown spin-wait
#include <cstdint>    // std::uint32_t — the wake token and the steal-victim PRNG state
#include <functional> // std::hash<std::thread::id> — seeds each worker's steal-victim PRNG
#include <stdexcept>  // std::runtime_error — thrown by submit() when the pool is already stopped
// clang-format on

namespace ThreadPoolPro {

namespace {

// ============================================================
//  Steal-victim randomization
// ============================================================
//
// Deterministically scanning victims in index+1, index+2, ... order
// means that whenever several workers run dry at roughly the same
// time (e.g. after a burst completes), they all probe the same
// low-index victim first, serializing what should be independent
// steal attempts on that victim's queue. A cheap thread-local xorshift
// PRNG picks a randomized starting offset per steal attempt instead,
// spreading contention across victims without needing a "real" random
// source or any synchronization.

thread_local std::uint32_t stealRngState = 0;

std::uint32_t nextStealOffset(std::uint32_t bound) noexcept {
    if (stealRngState == 0) {
        // Lazily seed from this thread's id so each worker's sequence
        // starts differently. The `| 1u` guards against a zero seed,
        // which would make xorshift32 degenerate (stuck at 0 forever).
        stealRngState =
            static_cast<std::uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())) |
            1u;
    }

    stealRngState ^= stealRngState << 13;
    stealRngState ^= stealRngState >> 17;
    stealRngState ^= stealRngState << 5;

    return stealRngState % bound;
}

} // namespace

// ============================================================
//  Section 1 — Static Thread State
// ============================================================

thread_local ThreadPool::Worker* ThreadPool::currentWorker_ = nullptr;
thread_local std::size_t ThreadPool::currentWorkerIndex_ = 0;
thread_local bool ThreadPool::selfDetachRequested_ = false;

// ============================================================
//  Section 2 — Constructors & Destructor
// ============================================================

ThreadPool::ThreadPool(std::size_t threadCount)
    : workerCount_{threadCount == 0 ? 1 : threadCount}, workers_(workerCount_)
      // Sized independently of workerCount_: shards are contended not just
      // by producers (who round-robin across them) but by every worker's
      // fetchTask() *and* any thread blocked in waitIdle() helping drain
      // (see fetchTaskExternal()) — with a small pool, that consumer-side
      // headcount alone can exceed a shard count tied 1:1 to workerCount_,
      // making shard-mutex contention the dominant cost for short tasks.
      ,
      injectionShardCount_{workerCount_ * 2 < 16 ? std::size_t{16} : workerCount_ * 2},
      injectionShards_(injectionShardCount_), injectionRoundRobin_{0}, wakeToken_{0},
      runState_{RunState::Running}, paused_{false}, activeTasks_{0}, exceptionCounter_{0},
      pendingTasks_{0}, idleWorkers_{0}, waitIdleWaiters_{0} {
    // Borrow already-running threads from the global market instead of
    // spawning new ones — see Detail/ThreadMarket.h. Only however many
    // threads the market's idle pool can't currently cover actually pay
    // OS thread-creation cost; every subsequent ThreadPool construction
    // reuses what's already warm.
    auto leased = Detail::ThreadMarket::instance().lease(workerCount_);

    for (std::size_t i = 0; i < workerCount_; ++i) {
        workers_[i].marketThread_ = leased[i];
        leased[i]->assign([this, i] { workerLoop(i); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

// ============================================================
//  Section 3 — Execution Control
// ============================================================

void ThreadPool::pause() noexcept {
    paused_.store(true, std::memory_order_release);
}

void ThreadPool::resume() noexcept {
    paused_.store(false, std::memory_order_release);
    wakeAll();
}

void ThreadPool::waitIdle() noexcept {
    waitIdleWaiters_.fetch_add(1, std::memory_order_relaxed);

    // Unlike a plain "block until predicate()" wait, this thread has
    // real work it could be doing: draining the very queue it's
    // waiting on. Sitting idle here would leave the pool's workers as
    // the only executors while this thread does nothing — one fewer
    // effective worker than if this thread pitched in, which matters
    // most for exactly the case that motivates waitIdle() (submit a
    // batch, then wait for it). So this thread helps drain first, and
    // only actually blocks once there's genuinely nothing left to grab.
    for (;;) {
        if (runState_.load(std::memory_order_acquire) != RunState::Running)
            break;

        if (pendingTasks_.load(std::memory_order_acquire) == 0 &&
            activeTasks_.load(std::memory_order_acquire) == 0)
            break;

        if (auto task = fetchTaskExternal()) {
            pendingTasks_.fetch_sub(1, std::memory_order_relaxed);
            activeTasks_.fetch_add(1, std::memory_order_relaxed);

            try {
                (*task)();
            } catch (...) {
                exceptionCounter_.fetch_add(1, std::memory_order_relaxed);
            }

            activeTasks_.fetch_sub(1, std::memory_order_relaxed);

            // A task finishing (whether run by this thread or a worker)
            // is exactly the event other waitIdle() callers care about.
            if (waitIdleWaiters_.load(std::memory_order_relaxed) > 1)
                wakeAll();

            continue;
        }

        // Nothing left to steal right now, but the pool isn't
        // necessarily idle yet — another worker may still be mid-task
        // and about to enqueue more. Wait for that to change (spin ->
        // yield -> park, see waitUntil()) rather than busy-looping
        // fetchTaskExternal() calls against empty queues.
        waitUntil([this] {
            return runState_.load(std::memory_order_acquire) != RunState::Running ||
                   (pendingTasks_.load(std::memory_order_acquire) == 0 &&
                    activeTasks_.load(std::memory_order_acquire) == 0);
        });
    }

    waitIdleWaiters_.fetch_sub(1, std::memory_order_relaxed);
}

void ThreadPool::shutdown(ShutdownMode mode) noexcept {
    RunState desired = (mode == ShutdownMode::FinishTasks) ? RunState::ShuttingDownFinish
                                                           : RunState::ShuttingDownDiscard;
    RunState expected = RunState::Running;

    // Single CAS on the combined run/shutdown state elects exactly one
    // caller as the "real" shutdown request — see the file-level
    // comment in ThreadPool.h for why this replaced a separate bool
    // flag plus a plain-enum mode (that pairing was a data race when
    // shutdown() was called concurrently from two threads).
    if (!runState_.compare_exchange_strong(expected, desired, std::memory_order_acq_rel))
        return;

    wakeAll();

    // A worker thread cannot join itself (it would deadlock, or
    // std::terminate depending on implementation). If shutdown() was
    // called from inside a running task, detach that one thread
    // instead — it will exit its workerLoop and clean itself up once
    // this call returns and the task finishes.
    const std::thread::id callerId = std::this_thread::get_id();

    for (auto& worker : workers_) {
        if (worker.marketThread_ == nullptr)
            continue;

        if (worker.marketThread_->id() == callerId) {
            // This worker's own task is what called shutdown(). We
            // can't block here waiting for our own job to finish — see
            // MarketThread::waitDone()'s doc comment — so just flag it
            // (workerLoop() checks this right after the in-flight task
            // returns and stops touching `this`) and stop tracking the
            // pointer. The MarketThread returns itself to the market's
            // idle pool on its own once workerLoop() returns — see
            // MarketThread::loop() — with no further action needed here.
            selfDetachRequested_ = true;
        } else {
            worker.marketThread_->waitDone();
        }

        worker.marketThread_ = nullptr;
    }
}

// ============================================================
//  Section 4 — Runtime Statistics
// ============================================================

std::size_t ThreadPool::activeTaskCount() const noexcept {
    return activeTasks_.load(std::memory_order_relaxed);
}

std::size_t ThreadPool::queuedTasks() const noexcept {
    return pendingTasks_.load(std::memory_order_relaxed);
}

std::size_t ThreadPool::threadCount() const noexcept {
    return workerCount_;
}

std::size_t ThreadPool::exceptionCount() const noexcept {
    return exceptionCounter_.load(std::memory_order_relaxed);
}

std::size_t ThreadPool::idleThreadCount() const noexcept {
    return idleWorkers_.load(std::memory_order_relaxed);
}

bool ThreadPool::empty() const noexcept {
    return pendingTasks_.load(std::memory_order_relaxed) == 0;
}

// ============================================================
//  Section 5 — State Queries
// ============================================================

bool ThreadPool::isPaused() const noexcept {
    return paused_.load(std::memory_order_relaxed);
}

bool ThreadPool::isStopped() const noexcept {
    return runState_.load(std::memory_order_relaxed) != RunState::Running;
}

// ============================================================
//  Section 6 — Worker Execution
// ============================================================

void ThreadPool::workerLoop(std::size_t index) {
    currentWorker_ = &workers_[index];
    currentWorkerIndex_ = index;

    // This thread may be a MarketThread reused from an earlier,
    // already-destroyed ThreadPool — reset state that was scoped to
    // "one thread's whole lifetime" under the old spawn-per-pool model
    // and is now scoped to "one job" instead.
    selfDetachRequested_ = false;

    while (true) {
        if (paused_.load(std::memory_order_acquire) &&
            runState_.load(std::memory_order_acquire) == RunState::Running) {
            idleWorkers_.fetch_add(1, std::memory_order_relaxed);

            waitUntil([this] {
                return runState_.load(std::memory_order_acquire) != RunState::Running ||
                       !paused_.load(std::memory_order_acquire);
            });

            idleWorkers_.fetch_sub(1, std::memory_order_relaxed);

            continue;
        }

        // Cheap early-exit *before* paying for fetchTask()'s full
        // local-pop + steal-from-every-worker + scan-every-injection-
        // shard sequence. submit() refuses new work once shutdown() has
        // run, so once we're shutting down in Discard mode, or in
        // FinishTasks mode with nothing left queued, fetchTask() is
        // guaranteed to come back empty — it just wouldn't know that
        // without doing the whole scan first. Checking here turns a
        // shutdown wake-up that finds no work into an O(1) check
        // instead of an O(workerCount_ + injectionShardCount_) one.
        // This is the dominant cost of construct/destroy with no
        // submitted tasks: every worker was paying for that scan, on
        // every shutdown, purely to reach a conclusion this check
        // already knows.
        {
            RunState earlyState = runState_.load(std::memory_order_acquire);

            if (earlyState == RunState::ShuttingDownDiscard)
                return;

            if (earlyState == RunState::ShuttingDownFinish &&
                pendingTasks_.load(std::memory_order_acquire) == 0)
                return;
        }

        if (auto task = fetchTask(index)) {

            // pause() only takes effect at the top of the loop, so a
            // pause request can land in the small window between that
            // check and fetchTask() returning a task. Re-check here,
            // right before committing to execute — if a pause slipped
            // in, put the task back instead of running it, so pause()
            // reliably blocks new task execution rather than only
            // "usually" doing so.
            if (paused_.load(std::memory_order_acquire) &&
                runState_.load(std::memory_order_acquire) == RunState::Running) {
                currentWorker_->queue_.pushBottom(std::move(*task));
                continue;
            }

            pendingTasks_.fetch_sub(1, std::memory_order_relaxed);
            activeTasks_.fetch_add(1, std::memory_order_relaxed);

            bool threw = false;

            try {
                (*task)();
            } catch (...) {
                threw = true;
            }

            // If this task itself called shutdown() on this pool (and
            // this is the worker running it), the caller may destroy the
            // pool as soon as the task's result is observable (e.g. its
            // future). Stop touching `this` right here — before even
            // the exception counter — rather than risk a use-after-free
            // on a pool that may no longer exist.
            if (selfDetachRequested_)
                return;

            if (threw)
                exceptionCounter_.fetch_add(1, std::memory_order_relaxed);

            activeTasks_.fetch_sub(1, std::memory_order_relaxed);

            // Only pay for a notify if someone is actually blocked in
            // waitIdle() — an idle worker looking for new work has no
            // reason to care that a task merely *finished* (that event
            // creates no new work), so gating on idleWorkers_ too would
            // just be waking the wrong audience for no benefit.
            if (waitIdleWaiters_.load(std::memory_order_relaxed) != 0)
                wakeAll();

            continue;
        }

        RunState rs = runState_.load(std::memory_order_acquire);

        if (rs != RunState::Running) {

            if (rs == RunState::ShuttingDownDiscard)
                return;

            if (pendingTasks_.load(std::memory_order_acquire) == 0)
                return;

            // FinishTasks shutdown with no local/stealable work left but
            // pendingTasks_ still nonzero: another worker is still
            // executing a task that may itself enqueue more work (or
            // simply hasn't decremented pendingTasks_ yet). Poll rather
            // than block indefinitely, since submit() is disabled during
            // shutdown so no future wakeToken_ bump is coming for
            // "new work arrived" — only for "another worker finished".
            std::this_thread::sleep_for(std::chrono::microseconds(50));

            continue;
        }

        idleWorkers_.fetch_add(1, std::memory_order_relaxed);

        waitUntil([this] {
            return runState_.load(std::memory_order_acquire) != RunState::Running ||
                   paused_.load(std::memory_order_acquire) ||
                   pendingTasks_.load(std::memory_order_relaxed) != 0;
        });

        idleWorkers_.fetch_sub(1, std::memory_order_relaxed);
    }
}

// ============================================================
//  Section 7 — Task Retrieval
// ============================================================

std::optional<ThreadPool::Task> ThreadPool::fetchTask(std::size_t index) {
    Worker& self = workers_[index];

    if (auto task = self.queue_.popBottom())
        return task;

    if (workerCount_ > 1) {
        // Randomized starting offset so concurrently-idle workers don't
        // all probe the same victim first — see nextStealOffset().
        std::size_t offset = 1 + nextStealOffset(static_cast<std::uint32_t>(workerCount_ - 1));

        for (std::size_t i = 0; i < workerCount_ - 1; ++i) {
            std::size_t victim = (index + offset + i) % workerCount_;
            Detail::WorkStealingQueue& victimQueue = workers_[victim].queue_;

            // Cheap relaxed pre-check before paying for steal()'s full
            // seq_cst fence + CAS. Mirrors the same fast-skip already
            // used for injection shards below. This matters most for
            // workloads where local queues are rarely populated (e.g.
            // all work arrives via detach()/enqueue() from outside any
            // worker, never from a task recursively submitting more
            // work) — every idle worker would otherwise pay this
            // fence workerCount_-1 times on every single failed fetch,
            // which is exactly what made cost scale badly with worker
            // count even though there was never anything to steal.
            if (victimQueue.size() == 0)
                continue;

            if (auto task = victimQueue.steal())
                return task;
        }
    }

    // Scan the injection shards starting at a rotating offset (keyed off
    // this worker's own index, so different workers don't all start at
    // shard 0 together) and skip locking any shard the lock-free size_
    // check already shows as empty — the common case when the injection
    // queues are mostly drained, which is exactly when this scan runs
    // most often.
    for (std::size_t i = 0; i < injectionShardCount_; ++i) {
        InjectionShard& shard = injectionShards_[(index + i) % injectionShardCount_];

        if (shard.size_.load(std::memory_order_acquire) == 0)
            continue;

        std::lock_guard lock(shard.mutex_);

        if (!shard.queue_.empty()) {
            Task task(std::move(shard.queue_.front()));
            shard.queue_.pop_front();
            shard.size_.fetch_sub(1, std::memory_order_relaxed);

            return task;
        }
    }

    return std::nullopt;
}

std::optional<ThreadPool::Task> ThreadPool::fetchTaskExternal() {
    if (workerCount_ > 0) {
        std::size_t offset = nextStealOffset(static_cast<std::uint32_t>(workerCount_));

        for (std::size_t i = 0; i < workerCount_; ++i) {
            std::size_t victim = (offset + i) % workerCount_;
            Detail::WorkStealingQueue& victimQueue = workers_[victim].queue_;

            if (victimQueue.size() == 0)
                continue;

            if (auto task = victimQueue.steal())
                return task;
        }
    }

    for (std::size_t i = 0; i < injectionShardCount_; ++i) {
        InjectionShard& shard = injectionShards_[i];

        if (shard.size_.load(std::memory_order_acquire) == 0)
            continue;

        std::lock_guard lock(shard.mutex_);

        if (!shard.queue_.empty()) {
            Task task(std::move(shard.queue_.front()));
            shard.queue_.pop_front();
            shard.size_.fetch_sub(1, std::memory_order_relaxed);

            return task;
        }
    }

    return std::nullopt;
}

// ============================================================
//  Section 8 — Internal Task Submission
// ============================================================

void ThreadPool::submit(Task&& task) {
    if (runState_.load(std::memory_order_acquire) != RunState::Running)
        throw std::runtime_error("submit on stopped ThreadPool");

    if (currentWorker_ != nullptr) {
        currentWorker_->queue_.pushBottom(std::move(task));
    } else {
        // Round-robin across shards so that even a single external
        // producer thread spreads its submissions across multiple
        // locks instead of funneling everything through one — the
        // scenario that made the single-queue version's contention
        // scale badly with worker count.
        std::size_t shardIndex =
            injectionRoundRobin_.fetch_add(1, std::memory_order_relaxed) % injectionShardCount_;
        InjectionShard& shard = injectionShards_[shardIndex];

        std::lock_guard<std::mutex> lock(shard.mutex_);
        shard.queue_.push_back(std::move(task));
        shard.size_.fetch_add(1, std::memory_order_relaxed);
    }

    pendingTasks_.fetch_add(1, std::memory_order_release);

    // Only pay for a notify if a worker is actually idle right now.
    // Safe to skip otherwise: any worker about to go idle re-checks
    // pendingTasks_ directly (see waitUntil()'s double-check pattern)
    // before it can actually block, so it can't miss a task that was
    // already counted in pendingTasks_ by the time it checks — and if a
    // worker is already blocked, it must have incremented idleWorkers_
    // strictly before blocking, so this check will see it.
    if (idleWorkers_.load(std::memory_order_acquire) != 0)
        wakeOne();
}

// ============================================================
//  Section 9 — Worker Wakeup
// ============================================================

void ThreadPool::wakeOne() noexcept {
    wakeToken_.fetch_add(1, std::memory_order_release);
    wakeToken_.notify_one();
}

void ThreadPool::wakeAll() noexcept {
    wakeToken_.fetch_add(1, std::memory_order_release);
    wakeToken_.notify_all();
}

} // namespace ThreadPoolPro

