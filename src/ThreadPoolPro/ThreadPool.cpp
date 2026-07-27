/**
 * @file ThreadPool.cpp
 * @brief ThreadPool implementation.
 */

#include <ThreadPoolPro/ThreadPool.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <thread>

namespace ThreadPoolPro {

namespace {

thread_local std::uint32_t stealRngState = 0;

std::uint32_t nextStealRandom() noexcept {
    if (stealRngState == 0) {
        stealRngState =
            static_cast<std::uint32_t>(
                std::hash<std::thread::id>{}(
                    std::this_thread::get_id())) |
            1u;
    }

    stealRngState ^= stealRngState << 13;
    stealRngState ^= stealRngState >> 17;
    stealRngState ^= stealRngState << 5;

    return stealRngState;
}

std::size_t randomBelow(std::size_t bound) noexcept {
    if (bound <= 1)
        return 0;

    return static_cast<std::size_t>(
        nextStealRandom() % static_cast<std::uint32_t>(bound));
}

} // namespace


// ============================================================
// Static Thread State
// ============================================================

thread_local ThreadPool::Worker*
    ThreadPool::currentWorker_ = nullptr;

thread_local std::size_t
    ThreadPool::currentWorkerIndex_ = 0;

thread_local bool
    ThreadPool::selfDetachRequested_ = false;


// ============================================================
// Constructors & Destructor
// ============================================================

ThreadPool::ThreadPool(std::size_t threadCount)
    : workerCount_{
          threadCount == 0 ? std::size_t{1} : threadCount}
    , workers_(workerCount_)
    , injectionShardCount_{
          std::clamp(
              workerCount_,
              MinInjectionShards,
              MaxInjectionShards)}
    , injectionShards_(injectionShardCount_)
    , injectionRoundRobin_{0}
    , injectionTasks_{0}
    , pendingTasks_{0}
    , wakeToken_{0}
    , runState_{RunState::Running}
    , paused_{false}
    , activeTasks_{0}
    , exceptionCounter_{0}
    , idleWorkers_{0}
    , waitIdleWaiters_{0}
{
    auto leased =
        Detail::ThreadMarket::instance().lease(workerCount_);

    for (std::size_t i = 0; i < workerCount_; ++i) {
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

    for (;;) {
        RunState state =
            runState_.load(std::memory_order_acquire);

        if (state != RunState::Running)
            break;

        if (pendingTasks_.load(
                std::memory_order_acquire) == 0 &&
            activeTasks_.load(
                std::memory_order_acquire) == 0) {
            break;
        }

        /*
         * Help execute work.

         * This is intentionally retained because it improves latency
         * for the common:
         *
         *     submit batch
         *     waitIdle()
         *
         * pattern.
         */
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

            /*
             * A task completion can make the wait condition true.
             * Only wake other waiters; the current waiter is already
             * executing.
             */
            if (waitIdleWaiters_.load(
                    std::memory_order_relaxed) > 1) {
                wakeAll();
            }

            continue;
        }

        waitUntil(
            [this] {
                return
                    runState_.load(
                        std::memory_order_acquire) !=
                        RunState::Running ||

                    (
                        pendingTasks_.load(
                            std::memory_order_acquire) == 0 &&

                        activeTasks_.load(
                            std::memory_order_acquire) == 0
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

    RunState desired =
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
        if (worker.marketThread_ == nullptr)
            continue;

        if (worker.marketThread_->id() == callerId) {
            selfDetachRequested_ = true;
        } else {
            worker.marketThread_->waitDone();
        }

        worker.marketThread_ = nullptr;
    }
}


// ============================================================
// Runtime Statistics
// ============================================================

std::size_t ThreadPool::activeTaskCount() const noexcept {
    return activeTasks_.load(
        std::memory_order_relaxed);
}


std::size_t ThreadPool::queuedTasks() const noexcept {
    return pendingTasks_.load(
        std::memory_order_relaxed);
}


std::size_t ThreadPool::threadCount() const noexcept {
    return workerCount_;
}


std::size_t ThreadPool::exceptionCount() const noexcept {
    return exceptionCounter_.load(
        std::memory_order_relaxed);
}


std::size_t ThreadPool::idleThreadCount() const noexcept {
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
        std::memory_order_relaxed) !=
        RunState::Running;
}


// ============================================================
// Worker Execution
// ============================================================

void ThreadPool::workerLoop(
    std::size_t index) {

    currentWorker_ =
        &workers_[index];

    currentWorkerIndex_ =
        index;

    selfDetachRequested_ =
        false;

    std::size_t failedStealRounds = 0;

    while (true) {

        RunState state =
            runState_.load(
                std::memory_order_acquire);

        if (state ==
            RunState::ShuttingDownDiscard) {
            return;
        }

        if (state ==
                RunState::ShuttingDownFinish &&
            pendingTasks_.load(
                std::memory_order_acquire) == 0) {
            return;
        }


        if (paused_.load(
                std::memory_order_acquire) &&
            state == RunState::Running) {

            idleWorkers_.fetch_add(
                1,
                std::memory_order_relaxed);

            waitUntil(
                [this] {
                    return
                        runState_.load(
                            std::memory_order_acquire) !=
                            RunState::Running ||

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

            failedStealRounds = 0;

            if (paused_.load(
                    std::memory_order_acquire) &&
                runState_.load(
                    std::memory_order_acquire) ==
                    RunState::Running) {

                currentWorker_->queue_.pushBottom(
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
                    std::memory_order_relaxed) != 0) {
                wakeAll();
            }

            continue;
        }


        state =
            runState_.load(
                std::memory_order_acquire);


        if (state !=
            RunState::Running) {

            if (state ==
                RunState::ShuttingDownDiscard) {
                return;
            }

            if (pendingTasks_.load(
                    std::memory_order_acquire) == 0) {
                return;
            }

            /*
             * FinishTasks:
             *
             * A task may still be executing and may submit another
             * task. Because submit() is disabled once shutdown starts,
             * no new external producer can add work.
             *
             * Use a short yield rather than a fixed 50us sleep.
             * This avoids unnecessarily delaying shutdown for tiny
             * tasks.
             */
            std::this_thread::yield();

            continue;
        }


        ++failedStealRounds;

        /*
         * After repeated failed rounds, stop hammering the queues.
         * waitUntil() provides spin -> yield -> park behavior.
         */
        if (failedStealRounds >= 2) {
            idleWorkers_.fetch_add(
                1,
                std::memory_order_relaxed);

            waitUntil(
                [this] {
                    return
                        runState_.load(
                            std::memory_order_acquire) !=
                            RunState::Running ||

                        paused_.load(
                            std::memory_order_acquire) ||

                        pendingTasks_.load(
                            std::memory_order_acquire) != 0;
                });

            idleWorkers_.fetch_sub(
                1,
                std::memory_order_relaxed);

            failedStealRounds = 0;
        }
    }
}


// ============================================================
// Task Retrieval
// ============================================================

std::optional<ThreadPool::Task>
ThreadPool::fetchTask(
    std::size_t index) {

    Worker& self =
        workers_[index];


    /*
     * 1. Always prefer local work.
     *
     * This is the fastest path.
     */
    if (auto task =
            self.queue_.popBottom()) {
        return task;
    }


    /*
     * 2. Try a bounded number of random victims.
     *
     * The old implementation checked every worker.
     *
     * With 32 workers:
     *
     *     32 workers × 31 victims
     *
     * could generate hundreds of unnecessary atomic operations
     * for every empty scheduling round.
     *
     * Bounded stealing keeps this approximately constant.
     */
    if (workerCount_ > 1) {

        const std::size_t attempts =
            std::min(
                workerCount_ - 1,
                MaxStealAttempts);

        std::size_t offset =
            1 + randomBelow(
                    workerCount_ - 1);

        for (std::size_t i = 0;
             i < attempts;
             ++i) {

            const std::size_t victim =
                (index + offset + i) %
                workerCount_;

            if (victim == index)
                continue;

            if (auto task =
                    workers_[victim]
                        .queue_
                        .steal()) {
                return task;
            }
        }
    }


    /*
     * 3. External injection queues.
     *
     * First use the global hint.
     *
     * If zero, don't touch any mutex or shard.
     */
    if (injectionTasks_.load(
            std::memory_order_acquire) != 0) {

        if (refillLocalQueue(index)) {
            return self.queue_.popBottom();
        }
    }


    return std::nullopt;
}


// ============================================================
// External Task Retrieval
// ============================================================

std::optional<ThreadPool::Task>
ThreadPool::fetchTaskExternal() {

    /*
     * First steal from a bounded number of worker queues.
     */
    if (workerCount_ > 0) {

        const std::size_t attempts =
            std::min(
                workerCount_,
                MaxStealAttempts);

        const std::size_t offset =
            randomBelow(workerCount_);

        for (std::size_t i = 0;
             i < attempts;
             ++i) {

            const std::size_t victim =
                (offset + i) %
                workerCount_;

            if (auto task =
                    workers_[victim]
                        .queue_
                        .steal()) {
                return task;
            }
        }
    }


    /*
     * No injection work is known to exist.
     */
    if (injectionTasks_.load(
            std::memory_order_acquire) == 0) {
        return std::nullopt;
    }


    /*
     * External helper directly consumes from injection queues.
     *
     * Unlike workers, it doesn't have a local queue, so take one task.
     */
    const std::size_t offset =
        randomBelow(injectionShardCount_);

    for (std::size_t i = 0;
         i < injectionShardCount_;
         ++i) {

        InjectionShard& shard =
            injectionShards_[
                (offset + i) %
                injectionShardCount_];

        if (shard.size_.load(
                std::memory_order_acquire) == 0) {
            continue;
        }

        std::lock_guard lock(
            shard.mutex_);

        if (shard.queue_.empty()) {
            continue;
        }

        Task task(
            std::move(
                shard.queue_.front()));

        shard.queue_.pop_front();

        shard.size_.fetch_sub(
            1,
            std::memory_order_relaxed);

        injectionTasks_.fetch_sub(
            1,
            std::memory_order_release);

        return task;
    }


    return std::nullopt;
}


// ============================================================
// Injection Queue Refill
// ============================================================

bool ThreadPool::refillLocalQueue(
    std::size_t workerIndex) {

    if (injectionTasks_.load(
            std::memory_order_acquire) == 0) {
        return false;
    }


    /*
     * Randomize the starting shard.
     *
     * Different workers therefore don't all attack shard zero
     * simultaneously.
     */
    const std::size_t offset =
        (workerIndex +
         randomBelow(injectionShardCount_)) %
        injectionShardCount_;


    for (std::size_t i = 0;
         i < injectionShardCount_;
         ++i) {

        const std::size_t shardIndex =
            (offset + i) %
            injectionShardCount_;


        if (drainInjectionShard(
                workerIndex,
                shardIndex)) {
            return true;
        }
    }


    return false;
}


// ============================================================
// Injection Queue Batch Drain
// ============================================================

bool ThreadPool::drainInjectionShard(
    std::size_t workerIndex,
    std::size_t shardIndex) {

    InjectionShard& shard =
        injectionShards_[shardIndex];

    if (shard.size_.load(
            std::memory_order_acquire) == 0) {
        return false;
    }


    std::lock_guard lock(
        shard.mutex_);


    if (shard.queue_.empty()) {
        return false;
    }


    Worker& worker =
        workers_[workerIndex];


    /*
     * Move multiple tasks into the worker's local queue.
     *
     * One mutex acquisition now services up to 32 tasks.
     */
    const std::size_t count =
        std::min(
            InjectionBatchSize,
            shard.queue_.size());


    for (std::size_t i = 0;
         i < count;
         ++i) {

        worker.queue_.pushBottom(
            std::move(
                shard.queue_.front()));

        shard.queue_.pop_front();
    }


    shard.size_.fetch_sub(
        count,
        std::memory_order_relaxed);

    injectionTasks_.fetch_sub(
        count,
        std::memory_order_release);


    return true;
}


// ============================================================
// Task Submission
// ============================================================

void ThreadPool::submit(
    Task&& task) {

    if (runState_.load(
            std::memory_order_acquire) !=
        RunState::Running) {

        throw std::runtime_error(
            "submit on stopped ThreadPool");
    }


    /*
     * Worker-local submission:
     *
     * This remains the fastest path.
     */
    if (currentWorker_ != nullptr) {

        currentWorker_->queue_.pushBottom(
            std::move(task));

        pendingTasks_.fetch_add(
            1,
            std::memory_order_release);

        /*
         * A worker submitting work to itself does not need a wakeup.
         * It will immediately return to its own queue.
         */
        return;
    }


    /*
     * External submission.
     */
    const std::size_t shardIndex =
        injectionRoundRobin_.fetch_add(
            1,
            std::memory_order_relaxed) %
        injectionShardCount_;


    InjectionShard& shard =
        injectionShards_[shardIndex];


    {
        std::lock_guard lock(
            shard.mutex_);

        shard.queue_.push_back(
            std::move(task));

        shard.size_.fetch_add(
            1,
            std::memory_order_relaxed);
    }


    /*
     * Publish the task as globally available.
     *
     * The queue insertion happened before this release operation.
     */
    injectionTasks_.fetch_add(
        1,
        std::memory_order_release);


    pendingTasks_.fetch_add(
        1,
        std::memory_order_release);


    /*
     * Wake one sleeping worker.
     *
     * We intentionally don't wake every worker. One newly submitted
     * task generally only requires one worker.
     */
    if (idleWorkers_.load(
            std::memory_order_acquire) != 0) {

        wakeOne();
    }
}


// ============================================================
// Worker Wakeup
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