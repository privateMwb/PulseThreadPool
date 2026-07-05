// ThreadPool exception handling test suite.
//
// Coverage:
// - detach() tasks that throw increment exceptionCount()
// - a worker survives a thrown exception and keeps processing tasks
// - enqueue() tasks that throw do NOT increment exceptionCount()
//   (the exception is captured by the future instead)
// - multiple thrown exceptions are all counted
// - a throwing task does not prevent unrelated tasks from completing

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <functional>
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

// Verifies a detach() task that throws increments exceptionCount().
static void detach_exception_increments_counter() {
    ThreadPool pool{2};

    pool.detach([] { throw std::runtime_error("boom"); });

    CHK(waitUntil([&] { return pool.exceptionCount() == 1; }));
}

// Verifies the worker that caught an exception keeps processing later tasks.
static void worker_survives_exception_and_continues() {
    ThreadPool pool{1};
    std::atomic<bool> laterTaskRan{false};

    pool.detach([] { throw std::runtime_error("boom"); });
    pool.detach([&laterTaskRan] { laterTaskRan.store(true); });

    CHK(waitUntil([&] { return laterTaskRan.load(); }));
    CHK(pool.exceptionCount() == 1);
}

// Verifies enqueue() tasks that throw do NOT increment exceptionCount(),
// since the exception is captured by the returned future instead.
static void enqueue_exception_does_not_increment_counter() {
    ThreadPool pool{2};

    auto future = pool.enqueue([] () -> int {
        throw std::runtime_error("boom");
    });

    bool caught = false;
    try {
        future.get();
    } catch (const std::runtime_error&) {
        caught = true;
    }

    CHK(caught == true);
    CHK(pool.exceptionCount() == 0);
}

// Verifies multiple thrown exceptions are all counted.
static void multiple_exceptions_all_counted() {
    ThreadPool pool{4};

    constexpr int throwCount = 25;
    for (int i = 0; i < throwCount; ++i)
        pool.detach([] { throw std::runtime_error("boom"); });

    CHK(waitUntil([&] { return pool.exceptionCount() == throwCount; }));
}

// Verifies a throwing task does not prevent unrelated tasks from completing.
static void exception_does_not_affect_other_tasks() {
    ThreadPool pool{4};
    std::atomic<int> completed{0};

    constexpr int normalTasks = 50;
    for (int i = 0; i < normalTasks; ++i) {
        pool.detach([&completed] { completed.fetch_add(1, std::memory_order_relaxed); });
        pool.detach([] { throw std::runtime_error("boom"); });
    }

    CHK(waitUntil([&] { return completed.load() == normalTasks; }));
    CHK(waitUntil([&] { return pool.exceptionCount() == normalTasks; }));
}

// Executes all exception handling test cases.
static void run_tests() {
    RUN(detach_exception_increments_counter);
    RUN(worker_survives_exception_and_continues);
    RUN(enqueue_exception_does_not_increment_counter);
    RUN(multiple_exceptions_all_counted);
    RUN(exception_does_not_affect_other_tasks);
}

REGISTER_TEST_SUITE();
