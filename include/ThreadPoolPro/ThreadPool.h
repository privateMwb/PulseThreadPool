/**
 * @file            ThreadPool.h
 *
 * @date            2026-07-25
 *
 * @version         2.0.0
 *
 * @copyright       Copyright (c) 2026 MWB
 *                  All rights reserved.
 *                  https://github.com/privateMwb/ThreadPoolPro
 *
 * @attention       This source is released under the MIT license
 *                  SPDX-License-Identifier: MIT
 *                  <http://opensource.org/licenses/MIT>
 */

#pragma once

// clang-format off
#include <ThreadPoolPro/Detail/Future.h>            // Detail::Future — enqueue()'s return type; replaces std::future
#include <ThreadPoolPro/Detail/Task.h>              // Detail::Task — the type-erased unit of work this pool queues
#include <ThreadPoolPro/Detail/ThreadMarket.h>      // Detail::ThreadMarket, Detail::MarketThread — leased persistent worker threads
#include <ThreadPoolPro/Detail/Utility.h>           // Detail::CacheLineSize — alignment for the hot atomics below
#include <ThreadPoolPro/Detail/WorkStealingQueue.h> // Detail::WorkStealingQueue — each Worker's local task deque

#include <atomic>      // std::atomic — runState_, paused_, wakeToken_, and the runtime-stats counters
#include <cstddef>     // std::size_t — thread/worker counts and indices
#include <deque>       // std::deque — each InjectionShard's queue_
#include <memory>      // std::unique_ptr — owns each Worker and InjectionShard
#include <mutex>       // std::mutex — each InjectionShard's own lock
#include <optional>    // std::optional — fetchTask()'s return type
#include <thread>      // std::thread — each Worker's OS thread
#include <type_traits> // std::invoke_result_t, std::decay_t — deduce enqueue()'s return type
#include <vector>      // std::vector — workers_, injectionShards_
// clang-format on

// A work-stealing thread pool. Each worker owns a Chase-Lev
// WorkStealingQueue (see Detail/WorkStealingQueue.h): local push/pop
// costs no atomic read-modify-write in the uncontended case, and idle
// workers steal from one another before falling back to a set of
// sharded, mutex-guarded injection queues used for submissions from
// non-worker threads — sharded because a single shared queue became the
// dominant contention point as worker count grew (every idle worker
// checks it on every failed steal round).
//
// Run/shutdown state is a single atomic RunState rather than a separate
// stop flag plus a plain-enum shutdown mode: two threads calling
// shutdown() concurrently with different modes would otherwise be a
// data race on that plain enum (only one CAS winner may ever write it;
// see ThreadPool.cpp). Worker wakeup uses a C++20 atomic wait/notify
// token instead of a condition_variable + mutex pair, so submit()'s hot
// path never has to acquire a lock just to (usually redundantly) notify
// a condition variable — see waitUntil() and wakeOne()/wakeAll().

namespace ThreadPoolPro {

/**
 * @brief A work-stealing thread pool.
 * @details Executes submitted tasks across a fixed set of worker
 * threads. Each worker has its own lock-free work-stealing queue for
 * local task storage; a set of sharded, mutex-guarded queues handles
 * submissions from threads that aren't themselves pool workers. See the
 * file-level comment for the synchronization design.
 */
class ThreadPool {
  public:
    /// @brief Behavior of shutdown() with respect to already-queued tasks.
    enum class ShutdownMode {
        FinishTasks, ///< Run every already-queued task to completion before stopping.
        DiscardTasks ///< Stop as soon as possible, discarding anything still queued.
    };

  private:
    // Internal type aliases.
    using Task = Detail::Task;
    using WorkQueue = Detail::WorkStealingQueue;

    /// @brief The combined run/shutdown state of the pool, updated
    /// exactly once (by whichever thread wins the shutdown() CAS) for
    /// the lifetime of the pool.
    enum class RunState : int {
        Running = 0,
        ShuttingDownFinish = 1,
        ShuttingDownDiscard = 2,
    };

    /// @brief Per-worker execution state: this worker's local queue and
    /// a pointer to the persistent MarketThread leased from
    /// ThreadMarket to run its workerLoop(). The thread itself outlives
    /// this Worker — it's returned to the market on shutdown() instead
    /// of being joined/destroyed, so a later ThreadPool can reuse it
    /// without paying OS thread-creation cost again.
    struct alignas(Detail::CacheLineSize) Worker {
        WorkQueue queue_;
        Detail::MarketThread* marketThread_ = nullptr;

        Worker() = default;
    };

    // Worker pool. Sized via the vector's counting constructor
    // (workers_(workerCount_) in the init list below), not
    // reserve()+emplace_back(): Worker holds an atomic/thread and is
    // therefore neither movable nor copyable, and reserve() requires
    // Cpp17MoveInsertable *at compile time* regardless of runtime
    // element count. The counting constructor only requires
    // DefaultInsertable — it builds all elements directly into their
    // final storage in one allocation, with no relocation ever
    // involved, which also happens to be exactly what we want (no
    // unique_ptr<Worker> indirection, one allocation instead of N).
    // workerCount_ must be declared (and thus initialized) before
    // workers_ for this to be well-defined — member init order follows
    // declaration order, not the order written in the init list.
    std::size_t workerCount_;
    std::vector<Worker> workers_;

