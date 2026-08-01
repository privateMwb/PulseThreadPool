/**
 * @file Buffer.cpp
 * @brief Buffer implementation.
 *
 * Contains the implementation of Detail::Buffer's construction, element
 * access, and growth operations.
 */

// ============================================================
// Implementation for ThreadPoolPro::Detail::Buffer.
// ============================================================
//
//  Sections:
//   1. Constructors
//   2. Element Access
//   3. Buffer Growth
//
// ============================================================

// clang-format off
#include <ThreadPoolPro/Detail/Buffer.h> // Buffer — the class this file implements

#include <bit>     // std::has_single_bit
#include <cassert> // assertl
// clang-format on

namespace ThreadPoolPro::Detail {

// ============================================================
//  Section 1 — Constructors
// ============================================================

Buffer::Buffer(std::size_t capacity)
    : capacity_{capacity}, mask_{capacity - 1}, tasks_{std::make_unique<Task*[]>(capacity)} {
    assert(capacity_ > 0);
    assert(std::has_single_bit(capacity_));
}

// ============================================================
//  Section 2 — Element Access
// ============================================================

Task*& Buffer::at(std::size_t index) noexcept {
    return tasks_[index & mask_];
}

// ============================================================
//  Section 3 — Buffer Growth
// ============================================================

// Copies the live [top, bottom) range of pointer values into a new,
// larger buffer. This never reads through the source pointers and never
// writes to the source buffer — the old buffer's slots are left exactly
// as they were, so a concurrent steal() that already captured the old
// buffer pointer keeps seeing valid, unmodified data. Only whichever
// side (old or new buffer) actually wins the index via the top/bottom
// protocol ever dereferences and frees the pointee, so the duplicated
// pointer value left behind in the retired buffer is never touched again.
Buffer* Buffer::grow(std::size_t bottom, std::size_t top) {
    Buffer* newBuf = new Buffer(capacity_ * 2);

    for (std::size_t i = top; i < bottom; ++i)
        newBuf->tasks_[i & newBuf->mask_] = tasks_[i & mask_];

    return newBuf;
}

} // namespace ThreadPoolPro::Detail

