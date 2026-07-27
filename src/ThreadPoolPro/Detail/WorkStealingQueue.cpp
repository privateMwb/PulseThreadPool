/**
 * @file WorkStealingQueue.cpp
 * @brief Chase-Lev work-stealing deque implementation.
 */

#include <ThreadPoolPro/Detail/WorkStealingQueue.h>

#include <bit>
#include <cassert>
#include <new>
#include <utility>

namespace ThreadPoolPro::Detail {


// ============================================================
// Helpers
// ============================================================

void WorkStealingQueue::destroyNode(
    Task* node) noexcept {

    if (!node)
        return;

    node->~Task();

    if constexpr (
        alignof(Task)
        > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {

        ::operator delete(
            static_cast<void*>(node),
            std::align_val_t(
                alignof(Task)));

    } else {

        ::operator delete(
            static_cast<void*>(node));
    }
}


// ============================================================
// Constructor
// ============================================================

WorkStealingQueue::WorkStealingQueue(
    std::size_t initialCapacity) {

    assert(
        initialCapacity > 0);

    assert(
        std::has_single_bit(
            initialCapacity));


    /*
     * Reserve enough space for the common case so growth of the
     * retired-buffer vector does not occur during queue resizing.
     */
    retiredBuffers_.reserve(16);


    Buffer* initial =
        new Buffer(
            initialCapacity);


    buffer_.store(
        initial,
        std::memory_order_relaxed);
}


// ============================================================
// Destructor
// ============================================================

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
     * The active buffer contains the currently live Task pointers.
     *
     * Retired buffers may contain stale copies of the same pointers
     * and therefore must never be deleted as Task owners.
     */
    if (buffer) {

        for (std::size_t i = top;
             i < bottom;
             ++i) {

            Task* node =
                buffer->at(i);

            destroyNode(
                node);
        }
    }


    delete buffer;


    /*
     * Destroy all owner-side recycled nodes.
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
// Push Bottom
// ============================================================

void WorkStealingQueue::pushBottom(
    Task&& task) {

    const std::size_t bottom =
        bottomIndex_.load(
            std::memory_order_relaxed);


    const std::size_t top =
        topIndex_.load(
            std::memory_order_acquire);


    Buffer* buffer =
        buffer_.load(
            std::memory_order_relaxed);


    /*
     * Buffer capacity is power-of-two.
     *
     * Leave one slot unused so that the circular buffer never
     * confuses full and empty states.
     */
    if (bottom - top
        >= buffer->capacity_ - 1) {

        Buffer* oldBuffer =
            buffer;


        Buffer* newBuffer =
            oldBuffer->grow(
                bottom,
                top);


        /*
         * Publish the new buffer before publishing the new bottom.
         */
        buffer_.store(
            newBuffer,
            std::memory_order_release);


        /*
         * Keep the old buffer alive for thieves.
         */
        retiredBuffers_.emplace_back(
            oldBuffer);


        buffer =
            newBuffer;
    }


    /*
     * Construct the Task node before publishing bottomIndex_.
     */
    Task* node =
        acquireNode(
            std::move(task));


    buffer->at(bottom) =
        node;


    /*
     * Publish the task pointer.
     */
    std::atomic_thread_fence(
        std::memory_order_release);


    bottomIndex_.store(
        bottom + 1,
        std::memory_order_relaxed);
}


// ============================================================
// Pop Bottom
// ============================================================

std::optional<Task>
WorkStealingQueue::popBottom() noexcept {

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
     * Synchronize with concurrent thieves.
     */
    std::atomic_thread_fence(
        std::memory_order_seq_cst);


    std::size_t top =
        topIndex_.load(
            std::memory_order_relaxed);


    /*
     * Queue was empty.
     */
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
     * Last item:
     *
     * Owner and thieves race for ownership.
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


        /*
         * Restore the canonical empty state.
         */
        bottomIndex_.store(
            bottom + 1,
            std::memory_order_relaxed);
    }


    Task* node =
        buffer->at(bottom);


    /*
     * Move the Task out before recycling its storage.
     */
    std::optional<Task> result{
        std::move(*node)
    };


    releaseNode(
        node);


    return result;
}


// ============================================================
// Steal
// ============================================================

std::optional<Task>
WorkStealingQueue::steal() noexcept {

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


    /*
     * The active buffer must be loaded after observing the queue
     * boundaries.
     */
    Buffer* buffer =
        buffer_.load(
            std::memory_order_acquire);


    /*
     * Exactly one thief can claim this slot.
     */
    if (!topIndex_.compare_exchange_strong(
            top,
            top + 1,
            std::memory_order_seq_cst,
            std::memory_order_relaxed)) {

        return std::nullopt;
    }


    Task* node =
        buffer->at(top);


    /*
     * The thief owns the node now.
     */
    std::optional<Task> result{
        std::move(*node)
    };


    /*
     * Never use the owner's free list from a thief.
     */
    destroyNode(
        node);


    return result;
}


// ============================================================
// Node Allocation
// ============================================================

Task* WorkStealingQueue::acquireNode(
    Task&& task) {

    /*
     * Owner-side recycling.
     */
    if (freeHead_) {

        void* raw =
            freeHead_;


        freeHead_ =
            freeHead_->next;


        --freeCount_;


        return ::new (
            raw)
            Task(
                std::move(task));
    }


    /*
     * Fresh allocation.
     */
    if constexpr (
        alignof(Task)
        > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {

        void* raw =
            ::operator new(
                sizeof(Task),
                std::align_val_t(
                    alignof(Task)));


        return ::new (
            raw)
            Task(
                std::move(task));

    } else {

        void* raw =
            ::operator new(
                sizeof(Task));


        return ::new (
            raw)
            Task(
                std::move(task));
    }
}


// ============================================================
// Node Recycling
// ============================================================

void WorkStealingQueue::releaseNode(
    Task* node) noexcept {

    if (!node)
        return;


    node->~Task();


    /*
     * Do not grow the free list beyond its configured bound.
     */
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