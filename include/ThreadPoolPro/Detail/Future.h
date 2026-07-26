/**
 * @file Future.h
 * @brief Lightweight move-only future, purpose-built for ThreadPool::enqueue().
 *
 * Replaces std::packaged_task + std::future in enqueue(): a single
 * manually-refcounted ResultState<T> (one heap allocation total, sized
 * for exactly what's needed — a value slot, an exception_ptr, and a
 * completion flag) instead of std::packaged_task's own general-purpose
 * shared state, and a spin -> yield -> park atomic-wait for get()
 * instead of std::future's condition_variable + mutex — the same
 * pattern ThreadPool::waitUntil() already uses for worker wakeup.
 */

#pragma once

// clang-format off
#include "Utility.h" // WaitSpinIterations, WaitYieldIterations, cpuRelax

#include <atomic>      // std::atomic — the completion flag and refcount
#include <cstdint>     // std::uint32_t — the completion flag's type
#include <exception>   // std::exception_ptr, std::rethrow_exception, std::current_exception
#include <optional>    // std::optional — value storage for non-void T
#include <stdexcept>   // std::logic_error — thrown by get() on an empty Future
#include <thread>      // std::this_thread::yield
#include <utility>     // std::forward, std::move
// clang-format on

namespace ThreadPoolPro::Detail {

/**
 * @brief Shared state between the task producing a result and the
 * Future consuming it.
 * @details Manually reference counted — exactly two owners (the
 * task's closure, and the Future) — rather than a shared_ptr, since
 * that fixed ownership shape doesn't need shared_ptr's general-purpose
 * atomic control block. The last owner to call release() deletes it.
 */
template <typename T> class ResultState {
  public:
    ResultState() noexcept : refCount_{2} {}

    ResultState(const ResultState&) = delete;
    ResultState& operator=(const ResultState&) = delete;

    /// @brief Stores the result and wakes anyone blocked in get().
    /// Called at most once, by the task's closure.
    template <typename... Args> void setValue(Args&&... args) {
        value_.emplace(std::forward<Args>(args)...);
        publish();
    }

    /// @brief Stores an exception (in place of a value) and wakes
    /// anyone blocked in get(). Called at most once, by the task's
    /// closure, if the task threw instead of returning normally.
    void setException(std::exception_ptr eptr) noexcept {
        exception_ = std::move(eptr);
        publish();
    }

    /// @brief Blocks until a value or exception has been published,
    /// then returns the value (moved out) or rethrows the exception.
    /// Must not be called more than once on the same ResultState.
    [[nodiscard]] T get() {
        wait();

        if (exception_)
            std::rethrow_exception(exception_);

        return std::move(*value_);
    }

    /// @brief Releases this owner's share. The second (last) caller
    /// deletes the state.
    void release() noexcept {
        if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete this;
    }

  private:
    void publish() noexcept {
        ready_.store(1, std::memory_order_release);
        ready_.notify_all();
    }

    // Spin -> yield -> park, identical in shape to
    // ThreadPool::waitUntil() — see that function's comments for the
    // rationale. Kept as a free-standing copy here rather than shared
    // with ThreadPool since this has no ThreadPool instance to call
    // into and the whole routine is a handful of lines.
    void wait() noexcept {
        for (int i = 0; i < WaitSpinIterations; ++i) {
            if (ready_.load(std::memory_order_acquire))
                return;

            cpuRelax();
        }

        for (int i = 0; i < WaitYieldIterations; ++i) {
            if (ready_.load(std::memory_order_acquire))
                return;

            std::this_thread::yield();
        }

        for (;;) {
            std::uint32_t observed = ready_.load(std::memory_order_acquire);

            if (observed != 0)
                return;

            ready_.wait(observed, std::memory_order_acquire);
        }
    }

    std::atomic<std::uint32_t> ready_{0};
    std::atomic<int> refCount_;
    std::optional<T> value_;
    std::exception_ptr exception_;
};

/// @brief ResultState<void> — no value slot, just completion + exception.
template <> class ResultState<void> {
  public:
    ResultState() noexcept : refCount_{2} {}

    ResultState(const ResultState&) = delete;
    ResultState& operator=(const ResultState&) = delete;

    void setValue() noexcept { publish(); }

    void setException(std::exception_ptr eptr) noexcept {
        exception_ = std::move(eptr);
        publish();
    }

    void get() {
        wait();

        if (exception_)
            std::rethrow_exception(exception_);
    }

    void release() noexcept {
        if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete this;
    }

  private:
    void publish() noexcept {
        ready_.store(1, std::memory_order_release);
        ready_.notify_all();
    }

    void wait() noexcept {
        for (int i = 0; i < WaitSpinIterations; ++i) {
            if (ready_.load(std::memory_order_acquire))
                return;

            cpuRelax();
        }

        for (int i = 0; i < WaitYieldIterations; ++i) {
            if (ready_.load(std::memory_order_acquire))
                return;

            std::this_thread::yield();
        }

        for (;;) {
            std::uint32_t observed = ready_.load(std::memory_order_acquire);

            if (observed != 0)
                return;

            ready_.wait(observed, std::memory_order_acquire);
        }
    }

    std::atomic<std::uint32_t> ready_{0};
    std::atomic<int> refCount_;
    std::exception_ptr exception_;
};

/**
 * @brief Move-only handle to a task's eventual result. Returned by
 * ThreadPool::enqueue() in place of std::future<T> — see the file
 * comment for why.
 */
template <typename T> class Future {
  public:
    Future() noexcept = default;

    /// @brief Takes ownership of one share of `state`. Used internally
    /// by enqueue(); not intended to be constructed directly.
    explicit Future(ResultState<T>* state) noexcept : state_{state} {}

    Future(Future&& other) noexcept : state_{other.state_} { other.state_ = nullptr; }

    Future& operator=(Future&& other) noexcept {
        if (this != &other) {
            releaseOwnedState();
            state_ = other.state_;
            other.state_ = nullptr;
        }

        return *this;
    }

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

    ~Future() { releaseOwnedState(); }

    /**
     * @brief Blocks until the result is ready, then returns it (or
     * rethrows the task's exception).
     * @throws std::logic_error if called on an empty Future (default-
     * constructed, moved-from, or already get()'d).
     * @details Consumes the Future — must be called at most once, same
     * contract as std::future::get().
     */
    T get() {
        if (!state_)
            throw std::logic_error("Future::get() called on an empty Future");

        ResultState<T>* state = state_;
        state_ = nullptr;

        struct Releaser {
            ResultState<T>* state;
            ~Releaser() { state->release(); }
        } releaser{state};

        return state->get();
    }

    /// @brief Returns whether this Future currently owns a shared state.
    [[nodiscard]] bool valid() const noexcept { return state_ != nullptr; }

  private:
    void releaseOwnedState() noexcept {
        if (state_) {
            state_->release();
            state_ = nullptr;
        }
    }

    ResultState<T>* state_ = nullptr;
};

} // namespace ThreadPoolPro::Detail

/// @brief Short alias so this library can be used as `rain::ThreadPool`,
/// while its true namespace (and all internal diagnostics) remains
/// `ThreadPoolPro`. Repeated identically in every header of this project.
namespace rain = ThreadPoolPro;
