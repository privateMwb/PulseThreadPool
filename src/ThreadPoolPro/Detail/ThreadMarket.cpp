/**
 * @file ThreadMarket.cpp
 * @brief ThreadMarket / MarketThread implementation.
 */

// clang-format off
#include <ThreadPoolPro/Detail/ThreadMarket.h> // ThreadMarket — the class this file implements
// clang-format on

namespace ThreadPoolPro::Detail {

// ============================================================
//  MarketThread
// ============================================================

MarketThread::MarketThread() : thread_(&MarketThread::loop, this) {
    // Valid immediately — std::thread::get_id() doesn't require the
    // thread function to have actually started running yet.
    id_ = thread_.get_id();
}

MarketThread::~MarketThread() {
    exiting_.store(true, std::memory_order_release);
    hasWork_.store(true, std::memory_order_release); // wake loop() out of its wait-for-work park
    hasWork_.notify_all();

    if (thread_.joinable())
        thread_.join();
}

void MarketThread::assign(std::function<void()> job) {
    job_ = std::move(job);
    hasWork_.store(true, std::memory_order_release);
    hasWork_.notify_all();
}

void MarketThread::waitDone() {
    hasWork_.wait(true, std::memory_order_acquire);
}

void MarketThread::loop() {
    for (;;) {
        hasWork_.wait(false, std::memory_order_acquire);

        if (exiting_.load(std::memory_order_acquire))
            return;

        job_();
        job_ = nullptr;

        // Flip to the idle state and register this thread back with
        // the market BEFORE notifying hasWork_. waitDone() unblocks as
        // soon as it observes hasWork_ == false, and whichever thread
        // that wakes (e.g. ~ThreadPool -> shutdown()) is free to
        // immediately construct a new ThreadPool and call
        // ThreadMarket::lease() again. If notify_all() happened before
        // returnToIdle(), that lease() could run before this thread had
        // actually reached idleThreads_ — invisible to lease(), so it
        // would spawn a brand-new OS thread instead of reusing this
        // one. Doing the store first (so this thread's state is fully
        // "idle" the instant it becomes visible), then returnToIdle()
        // (making it visible), then notify_all() last (only waking
        // waitDone() once reuse is guaranteed to succeed) closes that
        // race — this was the actual cause of construct/destroy being
        // pathologically slow, since it meant real OS thread creation
        // was being paid on almost every ThreadPool construction
        // instead of only the first few.
        hasWork_.store(false, std::memory_order_release);
        ThreadMarket::instance().returnToIdle(this);
        hasWork_.notify_all();
    }
}

// ============================================================
//  ThreadMarket
// ============================================================

ThreadMarket& ThreadMarket::instance() {
    static ThreadMarket market;
    return market;
}

ThreadMarket::~ThreadMarket() {
    // Explicit, rather than relying on implicit reverse-declaration-
    // order member destruction: allThreads_ must be torn down (which
    // signals exiting_ and joins each MarketThread — see
    // MarketThread::~MarketThread()) BEFORE idleThreads_ is touched.
    // Otherwise a MarketThread still finishing its last job at process
    // exit could call returnToIdle() (mutating idleThreads_) while
    // idleThreads_ was concurrently being destroyed — a genuine race.
    // By the time this line returns, every thread is guaranteed to
    // have exited loop() and can no longer touch either vector.
    allThreads_.clear();
    idleThreads_.clear();
}

std::vector<MarketThread*> ThreadMarket::lease(std::size_t count) {
    std::vector<MarketThread*> out;
    out.reserve(count);

    std::lock_guard<std::mutex> lock(mutex_);

    while (out.size() < count && !idleThreads_.empty()) {
        out.push_back(idleThreads_.back());
        idleThreads_.pop_back();
    }

    while (out.size() < count) {
        allThreads_.push_back(std::make_unique<MarketThread>());
        out.push_back(allThreads_.back().get());
    }

    return out;
}

void ThreadMarket::returnToIdle(MarketThread* thread) {
    std::lock_guard<std::mutex> lock(mutex_);
    idleThreads_.push_back(thread);
}

} // namespace ThreadPoolPro::Detail
