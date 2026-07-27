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

template <typename F, typename... Args>
auto ThreadPool::enqueue(F&& task, Args&&... args)
    -> Detail::Future<
        std::invoke_result_t<
            std::decay_t<F>,
            std::decay_t<Args>...>> {

    using Result =
        std::invoke_result_t<
            std::decay_t<F>,
            std::decay_t<Args>...>;

    using State =
        Detail::ResultState<Result>;

    // The Future constructor shown by your compiler requires:
    //
    //     Future(ResultState<T>*)
    //
    // Therefore the state must be allocated separately and the
    // Future receives the raw ResultState pointer.
    //
    // The ResultState itself must remain alive until both the Future
    // and the executing task have released their references.
    auto state =
        std::make_shared<State>();

    Detail::Future<Result> future{
        state.get()
    };

    auto boundTask =
        [fn = std::forward<F>(task),
         arguments =
             std::make_tuple(
                 std::forward<Args>(args)...),
         state]() mutable {

            try {

                if constexpr (
                    std::is_void_v<Result>) {

                    std::apply(
                        [&fn](auto&&... unpacked) mutable {
                            std::invoke(
                                std::move(fn),
                                std::forward<
                                    decltype(unpacked)>(
                                    unpacked)...);
                        },
                        std::move(arguments));

                    state->setValue();

                } else {

                    Result result =
                        std::apply(
                            [&fn](auto&&... unpacked) mutable
                                -> Result {

                                return std::invoke(
                                    std::move(fn),
                                    std::forward<
                                        decltype(unpacked)>(
                                        unpacked)...);
                            },
                            std::move(arguments));

                    state->setValue(
                        std::move(result));
                }

            } catch (...) {

                state->setException(
                    std::current_exception());
            }
        };

    submit(
        Task{
            std::move(boundTask)
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