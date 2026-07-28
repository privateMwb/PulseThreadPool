// PulseThreadPool self-shutdown test suite.
//
// Coverage:
// - A task calling shutdown() on its own pool from a worker thread
//   doesn't deadlock, and destroying the pool afterward completes cleanly

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies a task calling shutdown() on its own pool doesn't deadlock,
// and that destroying the pool afterward completes cleanly.
static void self_shutdown_does_not_deadlock() {
    ThreadPool pool(2);
    std::atomic<bool> shutdownReturned{false};

    pool.detach([&pool, &shutdownReturned]() {
        pool.shutdown(ThreadPool::ShutdownMode::FinishTasks);
        shutdownReturned.store(true);
    });

    while (!shutdownReturned.load())
        std::this_thread::yield();

    CHK(pool.isStopped());
    // Pool destructs here. If the self-detach path (selfDetachRequested_)
    // were broken, this would hang trying to join the worker that just
    // shut the pool down from inside its own task.
}

// Executes all self-shutdown test cases.
static void run_tests() {
    RUN(self_shutdown_does_not_deadlock);
}

REGISTER_TEST_SUITE();
