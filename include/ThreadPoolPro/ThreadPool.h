/**
 * @file ThreadPool.h
 * @brief High-performance work-stealing thread pool.
 */

#pragma once

// clang-format off
#include <ThreadPoolPro/Detail/Future.h>
#include <ThreadPoolPro/Detail/Task.h>
#include <ThreadPoolPro/Detail/ThreadMarket.h>
#include <ThreadPoolPro/Detail/Utility.h>
#include <ThreadPoolPro/Detail/WorkStealingQueue.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>
// clang-format on

namespace ThreadPoolPro {

class ThreadPool {
  public:

    enum class ShutdownMode {
        FinishTasks,
        DiscardTasks
    };


  private:

    using Task =
        Detail::Task;

    using WorkQueue =
        Detail::WorkStealingQueue;


    enum class RunState : std::uint8_t {
        Running,
        ShuttingDownFinish,
        ShuttingDownDiscard
    };


    struct alignas(
        Detail::CacheLineSize)
    Worker {

        WorkQueue queue_;

        Detail::MarketThread*
            marketThread_ = nullptr;

        std::size_t index_ = 0;
    };


    std::size_t
        workerCount_;


    std::vector<Worker>
        workers_;


    struct alignas(
        Detail::CacheLineSize)
    InjectionShard {

        std::mutex mutex_;

        std::deque<Task>
            queue_;

        std::atomic<std::size_t>
            size_{0};
    };


    std::size_t
        injectionShardCount_;


    std::vector<InjectionShard>
        injectionShards_;


    std::atomic<std::size_t>
        injectionRoundRobin_{0};


    alignas(
        Detail::CacheLineSize)
    std::atomic<std::uint32_t>
        wakeToken_{0};


    alignas(
        Detail::CacheLineSize)
    std::atomic<RunState>
        runState_{
            RunState::Running
        };


    alignas(
        Detail::CacheLineSize)
    std::atomic<bool>
        paused_{false};


    alignas(
        Detail::CacheLineSize)
    std::atomic<std::size_t>
        activeTasks_{0};


    alignas(
        Detail::CacheLineSize)
    std::atomic<std::size_t>
        exceptionCounter_{0};


    alignas(
        Detail::CacheLineSize)
    std::atomic<std::size_t>
        pendingTasks_{0};


    alignas(
        Detail::CacheLineSize)
    std::atomic<std::size_t>
        idleWorkers_{0};


    alignas(
        Detail::CacheLineSize)
    std::atomic<std::size_t>
        waitIdleWaiters_{0};


    static thread_local Worker*
        currentWorker_;


    static thread_local std::size_t
        currentWorkerIndex_;


    static thread_local bool
        selfDetachRequested_;


  public:

    explicit ThreadPool(
        std::size_t threadCount =
            std::thread::hardware_concurrency());


    ~ThreadPool();


    ThreadPool(
        const ThreadPool&) = delete;


    ThreadPool& operator=(
        const ThreadPool&) = delete;


    ThreadPool(
        ThreadPool&&) = delete;


    ThreadPool& operator=(
        ThreadPool&&) = delete;


    void pause() noexcept;


    void resume() noexcept;


    void shutdown(
        ShutdownMode mode =
            ShutdownMode::FinishTasks) noexcept;


    template <
        typename F,
        typename... Args>
    [[nodiscard]]
    auto enqueue(
        F&& task,
        Args&&... args)
        -> Detail::Future<
            std::invoke_result_t<
                std::decay_t<F>,
                std::decay_t<Args>...>>;


    template <
        typename F>
    void detach(
        F&& task);


    [[nodiscard]]
    std::size_t
    activeTaskCount()
        const noexcept;


    [[nodiscard]]
    std::size_t
    queuedTasks()
        const noexcept;


    [[nodiscard]]
    std::size_t
    threadCount()
        const noexcept;


    [[nodiscard]]
    std::size_t
    exceptionCount()
        const noexcept;


    [[nodiscard]]
    std::size_t
    idleThreadCount()
        const noexcept;


    [[nodiscard]]
    bool
    empty()
        const noexcept;


    [[nodiscard]]
    bool
    isPaused()
        const noexcept;


    [[nodiscard]]
    bool
    isStopped()
        const noexcept;


    void waitIdle()
        noexcept;


  private:

    void workerLoop(
        std::size_t index);


    [[nodiscard]]
    std::optional<Task>
    fetchTask(
        std::size_t index);


    [[nodiscard]]
    std::optional<Task>
    fetchTaskExternal();


    void submit(
        Task&& task);


    void wakeOne()
        noexcept;


    void wakeAll()
        noexcept;


    template <
        typename Predicate>
    void waitUntil(
        Predicate predicate)
        noexcept;
};

} // namespace ThreadPoolPro

#include "ThreadPool.tpp"

namespace rain = ThreadPoolPro;