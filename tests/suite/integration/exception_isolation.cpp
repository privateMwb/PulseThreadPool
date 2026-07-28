// PulseThreadPool exception isolation test suite.
//
// Coverage:
// - A throwing detach()ed task doesn't stop other queued tasks from
//   running, and is counted via exceptionCount()
// - A throwing enqueue()d task's exception is confined to its own
//   Future; the pool remains fully usable afterward

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies a throwing task doesn't stop other tasks from running, and
// is counted via exceptionCount().
static void throwing_task_does_not_affect_others() {
    ThreadPool pool(4);
    std::atomic<int> completed{0};
    constexpr int taskCount = 20;

    for (int i = 0; i < taskCount; ++i) {
        pool.detach([&completed, i]() {
            if (i % 3 == 0)
                throw std::runtime_error("boom");
            completed.fetch_add(1);
        });
    }

    pool.waitIdle();

    int expectedThrows = 0;
    for (int i = 0; i < taskCount; ++i)
        if (i % 3 == 0)
            ++expectedThrows;

    CHK(pool.exceptionCount() == static_cast<std::size_t>(expectedThrows));
    CHK(completed.load() == taskCount - expectedThrows);
}

// Verifies an enqueue()d task's exception stays confined to its own
// Future, and the pool remains fully usable afterward.
static void enqueue_exception_does_not_affect_pool() {
    ThreadPool pool(2);

    auto future = pool.enqueue([]() -> int { throw std::runtime_error("boom"); });
    CHK_THROWS(future.get(), std::runtime_error);

    auto ok = pool.enqueue([]() { return 99; });
    CHK(ok.get() == 99);
}

// Executes all exception isolation test cases.
static void run_tests() {
    RUN(throwing_task_does_not_affect_others);
    RUN(enqueue_exception_does_not_affect_pool);
}

REGISTER_TEST_SUITE();
