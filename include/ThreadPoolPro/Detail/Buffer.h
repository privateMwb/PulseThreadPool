#pragma once

#include <cstddef>
#include <memory>

#include "Task.h"

namespace ThreadPoolPro::Detail {

// Circular buffer used by the work-stealing queue.
//
// The buffer size is always a power of two, allowing indices to
// wrap efficiently using a bitmask instead of modulo arithmetic.
struct Buffer {

    // Buffer metadata.
    std::size_t capacity_;
    std::size_t mask_;

    // Circular task storage. Slots hold pointers to heap-allocated
    // Task objects so that growing the buffer only ever copies
    // pointer values — the old buffer is never mutated and stays
    // safe for concurrent readers (see grow()).
    std::unique_ptr<Task*[]> tasks_;

    // Constructors.
    explicit Buffer(std::size_t capacity);

    // Returns the task pointer at the specified logical index.
    [[nodiscard]] Task*& at(std::size_t index) noexcept;

    // Allocates a larger buffer and migrates the active task range.
    [[nodiscard]] Buffer* grow(std::size_t bottom, std::size_t top);
};

} // namespace ThreadPoolPro::Detail