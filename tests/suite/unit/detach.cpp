// PulseThreadPool detach() test suite.
//
// Coverage:
// - Submitted task actually executes
// - An exception thrown by the task is counted, not propagated
// - Throws std::runtime_error once the pool has begun shutting down

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies a detached task actually executes.
static void detach_runs_task() {
    ThreadPool pool(2);
    std::atomic<bool> ran{false};
    pool.detach([&ran]() { ran.store(true); });
    pool.waitIdle();
    CHK(ran.load());
}

// Verifies an exception thrown by a detached task is counted rather than propagated.
static void detach_counts_exception() {
    ThreadPool pool(2);
    pool.detach([]() { throw std::runtime_error("boom"); });
    pool.waitIdle();
    CHK(pool.exceptionCount() == 1);
}

// Verifies detach() rejects new work once shutdown() has been called.
static void detach_after_shutdown_throws() {
    ThreadPool pool(2);
    pool.shutdown();
    CHK_THROWS(pool.detach([]() {}), std::runtime_error);
}

// Executes all detach() test cases.
static void run_tests() {
    RUN(detach_runs_task);
    RUN(detach_counts_exception);
    RUN(detach_after_shutdown_throws);
}

REGISTER_TEST_SUITE();
