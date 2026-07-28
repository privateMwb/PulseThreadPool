// PulseThreadPool concurrent-shutdown test suite.
//
// Coverage:
// - Two threads calling shutdown() concurrently with different modes
//   resolve to exactly one mode, cleanly — never a corrupted in-between
//   state where some but not all queued tasks ran

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies two threads calling shutdown() concurrently with different
// modes don't corrupt pool state — exactly one mode wins, and it wins
// cleanly (all queued tasks finish, or none of them start).
static void concurrent_shutdown_resolves_to_one_mode() {
    ThreadPool pool(1);
    std::atomic<int> completed{0};

    pool.pause();
    for (int i = 0; i < 20; ++i)
        pool.detach([&completed]() { completed.fetch_add(1); });

    std::thread finisher([&pool]() { pool.shutdown(ThreadPool::ShutdownMode::FinishTasks); });
    std::thread discarder([&pool]() { pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks); });

    finisher.join();
    discarder.join();

    CHK(pool.isStopped());

    int result = completed.load();
    CHK(result == 0 || result == 20);
}

// Executes all concurrent-shutdown test cases.
static void run_tests() {
    RUN(concurrent_shutdown_resolves_to_one_mode);
}

REGISTER_TEST_SUITE();
