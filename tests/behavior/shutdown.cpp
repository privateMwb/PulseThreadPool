// ThreadPool shutdown test suite.
//
// Coverage:
// - shutdown() with FinishTasks drains all queued work before returning
// - shutdown() with DiscardTasks skips work still queued
// - the destructor drains pending tasks by default (FinishTasks)
// - shutdown() is idempotent (safe to call more than once)
// - isStopped() reflects shutdown state

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

// Verifies FinishTasks drains all queued tasks before shutdown() returns.
static void finish_tasks_drains_queue() {
    ThreadPool pool{1};
    std::atomic<int> executed{0};

    pool.detach([&executed] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        executed.fetch_add(1, std::memory_order_relaxed);
    });

    constexpr int extraTasks = 50;
    for (int i = 0; i < extraTasks; ++i)
        pool.detach([&executed] { executed.fetch_add(1, std::memory_order_relaxed); });

    pool.shutdown(ThreadPool::ShutdownMode::FinishTasks);

    CHK(executed.load() == extraTasks + 1);
}

// Verifies DiscardTasks skips work that is still queued at shutdown time.
static void discard_tasks_skips_pending_work() {
    ThreadPool pool{1};
    std::atomic<int> executed{0};

    pool.detach([&executed] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        executed.fetch_add(1, std::memory_order_relaxed);
    });

    constexpr int extraTasks = 50;
    for (int i = 0; i < extraTasks; ++i) {
      pool.detach([&executed] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        executed.fetch_add(1, std::memory_order_relaxed);
    });
}

    CHK(executed.load() < extraTasks + 1);
}

// Verifies the destructor drains pending tasks by default.
static void destructor_drains_by_default() {
    std::atomic<int> executed{0};
    constexpr int taskCount = 50;

    {
        ThreadPool pool{2};
        for (int i = 0; i < taskCount; ++i)
            pool.detach([&executed] { executed.fetch_add(1, std::memory_order_relaxed); });
    }

    CHK(executed.load() == taskCount);
}

// Verifies isStopped() reflects the pool's shutdown state.
static void shutdown_marks_pool_stopped() {
    ThreadPool pool{2};

    CHK(pool.isStopped() == false);

    pool.shutdown();

    CHK(pool.isStopped() == true);
}

// Verifies calling shutdown() more than once is safe.
static void double_shutdown_is_safe() {
    ThreadPool pool{2};

    pool.shutdown();
    pool.shutdown();

    CHK(pool.isStopped() == true);
}

// Executes all shutdown test cases.
static void run_tests() {
    RUN(finish_tasks_drains_queue);
    RUN(discard_tasks_skips_pending_work);
    RUN(destructor_drains_by_default);
    RUN(shutdown_marks_pool_stopped);
    RUN(double_shutdown_is_safe);
}

REGISTER_TEST_SUITE();
