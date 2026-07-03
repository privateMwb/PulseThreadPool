#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

#include <ThreadPoolPro/Detail/Task.h>
#include <ThreadPoolPro/Detail/Utility.h>
#include <ThreadPoolPro/Detail/WorkStealingQueue.h>

namespace ThreadPoolPro {

    // High-performance work-stealing thread pool.
    // Executes submitted tasks across a fixed set of worker threads using
    // per-thread work-stealing queues and a global injection queue.
    class ThreadPool {
    public:

        // Shutdown behavior.
        enum class ShutdownMode {
            FinishTasks,
            DiscardTasks
        };

    private:

        // Internal type aliases.
        using Task = Detail::Task;
        using WorkQueue = Detail::WorkStealingQueue;

        // Per-worker execution state.
        struct alignas(Detail::CacheLineSize) Worker {
            WorkQueue   queue_;
            std::thread thread_;

            Worker() = default;
        };

        // Worker pool.
        using WorkerPtr = std::unique_ptr<Worker>;
        std::vector<WorkerPtr> workers_;
        std::size_t            workerCount_;

        // Global injection queue.
        std::mutex       injectionMutex_;
        std::deque<Task> injectionQueue_;

        // Worker synchronization.
        std::condition_variable wakeCondition_;
        std::mutex              wakeMutex_;

        // Runtime state.
        alignas(Detail::CacheLineSize) std::atomic<bool>        stopRequested_;
        alignas(Detail::CacheLineSize) std::atomic<bool>        paused_;
        alignas(Detail::CacheLineSize) std::atomic<std::size_t> activeTasks_;
        alignas(Detail::CacheLineSize) std::atomic<std::size_t> exceptionCounter_;
        alignas(Detail::CacheLineSize) std::atomic<std::size_t> pendingTasks_;
        alignas(Detail::CacheLineSize) std::atomic<std::size_t> idleWorkers_;

        // Shutdown configuration.
        ShutdownMode shutdownMode_;

        // Thread-local worker state.
        static thread_local Worker* currentWorker_;
        static thread_local std::size_t currentWorkerIndex_;

    public:

        // Constructors and destructor.
        explicit ThreadPool(std::size_t threadCount = std::thread::hardware_concurrency());
        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        // Execution control.
        void pause() noexcept;
        void resume() noexcept;
        void shutdown(ShutdownMode mode = ShutdownMode::FinishTasks) noexcept;

        // Task submission.
        template<typename F, typename... Args>
        [[nodiscard]] auto enqueue(F&& task, Args&&... args)
            -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>;

        template<typename F>
        void detach(F&& task);

        // Runtime statistics.
        [[nodiscard]] std::size_t activeTaskCount() const noexcept;
        [[nodiscard]] std::size_t queuedTasks()     const noexcept;
        [[nodiscard]] std::size_t threadCount()     const noexcept;
        [[nodiscard]] std::size_t exceptionCount()  const noexcept;
        [[nodiscard]] std::size_t idleThreadCount() const noexcept;
        [[nodiscard]] bool        empty()           const noexcept;

        // State queries.
        [[nodiscard]] bool isPaused()  const noexcept;
        [[nodiscard]] bool isStopped() const noexcept;

    private:

        // Internal helpers.
        void workerLoop(std::size_t index);
        [[nodiscard]] std::optional<Task> fetchTask(std::size_t index);

        void submit(Task&& task);

        void wakeOne() noexcept;
        void wakeAll() noexcept;
    };

} // namespace ThreadPoolPro

#include "ThreadPool.tpp"