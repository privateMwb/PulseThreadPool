// Unbounded submission pitfall example.
//
// Demonstrates:
// - a fast producer submitting work faster than a slow pool can drain it
// - the backlog (queuedTasks()) growing without any check
// - why this matters: with no upper bound, memory usage tracks the
//   backlog, and it can grow arbitrarily large under sustained load
//
// See advanced/backpressure.cpp for the throttled fix.

#include <common/framework.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {
    setTitle("The Pitfall: No Backpressure");

    ThreadPool pool{2};
    std::atomic<int> completed{0};

    constexpr int totalItems = 2'000;
    std::size_t peakBacklog = 0;

    // The producer submits as fast as it can, never checking how far
    // behind the pool has fallen.
    for (int i = 0; i < totalItems; ++i) {
        pool.detach([&completed] {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            completed.fetch_add(1, std::memory_order_relaxed);
        });

        if (i % 50 == 0)
            peakBacklog = std::max(peakBacklog, pool.queuedTasks());
    }

    std::cout << "Submitted    : " << totalItems  << "\n";
    std::cout << "Peak backlog : " << peakBacklog
              << " (grew unchecked while the producer kept going)\n\n";

    setTitle("Draining The Backlog");

    while (completed.load(std::memory_order_relaxed) < totalItems)
        std::this_thread::yield();

    std::cout << "Completed : " << completed.load() << " / " << totalItems
              << " (a sustained or faster producer would push this backlog\n"
              << "            arbitrarily higher; see backpressure.cpp for the fix)\n";
}

REGISTER_EXAMPLE_SUITE();