    // Sharded injection queues, used only for submissions arriving from
    // threads that are not themselves pool workers. Sharded (rather than
    // one shared mutex+deque) because every idle worker checks here on
    // every failed steal round — with a single shared lock, that check
    // itself becomes the dominant point of contention as worker count
    // grows. Each shard tracks its own lock-free size so a worker can
    // skip locking a shard it can already see is empty.
    struct alignas(Detail::CacheLineSize) InjectionShard {
        std::mutex mutex_;
        std::deque<Task> queue_;
        std::atomic<std::size_t> size_{0};
    };

    // Same reasoning as workers_ above: InjectionShard holds a
    // mutex/atomic (non-movable), so it's sized via the counting
    // constructor, and injectionShardCount_ must be declared first.
    std::size_t injectionShardCount_;
    std::vector<InjectionShard> injectionShards_;
    std::atomic<std::size_t> injectionRoundRobin_;

    /// @brief Generation counter used with `std::atomic::wait/notify` to
    /// wake idle workers without a condition_variable + mutex pair. Bumped
    /// (and workers notified) on every submit(), pause(), resume(), and
    /// shutdown() state change. See waitUntil().
    alignas(Detail::CacheLineSize) std::atomic<std::uint32_t> wakeToken_;

    // Runtime state.
    alignas(Detail::CacheLineSize) std::atomic<RunState> runState_;
    alignas(Detail::CacheLineSize) std::atomic<bool> paused_;
    alignas(Detail::CacheLineSize) std::atomic<std::size_t> activeTasks_;
    alignas(Detail::CacheLineSize) std::atomic<std::size_t> exceptionCounter_;
    alignas(Detail::CacheLineSize) std::atomic<std::size_t> pendingTasks_;
    alignas(Detail::CacheLineSize) std::atomic<std::size_t> idleWorkers_;

    /// @brief Number of threads currently blocked in waitIdle(). Lets
    /// workers skip the completion-path notify entirely when nobody is
    /// actually waiting on it — see workerLoop()'s task-execution block.
    alignas(Detail::CacheLineSize) std::atomic<std::size_t> waitIdleWaiters_;

    // Thread-local worker state, used so a task running on a worker
    // thread can submit new work directly into that worker's own queue
    // (see submit()) instead of always going through the injection queue.
    static thread_local Worker* currentWorker_;
    static thread_local std::size_t currentWorkerIndex_;

    /// @brief Set by shutdown() on the calling thread, if that thread is
    /// itself a pool worker being detached (see shutdown()'s doc
    /// comment). workerLoop() checks this immediately after finishing
    /// the in-flight task and, if set, returns without touching `this`
    /// again: the caller may destroy the pool as soon as that task's
    /// result becomes visible (e.g. via its future), which can race with
    /// this thread's own post-task bookkeeping otherwise.
    static thread_local bool selfDetachRequested_;

  public:
    /**
     * @brief Constructs the pool and starts `threadCount` worker threads.
     * @param threadCount Number of worker threads. `0` is treated as
     * `1`. Defaults to `std::thread::hardware_concurrency()`.
     */
    explicit ThreadPool(std::size_t threadCount = std::thread::hardware_concurrency());

    /// @brief Calls `shutdown()` with the default mode
    /// (`ShutdownMode::FinishTasks`) if not already shut down, then
    /// joins every worker thread.
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /// @brief Pauses task execution. Already-running tasks finish; no
    /// new task starts until `resume()`. Safe to call from any thread,
    /// including a pool worker.
    void pause() noexcept;

    /// @brief Resumes task execution after `pause()`.
    void resume() noexcept;

    /**
     * @brief Requests shutdown and joins every worker thread.
     * @param mode Whether queued tasks should be finished or discarded.
     * Only the first call's `mode` takes effect — subsequent calls
     * (concurrent or not) are no-ops, since the pool can only shut down
     * once.
     * @details Safe to call from a pool worker's own task (that worker
     * is detached rather than joined, avoiding a self-join deadlock) or
     * from any external thread. Called automatically by the destructor
     * if not already invoked.
     */
    void shutdown(ShutdownMode mode = ShutdownMode::FinishTasks) noexcept;

    /**
     * @brief Submits a callable for execution and returns a future for
     * its result.
     * @tparam F Deduced callable type.
     * @tparam Args Deduced argument types.
     * @param task Callable to invoke.
     * @param args Arguments to forward into `task` when it runs.
     * @return A `Detail::Future` that becomes ready with `task`'s
     * return value (or exception) once it has run. Lighter-weight than
     * `std::future` — see Detail/Future.h.
     * @throws std::runtime_error if the pool has already begun shutting
     * down.
     */
    template <typename F, typename... Args>
    [[nodiscard]] auto enqueue(F&& task, Args&&... args)
        -> Detail::Future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>;

