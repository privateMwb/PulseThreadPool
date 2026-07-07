// Silent detach() exceptions pitfall example.
//
// Demonstrates:
// - a task submitted with detach() that throws leaves no visible trace
//   at the call site: no exception there, no return value, no log
// - the failure is only detectable in aggregate, via exceptionCount()
// - why this matters: work that was supposed to happen silently doesn't

#include <common/framework.h>

#include <atomic>
#include <stdexcept>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {
    ThreadPool pool{2};

    // A task that's supposed to record a result, but throws before doing so.
    setTitle("The Pitfall");

    std::atomic<bool> resultRecorded{false};

    pool.detach([&resultRecorded] {
        if (true) // stands in for some real failure condition
            throw std::runtime_error("failed to compute result");

        resultRecorded.store(true, std::memory_order_relaxed);
    });

    while (pool.queuedTasks() > 0 || pool.activeTaskCount() > 0)
        std::this_thread::yield();

    std::cout << "detach() returned : normally, no exception thrown here\n";
    std::cout << "resultRecorded     : " << resultRecorded.load()
              << " (silently never happened)\n";
    std::cout << "exceptionCount()   : " << pool.exceptionCount()
              << " (this is the only trace the failure left)\n\n";

    // The fix: use enqueue() when you need to know whether a task
    // actually succeeded, so failure surfaces at a call site you control.
    setTitle("The Fix");

    auto future = pool.enqueue([] {
        throw std::runtime_error("failed to compute result");
    });

    try {
        future.get();
    } catch (const std::exception& e) {
        std::cout << "Caught explicitly : " << e.what() << "\n";
    }
}

REGISTER_EXAMPLE_SUITE();
