/**
 * @file Task.h
 * @brief Move-only, type-erased callable wrapper with small-buffer
 * optimization.
 *
 * Contains Detail::Task, the unit of work stored in
 * WorkStealingQueue and the global injection queue.
 */

#pragma once

// clang-format off
#include "Utility.h" // SboCapacity — size of Task's inline storage buffer
#include "VTable.h"  // VTable, getVTable — the type-erased operation table Task dispatches through

#include <cstddef>     // std::byte, std::max_align_t
#include <type_traits> // std::decay_t
#include <utility>     // std::forward
// clang-format on

namespace ThreadPoolPro::Detail {

/**
 * @brief Move-only, type-erased callable wrapper with small-buffer
 * optimization.
 * @details Stores callables whose decayed type fits within
 * `SboCapacity` bytes and is nothrow-move-constructible directly inline;
 * everything else is heap-allocated. Type erasure is done through a
 * single shared `VTable*` per callable type (see VTable.h) rather than
 * `std::function`'s per-instance heap-allocated wrapper, so the common
 * case — a small lambda submitted to the pool — costs zero additional
 * allocations beyond whatever the caller's own callable required.
 */
class Task {
  private:
    /// @brief Returns a pointer to wherever this Task's callable
    /// currently lives — `inlineStorage_` or `heapPtr_`, depending on
    /// `isHeap_`.
    [[nodiscard]] void* target() noexcept;

    /// @brief Destroys and releases the currently held callable, if any,
    /// and leaves this `Task` empty. Idempotent — safe to call on an
    /// already-empty `Task`.
    void reset() noexcept;

    /// @brief Inline storage for small, nothrow-movable callables.
    alignas(std::max_align_t) std::byte inlineStorage_[SboCapacity];

    /// @brief Heap allocation for callables too large (or not
    /// nothrow-movable) for `inlineStorage_`. Null whenever `isHeap_` is
    /// `false`.
    void* heapPtr_ = nullptr;

    /// @brief Type-erased operations for the held callable, or `nullptr`
    /// if this `Task` is empty.
    const VTable* vtable_ = nullptr;

    /// @brief Whether the held callable lives at `heapPtr_` (`true`) or
    /// inline at `inlineStorage_` (`false`).
    bool isHeap_ = false;

  public:
    /// @brief Constructs an empty `Task`. `operator()` on an empty `Task`
    /// throws; `operator bool` reports `false`.
    Task() noexcept = default;

    /**
     * @brief Constructs a `Task` wrapping callable `f`.
     * @tparam F Deduced callable type; stored as `std::decay_t<F>`.
     * @param f Callable to wrap. Copied into inline storage if
     * `std::decay_t<F>` fits `SboCapacity` and is nothrow-move-
     * constructible, otherwise heap-allocated.
     */
    template <typename F> Task(F&& f);

    /// @brief Move-constructs from `other`, leaving `other` empty.
    Task(Task&& other) noexcept;
    /// @brief Move-assigns from `other`, releasing any callable this
    /// `Task` previously held and leaving `other` empty.
    Task& operator=(Task&& other) noexcept;

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    /// @brief Releases the held callable, if any.
    ~Task() noexcept;

    /**
     * @brief Invokes the held callable.
     * @throws std::logic_error if this `Task` is empty. Deliberately not
     * a silent no-op — a lost or corrupted task should surface as a
     * counted exception in the pool's worker loop, not vanish silently.
     */
    void operator()();

    /// @brief Returns whether this `Task` currently holds a callable.
    [[nodiscard]] explicit operator bool() const noexcept;
};

} // namespace ThreadPoolPro::Detail

#include "Task.tpp"

/// @brief Short alias so this library can be used as `rain::ThreadPool`,
/// while its true namespace (and all internal diagnostics) remains
/// `ThreadPoolPro`. Repeated identically in every header of this project.
namespace rain = ThreadPoolPro;
