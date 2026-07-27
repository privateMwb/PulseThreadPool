/**
 * @file            ThreadPool.h
 *
 * @date            2026-07-25
 *
 * @version         2.1.0
 *
 * @copyright       Copyright (c) 2026 Your Name
 *                  All rights reserved.
 *                  https://github.com/yourname/PulseThreadPool
 *
 * @attention       This source is released under the MIT license
 *                  SPDX-License-Identifier: MIT
 *                  <http://opensource.org/licenses/MIT>
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

    enum class RunState : int {
        Running = 0,
        ShuttingDownFinish = 1,
        ShuttingDownDiscard = 2,
    };

    /*
     * Maximum number of victims a worker probes during one failed
     * steal round.
     *
     * The old implementation scanned every worker:
     *
     *     O(workerCount)
     *
     * for every failed fetch.
     *
     * At 32 workers this produced a huge amount of failed CAS/fence
     * traffic. A bounded probe keeps the scheduler cost approximately
     * constant as the pool grows.
     */
    static constexpr std::size_t MaxStealAttempts = 4;

    /*
     * Number of tasks moved from an injection queue into a worker's
     * local queue during one refill.
     *
     * This amortizes the injection-shard mutex acquisition.
     */
    static constexpr std::size_t InjectionBatchSize = 32;

    /*
     * Keep the number of injection shards bounded.
     *
     * Too few shards creates producer contention.
     * Too many shards make every failed consumer scan expensive.
     *
     * 16 is enough to spread external producers on most systems,
     * while the upper bound prevents 32/64/128-worker pools from
     * creating unnecessarily large injection queues.
     */
    static constexpr std::size_t MinInjectionShards = 4;
    static constexpr std::size_t MaxInjectionShards = 16;

    struct alignas(Detail::CacheLineSize) Worker {
        WorkQueue queue_;
        Detail::MarketThread* marketThread_ = nullptr;

        Worker() = default;
    };

    std::size_t workerCount_;
    std::vector<Worker> workers_;

    struct alignas(Detail::CacheLineSize) InjectionShard {
        std::mutex mutex_;
        std::deque<Task> queue_;
        std::atomic<std::size_t> size_{0};
    };

    std::size_t injectionShardCount_;
    std::vector<InjectionShard> injectionShards_;

    /*
     * Producer-side round robin.
     *
     * Relaxed ordering is sufficient. This is only used to distribute
     * submissions; it is not used for synchronization.
     */
    alignas(Detail::CacheLineSize)
        std::atomic<std::size_t> injectionRoundRobin_;

    /*
     * Fast global hint:
     *
     *     0 -> no task is currently believed to be in injection queues
     *    >0 -> at least one task may be available
     *
     * This lets workers completely skip the O(shardCount) injection
     * scan in the overwhelmingly common empty-injection case.
     */
    alignas(Detail::CacheLineSize)
        std::atomic<std::size_t> injectionTasks_;

    /*
     * Number of tasks that have been submitted but have not yet started.
     */
    alignas(Detail::CacheLineSize)
        std::atomic<std::size_t> pendingTasks_;

    alignas(Detail::CacheLineSize)
        std::atomic<std::uint32_t> wakeToken_;

    alignas(Detail::CacheLineSize)
        std::atomic<RunState> runState_;

    alignas(Detail::CacheLineSize)
        std::atomic<bool> paused_;

    alignas(Detail::CacheLineSize)
        std::atomic<std::size_t> activeTasks_;

    alignas(Detail::CacheLineSize)
        std::atomic<std::size_t> exceptionCounter_;

    alignas(Detail::CacheLineSize)
        std::atomic<std::size_t> idleWorkers_;

    alignas(Detail::CacheLineSize)
        std::atomic<std::size_t> waitIdleWaiters_;

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

    /*
     * Attempts to refill a worker's local queue from the injection
     * queues.
     *
     * Returns true if at least one task was moved.
     */
    bool refillLocalQueue(std::size_t workerIndex);

    /*
     * Attempts to drain one injection shard into a worker's local queue.
     */
    bool drainInjectionShard(
        std::size_t workerIndex,
        std::size_t shardIndex);

    void submit(Task&& task);

    void wakeOne() noexcept;
    void wakeAll() noexcept;

    template <typename Predicate>
    void waitUntil(Predicate predicate) noexcept;
};

} // namespace ThreadPoolPro

#include "ThreadPool.tpp"

namespace rain = ThreadPoolPro;