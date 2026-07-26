/**
 * @file Buffer.h
 * @brief Circular task-pointer buffer backing WorkStealingQueue.
 *
 * Contains the fixed-size, power-of-two circular buffer that
 * WorkStealingQueue indexes into. Stores `Task*` rather than `Task` by
 * value so that growing the buffer never has to relocate a `Task`
 * object a concurrent thief might be reading — see `grow()`.
 */

#pragma once

// clang-format off
#include "Task.h"  // Task — the pointee type this buffer's slots hold

#include <cstddef> // std::size_t
#include <memory>  // std::unique_ptr
// clang-format on

namespace ThreadPoolPro::Detail {

/**
 * @brief Fixed-capacity circular buffer of `Task*`, sized as a power of
 * two so logical indices wrap via a bitmask instead of modulo.
 * @details Owned exclusively by one `WorkStealingQueue` at a time. A
 * buffer is never mutated once superseded by a larger one from `grow()`
 * — the old buffer is only retained (not written to) so that a thief
 * thread already holding a pointer to it keeps reading valid data. See
 * `WorkStealingQueue::pushBottom()` for how old buffers are retired.
 */
struct Buffer {

    std::size_t capacity_; ///< Number of slots. Always a power of two.
    std::size_t mask_;     ///< `capacity_ - 1`; used for `index & mask_` instead of `%`.

    /// @brief Circular task-pointer storage. Each slot holds a pointer to
    /// a heap-allocated `Task`, not the `Task` itself — see the file-level
    /// comment for why.
    std::unique_ptr<Task*[]> tasks_;

    /**
     * @brief Allocates an empty buffer of `capacity` slots.
     * @param capacity Number of slots. Must be greater than 0 and a
     * power of two.
     */
    explicit Buffer(std::size_t capacity);

    /**
     * @brief Returns a reference to the slot for logical index `index`.
     * @param index Logical (monotonically increasing) index; wrapped
     * into range via `index & mask_`.
     * @return Reference to the `Task*` stored at that slot, so callers
     * can both read and overwrite it.
     */
    [[nodiscard]] Task*& at(std::size_t index) noexcept;

    /**
     * @brief Allocates a buffer at double this buffer's capacity and
     * copies the live `[top, bottom)` pointer range into it.
     * @param bottom Exclusive upper bound of the live range (the
     * queue's current bottom index).
     * @param top Inclusive lower bound of the live range (the queue's
     * current top index).
     * @return The newly allocated, larger buffer. Ownership transfers to
     * the caller.
     * @details Copies pointer *values* only — never reads through them,
     * never mutates `this` buffer. That makes it safe to run
     * concurrently with a thief thread that already captured `this` via
     * `WorkStealingQueue::buffer_` and is mid-`steal()`: the thief keeps
     * seeing the same, unmodified slot contents in the old buffer
     * regardless of how this call proceeds.
     */
    [[nodiscard]] Buffer* grow(std::size_t bottom, std::size_t top);
};

} // namespace ThreadPoolPro::Detail

/// @brief Short alias so this library can be used as `rain::ThreadPool`,
/// while its true namespace (and all internal diagnostics) remains
/// `ThreadPoolPro`. Repeated identically in every header of this project.
namespace rain = ThreadPoolPro;
