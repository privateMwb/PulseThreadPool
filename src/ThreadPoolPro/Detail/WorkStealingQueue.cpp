/**
 * @file WorkStealingQueue.cpp
 * @brief WorkStealingQueue implementation.
 */

#include <ThreadPoolPro/Detail/WorkStealingQueue.h>

#include <bit>
#include <cassert>
#include <new>

namespace ThreadPoolPro::Detail {


// ============================================================
// Constructors & Destructor
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


    /*
     * Buffer growth is infrequent.
     * Reserve enough room for the common growth history so that
     * vector reallocation doesn't occur during the hot path.
     */
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
     * Only the active buffer owns the currently reachable pointers.
     *
     * Retired buffers contain stale pointer copies.
     */
    for (std::size_t i = top;
         i < bottom;
         ++i) {

        delete buffer->at(i);
    }


    delete buffer;


    /*
     * Destroy recycled nodes.
     */
    while (freeHead_) {

        FreeNode* next =
            freeHead_->next;


        if constexpr (
            alignof(Task) >
            __STDCPP_DEFAULT_NEW_ALIGNMENT__) {

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
// pushBottom
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
     * Grow only when the circular buffer is actually full.
     */
    if (bottom - top >
        buffer->capacity_ - 1) {

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


    buffer->at(bottom) =
        acquireNode(
            std::move(task));


    /*
     * Publish the task before publishing bottom.
     */
    std::atomic_thread_fence(
        std::memory_order_release);


    bottomIndex_.store(
        bottom + 1,
        std::memory_order_relaxed);
}


// ============================================================
// popBottom
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


    /*
     * Chase-Lev owner/thief synchronization.
     *
     * This fence is required for the last-element race.
     */
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
     * Last-element race.
     *
     * The owner and a thief may both attempt to claim it.
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


    releaseNode(ptr);


    return result;
}


// ============================================================
// steal
// ============================================================

std::optional<Task>
WorkStealingQueue::steal() {

    /*
     * Acquire-load top.
     */
    std::size_t top =
        topIndex_.load(
            std::memory_order_acquire);


    /*
     * Synchronize with the owner's bottom publication.
     */
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
     * Only one thief can claim this top index.
     *
     * seq_cst is retained here because this is the critical
     * Chase-Lev last-element race.
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
     * Thieves never touch the owner's free list.
     */
    delete ptr;


    return result;
}


// ============================================================
// Node Recycling
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
        alignof(Task) >
        __STDCPP_DEFAULT_NEW_ALIGNMENT__) {

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


void WorkStealingQueue::releaseNode(
    Task* node) noexcept {

    node->~Task();


    if (freeCount_ >=
        TaskFreeListCapacity) {

        if constexpr (
            alignof(Task) >
            __STDCPP_DEFAULT_NEW_ALIGNMENT__) {

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
// Capacity
// ============================================================

std::size_t
WorkStealingQueue::size() const noexcept {

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