/**
 * @file ThreadPool.cpp
 * @brief High-performance work-stealing thread pool implementation.
 */

#include <ThreadPoolPro/ThreadPool.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <random>
#include <stdexcept>
#include <utility>

namespace ThreadPoolPro {

thread_local ThreadPool::Worker*
    ThreadPool::currentWorker_ =
        nullptr;


thread_local std::size_t
    ThreadPool::currentWorkerIndex_ =
        static_cast<std::size_t>(-1);


thread_local bool
    ThreadPool::selfDetachRequested_ =
        false;


// ============================================================
// Constructor
// ============================================================

ThreadPool::ThreadPool(
    std::size_t threadCount)
    : workerCount_(
          threadCount == 0
              ? 1
              : threadCount)
    , workers_(
          workerCount_)
    , injectionShardCount_(
          std::max<std::size_t>(
              1,
              std::min<std::size_t>(
                  workerCount_,
                  16)))
    , injectionShards_(
          injectionShardCount_) {

    /*
     * Initialize worker indices before starting any threads.
     */
    for (std::size_t i = 0;
         i < workerCount_;
         ++i) {

        workers_[i].index_ =
            i;
    }


    /*
     * Lease the persistent OS threads once.
     *
     * The important optimization is that MarketThread::assign()
     * happens once here, rather than once per submitted task.
     */
    const auto marketThreads =
        Detail::ThreadMarket::instance()
            .lease(
                workerCount_);


    for (std::size_t i = 0;
         i < workerCount_;
         ++i) {

        workers_[i].marketThread_ =
            marketThreads[i];


        workers_[i]
            .marketThread_
            ->assign(
                [this, i] {
                    workerLoop(i);
                });
    }
}


// ============================================================
// Destructor
// ============================================================

ThreadPool::~ThreadPool() {

    if (!isStopped()) {

        shutdown(
            ShutdownMode::FinishTasks);
    }
}


// ============================================================
// Pause / Resume
// ============================================================

void ThreadPool::pause()
    noexcept {

    paused_.store(
        true,
        std::memory_order_release);


    wakeAll();
}


void ThreadPool::resume()
    noexcept {

    paused_.store(
        false,
        std::memory_order_release);


    wakeAll();
}


// ============================================================
// Shutdown
// ============================================================

void ThreadPool::shutdown(
    ShutdownMode mode)
    noexcept {

    RunState desired =
        mode ==
                ShutdownMode::FinishTasks
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


    /*
     * Discard mode clears all queues immediately.
     *
     * Tasks are destroyed by clearing the queues.
     */
    if (mode ==
        ShutdownMode::DiscardTasks) {

        for (auto& worker :
             workers_) {

            while (
                worker.queue_
                    .popBottom()) {

                pendingTasks_.fetch_sub(
                    1,
                    std::memory_order_relaxed);
            }
        }


        for (auto& shard :
             injectionShards_) {

            std::lock_guard lock{
                shard.mutex_
            };


            const std::size_t count =
                shard.queue_.size();


            shard.queue_.clear();


            shard.size_.store(
                0,
                std::memory_order_relaxed);


            pendingTasks_.fetch_sub(
                count,
                std::memory_order_relaxed);
        }
    }


    wakeAll();


    /*
     * A worker calling shutdown() cannot wait for itself.
     */
    if (currentWorker_) {

        selfDetachRequested_ =
            true;


        return;
    }


    /*
     * External caller waits for every persistent worker to
     * finish its workerLoop().
     */
    for (auto& worker :
         workers_) {

        if (worker.marketThread_) {

            worker.marketThread_
                ->waitDone();
        }
    }
}


// ============================================================
// Task Fetching
// ============================================================

std::optional<
    ThreadPool::Task>
ThreadPool::fetchTask(
    std::size_t index) {

    /*
     * Owner-local queue has highest priority.
     */
    if (auto task =
            workers_[index]
                .queue_
                .popBottom()) {

        return task;
    }


    /*
     * Steal from other workers.
     *
     * Start at a pseudo-randomized position based on the worker
     * index and current queue state. This avoids every worker
     * hammering worker zero first.
     */
    const std::size_t count =
        workerCount_;


    if (count > 1) {

        const std::size_t start =
            (index * 0x9E3779B97F4A7C15ULL)
            % count;


        for (std::size_t n = 0;
             n < count - 1;
             ++n) {

            const std::size_t victim =
                (start + n + 1)
                % count;


            if (auto task =
                    workers_[victim]
                        .queue_
                        .steal()) {

                return task;
            }
        }
    }


    /*
     * Finally inspect injection shards.
     */
    const std::size_t shardStart =
        (index +
         injectionRoundRobin_
             .load(
                 std::memory_order_relaxed))
        % injectionShardCount_;


    for (std::size_t n = 0;
         n < injectionShardCount_;
         ++n) {

        InjectionShard& shard =
            injectionShards_[
                (shardStart + n)
                % injectionShardCount_];


        if (shard.size_.load(
                std::memory_order_acquire)
            == 0) {

            continue;
        }


        std::lock_guard lock{
            shard.mutex_
        };


        if (shard.queue_.empty())
            continue;


        Task task =
            std::move(
                shard.queue_.front());


        shard.queue_.pop_front();


        shard.size_.fetch_sub(
            1,
            std::memory_order_release);


        pendingTasks_.fetch_sub(
            1,
            std::memory_order_relaxed);


        return task;
    }


    return std::nullopt;
}


std::optional<
    ThreadPool::Task>
ThreadPool::fetchTaskExternal() {

    /*
     * External waiters should first help drain injection queues.
     */
    for (std::size_t n = 0;
         n < injectionShardCount_;
         ++n) {

        InjectionShard& shard =
            injectionShards_[n];


        if (shard.size_.load(
                std::memory_order_acquire)
            == 0) {

            continue;
        }


        std::lock_guard lock{
            shard.mutex_
        };


        if (shard.queue_.empty())
            continue;


        Task task =
            std::move(
                shard.queue_.front());


        shard.queue_.pop_front();


        shard.size_.fetch_sub(
            1,
            std::memory_order_release);


        pendingTasks_.fetch_sub(
            1,
            std::memory_order_relaxed);


        return task;
    }


    /*
     * Then steal from workers.
     */
    for (std::size_t i = 0;
         i < workerCount_;
         ++i) {

        if (auto task =
                workers_[i]
                    .queue_
                    .steal()) {

            return task;
        }
    }


    return std::nullopt;
}


// ============================================================
// Submit
// ============================================================

void ThreadPool::submit(
    Task&& task) {

    if (runState_.load(
            std::memory_order_acquire)
        != RunState::Running) {

        throw std::runtime_error(
            "ThreadPool has been shut down");
    }


    /*
     * Worker-originated submissions go directly into the
     * submitting worker's local queue.
     */
    if (currentWorker_) {

        currentWorker_
            ->queue_
            .pushBottom(
                std::move(task));


        pendingTasks_.fetch_add(
            1,
            std::memory_order_relaxed);


        /*
         * The worker itself is already running, so no wakeup
         * is necessary.
         */
        return;
    }


    /*
     * External submissions use round-robin sharding.
     */
    const std::size_t shardIndex =
        injectionRoundRobin_
            .fetch_add(
                1,
                std::memory_order_relaxed)
        % injectionShardCount_;


    InjectionShard& shard =
        injectionShards_[
            shardIndex];


    {
        std::lock_guard lock{
            shard.mutex_
        };


        shard.queue_.emplace_back(
            std::move(task));


        shard.size_.fetch_add(
            1,
            std::memory_order_release);
    }


    pendingTasks_.fetch_add(
        1,
        std::memory_order_relaxed);


    wakeOne();
}


// ============================================================
// Worker Loop
// ============================================================

void ThreadPool::workerLoop(
    std::size_t index) {

    Worker& worker =
        workers_[index];


    currentWorker_ =
        &worker;


    currentWorkerIndex_ =
        index;


    selfDetachRequested_ =
        false;


    for (;;) {

        const RunState state =
            runState_.load(
                std::memory_order_acquire);


        /*
         * Discard shutdown exits immediately.
         */
        if (state ==
            RunState::ShuttingDownDiscard) {

            break;
        }


        /*
         * Finish shutdown exits only after all work has drained.
         */
        if (state ==
            RunState::ShuttingDownFinish) {

            if (pendingTasks_.load(
                    std::memory_order_acquire)
                == 0
                &&
                activeTasks_.load(
                    std::memory_order_acquire)
                == 0) {

                break;
            }
        }


        /*
         * Pause stops new execution.
         */
        if (paused_.load(
                std::memory_order_acquire)) {

            idleWorkers_.fetch_add(
                1,
                std::memory_order_relaxed);


            waitUntil([this] {

                return
                    !paused_.load(
                        std::memory_order_acquire)
                    ||
                    runState_.load(
                        std::memory_order_acquire)
                        != RunState::Running;

            });


            idleWorkers_.fetch_sub(
                1,
                std::memory_order_relaxed);


            continue;
        }


        auto task =
            fetchTask(index);


        if (!task) {

            idleWorkers_.fetch_add(
                1,
                std::memory_order_relaxed);


            /*
             * Recheck before sleeping to avoid a lost wakeup.
             */
            if (!fetchTask(index)) {

                waitUntil([this] {

                    return
                        pendingTasks_.load(
                            std::memory_order_acquire)
                            != 0
                        ||
                        runState_.load(
                            std::memory_order_acquire)
                            != RunState::Running
                        ||
                        paused_.load(
                            std::memory_order_acquire);

                });
            }


            idleWorkers_.fetch_sub(
                1,
                std::memory_order_relaxed);


            continue;
        }


        activeTasks_.fetch_add(
            1,
            std::memory_order_relaxed);


        pendingTasks_.fetch_sub(
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
            std::memory_order_release);


        /*
         * A worker requested shutdown from inside its own task.
         * Do not touch the ThreadPool again after returning control
         * to MarketThread::loop().
         */
        if (selfDetachRequested_)
            break;


        if (waitIdleWaiters_.load(
                std::memory_order_relaxed)
            != 0) {

            wakeAll();
        }
    }


    currentWorker_ =
        nullptr;


    currentWorkerIndex_ =
        static_cast<std::size_t>(-1);
}


// ============================================================
// Wakeup
// ============================================================

void ThreadPool::wakeOne()
    noexcept {

    wakeToken_.fetch_add(
        1,
        std::memory_order_release);


    wakeToken_.notify_one();
}


void ThreadPool::wakeAll()
    noexcept {

    wakeToken_.fetch_add(
        1,
        std::memory_order_release);


    wakeToken_.notify_all();
}


// ============================================================
// Wait
// ============================================================

void ThreadPool::waitIdle()
    noexcept {

    ++waitIdleWaiters_;


    for (;;) {

        if (pendingTasks_.load(
                std::memory_order_acquire)
                == 0
            &&
            activeTasks_.load(
                std::memory_order_acquire)
                == 0) {

            break;
        }


        /*
         * Help execute work rather than simply sleeping.
         */
        if (auto task =
                fetchTaskExternal()) {

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
                std::memory_order_release);


            continue;
        }


        const std::uint32_t token =
            wakeToken_.load(
                std::memory_order_acquire);


        if (pendingTasks_.load(
                std::memory_order_acquire)
                == 0
            &&
            activeTasks_.load(
                std::memory_order_acquire)
                == 0) {

            break;
        }


        wakeToken_.wait(
            token,
            std::memory_order_acquire);
    }


    --waitIdleWaiters_;
}


// ============================================================
// Queries
// ============================================================

std::size_t
ThreadPool::activeTaskCount()
    const noexcept {

    return activeTasks_.load(
        std::memory_order_relaxed);
}


std::size_t
ThreadPool::queuedTasks()
    const noexcept {

    return pendingTasks_.load(
        std::memory_order_relaxed);
}


std::size_t
ThreadPool::threadCount()
    const noexcept {

    return workerCount_;
}


std::size_t
ThreadPool::exceptionCount()
    const noexcept {

    return exceptionCounter_.load(
        std::memory_order_relaxed);
}


std::size_t
ThreadPool::idleThreadCount()
    const noexcept {

    return idleWorkers_.load(
        std::memory_order_relaxed);
}


bool ThreadPool::empty()
    const noexcept {

    return pendingTasks_.load(
               std::memory_order_acquire)
               == 0;
}


bool ThreadPool::isPaused()
    const noexcept {

    return paused_.load(
        std::memory_order_acquire);
}


bool ThreadPool::isStopped()
    const noexcept {

    return runState_.load(
               std::memory_order_acquire)
        != RunState::Running;
}

} // namespace ThreadPoolPro

namespace rain = ThreadPoolPro;