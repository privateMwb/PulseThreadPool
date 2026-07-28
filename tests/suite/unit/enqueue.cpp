// PulseThreadPool enqueue() test suite.
//
// Coverage:
// - Returns the task's return value via Future::get()
// - Propagates an exception thrown by the task
// - Throws std::runtime_error once the pool has begun shutting down
// - Future is no longer valid() after get() consumes it

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies a submitted task's return value is observable through get().
static void enqueue_returns_result() {
    ThreadPool pool(2);
    auto future = pool.enqueue([](int a, int b) { return a + b; }, 2, 3);
    CHK(future.get() == 5);
}

// Verifies an exception thrown by the task is rethrown from get().
static void enqueue_propagates_exception() {
    ThreadPool pool(2);
    auto future = pool.enqueue([]() -> int { throw std::runtime_error("boom"); });
    CHK_THROWS(future.get(), std::runtime_error);
}

// Verifies enqueue() rejects new work once shutdown() has been called.
static void enqueue_after_shutdown_throws() {
    ThreadPool pool(2);
    pool.shutdown();
    CHK_THROWS(pool.enqueue([]() { return 1; }), std::runtime_error);
}

// Verifies the Future becomes invalid once get() has consumed it.
static void future_invalid_after_get() {
    ThreadPool pool(2);
    auto future = pool.enqueue([]() { return 42; });
    CHK(future.valid());
    (void)future.get();
    CHK(!future.valid());
}

// Executes all enqueue() test cases.
static void run_tests() {
    RUN(enqueue_returns_result);
    RUN(enqueue_propagates_exception);
    RUN(enqueue_after_shutdown_throws);
    RUN(future_invalid_after_get);
}

REGISTER_TEST_SUITE();
