// Reading the pool's runtime stats.
//
// Demonstrates:
// - threadCount() as a fixed, construction-time value
// - queuedTasks() and activeTaskCount() moving as work flows through
// - idleThreadCount() complementing activeTaskCount()
// - exceptionCount() as a running total across the pool's lifetime

#include <support/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(4);

    // threadCount() never changes after construction — there's no
    // resize() on this pool.
    setTitle("threadCount() is fixed");

    std::cout << "threadCount: " << pool.threadCount() << "\n\n";

    // Push more work than there are threads and sample the counters
    // while it's still draining.
    setTitle("Counters while busy");

    std::atomic<int> completed{0};

    for (int i = 0; i < 12; ++i) {
        pool.detach([&completed] {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            ++completed;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::cout << "activeTaskCount : " << pool.activeTaskCount() << "\n";
    std::cout << "queuedTasks     : " << pool.queuedTasks() << "\n";
    std::cout << "idleThreadCount : " << pool.idleThreadCount() << "\n\n";

    pool.waitIdle();

    // Once idle, active and queued counts settle back to zero.
    setTitle("Counters once idle");

    std::cout << "activeTaskCount : " << pool.activeTaskCount() << "\n";
    std::cout << "queuedTasks     : " << pool.queuedTasks() << "\n";
    std::cout << "idleThreadCount : " << pool.idleThreadCount() << "\n\n";

    // exceptionCount() accumulates across the whole lifetime of the
    // pool, not just the most recent batch.
    setTitle("exceptionCount() accumulates");

    pool.detach([] { throw std::runtime_error("first"); });
    pool.detach([] { throw std::runtime_error("second"); });
    pool.waitIdle();

    std::cout << "exceptionCount: " << pool.exceptionCount() << "\n";
}

REGISTER_EXAMPLE_SUITE();
