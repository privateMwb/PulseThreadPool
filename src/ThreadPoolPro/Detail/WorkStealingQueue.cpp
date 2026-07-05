// ============================================================
// WorkStealingQueue.cpp
// Implementation for ThreadPoolPro::Detail::WorkStealingQueue.
// ============================================================
//
//  Sections:
//   1. Constructors & Destructor
//   2. Queue Operations
//   3. Capacity
//
// ============================================================

#include <ThreadPoolPro/Detail/WorkStealingQueue.h>

#include <cassert>
#include <bit>

namespace ThreadPoolPro::Detail {


// ============================================================
//  Section 1 — Constructors & Destructor
// ============================================================

WorkStealingQueue::WorkStealingQueue(std::size_t initialCapacity)
    : topIndex_{ 0 }
    , bottomIndex_{ 0 }
    , buffer_{ nullptr }
{
    assert(initialCapacity > 0);
    assert(std::has_single_bit(initialCapacity));

    retiredBuffers_.reserve(8);

    buffer_.store(
        new Buffer(initialCapacity),
        std::memory_order_relaxed
    );
}

WorkStealingQueue::~WorkStealingQueue() {
    delete buffer_.load(std::memory_order_relaxed);
}


// ============================================================
//  Section 2 — Queue Operations
// ============================================================

void WorkStealingQueue::pushBottom(Task&& task) {
    std::size_t bottom = bottomIndex_.load(std::memory_order_relaxed);
    std::size_t top    = topIndex_.load(std::memory_order_acquire);

    Buffer* buffer = buffer_.load(std::memory_order_relaxed);

    if (bottom - top > buffer->capacity_ - 1) {
        Buffer* oldBuffer = buffer;
        Buffer* newBuffer = oldBuffer->grow(bottom, top);

        // Publish the new active buffer.
        buffer_.store(newBuffer, std::memory_order_release);

        // Retire the previous buffer. Stealing threads may still be
        // reading from it, so defer destruction until queue teardown.
        retiredBuffers_.emplace_back(oldBuffer);

        buffer = newBuffer;
    }

    buffer->at(bottom) = std::move(task);

    std::atomic_thread_fence(std::memory_order_release);

    bottomIndex_.store(bottom + 1, std::memory_order_relaxed);
}

std::optional<Task> WorkStealingQueue::popBottom() {
    std::size_t bottom = bottomIndex_.load(std::memory_order_relaxed);

    if (bottom == 0)
        return std::nullopt;

    --bottom;

    bottomIndex_.store(bottom, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_seq_cst);

    std::size_t top = topIndex_.load(std::memory_order_relaxed);

    if (top > bottom) {
        bottomIndex_.store(bottom + 1, std::memory_order_relaxed);
        return std::nullopt;
    }

    Buffer* buffer = buffer_.load(std::memory_order_relaxed);

    if (top == bottom) {
        if (!topIndex_.compare_exchange_strong(
                top,
                top + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed)) {
            bottomIndex_.store(bottom + 1, std::memory_order_relaxed);
            return std::nullopt;
        }

        bottomIndex_.store(bottom + 1, std::memory_order_relaxed);
    }

    return std::optional<Task>{ std::move(buffer->at(bottom)) };
}

std::optional<Task> WorkStealingQueue::steal() {
    std::size_t top = topIndex_.load(std::memory_order_acquire);

    std::atomic_thread_fence(std::memory_order_seq_cst);

    std::size_t bottom = bottomIndex_.load(std::memory_order_acquire);

    if (top >= bottom)
        return std::nullopt;

    Buffer* buffer = buffer_.load(std::memory_order_acquire);

    if (!topIndex_.compare_exchange_strong(
      top,
      top + 1,
      std::memory_order_seq_cst,
      std::memory_order_relaxed)) {
        return std::nullopt;
      }

    return std::optional<Task>{ std::move(buffer->at(top)) };
}


// ============================================================
//  Section 3 — Capacity
// ============================================================

// Returns an approximate queue size.
// Concurrent pushes and steals may change the value immediately.
std::size_t WorkStealingQueue::size() const noexcept {
    const std::size_t bottom = bottomIndex_.load(std::memory_order_relaxed);
    const std::size_t top    = topIndex_.load(std::memory_order_relaxed);

    return bottom > top ? bottom - top : 0;
}

} // namespace ThreadPoolPro::Detail