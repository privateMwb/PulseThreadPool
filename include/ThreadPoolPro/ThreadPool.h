/**
 * @file ThreadPool.h
 * @brief High-performance work-stealing thread pool.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
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
#include <deque>
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
    using Task = Detail::Task;
    using WorkQueue = Detail::WorkStealingQueue;

    enum class RunState : std::uint8_t {
        Running = 0,
        ShuttingDownFinish = 1,
        ShuttingDownDiscard = 2
    };

    /*
     * Number of workers probed during a steal attempt.
     *
     * Full O(N) scans become extremely expensive with 16/32/64
     * workers. A bounded probe keeps stealing approximately O(1).
     */
    static constexpr std::size_t MaxStealProbes = 4;

    /*
     * Number of injection shards probed in a single fetch attempt.
     *
     * A worker that fails to find work will retry later anyway, so
     * scanning every shard here is wasted work.
     */
    static constexpr std::size_t MaxInjectionProbes = 4;

    /*
     * Number of times an external waitIdle() caller helps before
     * yielding to the workers.
     */
    static constexpr std::size_t MaxExternalHelpAttempts = 8;

    struct alignas(Detail::CacheLineSize) Worker {
        WorkQueue queue_;
        Detail::MarketThread* marketThread_ = nullptr;

        Worker() = default;
    };

    std::size_t workerCount_;
    std::vector<Worker> workers_;

    /*
     * Injection queues are used only by non-worker producers.
     *
     * The one-worker case gets a dedicated single queue because
     * sharding is strictly counterproductive there.
     */
    struct alignas(Detail::CacheLineSize) InjectionShard {
        std::mutex mutex_;
        std::deque<Task> queue_;
        std::atomic<std::size_t> size_{0};
    };

    std::size_t injectionShardCount_;
    std::vector<InjectionShard> injectionShards_;

    std::atomic<std::size_t> injectionRoundRobin_{0};

    /*
     * Wake generation.
     *
     * This is not a task counter. It only exists to avoid sleeping
     * forever when work/state changes.
     */
    alignas(Detail::CacheLineSize)
    std::atomic<std::uint32_t> wakeToken_{0};

    alignas(Detail::CacheLineSize)
    std::atomic<RunState> runState_{RunState::Running};

    alignas(Detail::CacheLineSize)
    std::atomic<bool> paused_{false};

    /*
     * Number of tasks currently executing.
     */
    alignas(Detail::CacheLineSize)
    std::atomic<std::size_t> activeTasks_{0};

    /*
     * Number of tasks submitted but not yet started.
     */
    alignas(Detail::CacheLineSize)
    std::atomic<std::size_t> pendingTasks_{0};

    alignas(Detail::CacheLineSize)
    std::atomic<std::size_t> exceptionCounter_{0};

    alignas(Detail::CacheLineSize)
    std::atomic<std::size_t> idleWorkers_{0};

    /*
     * Number of external threads currently waiting in waitIdle().
     */
    alignas(Detail::CacheLineSize)
    std::atomic<std::size_t> waitIdleWaiters_{0};

    /*
     * Worker-local TLS.
     *
     * A worker submitting work to itself bypasses all injection locks.
     */
    static thread_local Worker* currentWorker_;
    static thread_local std::size_t currentWorkerIndex_;
    static thread_local bool selfDetachRequested_;

  public:
    explicit ThreadPool(
        std::size_t threadCount = std::thread::hardware_concurrency());

    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    void pause() noexcept;
    void resume() noexcept;

    void shutdown(
        ShutdownMode mode = ShutdownMode::FinishTasks) noexcept;

    template <typename F, typename... Args>
    [[nodiscard]] auto enqueue(F&& task, Args&&... args)
        -> Detail::Future<
            std::invoke_result_t<
                std::decay_t<F>,
                std::decay_t<Args>...>>;

    template <typename F>
    void detach(F&& task);

    [[nodiscard]]
    std::size_t activeTaskCount() const noexcept;

    [[nodiscard]]
    std::size_t queuedTasks() const noexcept;

    [[nodiscard]]
    std::size_t threadCount() const noexcept;

    [[nodiscard]]
    std::size_t exceptionCount() const noexcept;

    [[nodiscard]]
    std::size_t idleThreadCount() const noexcept;

    [[nodiscard]]
    bool empty() const noexcept;

    [[nodiscard]]
    bool isPaused() const noexcept;

    [[nodiscard]]
    bool isStopped() const noexcept;

    void waitIdle() noexcept;

  private:
    void workerLoop(std::size_t index);

    [[nodiscard]]
    std::optional<Task>
    fetchTask(std::size_t index);

    [[nodiscard]]
    std::optional<Task>
    fetchTaskExternal();

    void submit(Task&& task);

    void wakeOne() noexcept;
    void wakeAll() noexcept;

    template <typename Predicate>
    void waitUntil(Predicate predicate) noexcept;
};

} // namespace ThreadPoolPro

#include "ThreadPool.tpp"

namespace rain = ThreadPoolPro;