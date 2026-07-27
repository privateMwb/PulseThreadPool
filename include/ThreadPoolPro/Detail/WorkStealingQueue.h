/**
 * @file WorkStealingQueue.h
 * @brief Lock-free Chase-Lev work-stealing deque of Task pointers.
 *
 * Contains WorkStealingQueue, the per-worker task deque that
 * ThreadPool uses for local task storage and cross-thread stealing.
 */

#pragma once

// clang-format off
#include "Buffer.h"  // Buffer — the circular Task* storage this queue indexes into
#include "Task.h"    // Task — the element type pushed, popped, and stolen
#include "Utility.h" // CacheLineSize, TaskFreeListCapacity — index alignment and free-list cap

#include <atomic>   // std::atomic
#include <cstddef>  // std::size_t
#include <memory>   // std::unique_ptr
#include <optional> // std::optional
#include <vector>   // std::vector
// clang-format on

namespace ThreadPoolPro::Detail {

/**
 * @brief Lock-free Chase-Lev work-stealing deque of `Task`.
 * @details The owning worker thread pushes and pops its own end
 * (`pushBottom`/`popBottom`) with no atomic read-modify-write in the
 * uncontended case; other worker threads steal from the opposite end
 * (`steal()`) using a single CAS. Memory ordering follows Lê et al.'s
 * "Correct and Efficient Work-Stealing for Weak Memory Models".
 *
 * The buffer stores `Task*`, not `Task` by value — see Buffer.h for why
 * — which means every push/pop/steal implies a heap allocation or
 * deallocation of the pointed-to `Task`. Because `pushBottom()` and
 * `popBottom()` only ever run on the single owning thread, this class
 * recycles those allocations through a private, non-atomic free list
 * (`freeHead_`) instead of calling `new`/`delete` on every operation —
 * safe precisely because that free list is never touched from any other
 * thread.
 *
 * `steal()` runs on thief threads, so it can't touch `freeHead_`
 * directly — but it doesn't fall back to a real `delete` either. Freed
 * nodes are instead pushed onto `remoteFreeHead_`, a lock-free
 * multi-producer/single-consumer stack: any number of thieves may push
 * to it concurrently (one CAS each, no lock), and only the owner thread
 * ever drains it (via `acquireNode()`), so the drain itself needs no
 * synchronization beyond the single atomic exchange that claims the
 * whole chain. Under steal-heavy contention this turns what would
 * otherwise be a real free()/malloc() round trip per stolen task into a
 * single uncontended-in-practice CAS — the dominant cost this design
 * avoids.
 */
class WorkStealingQueue {
  public:
    /**
     * @brief Constructs an empty queue with a starting buffer capacity.
     * @param initialCapacity Initial number of slots. Must be a power of
     * two.
     */
    explicit WorkStealingQueue(std::size_t initialCapacity = 1024);

    /// @brief Deletes any still-queued tasks and releases the active
    /// buffer, retired buffers, and the recycled-node free list.
    /// @details Assumes exclusive access — the owning worker thread must
    /// have already stopped, and no thief may still be calling `steal()`.
    ~WorkStealingQueue();

    WorkStealingQueue(const WorkStealingQueue&) = delete;
    WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;

    /**
     * @brief Pushes `task` onto the bottom (owner) end of the queue.
     * @param task Task to push. Moved from.
     * @details Owner-thread-only. Grows the active buffer first if it's
     * full. Allocates (or reuses a recycled) heap node to hold `task`.
     */
    void pushBottom(Task&& task);

    /**
     * @brief Pops a task from the bottom (owner) end of the queue.
     * @return The popped task, or `std::nullopt` if the queue is empty
     * or this pop lost a race with a concurrent `steal()` for the last
     * remaining element.
     * @details Owner-thread-only.
     */
    [[nodiscard]] std::optional<Task> popBottom();

    /**
     * @brief Attempts to steal a task from the top (thief) end of the
     * queue.
     * @return The stolen task, or `std::nullopt` if the queue currently
     * appears empty or this steal lost a CAS race with another thief or
     * with the owner's `popBottom()`.
     * @details Safe to call concurrently from any number of threads
     * other than the owner, and concurrently with the owner's
     * `pushBottom()`/`popBottom()`.
     */
    [[nodiscard]] std::optional<Task> steal();

