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
 * @brief Chase-Lev work-stealing deque.
 *
 * One owner thread pushes/pops at the bottom.
 * Multiple thief threads steal from the top.
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
     * @brief Pushes a task at the owner end.
     */
    void pushBottom(
        Task&& task);


    /**
     * @brief Pops a task from the owner end.
     */
    [[nodiscard]]
    std::optional<Task>
    popBottom();


    /**
     * @brief Steals a task from the thief end.
     */
    [[nodiscard]]
    std::optional<Task>
    steal();


    /**
     * @brief Approximate queue size.
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


    alignas(CacheLineSize)
    std::atomic<std::size_t>
        topIndex_{0};


    alignas(CacheLineSize)
    std::atomic<std::size_t>
        bottomIndex_{0};


    /*
     * Active buffer.
     */
    std::atomic<Buffer*>
        buffer_{nullptr};


    /*
     * Retired buffers remain alive until destruction.
     */
    std::vector<
        std::unique_ptr<Buffer>>
        retiredBuffers_;


    /*
     * Owner-only free list.
     */
    FreeNode*
        freeHead_ = nullptr;


    std::size_t
        freeCount_ = 0;
};

} // namespace ThreadPoolPro::Detail

namespace rain = ThreadPoolPro;