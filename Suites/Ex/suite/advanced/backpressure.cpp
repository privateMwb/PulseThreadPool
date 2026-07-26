// Backpressure example.
//
// Demonstrates:
// - throttling submission based on queuedTasks()
// - a producer that slows down when the backlog grows too large
// - avoiding unbounded queue growth under a fast producer / slow consumer

#include <common/framework.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {
    ThreadPool pool{2};
    std::atomic<int> completed{0};

    setTitle("Throttled Submission");

    constexpr std::size_t highWaterMark = 50;
    constexpr int totalItems = 500;

    std::size_t maxBacklogSeen = 0;

    for (int i = 0; i < totalItems; ++i) {
        // Back off while the backlog is too deep, instead of piling on
        // more work than the pool can realistically keep up with.
        while (pool.queuedTasks() > highWaterMark)
            std::this_thread::yield();

        maxBacklogSeen = std::max(maxBacklogSeen, pool.queuedTasks());

        pool.detach([&completed] {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }

    std::cout << "Submitted        : " << totalItems     << "\n";
    std::cout << "High-water mark  : " << highWaterMark  << "\n";
    std::cout << "Max backlog seen : " << maxBacklogSeen << "\n\n";

    setTitle("Draining");

    while (completed.load(std::memory_order_relaxed) < totalItems)
        std::this_thread::yield();

    std::cout << "Completed : " << completed.load() << " / " << totalItems << "\n";
}

REGISTER_EXAMPLE_SUITE();
