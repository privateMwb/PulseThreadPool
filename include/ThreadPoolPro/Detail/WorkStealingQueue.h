/**
 * @file WorkStealingQueue.h
 * @brief Chase-Lev work-stealing deque.
 */

#pragma once

// clang-format off
#include "Buffer.h"
#include "Task.h"
#include "Utility.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>
// clang-format on

namespace ThreadPoolPro::Detail {

/**
 * @brief Lock-free Chase-Lev work-stealing deque.
 *
 * The owning worker is the only thread allowed to push and pop from
 * the bottom. Any number of thief threads may steal from the top.
 *
 * The queue stores Task* rather than Task objects directly. Owner-side
 * nodes are recycled through a private free list. Thieves never touch
 * that free list.
 *
 * Lifetime requirement:
 * - The owner must stop using the queue before destruction.
 * - All thieves must have stopped before destruction.
 */
class WorkStealingQueue {
  public:
    explicit WorkStealingQueue(
        std::size_t initialCapacity = 4096);

    ~WorkStealingQueue();

    WorkStealingQueue(
        const WorkStealingQueue&) = delete;

    WorkStealingQueue& operator=(
        const WorkStealingQueue&) = delete;


    /**
     * @brief Pushes a task onto the owner end.
     *
     * Owner thread only.
     */
    void pushBottom(
        Task&& task);


    /**
     * @brief Pops a task from the owner end.
     *
     * Owner thread only.
     */
    [[nodiscard]]
    std::optional<Task>
    popBottom() noexcept;


    /**
     * @brief Attempts to steal a task.
     *
     * May be called concurrently by multiple thieves.
     */
    [[nodiscard]]
    std::optional<Task>
    steal() noexcept;


    /**
     * @brief Returns an approximate queue size.
     */
    [[nodiscard]]
    std::size_t size() const noexcept;


  private:

    struct FreeNode {
        FreeNode* next;
    };


    [[nodiscard]]
    Task* acquireNode(
        Task&& task);


    void releaseNode(
        Task* node) noexcept;


    static void destroyNode(
        Task* node) noexcept;


    alignas(CacheLineSize)
    std::atomic<std::size_t>
        topIndex_{0};


    alignas(CacheLineSize)
    std::atomic<std::size_t>
        bottomIndex_{0};


    /*
     * The currently active circular buffer.
     *
     * Only the owner replaces this pointer.
     * Thieves load it when stealing.
     */
    std::atomic<Buffer*>
        buffer_{nullptr};


    /*
     * Old buffers cannot immediately be destroyed because a thief
     * may have loaded an old buffer pointer immediately before a
     * resize.
     *
     * They are therefore retained until queue destruction.
     */
    std::vector<
        std::unique_ptr<Buffer>>
        retiredBuffers_;


    /*
     * Owner-only Task node cache.
     */
    FreeNode*
        freeHead_ = nullptr;


    std::size_t
        freeCount_ = 0;
};

} // namespace ThreadPoolPro::Detail

namespace rain = ThreadPoolPro;