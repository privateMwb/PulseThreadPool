// A producer/consumer pipeline built on the pool.
//
// Demonstrates:
// - One thread producing work items and detach()ing a consumer per item
// - A shared, mutex-guarded sink collecting results from many consumers
// - waitIdle() as the join point between production and consumption
// - Producing and consuming overlapping rather than running in lockstep

#include <support/framework.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(4);

    // The "producer" here is just the loop below submitting one
    // consumer task per item — no separate producer thread is needed
    // because submission itself is cheap and non-blocking.
    setTitle("Producing items, consuming on the pool");

    std::mutex sinkMutex;
    std::vector<int> sink;

    auto produce = [&pool, &sinkMutex, &sink](int item) {
        pool.detach([item, &sinkMutex, &sink] {
            int processed = item * 2; // stand-in for real consumer work
            std::lock_guard<std::mutex> lock(sinkMutex);
            sink.push_back(processed);
        });
    };

    for (int i = 1; i <= 10; ++i) {
        produce(i);
    }

    // waitIdle() is the natural join point: it returns once every
    // consumer has finished, regardless of how the 10 items were
    // interleaved across workers.
    pool.waitIdle();

    std::sort(sink.begin(), sink.end());
    std::cout << "consumed " << sink.size() << " items: ";
    for (int v : sink) {
        std::cout << v << " ";
    }
    std::cout << "\n\n";

    // Production and consumption naturally overlap — items 6-10 can
    // start being consumed before items 1-5 are even done, since
    // detach() doesn't block the producing loop.
    setTitle("Overlap, not lockstep");

    std::atomic<int> consumedCount{0};

    for (int i = 0; i < 20; ++i) {
        pool.detach([&consumedCount] { ++consumedCount; });
    }

    std::cout << "submitted 20 items without waiting between them\n";
    pool.waitIdle();
    std::cout << "consumedCount: " << consumedCount.load() << " / 20\n";
}

REGISTER_EXAMPLE_SUITE();