    /**
     * @brief Submits a callable for execution without a future.
     * @tparam F Deduced callable type.
     * @param task Callable to invoke. Any return value is discarded; any
     * exception it throws is counted (see `exceptionCount()`) and
     * swallowed rather than propagated.
     * @details Cheaper than `enqueue()` when the result and completion
     * signal aren't needed — skips `std::packaged_task` and its shared
     * state entirely.
     * @throws std::runtime_error if the pool has already begun shutting
     * down.
     */
    template <typename F> void detach(F&& task);

    /// @brief Returns the number of tasks currently executing.
    [[nodiscard]] std::size_t activeTaskCount() const noexcept;
    /// @brief Returns the number of tasks queued but not yet started.
    [[nodiscard]] std::size_t queuedTasks() const noexcept;
    /// @brief Returns the number of worker threads.
    [[nodiscard]] std::size_t threadCount() const noexcept;
    /// @brief Returns the number of tasks that have thrown an uncaught exception.
    [[nodiscard]] std::size_t exceptionCount() const noexcept;
    /// @brief Returns the number of worker threads currently idle.
    [[nodiscard]] std::size_t idleThreadCount() const noexcept;
    /// @brief Returns whether there are no queued (not-yet-started) tasks.
    [[nodiscard]] bool empty() const noexcept;

    /// @brief Returns whether the pool is currently paused.
    [[nodiscard]] bool isPaused() const noexcept;
    /// @brief Returns whether shutdown() has been called (in either mode).
    [[nodiscard]] bool isStopped() const noexcept;

    /**
     * @brief Blocks the calling thread until every submitted task has
     * finished, or the pool stops.
     * @details Uses the same atomic wake token workers block on — no
     * busy-polling. Safe to call from any thread other than a pool
     * worker (a worker blocking here on its own pool would deadlock,
     * same as it would joining its own thread).
     */
    void waitIdle() noexcept;

  private:
    /// @brief The body run by each worker thread.
    /// @param index This worker's index into `workers_`.
    void workerLoop(std::size_t index);

    /**
     * @brief Attempts to obtain one task to run, in priority order: this
     * worker's own queue, then stealing from other workers (starting at
     * a randomized offset to spread contention across thieves), then the
     * sharded injection queues (checked at a rotating offset, and only
     * locked if a lock-free size check shows the shard isn't empty).
     * @param index This worker's index into `workers_`.
     * @return A task if one was found, otherwise `std::nullopt`.
     */
    [[nodiscard]] std::optional<Task> fetchTask(std::size_t index);

    /**
     * @brief Like `fetchTask()`, but for a calling thread that is *not*
     * a pool worker (has no local queue of its own) — used by
     * `waitIdle()` so a thread blocked waiting for the pool to drain
     * helps drain it instead of sitting idle.
     * @details Steals from a randomized starting worker (see
     * `fetchTask()`), then scans the injection shards starting at
     * index 0 (there's no worker index to rotate the starting point
     * off of here).
     * @return A task if one was found, otherwise `std::nullopt`.
     */
    [[nodiscard]] std::optional<Task> fetchTaskExternal();

    /**
     * @brief Routes a task into the pool: the calling worker's own queue
     * if called from within a worker, otherwise one of the sharded
     * injection queues (chosen round-robin).
     * @param task Task to submit. Moved from.
     * @throws std::runtime_error if the pool has already begun shutting
     * down.
     */
    void submit(Task&& task);

    /// @brief Bumps the wake token and wakes at most one idle worker.
    void wakeOne() noexcept;
    /// @brief Bumps the wake token and wakes every idle worker.
    void wakeAll() noexcept;

    /**
     * @brief Blocks the calling worker until `predicate()` returns
     * `true`, using the atomic wake token instead of a
     * condition_variable.
     * @tparam Predicate Deduced callable type, invocable with no
     * arguments and returning something contextually convertible to `bool`.
     * @param predicate Condition to wait for. Checked before blocking
     * and re-checked after every wakeup (spurious or real).
     * @details Captures `wakeToken_`'s value between the two predicate
     * checks so that a wakeOne()/wakeAll() racing with the caller can
     * never be missed: if the token changes between the checks, the
     * second check either already observes the new state directly, or
     * `wait()` returns immediately because the token no longer matches
     * what was captured.
     */
    template <typename Predicate> void waitUntil(Predicate predicate) noexcept;
};

} // namespace ThreadPoolPro

/// @brief Umbrella alias so this library's types are reachable as
/// `rain::ThreadPool`, alongside every other project library, while its
/// true namespace (and all internal diagnostics) remains `ThreadPoolPro`.
/// Reopens `rain` rather than aliasing it, since multiple libraries each
/// contribute their own names into the same `rain` namespace -- an alias
/// (`namespace rain = ThreadPoolPro;`) can only ever bind to one target and
/// collides the moment a second library declares its own `rain` alias to
/// something else. Repeated identically in every header of this project --
/// reopening (unlike aliasing) is safe to repeat, since it doesn't
/// collide with itself.
namespace rain {
using namespace ThreadPoolPro;
}

#include "ThreadPool.tpp"
