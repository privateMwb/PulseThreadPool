/**
 * @file WorkStealingQueue.cpp
 * @brief WorkStealingQueue implementation.
 *
 * Contains the implementation of WorkStealingQueue's construction,
 * destruction, queue operations, node recycling, and capacity query.
 */

// ============================================================
// Implementation for ThreadPoolPro::Detail::WorkStealingQueue.
// ============================================================
//
//  Sections:
//   1. Constructors & Destructor
//   2. Queue Operations
//   3. Node Recycling
//   4. Capacity
//
// ============================================================

// clang-format off
#include <ThreadPoolPro/Detail/WorkStealingQueue.h> // WorkStealingQueue — the class this file implements

#include <bit>     // std::has_single_bit
#include <cassert> // assert
#include <new>     // placement new and __STDCPP_DEFAULT_NEW_ALIGNMENT__
// clang-format on

namespace ThreadPoolPro::Detail {

// ============================================================
//  Section 1 — Constructors & Destructor
// ============================================================

WorkStealingQueue::WorkStealingQueue(std::size_t initialCapacity)
    : topIndex_{0}, bottomIndex_{0}, buffer_{nullptr} {
    assert(initialCapacity > 0);
    assert(std::has_single_bit(initialCapacity));

    retiredBuffers_.reserve(8);

    buffer_.store(new Buffer(initialCapacity), std::memory_order_relaxed);
}

WorkStealingQueue::~WorkStealingQueue() {
    Buffer* buffer = buffer_.load(std::memory_order_relaxed);

    // Free any tasks still pending in the active buffer. Retired
    // buffers only hold stale duplicate pointer values (see
    // Buffer::grow()), never the sole owner of a pointee, so they must
    // not be freed here.
    std::size_t top = topIndex_.load(std::memory_order_relaxed);
    std::size_t bottom = bottomIndex_.load(std::memory_order_relaxed);

    for (std::size_t i = top; i < bottom; ++i)
        delete buffer->at(i);

    delete buffer;

    // Release the recycled-node free list. Each node's storage was
    // last constructed as a FreeNode (its Task was already destroyed in
    // releaseNode()), so this frees raw memory rather than deleting a
    // Task — matching the allocation alignment acquireNode() used.
    while (freeHead_) {
        FreeNode* next = freeHead_->next;

        if constexpr (alignof(Task) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            ::operator delete(static_cast<void*>(freeHead_), std::align_val_t(alignof(Task)));
        else
            ::operator delete(static_cast<void*>(freeHead_));

        freeHead_ = next;
    }
}

// ============================================================
//  Section 2 — Queue Operations
// ============================================================

void WorkStealingQueue::pushBottom(Task&& task) {
    std::size_t bottom = bottomIndex_.load(std::memory_order_relaxed);
    std::size_t top = topIndex_.load(std::memory_order_acquire);

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

    buffer->at(bottom) = acquireNode(std::move(task));

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
        if (!topIndex_.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst,
                                               std::memory_order_relaxed)) {
            bottomIndex_.store(bottom + 1, std::memory_order_relaxed);
            return std::nullopt;
        }

        bottomIndex_.store(bottom + 1, std::memory_order_relaxed);
    }

    Task* ptr = buffer->at(bottom);
    std::optional<Task> result{std::move(*ptr)};
    releaseNode(ptr);
    return result;
}

std::optional<Task> WorkStealingQueue::steal() {
    std::size_t top = topIndex_.load(std::memory_order_acquire);

    std::atomic_thread_fence(std::memory_order_seq_cst);

    std::size_t bottom = bottomIndex_.load(std::memory_order_acquire);

    if (top >= bottom)
        return std::nullopt;

    Buffer* buffer = buffer_.load(std::memory_order_acquire);

    if (!topIndex_.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst,
                                           std::memory_order_relaxed)) {
        return std::nullopt;
    }

    Task* ptr = buffer->at(top);
    std::optional<Task> result{std::move(*ptr)};

    // Cross-thread: this runs on a thief thread, never the owner, so it
    // must not touch the owner-thread-only free list — see
    // releaseNode()'s doc comment. Plain delete is safe here since it
    // goes through the global allocator, which is thread-safe.
    delete ptr;

    return result;
}

// ============================================================
//  Section 3 — Node Recycling
// ============================================================

Task* WorkStealingQueue::acquireNode(Task&& task) {
    if (freeHead_) {
        void* raw = freeHead_;
        freeHead_ = freeHead_->next;
        --freeCount_;
        return ::new (raw) Task(std::move(task));
    }

    if constexpr (alignof(Task) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        void* raw = ::operator new(sizeof(Task), std::align_val_t(alignof(Task)));
        return ::new (raw) Task(std::move(task));
    } else {
        void* raw = ::operator new(sizeof(Task));
        return ::new (raw) Task(std::move(task));
    }
}

void WorkStealingQueue::releaseNode(Task* node) noexcept {
    node->~Task();

    if (freeCount_ >= TaskFreeListCapacity) {
        if constexpr (alignof(Task) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            ::operator delete(static_cast<void*>(node), std::align_val_t(alignof(Task)));
        else
            ::operator delete(static_cast<void*>(node));
        return;
    }

    FreeNode* freeNode = ::new (static_cast<void*>(node)) FreeNode{freeHead_};
    freeHead_ = freeNode;
    ++freeCount_;
}

// ============================================================
//  Section 4 — Capacity
// ============================================================

// Returns an approximate queue size.
// Concurrent pushes and steals may change the value immediately.
std::size_t WorkStealingQueue::size() const noexcept {
    const std::size_t bottom = bottomIndex_.load(std::memory_order_relaxed);
    const std::size_t top = topIndex_.load(std::memory_order_relaxed);

    return bottom > top ? bottom - top : 0;
}

} // namespace ThreadPoolPro::Detail

