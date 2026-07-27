/**
 * @file WorkStealingQueue.cpp
 * @brief Chase-Lev work-stealing deque implementation.
 */

#include <ThreadPoolPro/Detail/WorkStealingQueue.h>

#include <bit>
#include <cassert>
#include <new>

namespace ThreadPoolPro::Detail {


// ============================================================
// Constructor / Destructor
// ============================================================

WorkStealingQueue::WorkStealingQueue(
    std::size_t initialCapacity)
    : topIndex_{0}
    , bottomIndex_{0}
    , buffer_{nullptr} {

    assert(
        initialCapacity > 0);

    assert(
        std::has_single_bit(
            initialCapacity));

    retiredBuffers_.reserve(8);

    buffer_.store(
        new Buffer(initialCapacity),
        std::memory_order_relaxed);
}


WorkStealingQueue::~WorkStealingQueue() {

    Buffer* buffer =
        buffer_.load(
            std::memory_order_relaxed);

    const std::size_t top =
        topIndex_.load(
            std::memory_order_relaxed);

    const std::size_t bottom =
        bottomIndex_.load(
            std::memory_order_relaxed);


    /*
     * Only the active buffer owns the live pointers.
     *
     * Retired buffers contain stale copies.
     */
    for (std::size_t i = top;
         i < bottom;
         ++i) {

        delete buffer->at(i);
    }


    delete buffer;


    /*
     * Free recycled owner nodes.
     */
    while (freeHead_) {

        FreeNode* next =
            freeHead_->next;


        if constexpr (
            alignof(Task)
            > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {

            ::operator delete(
                static_cast<void*>(
                    freeHead_),
                std::align_val_t(
                    alignof(Task)));

        } else {

            ::operator delete(
                static_cast<void*>(
                    freeHead_));
        }


        freeHead_ =
            next;
    }
}


// ============================================================
// Push
// ============================================================

void WorkStealingQueue::pushBottom(
    Task&& task) {

    std::size_t bottom =
        bottomIndex_.load(
            std::memory_order_relaxed);

    const std::size_t top =
        topIndex_.load(
            std::memory_order_acquire);

    Buffer* buffer =
        buffer_.load(
            std::memory_order_relaxed);


    /*
     * Grow only when the ring is actually full.
     */
    if (bottom - top
        >= buffer->capacity_ - 1) {

        Buffer* oldBuffer =
            buffer;

        Buffer* newBuffer =
            oldBuffer->grow(
                bottom,
                top);


        buffer_.store(
            newBuffer,
            std::memory_order_release);


        retiredBuffers_.emplace_back(
            oldBuffer);


        buffer =
            newBuffer;
    }


    /*
     * Allocate/recycle the Task node before
     * publishing the bottom index.
     */
    buffer->at(bottom) =
        acquireNode(
            std::move(task));


    /*
     * Release fence publishes the task pointer
     * before thieves observe bottom.
     */
    std::atomic_thread_fence(
        std::memory_order_release);


    bottomIndex_.store(
        bottom + 1,
        std::memory_order_relaxed);
}


// ============================================================
// Owner Pop
// ============================================================

std::optional<Task>
WorkStealingQueue::popBottom() {

    std::size_t bottom =
        bottomIndex_.load(
            std::memory_order_relaxed);


    if (bottom == 0)
        return std::nullopt;


    --bottom;


    bottomIndex_.store(
        bottom,
        std::memory_order_relaxed);


    std::atomic_thread_fence(
        std::memory_order_seq_cst);


    std::size_t top =
        topIndex_.load(
            std::memory_order_relaxed);


    if (top > bottom) {

        bottomIndex_.store(
            bottom + 1,
            std::memory_order_relaxed);

        return std::nullopt;
    }


    Buffer* buffer =
        buffer_.load(
            std::memory_order_relaxed);


    /*
     * Last element.
     *
     * Owner competes with thieves.
     */
    if (top == bottom) {

        if (!topIndex_.compare_exchange_strong(
                top,
                top + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed)) {

            bottomIndex_.store(
                bottom + 1,
                std::memory_order_relaxed);

            return std::nullopt;
        }


        bottomIndex_.store(
            bottom + 1,
            std::memory_order_relaxed);
    }


    Task* ptr =
        buffer->at(bottom);


    std::optional<Task> result{
        std::move(*ptr)
    };


    /*
     * Owner-side recycling is safe.
     */
    releaseNode(ptr);


    return result;
}


// ============================================================
// Steal
// ============================================================

std::optional<Task>
WorkStealingQueue::steal() {

    std::size_t top =
        topIndex_.load(
            std::memory_order_acquire);


    std::atomic_thread_fence(
        std::memory_order_seq_cst);


    const std::size_t bottom =
        bottomIndex_.load(
            std::memory_order_acquire);


    if (top >= bottom)
        return std::nullopt;


    Buffer* buffer =
        buffer_.load(
            std::memory_order_acquire);


    /*
     * The CAS is the only operation that arbitrates
     * competing thieves.
     */
    if (!topIndex_.compare_exchange_strong(
            top,
            top + 1,
            std::memory_order_seq_cst,
            std::memory_order_relaxed)) {

        return std::nullopt;
    }


    Task* ptr =
        buffer->at(top);


    std::optional<Task> result{
        std::move(*ptr)
    };


    /*
     * Never touch the owner free list from a thief.
     */
    delete ptr;


    return result;
}


// ============================================================
// Node Allocation
// ============================================================

Task* WorkStealingQueue::acquireNode(
    Task&& task) {

    if (freeHead_) {

        void* raw =
            freeHead_;

        freeHead_ =
            freeHead_->next;

        --freeCount_;


        return ::new (raw)
            Task(
                std::move(task));
    }


    if constexpr (
        alignof(Task)
        > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {

        void* raw =
            ::operator new(
                sizeof(Task),
                std::align_val_t(
                    alignof(Task)));

        return ::new (raw)
            Task(
                std::move(task));

    } else {

        void* raw =
            ::operator new(
                sizeof(Task));

        return ::new (raw)
            Task(
                std::move(task));
    }
}


// ============================================================
// Node Recycling
// ============================================================

void WorkStealingQueue::releaseNode(
    Task* node) noexcept {

    node->~Task();


    if (freeCount_
        >= TaskFreeListCapacity) {

        if constexpr (
            alignof(Task)
            > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {

            ::operator delete(
                static_cast<void*>(
                    node),
                std::align_val_t(
                    alignof(Task)));

        } else {

            ::operator delete(
                static_cast<void*>(
                    node));
        }

        return;
    }


    FreeNode* freeNode =
        ::new (
            static_cast<void*>(
                node))
        FreeNode{
            freeHead_
        };


    freeHead_ =
        freeNode;

    ++freeCount_;
}


// ============================================================
// Size
// ============================================================

std::size_t
WorkStealingQueue::size()
    const noexcept {

    const std::size_t bottom =
        bottomIndex_.load(
            std::memory_order_relaxed);

    const std::size_t top =
        topIndex_.load(
            std::memory_order_relaxed);


    return bottom > top
        ? bottom - top
        : 0;
}

} // namespace ThreadPoolPro::Detail

namespace rain = ThreadPoolPro;