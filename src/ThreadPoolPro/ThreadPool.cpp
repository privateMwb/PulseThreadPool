// ============================================================
// ThreadPool.cpp
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
//   9. Synchronization
//
// ============================================================

#include <ThreadPoolPro/ThreadPool.h>

#include <chrono>
#include <stdexcept>

namespace ThreadPoolPro {


    // ============================================================
    //  Section 1 — Static Thread State
    // ============================================================

    thread_local ThreadPool::Worker* ThreadPool::currentWorker_ = nullptr;
    thread_local std::size_t ThreadPool::currentWorkerIndex_ = 0;


    // ============================================================
    //  Section 2 — Constructors & Destructor
    // ============================================================

    ThreadPool::ThreadPool(std::size_t threadCount)
        : workerCount_{ threadCount == 0 ? 1 : threadCount }
        , stopRequested_{ false }
        , paused_{ false }
        , activeTasks_{ 0 }
        , exceptionCounter_{ 0 }
        , pendingTasks_{ 0 }
        , idleWorkers_{ 0 }
        , shutdownMode_{ ShutdownMode::FinishTasks }
    {
        workers_.reserve(workerCount_);

        for (std::size_t i = 0; i < workerCount_; ++i)
            workers_.emplace_back(std::make_unique<Worker>());

        for (std::size_t i = 0; i < workerCount_; ++i)
            workers_[i]->thread_ =
            std::thread(&ThreadPool::workerLoop, this, i);
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

    void ThreadPool::shutdown(ShutdownMode mode) noexcept {
        shutdownMode_ = mode;

        bool expected = false;

        if (!stopRequested_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel))
            return;

        wakeAll();

        // A worker thread cannot join itself (it would deadlock, or
        // std::terminate depending on implementation). If shutdown()
        // was called from inside a running task, detach that one
        // thread instead — it will exit its workerLoop and clean
        // itself up once this call returns and the task finishes.
        const std::thread::id callerId = std::this_thread::get_id();

        for (auto& worker : workers_) {
            if (!worker->thread_.joinable())
                continue;

            if (worker->thread_.get_id() == callerId)
                worker->thread_.detach();
            else
                worker->thread_.join();
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
        return stopRequested_.load(std::memory_order_relaxed);
    }


    // ============================================================
    //  Section 6 — Worker Execution
    // ============================================================

    void ThreadPool::workerLoop(std::size_t index) {
        currentWorker_ = workers_[index].get();
        currentWorkerIndex_ = index;

        while (true) {

            if (paused_.load(std::memory_order_acquire) &&
                !stopRequested_.load(std::memory_order_acquire))
            {
                idleWorkers_.fetch_add(1, std::memory_order_relaxed);

                std::unique_lock lock(wakeMutex_);

                wakeCondition_.wait(lock, [this] {
                    return stopRequested_.load(std::memory_order_acquire)
                        || !paused_.load(std::memory_order_acquire);
                    });

                idleWorkers_.fetch_sub(1, std::memory_order_relaxed);

                continue;
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
                    !stopRequested_.load(std::memory_order_acquire))
                {
                    currentWorker_->queue_.pushBottom(std::move(*task));
                    continue;
                }

                pendingTasks_.fetch_sub(1, std::memory_order_relaxed);
                activeTasks_.fetch_add(1, std::memory_order_relaxed);

                try {
                    (*task)();
                }
                catch (...) {
                    exceptionCounter_.fetch_add(1, std::memory_order_relaxed);
                }

                activeTasks_.fetch_sub(1, std::memory_order_relaxed);

                continue;
            }

            if (stopRequested_.load(std::memory_order_acquire)) {

                if (shutdownMode_ == ShutdownMode::DiscardTasks)
                    return;

                if (pendingTasks_.load(std::memory_order_acquire) == 0)
                    return;

                std::this_thread::sleep_for(std::chrono::microseconds(50));

                continue;
            }

            idleWorkers_.fetch_add(1, std::memory_order_relaxed);

            std::unique_lock lock(wakeMutex_);

            wakeCondition_.wait(lock, [this] {
                return stopRequested_.load(std::memory_order_acquire)
                    || paused_.load(std::memory_order_acquire)
                    || pendingTasks_.load(std::memory_order_relaxed) != 0;
                });

            idleWorkers_.fetch_sub(1, std::memory_order_relaxed);
        }
    }


    // ============================================================
    //  Section 7 — Task Retrieval
    // ============================================================

    std::optional<ThreadPool::Task>
        ThreadPool::fetchTask(std::size_t index) {

        Worker& self = *workers_[index];

        if (auto task = self.queue_.popBottom())
            return task;

        for (std::size_t i = 1; i < workerCount_; ++i) {

            std::size_t victim = (index + i) % workerCount_;

            if (auto task = workers_[victim]->queue_.steal())
                return task;
        }

        {
            std::lock_guard lock(injectionMutex_);

            if (!injectionQueue_.empty()) {

                Task task(std::move(injectionQueue_.front()));
                injectionQueue_.pop_front();

                return task;
            }
        }

        return std::nullopt;
    }

    
    // ============================================================
    //  Section 8 — Internal Task Submission
    // ============================================================

    void ThreadPool::submit(Task&& task) {
    if (stopRequested_.load(std::memory_order_acquire))
        throw std::runtime_error("submit on stopped ThreadPool");

    if (currentWorker_ != nullptr) {
        currentWorker_->queue_.pushBottom(std::move(task));
    } else {
        std::lock_guard<std::mutex> lock(injectionMutex_);
        injectionQueue_.push_back(std::move(task));
    }

    pendingTasks_.fetch_add(1, std::memory_order_release);
    wakeOne();
}


    // ============================================================
    //  Section 9 — Synchronization
    // ============================================================

    void ThreadPool::wakeOne() noexcept {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wakeCondition_.notify_one();
    }

    void ThreadPool::wakeAll() noexcept {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wakeCondition_.notify_all();
    }
    
} // namespace ThreadPoolPro