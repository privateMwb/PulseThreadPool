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


    /*
     * One allocation for the result state.
     */
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
                    std::is_void_v<ReturnType>) {

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
void ThreadPool::detach(
    F&& f) {

    submit(
        Task(
            std::decay_t<F>(
                std::forward<F>(f))));
}


// ============================================================
// Worker Synchronization
// ============================================================

template <typename Predicate>
void ThreadPool::waitUntil(
    Predicate predicate) noexcept {

    /*
     * Phase 1:
     *
     * Short busy-spin.
     *
     * This is useful when a producer is about to submit work.
     */
    for (int i = 0;
         i < Detail::WaitSpinIterations;
         ++i) {

        if (predicate())
            return;

        Detail::cpuRelax();
    }


    /*
     * Phase 2:
     *
     * Yield without fully parking.
     */
    for (int i = 0;
         i < Detail::WaitYieldIterations;
         ++i) {

        if (predicate())
            return;

        std::this_thread::yield();
    }


    /*
     * Phase 3:
     *
     * Atomic parking.
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