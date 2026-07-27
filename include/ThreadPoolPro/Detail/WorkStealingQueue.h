/**
 * @file WorkStealingQueue.h
 * @brief Lock-free Chase-Lev work-stealing deque of Task pointers.
 */

#pragma once

#include "Buffer.h"
#include "Task.h"
#include "Utility.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace ThreadPoolPro::Detail {

/**
 * @brief Lock-free Chase-Lev work-stealing deque.
 */
class WorkStealingQueue {
  public:
    explicit WorkStealingQueue(
        std::size_t initialCapacity = 1024);

    ~WorkStealingQueue();

    WorkStealingQueue(
        const WorkStealingQueue&) = delete;

    WorkStealingQueue& operator=(
        const WorkStealingQueue&) = delete;

    void pushBottom(
        Task&& task);

    [[nodiscard]]
    std::optional<Task>
    popBottom();

    [[nodiscard]]
    std::optional<Task>
    steal();

    [[nodiscard]]
    std::size_t
    size() const noexcept;

  private:
    struct FreeNode {
        FreeNode* next;
    };

    [[nodiscard]]
    Task* acquireNode(
        Task&& task);

    void releaseNode(
        Task* node) noexcept;

    /*
     * Owner writes bottom.
     * Thieves write top.
     *
     * Cache-line separation prevents false sharing between the two.
     */
    alignas(CacheLineSize)
        std::atomic<std::size_t>
            topIndex_;

    alignas(CacheLineSize)
        std::atomic<std::size_t>
            bottomIndex_;

    std::atomic<Buffer*>
        buffer_;

    std::vector<
        std::unique_ptr<Buffer>>
        retiredBuffers_;

    FreeNode* freeHead_ =
        nullptr;

    std::size_t freeCount_ =
        0;
};

} // namespace ThreadPoolPro::Detail

namespace rain = ThreadPoolPro;