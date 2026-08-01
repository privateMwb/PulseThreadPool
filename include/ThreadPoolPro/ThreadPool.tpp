/**
 * @file ThreadPool.tpp
 * @brief ThreadPool template implementation.
 *
 * Contains the implementation of ThreadPool's template member
 * functions: task submission and the atomic-wait-based worker block.
 * Non-template members are implemented in ThreadPool.cpp.
 */

// ============================================================
// Template implementation for ThreadPoolPro::ThreadPool.
// ============================================================
//
//  Sections:
//   1. Task Submission
//   2. Worker Synchronization
//
// ============================================================

// clang-format off
#include <exception> // std::current_exception
#include <stdexcept> // std::runtime_error
#include <tuple>     // std::make_tuple, std::apply
#include <utility>   // std::forward, std::move
// clang-format on

namespace ThreadPoolPro {

// ============================================================
//  Section 1 — Task Submission
// ============================================================

template <typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> Detail::Future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
    using ReturnType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
    using State = Detail::ResultState<ReturnType>;

    // Single allocation for the whole round trip — replaces
    // std::packaged_task's own (larger, more general-purpose) internal
    // shared state. Two owners share it: this function's Task closure
    // (released after it runs) and the Future returned below (released
    // on destruction or after get()) — see Detail/Future.h.
    State* state = new State();

    auto runAndPublish = [state, func = std::decay_t<F>(std::forward<F>(f)),
                          argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        try {
            if constexpr (std::is_void_v<ReturnType>) {
                std::apply(std::move(func), std::move(argsTuple));
                state->setValue();
            } else {
                state->setValue(std::apply(std::move(func), std::move(argsTuple)));
            }
        } catch (...) {
            state->setException(std::current_exception());
        }

        state->release();
    };

    try {
        submit(Task(std::move(runAndPublish)));
    } catch (...) {
        // submit() (or Task's own construction) threw before the task
        // was ever queued, and we're about to throw out of enqueue()
        // itself — so neither of state's two owners will ever exist to
        // release their share: the closure was destroyed with the Task
        // without running, and the Future below is never constructed.
        // Release both shares here or `state` leaks.
        state->release();
        state->release();
        throw;
    }

    return Detail::Future<ReturnType>(state);
}

template <typename F> void ThreadPool::detach(F&& f) {
    submit(Task(std::decay_t<F>(std::forward<F>(f))));
}

// ============================================================
//  Section 2 — Worker Synchronization
// ============================================================

template <typename Predicate> void ThreadPool::waitUntil(Predicate predicate) noexcept {
    // Phase 1 — pure spin. A parked thread's wake-up (park() -> real
    // syscall -> scheduler re-admission) can cost low-single-digit
    // microseconds up to low milliseconds under load, which dwarfs the
    // cost of the small, closely-spaced tasks these pools are commonly
    // fed (see enqueue()/detach()). Spinning here means a task that
    // shows up moments after we went idle is picked up without ever
    // touching the OS scheduler.
    for (int i = 0; i < Detail::WaitSpinIterations; ++i) {
        if (predicate())
            return;

        Detail::cpuRelax();
    }

    // Phase 2 — yield. Work still hasn't shown up; ease off pure
    // spinning (which would otherwise just burn the core) but don't
    // fully park yet, so a producer thread sharing this core still gets
    // scheduled promptly.
    for (int i = 0; i < Detail::WaitYieldIterations; ++i) {
        if (predicate())
            return;

        std::this_thread::yield();
    }

    // Phase 3 — park. Nothing showed up after spinning and yielding;
    // give up the core for real via the atomic wake token.
    for (;;) {
        if (predicate())
            return;

        // Capture the token *after* the first (failed) predicate check,
        // then re-check once more before actually blocking. Any
        // wakeOne()/wakeAll() that bumps the token between these two
        // checks is guaranteed not to be missed — see the doc comment
        // on the declaration in ThreadPool.h.
        std::uint32_t token = wakeToken_.load(std::memory_order_acquire);

        if (predicate())
            return;

        wakeToken_.wait(token, std::memory_order_acquire);
    }
}

} // namespace ThreadPoolPro
