// ThreadPool task submission test suite.
//
// Coverage:
// - enqueue() returns a future resolving to the callable's result
// - enqueue() forwards arguments correctly
// - enqueue() propagates exceptions through the future
// - detach() executes fire-and-forget tasks
// - detach() executes many tasks submitted concurrently
// - submitting from inside a running task (worker-owned queue path)
// - submitting after shutdown() throws

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace ThreadPoolPro;

// Spins until the predicate holds or the timeout elapses.
static bool waitUntil(const std::function<bool()>& pred, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

// Verifies enqueue() returns a future resolving to the callable's result.
static void enqueue_returns_future_result() {
    ThreadPool pool{2};

    auto future = pool.enqueue([] { return 42; });

    CHK(future.get() == 42);
}

// Verifies enqueue() forwards arguments to the callable.
static void enqueue_forwards_arguments() {
    ThreadPool pool{2};

    auto future = pool.enqueue([](int a, int b) { return a + b; }, 2, 3);

    CHK(future.get() == 5);
}

// Verifies an exception thrown inside a task propagates through the future.
static void enqueue_propagates_exception() {
    ThreadPool pool{2};

    auto future = pool.enqueue([] () -> int {
        throw std::runtime_error("boom");
    });

    bool caught = false;
    try {
        future.get();
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()) == "boom";
    }

    CHK(caught == true);
}

// Verifies detach() executes a fire-and-forget task.
static void detach_executes_task() {
    ThreadPool pool{2};
    std::atomic<bool> ran{false};

    pool.detach([&ran] { ran.store(true); });

    CHK(waitUntil([&] { return ran.load(); }));
}

// Verifies detach() executes many concurrently submitted tasks.
static void detach_multiple_tasks() {
    ThreadPool pool{4};
    std::atomic<int> counter{0};

    constexpr int taskCount = 200;
    for (int i = 0; i < taskCount; ++i)
        pool.detach([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });

    CHK(waitUntil([&] { return counter.load() == taskCount; }));
}

// Verifies a task can submit further work from inside a running worker.
static void submission_from_worker_thread() {
    ThreadPool pool{2};
    std::atomic<bool> innerRan{false};

    auto future = pool.enqueue([&pool, &innerRan] {
        pool.detach([&innerRan] { innerRan.store(true); });
        return 1;
    });

    CHK(future.get() == 1);
    CHK(waitUntil([&] { return innerRan.load(); }));
}

// Verifies submitting a task after shutdown() throws.
static void submission_after_shutdown_throws() {
    ThreadPool pool{2};
    pool.shutdown();

    bool threw = false;
    try {
        pool.detach([] {});
    } catch (const std::runtime_error&) {
        threw = true;
    }

    CHK(threw == true);
}

// Executes all task submission test cases.
static void run_tests() {
    RUN(enqueue_returns_future_result);
    RUN(enqueue_forwards_arguments);
    RUN(enqueue_propagates_exception);
    RUN(detach_executes_task);
    RUN(detach_multiple_tasks);
    RUN(submission_from_worker_thread);
    RUN(submission_after_shutdown_throws);
}

REGISTER_TEST_SUITE();
