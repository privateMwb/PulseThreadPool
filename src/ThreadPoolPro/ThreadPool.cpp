/**
 * @file ThreadPool.cpp
 * @brief ThreadPool implementation.
 */

#include <ThreadPoolPro/ThreadPool.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>

namespace ThreadPoolPro {

namespace {

thread_local std::uint32_t stealRngState = 0;

inline std::uint32_t nextRandom() noexcept {
    if (stealRngState == 0) {
        stealRngState =
            static_cast<std::uint32_t>(
                std::hash<std::thread::id>{}(
                    std::this_thread::get_id()))
            | 1u;
    }

    stealRngState ^= stealRngState << 13;
    stealRngState ^= stealRngState >> 17;
    stealRngState ^= stealRngState << 5;

    return stealRngState;
}

inline std::size_t randomIndex(std::size_t count) noexcept {
    return count == 0
        ? 0
        : static_cast<std::size_t>(nextRandom() % count);
}

} // namespace


thread_local ThreadPool::Worker*
    ThreadPool::currentWorker_ = nullptr;

thread_local std::size_t
    ThreadPool::currentWorkerIndex_ = 0;

thread_local bool
    ThreadPool::selfDetachRequested_ = false;


// ============================================================
// Constructor / Destructor
// ============================================================

ThreadPool::ThreadPool(std::size_t threadCount)
    : workerCount_(
          threadCount == 0
              ? std::size_t{1}
              : threadCount)
    , workers_(workerCount_)
    /*
     * One shard is enough for one worker.
     *
     * For multiple workers, use 2 shards per worker but cap the
     * number so construction doesn't become disproportionately
     * expensive for large pools.
     */
    , injectionShardCount_(
          workerCount_ == 1
              ? std::size_t{1}
              : std::min<std::size_t>(
                    workerCount_ * 2,
                    64))
    , injectionShards_(injectionShardCount_) {

    auto leased =
        Detail::ThreadMarket::instance()
            .lease(workerCount_);

    for (std::size_t i = 0;
         i < workerCount_;
         ++i) {

        workers_[i].marketThread_ = leased[i];

        leased[i]->assign(
            [this, i] {
                workerLoop(i);
            });
    }
}


ThreadPool::~ThreadPool() {
    shutdown();
}


// ============================================================
// Execution Control
// ============================================================

void ThreadPool::pause() noexcept {
    paused_.store(
        true,
        std::memory_order_release);

    wakeAll();
}


void ThreadPool::resume() noexcept {
    paused_.store(
        false,
        std::memory_order_release);

    wakeAll();
}


// ============================================================
// waitIdle
// ============================================================

void ThreadPool::waitIdle() noexcept {

    waitIdleWaiters_.fetch_add(
        1,
        std::memory_order_relaxed);

    /*
     * External waiters help, but only opportunistically.
     *
     * The previous implementation could repeatedly perform:
     *
     *   workerCount steals
     *   injectionShardCount locks
     *
     * on every failed attempt.
     *
     * That creates a second work-stealing system competing with
     * the actual workers.
     */
    std::size_t helpAttempts = 0;

    for (;;) {

        if (runState_.load(
                std::memory_order_acquire)
            != RunState::Running) {
            break;
        }

        if (pendingTasks_.load(
                std::memory_order_acquire)
                == 0
            &&
            activeTasks_.load(
                std::memory_order_acquire)
                == 0) {
            break;
        }

        if (helpAttempts < MaxExternalHelpAttempts) {

            if (auto task = fetchTaskExternal()) {

                pendingTasks_.fetch_sub(
                    1,
                    std::memory_order_relaxed);

                activeTasks_.fetch_add(
                    1,
                    std::memory_order_relaxed);

                try {
                    (*task)();
                } catch (...) {
                    exceptionCounter_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                }

                activeTasks_.fetch_sub(
                    1,
                    std::memory_order_relaxed);

                ++helpAttempts;

                if (waitIdleWaiters_.load(
                        std::memory_order_relaxed)
                    > 1) {
                    wakeAll();
                }

                continue;
            }

            ++helpAttempts;
        }

        helpAttempts = 0;

        waitUntil([this] {

            return
                runState_.load(
                    std::memory_order_acquire)
                    != RunState::Running

                ||

                (
                    pendingTasks_.load(
                        std::memory_order_acquire)
                        == 0

                    &&

                    activeTasks_.load(
                        std::memory_order_acquire)
                        == 0
                );
        });
    }

    waitIdleWaiters_.fetch_sub(
        1,
        std::memory_order_relaxed);
}


// ============================================================
// Shutdown
// ============================================================

void ThreadPool::shutdown(
    ShutdownMode mode) noexcept {

    const RunState desired =
        mode == ShutdownMode::FinishTasks
            ? RunState::ShuttingDownFinish
            : RunState::ShuttingDownDiscard;

    RunState expected =
        RunState::Running;

    if (!runState_.compare_exchange_strong(
            expected,
            desired,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return;
    }

    wakeAll();

    const std::thread::id callerId =
        std::this_thread::get_id();

    for (auto& worker : workers_) {

        auto* thread =
            worker.marketThread_;

        if (thread == nullptr)
            continue;

        if (thread->id() == callerId) {

            selfDetachRequested_ = true;

        } else {

            thread->waitDone();
        }

        worker.marketThread_ = nullptr;
    }
}


// ============================================================
// Statistics
// ============================================================

std::size_t
ThreadPool::activeTaskCount() const noexcept {
    return activeTasks_.load(
        std::memory_order_relaxed);
}


std::size_t
ThreadPool::queuedTasks() const noexcept {
    return pendingTasks_.load(
        std::memory_order_relaxed);
}


std::size_t
ThreadPool::threadCount() const noexcept {
    return workerCount_;
}


std::size_t
ThreadPool::exceptionCount() const noexcept {
    return exceptionCounter_.load(
        std::memory_order_relaxed);
}


std::size_t
ThreadPool::idleThreadCount() const noexcept {
    return idleWorkers_.load(
        std::memory_order_relaxed);
}


bool ThreadPool::empty() const noexcept {
    return pendingTasks_.load(
        std::memory_order_relaxed) == 0;
}


// ============================================================
// State Queries
// ============================================================

bool ThreadPool::isPaused() const noexcept {
    return paused_.load(
        std::memory_order_relaxed);
}


bool ThreadPool::isStopped() const noexcept {
    return runState_.load(
        std::memory_order_relaxed)
        != RunState::Running;
}


// ============================================================
// Worker Loop
// ============================================================

void ThreadPool::workerLoop(
    std::size_t index) {

    currentWorker_ =
        &workers_[index];

    currentWorkerIndex_ =
        index;

    selfDetachRequested_ =
        false;

    for (;;) {

        const RunState state =
            runState_.load(
                std::memory_order_acquire);

        if (state ==
            RunState::ShuttingDownDiscard) {
            return;
        }

        if (state ==
                RunState::ShuttingDownFinish
            &&
            pendingTasks_.load(
                std::memory_order_acquire)
                == 0) {
            return;
        }

        if (paused_.load(
                std::memory_order_acquire)
            &&
            state ==
                RunState::Running) {

            idleWorkers_.fetch_add(
                1,
                std::memory_order_relaxed);

            waitUntil([this] {

                return
                    runState_.load(
                        std::memory_order_acquire)
                        != RunState::Running

                    ||

                    !paused_.load(
                        std::memory_order_acquire);
            });

            idleWorkers_.fetch_sub(
                1,
                std::memory_order_relaxed);

            continue;
        }


        if (auto task =
                fetchTask(index)) {

            if (paused_.load(
                    std::memory_order_acquire)
                &&
                runState_.load(
                    std::memory_order_acquire)
                    == RunState::Running) {

                currentWorker_
                    ->queue_
                    .pushBottom(
                        std::move(*task));

                continue;
            }

            pendingTasks_.fetch_sub(
                1,
                std::memory_order_relaxed);

            activeTasks_.fetch_add(
                1,
                std::memory_order_relaxed);

            bool threw = false;

            try {
                (*task)();
            } catch (...) {
                threw = true;
            }

            /*
             * A worker called shutdown() from its task.
             *
             * Do not access the pool after this point.
             */
            if (selfDetachRequested_) {
                return;
            }

            if (threw) {
                exceptionCounter_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }

            activeTasks_.fetch_sub(
                1,
                std::memory_order_relaxed);

            if (waitIdleWaiters_.load(
                    std::memory_order_relaxed)
                != 0) {

                wakeAll();
            }

            continue;
        }


        /*
         * FinishTasks:
         *
         * pendingTasks_ may still be non-zero because another
         * worker is executing a task.
         */
        if (state ==
            RunState::ShuttingDownFinish) {

            if (pendingTasks_.load(
                    std::memory_order_acquire)
                == 0) {
                return;
            }

            std::this_thread::yield();

            continue;
        }


        idleWorkers_.fetch_add(
            1,
            std::memory_order_relaxed);

        waitUntil([this] {

            return
                runState_.load(
                    std::memory_order_acquire)
                    != RunState::Running

                ||

                paused_.load(
                    std::memory_order_acquire)

                ||

                pendingTasks_.load(
                    std::memory_order_acquire)
                    != 0;
        });

        idleWorkers_.fetch_sub(
            1,
            std::memory_order_relaxed);
    }
}


// ============================================================
// Worker Task Retrieval
// ============================================================

std::optional<ThreadPool::Task>
ThreadPool::fetchTask(
    std::size_t index) {

    Worker& self =
        workers_[index];


    /*
     * Fastest possible path.
     *
     * Tasks submitted recursively from a worker are always
     * available here.
     */
    if (auto task =
            self.queue_.popBottom()) {
        return task;
    }


    /*
     * With one worker there is nothing to steal.
     *
     * Go directly to the single injection queue.
     */
    if (workerCount_ == 1) {

        InjectionShard& shard =
            injectionShards_[0];

        if (shard.size_.load(
                std::memory_order_acquire)
            != 0) {

            std::lock_guard lock(
                shard.mutex_);

            if (!shard.queue_.empty()) {

                Task task(
                    std::move(
                        shard.queue_.front()));

                shard.queue_.pop_front();

                shard.size_.fetch_sub(
                    1,
                    std::memory_order_relaxed);

                return task;
            }
        }

        return std::nullopt;
    }


    /*
     * Bounded stealing.
     *
     * Never scan the entire worker set on every failed fetch.
     */
    const std::size_t probes =
        std::min(
            MaxStealProbes,
            workerCount_ - 1);

    const std::size_t start =
        randomIndex(workerCount_ - 1);

    for (std::size_t i = 0;
         i < probes;
         ++i) {

        std::size_t victim =
            (index + 1 +
             start + i)
            % workerCount_;

        WorkQueue& queue =
            workers_[victim].queue_;

        if (queue.size() == 0)
            continue;

        if (auto task =
                queue.steal()) {
            return task;
        }
    }


    /*
     * Bounded injection scanning.
     */
    const std::size_t shardStart =
        injectionRoundRobin_.fetch_add(
            1,
            std::memory_order_relaxed)
        % injectionShardCount_;

    const std::size_t probesInjection =
        std::min(
            MaxInjectionProbes,
            injectionShardCount_);

    for (std::size_t i = 0;
         i < probesInjection;
         ++i) {

        InjectionShard& shard =
            injectionShards_[
                (shardStart + i)
                % injectionShardCount_];

        if (shard.size_.load(
                std::memory_order_acquire)
            == 0) {
            continue;
        }

        std::lock_guard lock(
            shard.mutex_);

        if (shard.queue_.empty())
            continue;

        Task task(
            std::move(
                shard.queue_.front()));

        shard.queue_.pop_front();

        shard.size_.fetch_sub(
            1,
            std::memory_order_relaxed);

        return task;
    }

    return std::nullopt;
}


// ============================================================
// External Task Retrieval
// ============================================================

std::optional<ThreadPool::Task>
ThreadPool::fetchTaskExternal() {

    /*
     * External helper has no local queue.
     *
     * Limit the number of workers inspected.
     */
    const std::size_t probes =
        std::min(
            MaxStealProbes,
            workerCount_);

    const std::size_t start =
        randomIndex(workerCount_);

    for (std::size_t i = 0;
         i < probes;
         ++i) {

        WorkQueue& queue =
            workers_[
                (start + i)
                % workerCount_]
                .queue_;

        if (queue.size() == 0)
            continue;

        if (auto task =
                queue.steal()) {
            return task;
        }
    }


    /*
     * Check only a bounded number of
     * injection queues.
     */
    const std::size_t startShard =
        injectionRoundRobin_.fetch_add(
            1,
            std::memory_order_relaxed)
        % injectionShardCount_;

    const std::size_t probesInjection =
        std::min(
            MaxInjectionProbes,
            injectionShardCount_);

    for (std::size_t i = 0;
         i < probesInjection;
         ++i) {

        InjectionShard& shard =
            injectionShards_[
                (startShard + i)
                % injectionShardCount_];

        if (shard.size_.load(
                std::memory_order_acquire)
            == 0) {
            continue;
        }

        std::lock_guard lock(
            shard.mutex_);

        if (shard.queue_.empty())
            continue;

        Task task(
            std::move(
                shard.queue_.front()));

        shard.queue_.pop_front();

        shard.size_.fetch_sub(
            1,
            std::memory_order_relaxed);

        return task;
    }

    return std::nullopt;
}


// ============================================================
// Wakeup
// ============================================================

void ThreadPool::wakeOne() noexcept {

    wakeToken_.fetch_add(
        1,
        std::memory_order_release);

    wakeToken_.notify_one();
}


void ThreadPool::wakeAll() noexcept {

    wakeToken_.fetch_add(
        1,
        std::memory_order_release);

    wakeToken_.notify_all();
}

} // namespace ThreadPoolPro

namespace rain = ThreadPoolPro;