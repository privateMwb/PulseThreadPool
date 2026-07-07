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
        , tasks_{ std::make_unique<Task*[]>(cap) }
    {
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
    // larger buffer. This never reads-and-clears (moves) the source, and
    // never writes to it either — the old buffer's slots are left exactly
    // as they were, so a concurrent steal() that already captured the old
    // buffer pointer keeps seeing valid, unmodified data. Only whichever
    // side (old or new buffer) actually wins the index via the top/bottom
    // protocol ever dereferences and frees the pointee, so the duplicated
    // pointer value in the retired buffer is never touched again.
    Buffer* Buffer::grow(std::size_t bottom, std::size_t top) {
        Buffer* newBuf = new Buffer(capacity_ * 2);

        for (std::size_t i = top; i < bottom; ++i)
            newBuf->tasks_[i & newBuf->mask_] = tasks_[i & mask_];

        return newBuf;
    }

} // namespace ThreadPoolPro::Detail