    /**
     * @brief Returns an approximate number of queued tasks.
     * @return `bottom - top`, or 0 if that would underflow. Concurrent
     * pushes, pops, and steals may change the true value immediately
     * after this returns — for statistics/diagnostics only.
     */
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    /// @brief Intrusive free-list node. Placement-constructed directly
    /// into the storage of a just-destroyed `Task`, so recycling a node
    /// costs one pointer write rather than a `delete`+`new` pair.
    struct FreeNode {
        FreeNode* next;
    };

    /**
     * @brief Obtains a heap-allocated `Task*` initialized from `task`,
     * preferring a recycled node from `freeHead_` over a fresh
     * allocation.
     * @param task Task to move into the (re)used storage.
     * @return Pointer to the newly constructed `Task`, ready to store in
     * the buffer.
     * @details Owner-thread-only — reads and updates `freeHead_` with no
     * synchronization.
     */
    [[nodiscard]] Task* acquireNode(Task&& task);

    /**
     * @brief Destroys the `Task` at `node` and returns its storage to
     * the free list for reuse, unless the list is already at
     * `TaskFreeListCapacity`, in which case the memory is freed instead.
     * @param node Node to release. Must not be used again by the caller.
     * @details Owner-thread-only — reads and updates `freeHead_` with no
     * synchronization.
     */
    void releaseNode(Task* node) noexcept;

    /**
     * @brief Atomically claims the entire chain currently on
     * `remoteFreeHead_` (nodes freed by `steal()` on other threads) and
     * merges it into the private `freeHead_` list, respecting
     * `TaskFreeListCapacity` (excess nodes are freed immediately rather
     * than retained unboundedly).
     * @details Owner-thread-only to call, but safe to run concurrently
     * with any number of thieves still pushing onto `remoteFreeHead_` —
     * the single atomic exchange either claims a node a thief already
     * finished pushing, or leaves it for the next drain; either way no
     * node is ever seen by two threads at once.
     */
    void drainRemoteFreeList() noexcept;

    // Queue indices. Cache-line-separated because they're written by
    // different, potentially concurrently-running threads (owner vs.
    // thieves) and false sharing between them would serialize otherwise
    // independent operations.
    alignas(CacheLineSize) std::atomic<std::size_t> topIndex_;
    alignas(CacheLineSize) std::atomic<std::size_t> bottomIndex_;

    /// @brief Active circular buffer. Replaced (never mutated) by
    /// `pushBottom()` when growth is needed.
    std::atomic<Buffer*> buffer_;

    /// @brief Buffers superseded by growth, retained until this queue is
    /// destroyed because a thief may still hold a pointer to one.
    std::vector<std::unique_ptr<Buffer>> retiredBuffers_;

    /// @brief Head of the owner-thread-only recycled-node free list.
    /// `nullptr` when empty. Never touched outside `acquireNode()`,
    /// `releaseNode()`, `drainRemoteFreeList()`, and the destructor.
    FreeNode* freeHead_ = nullptr;

    /// @brief Current number of nodes on `freeHead_`'s list, kept so
    /// `releaseNode()`/`drainRemoteFreeList()` can enforce
    /// `TaskFreeListCapacity` in O(1).
    std::size_t freeCount_ = 0;

    /// @brief Lock-free MPSC stack of nodes freed by `steal()` on other
    /// threads, awaiting a drain into `freeHead_` by the owner — see the
    /// class-level comment. Cache-line separated: unlike `freeHead_`,
    /// this is written concurrently by every thief, so it would
    /// otherwise false-share with (and slow down) the owner-only state
    /// above it.
    alignas(CacheLineSize) std::atomic<FreeNode*> remoteFreeHead_{nullptr};
};

} // namespace ThreadPoolPro::Detail

/// @brief Short alias so this library can be used as `rain::ThreadPool`,
/// while its true namespace (and all internal diagnostics) remains
/// `ThreadPoolPro`. Repeated identically in every header of this project.
namespace rain = ThreadPoolPro;
