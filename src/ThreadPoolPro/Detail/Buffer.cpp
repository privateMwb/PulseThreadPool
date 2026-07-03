// ============================================================
// Buffer.cpp
// Implementation for ThreadPoolPro::Detail::Buffer.
// ============================================================
//
//  Sections:
//   1. Constructors
//   2. Element Access
//   3. Buffer Growth
//
// ============================================================

#include <ThreadPoolPro/Detail/Buffer.h>

#include <cassert>
#include <bit>

namespace ThreadPoolPro::Detail {


    // ============================================================
    //  Section 1 — Constructors
    // ============================================================

    Buffer::Buffer(std::size_t cap)
        : capacity_{ cap }
        , mask_{ cap - 1 }
        , tasks_{ std::make_unique<Task[]>(cap) }
    {
        assert(capacity_ > 0);
        assert(std::has_single_bit(capacity_));
    }


    // ============================================================
    //  Section 2 — Element Access
    // ============================================================

    Task& Buffer::at(std::size_t index) noexcept {
        return tasks_[index & mask_];
    }


    // ============================================================
    //  Section 3 — Buffer Growth
    // ============================================================

    Buffer* Buffer::grow(std::size_t bottom, std::size_t top) {
        Buffer* newBuf = new Buffer(capacity_ * 2);

        for (std::size_t i = top; i < bottom; ++i)
            newBuf->tasks_[i & newBuf->mask_] = std::move(tasks_[i & mask_]);

        return newBuf;
    }

} // namespace ThreadPoolPro::Detail