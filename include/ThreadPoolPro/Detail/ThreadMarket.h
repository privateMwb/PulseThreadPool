/**
 * @file ThreadMarket.h
 * @brief Process-wide pool of persistent OS threads leased by ThreadPool.
 */

#pragma once

// clang-format off
#include <ThreadPoolPro/Detail/Utility.h> // CacheLineSize

#include <atomic>     // std::atomic
#include <cstddef>    // std::size_t
#include <functional> // std::function
#include <memory>     // std::unique_ptr
#include <mutex>      // std::mutex
#include <thread>     // std::thread
#include <vector>     // std::vector
// clang-format on

namespace ThreadPoolPro::Detail {

/**
 * @brief A single persistent OS thread, leased out and reused across
 * many ThreadPool lifetimes instead of being spawned/joined per pool.
 * @details Lives until process teardown (owned by ThreadMarket). Runs
 * `assign()`ed jobs one at a time in a loop, self-returning to the
 * market's idle pool after each one — see loop().
 */
class MarketThread {
  public:
    MarketThread();
    ~MarketThread();

    MarketThread(const MarketThread&) = delete;
    MarketThread& operator=(const MarketThread&) = delete;

    std::thread::id id() const noexcept {
        return id_;
    }

    /// @brief Assigns `job` to run on this thread. Caller must only
    /// call this while the thread is idle (fresh lease, or after a
    /// prior job's waitDone() has returned).
    void assign(std::function<void()> job);

    /// @brief Blocks until the most recently assign()ed job has
    /// returned. Must NOT be called from this MarketThread's own OS
    /// thread — e.g. from inside the job itself — since that job
    /// hasn't returned yet on its own call stack; that case must
    /// instead let the thread self-return via loop() and simply stop
    /// tracking the pointer. See ThreadPool::shutdown()'s self-detach
    /// handling for the concrete example.
    void waitDone();

  private:
    void loop();

    // Declaration order matters here, not just cosmetically: C++
    // constructs members in declaration order regardless of
    // initializer-list order, and `thread_`'s constructor starts the
    // OS thread immediately — which begins running loop() and touching
    // job_/hasWork_/exiting_ right away. Those must already be fully
    // constructed by then, so thread_ has to be declared (and thus
    // constructed) last. This was a genuine, real data race in the
    // original member order (thread_ first): the new thread could
    // observe hasWork_/exiting_ mid-construction.
    std::function<void()> job_;
    alignas(CacheLineSize) std::atomic<bool> hasWork_{false};
    std::atomic<bool> exiting_{false};
    std::thread::id id_;
    std::thread thread_;
};

/**
 * @brief Global, lazily-constructed pool of MarketThreads.
 * @details Grow-only: threads are spawned on demand when a lease()
 * request exceeds the current idle count, and are never destroyed
 * until process teardown (this is a magic static — see instance()).
 * This is the piece that makes ThreadPool construct/destroy cheap
 * after the first few pools have warmed the market up, mirroring
 * oneTBB's global market / task_arena split.
 */
class ThreadMarket {
  public:
    static ThreadMarket& instance();

    ~ThreadMarket();

    /// @brief Returns `count` threads: idle ones first, then freshly
    /// spawned ones for whatever the idle pool couldn't cover. The
    /// only place real OS thread-creation cost is paid.
    std::vector<MarketThread*> lease(std::size_t count);

  private:
    friend class MarketThread;
    /// @brief Called by a MarketThread on itself once its current job
    /// returns — see MarketThread::loop(). Not for external callers.
    void returnToIdle(MarketThread* thread);

    ThreadMarket() = default;

    std::mutex mutex_;
    std::vector<std::unique_ptr<MarketThread>> allThreads_;
    std::vector<MarketThread*> idleThreads_;
};

} // namespace ThreadPoolPro::Detail

/// @brief Short alias so this library can be used as `rain::ThreadPool`,
/// while its true namespace (and all internal diagnostics) remains
/// `ThreadPoolPro`. Repeated identically in every header of this project.
namespace rain = ThreadPoolPro;
