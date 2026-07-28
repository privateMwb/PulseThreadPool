// Waiting for the pool to drain.
//
// Demonstrates:
// - queuedTasks() and activeTaskCount() while work is in flight
// - waitIdle() blocking until everything submitted has finished
// - empty() as a non-blocking queued-work check
// - The calling thread helping drain the pool while it waits

#include <support/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(2);

    // Right after a burst of submissions, some tasks are running and
    // some are still queued.
    setTitle("Counts while work is in flight");

    std::atomic<int> completed{0};

    for (int i = 0; i < 8; ++i) {
        pool.detach([&completed] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ++completed;
        });
    }

    std::cout << "queuedTasks    : " << pool.queuedTasks() << "\n";
    std::cout << "activeTaskCount: " << pool.activeTaskCount() << "\n\n";

    // waitIdle() blocks until every submitted task has actually run —
    // not just been picked up.
    setTitle("Blocking until idle");

    pool.waitIdle();

    std::cout << "completed  : " << completed.load() << " / 8\n";
    std::cout << "empty()    : " << pool.empty() << "\n";
    std::cout << "isPaused() : " << pool.isPaused() << "\n\n";

    // A second wait on an already-idle pool returns immediately.
    setTitle("Waiting on an already-idle pool");

    pool.waitIdle();
    std::cout << "returned immediately, nothing left to drain\n";
}

REGISTER_EXAMPLE_SUITE();
