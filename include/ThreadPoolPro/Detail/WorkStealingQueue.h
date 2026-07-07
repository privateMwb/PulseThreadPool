#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "Buffer.h"
#include "Task.h"
#include "Utility.h"

namespace ThreadPoolPro::Detail {

// Lock-free Chase-Lev work-stealing deque.
//
// Supports fast owner-thread push/pop operations while allowing
// concurrent stealing by other worker threads.
class WorkStealingQueue {
public:

    // Constructors and destructor.
    explicit WorkStealingQueue(std::size_t initialCapacity = 1024);
    ~WorkStealingQueue();

    WorkStealingQueue(const WorkStealingQueue&)            = delete;
    WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;

    // Queue operations.
    void pushBottom(Task&& task);
    [[nodiscard]] std::optional<Task> popBottom();
    [[nodiscard]] std::optional<Task> steal();

    // Capacity.
    [[nodiscard]] std::size_t size() const noexcept;

private:

    // Queue indices.
    alignas(CacheLineSize) std::atomic<std::size_t> topIndex_;
    alignas(CacheLineSize) std::atomic<std::size_t> bottomIndex_;

    // Active circular buffer.
    std::atomic<Buffer*> buffer_;

    // Previously allocated buffers retained until destruction.
    std::vector<std::unique_ptr<Buffer>> retiredBuffers_;
};

} // namespace ThreadPoolPro::Detail