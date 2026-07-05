// ============================================================
// ThreadPool.tpp
// Template implementation for ThreadPoolPro::ThreadPool.
// ============================================================
//
// Sections:
//   1. Task Submission
//
// This file contains only template member implementations.
// Non-template members are implemented in ThreadPool.cpp.
//
// ============================================================

#include <future>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace ThreadPoolPro {

// ============================================================
//  Section 1 — Task Submission
// ============================================================

template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>
{
    using ReturnType = std::invoke_result_t<
        std::decay_t<F>,
        std::decay_t<Args>...
    >;

    std::packaged_task<ReturnType()> packagedTask(
        [func = std::decay_t<F>(std::forward<F>(f)),
         argsTuple = std::make_tuple(std::forward<Args>(args)...)
        ]() mutable -> ReturnType {
            return std::apply(std::move(func), std::move(argsTuple));
        }
    );

    auto future = packagedTask.get_future();

    submit(Task(
        [pt = std::move(packagedTask)]() mutable {
            pt();
        }
    ));

    return future;
}

template<typename F>
void ThreadPool::detach(F&& f) {
    submit(Task(std::decay_t<F>(std::forward<F>(f))));
}

} // namespace ThreadPoolPro