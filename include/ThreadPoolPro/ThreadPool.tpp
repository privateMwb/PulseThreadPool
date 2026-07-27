/**
 * @file ThreadPool.tpp
 * @brief ThreadPool template implementation.
 */

#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace ThreadPoolPro {


// ============================================================
// enqueue()
// ============================================================

template <
    typename F,
    typename... Args>
auto ThreadPool::enqueue(
    F&& task,
    Args&&... args)
    -> Detail::Future<
        std::invoke_result_t<
            std::decay_t<F>,
            std::decay_t<Args>...>> {

    using Result =
        std::invoke_result_t<
            std::decay_t<F>,
            std::decay_t<Args>...>;


    /*
     * Future creates the shared result state.
     *
     * The exact Future interface is delegated to Detail::Future.
     */
    auto state =
        std::make_shared<
            Detail::FutureState<Result>>();


    Detail::Future<Result>
        future{
            state
        };


    /*
     * Capture the callable and arguments exactly once.
     *
     * The lambda itself becomes the Task's callable.
     */
    auto callable =
        [
            state,
            fn =
                std::decay_t<F>(
                    std::forward<F>(
                        task)),
            argsTuple =
                std::make_tuple(
                    std::forward<Args>(
                        args)...)
        ]() mutable {

            try {

                if constexpr (
                    std::is_void_v<
                        Result>) {

                    std::apply(
                        [&fn](
                            auto&&... unpacked) {

                            std::invoke(
                                fn,
                                std::forward<
                                    decltype(
                                        unpacked)>(
                                    unpacked)...);

                        },
                        std::move(
                            argsTuple));


                    state->setValue();

                } else {

                    Result result =
                        std::apply(
                            [&fn](
                                auto&&... unpacked)
                                -> Result {

                                return std::invoke(
                                    fn,
                                    std::forward<
                                        decltype(
                                            unpacked)>(
                                        unpacked)...);

                            },
                            std::move(
                                argsTuple));


                    state->setValue(
                        std::move(
                            result));
                }

            } catch (...) {

                state->setException(
                    std::current_exception());
            }
        };


    /*
     * Construct exactly one Task.
     */
    submit(
        Task{
            std::move(
                callable)
        });


    return future;
}


// ============================================================
// detach()
// ============================================================

template <
    typename F>
void ThreadPool::detach(
    F&& task) {

    using Function =
        std::decay_t<F>;


    /*
     * A detach task is intentionally simpler than enqueue():
     *
     * - no Future
     * - no shared state
     * - no packaged_task
     * - no result storage
     */
    auto callable =
        [fn =
             Function(
                 std::forward<F>(
                     task))]() mutable {

            fn();
        };


    submit(
        Task{
            std::move(
                callable)
        });
}


// ============================================================
// waitUntil()
// ============================================================

template <
    typename Predicate>
void ThreadPool::waitUntil(
    Predicate predicate)
    noexcept {

    while (!predicate()) {

        const std::uint32_t token =
            wakeToken_.load(
                std::memory_order_acquire);


        /*
         * Check the predicate again after obtaining the token.
         *
         * If a producer changed the state between the first check
         * and this load, the predicate will observe it and the
         * thread will not sleep unnecessarily.
         */
        if (predicate())
            return;


        wakeToken_.wait(
            token,
            std::memory_order_acquire);
    }
}

} // namespace ThreadPoolPro