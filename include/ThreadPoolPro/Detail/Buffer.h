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

    // Circular task storage.
    std::unique_ptr<Task[]> tasks_;

    // Constructors.
    explicit Buffer(std::size_t capacity);

    // Returns the task at the specified logical index.
    [[nodiscard]] Task& at(std::size_t index) noexcept;

    // Allocates a larger buffer and migrates the active task range.
    [[nodiscard]] Buffer* grow(std::size_t bottom, std::size_t top);
};

} // namespace ThreadPoolPro::Detail