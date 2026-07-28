// PulseThreadPool ThreadPool shutdown test suite.
//
// Coverage:
// - The destructor finishes already-queued tasks before returning
// - shutdown(FinishTasks) lets already-queued tasks finish
// - shutdown(DiscardTasks) drops tasks that hadn't started yet
// - Only the first shutdown() call's mode takes effect

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies the destructor finishes already-queued tasks before returning.
static void destructor_finishes_queued_tasks() {
    std::atomic<int> completed{0};

    {
        ThreadPool pool(1);
        pool.pause();
        for (int i = 0; i < 5; ++i)
            pool.detach([&completed]() { completed.fetch_add(1); });
        // Pool destructs here. pause() no longer applies once shutdown()
        // starts (runState_ leaves Running), so the default FinishTasks
        // destructor still runs all five before returning.
    }

    CHK(completed.load() == 5);
}

// Verifies shutdown(FinishTasks) lets already-queued tasks finish.
static void shutdown_finish_tasks_completes_queue() {
    ThreadPool pool(1);
    std::atomic<int> completed{0};

    pool.pause();
    for (int i = 0; i < 5; ++i)
        pool.detach([&completed]() { completed.fetch_add(1); });

    pool.shutdown(ThreadPool::ShutdownMode::FinishTasks);
    CHK(completed.load() == 5);
}

// Verifies shutdown(DiscardTasks) drops tasks that hadn't started yet.
static void shutdown_discard_tasks_drops_queue() {
    ThreadPool pool(1);
    std::atomic<int> completed{0};

    // Paused, so none of these can have started when shutdown() runs.
    pool.pause();
    for (int i = 0; i < 5; ++i)
        pool.detach([&completed]() { completed.fetch_add(1); });

    pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks);
    CHK(completed.load() == 0);
}

// Verifies only the first shutdown() call's mode takes effect.
static void second_shutdown_call_is_noop() {
    ThreadPool pool(1);
    std::atomic<int> completed{0};

    pool.pause();
    for (int i = 0; i < 5; ++i)
        pool.detach([&completed]() { completed.fetch_add(1); });

    pool.shutdown(ThreadPool::ShutdownMode::FinishTasks);
    pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks); // no-op: first call already won

    CHK(completed.load() == 5);
}

// Executes all ThreadPool shutdown test cases.
static void run_tests() {
    RUN(destructor_finishes_queued_tasks);
    RUN(shutdown_finish_tasks_completes_queue);
    RUN(shutdown_discard_tasks_drops_queue);
    RUN(second_shutdown_call_is_noop);
}

REGISTER_TEST_SUITE();
