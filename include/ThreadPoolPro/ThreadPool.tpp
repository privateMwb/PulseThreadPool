/**
 * @file ThreadPool.tpp
 * @brief ThreadPool template implementation.
 */

#pragma once

#include <exception>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace ThreadPoolPro {


// ============================================================
// Task Submission
// ============================================================

template <typename F, typename... Args>
auto ThreadPool::enqueue(
    F&& f,
    Args&&... args)
    -> Detail::Future<
        std::invoke_result_t<
            std::decay_t<F>,
            std::decay_t<Args>...>> {

    using ReturnType =
        std::invoke_result_t<
            std::decay_t<F>,
            std::decay_t<Args>...>;

    using State =
        Detail::ResultState<ReturnType>;

    State* state =
        new State();

    auto runAndPublish =
        [
            state,
            func =
                std::decay_t<F>(
                    std::forward<F>(f)),
            argsTuple =
                std::make_tuple(
                    std::forward<Args>(args)...)
        ]() mutable {

            try {

                if constexpr (
                    std::is_void_v<
                        ReturnType>) {

                    std::apply(
                        std::move(func),
                        std::move(argsTuple));

                    state->setValue();

                } else {

                    state->setValue(
                        std::apply(
                            std::move(func),
                            std::move(argsTuple)));
                }

            } catch (...) {

                state->setException(
                    std::current_exception());
            }

            state->release();
        };


    try {

        submit(
            Task(
                std::move(
                    runAndPublish)));

    } catch (...) {

        state->release();

        throw;
    }


    return Detail::Future<ReturnType>(
        state);
}


template <typename F>
void ThreadPool::detach(F&& f) {

    submit(
        Task(
            std::decay_t<F>(
                std::forward<F>(f))));
}


// ============================================================
// Task Submission Routing
// ============================================================

/*
 * This function is intentionally kept in the template header.
 *
 * detach() and enqueue() both converge here.
 *
 * Worker-originated submissions are completely lock-free from
 * the injection path.
 */
inline void ThreadPool::submit(
    Task&& task) {

    if (runState_.load(
            std::memory_order_acquire)
        != RunState::Running) {

        throw std::runtime_error(
            "submit on stopped ThreadPool");
    }


    /*
     * Worker fast path.
     *
     * This is the most important optimization for recursive
     * task spawning.
     */
    if (currentWorker_ != nullptr) {

        currentWorker_
            ->queue_
            .pushBottom(
                std::move(task));

        pendingTasks_.fetch_add(
            1,
            std::memory_order_release);

        return;
    }


    /*
     * External producer path.
     *
     * One worker -> one queue.
     *
     * Multiple workers -> sharded queues.
     */
    const std::size_t shardIndex =
        workerCount_ == 1
            ? 0
            : injectionRoundRobin_.fetch_add(
                  1,
                  std::memory_order_relaxed)
                  % injectionShardCount_;

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
     * Increment pending only after the task is visible
     * in its queue.
     */
    pendingTasks_.fetch_add(
        1,
        std::memory_order_release);


    /*
     * Wake only when there is evidence of sleeping workers.
     *
     * This avoids a notify operation for the common case where
     * workers are already running.
     */
    if (idleWorkers_.load(
            std::memory_order_acquire)
        != 0) {

        wakeOne();
    }
}


// ============================================================
// Worker Waiting
// ============================================================

template <typename Predicate>
void ThreadPool::waitUntil(
    Predicate predicate) noexcept {

    /*
     * Short spin.
     *
     * Keep this deliberately small. The previous implementation's
     * spin phase can become extremely expensive when many workers
     * are simultaneously idle.
     */
    for (int i = 0;
         i < Detail::WaitSpinIterations;
         ++i) {

        if (predicate())
            return;

        Detail::cpuRelax();
    }


    /*
     * Small yield phase.
     */
    for (int i = 0;
         i < Detail::WaitYieldIterations;
         ++i) {

        if (predicate())
            return;

        std::this_thread::yield();
    }


    /*
     * Atomic wait.
     */
    for (;;) {

        if (predicate())
            return;

        const std::uint32_t token =
            wakeToken_.load(
                std::memory_order_acquire);

        if (predicate())
            return;

        wakeToken_.wait(
            token,
            std::memory_order_acquire);
    }
}

} // namespace ThreadPoolPro