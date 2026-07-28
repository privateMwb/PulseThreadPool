// PulseThreadPool observer test suite.
//
// Coverage:
// - threadCount() matches the constructor argument
// - A fresh pool reports empty, with no active or queued tasks
// - activeTaskCount() reflects a task blocked mid-execution
// - isStopped() reflects shutdown()

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies threadCount() reflects the constructor argument.
static void thread_count_matches_constructor() {
    ThreadPool pool(4);
    CHK(pool.threadCount() == 4);
}

// Verifies a freshly constructed pool reports empty and idle.
static void fresh_pool_is_empty() {
    ThreadPool pool(2);
    CHK(pool.empty());
    CHK(pool.activeTaskCount() == 0);
    CHK(pool.queuedTasks() == 0);
}

// Verifies activeTaskCount() reflects a task blocked mid-execution.
static void active_task_count_during_execution() {
    ThreadPool pool(1);
    std::atomic<bool> release{false};
    std::atomic<bool> started{false};

    pool.detach([&]() {
        started.store(true);
        while (!release.load())
            std::this_thread::yield();
    });

    while (!started.load())
        std::this_thread::yield();

    CHK(pool.activeTaskCount() == 1);
    release.store(true);
    pool.waitIdle();
}

// Verifies isStopped() reflects shutdown().
static void is_stopped_after_shutdown() {
    ThreadPool pool(2);
    CHK(!pool.isStopped());
    pool.shutdown();
    CHK(pool.isStopped());
}

// Executes all observer test cases.
static void run_tests() {
    RUN(thread_count_matches_constructor);
    RUN(fresh_pool_is_empty);
    RUN(active_task_count_during_execution);
    RUN(is_stopped_after_shutdown);
}

REGISTER_TEST_SUITE();
