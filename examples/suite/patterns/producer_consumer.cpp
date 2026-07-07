// Producer-consumer pattern example.
//
// Demonstrates:
// - multiple external threads producing work concurrently
// - a single ThreadPool consuming and processing that work
// - waiting for all produced work to finish processing

#include <common/framework.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using namespace ThreadPoolPro;

static void run_examples() {
    ThreadPool pool{4};

    setTitle("Producers");

    constexpr int producerCount    = 3;
    constexpr int itemsPerProducer = 20;

    std::atomic<int> processedCount{0};
    std::mutex logMutex;

    // Each producer thread generates items and submits them to the pool
    // as it goes, rather than handing off a finished batch all at once.
    std::vector<std::thread> producers;
    for (int p = 0; p < producerCount; ++p) {
        producers.emplace_back([&pool, &processedCount, &logMutex, p] {
            for (int i = 0; i < itemsPerProducer; ++i) {
                pool.detach([&processedCount, &logMutex, p, i] {
                    // "Process" the item.
                    processedCount.fetch_add(1, std::memory_order_relaxed);

                    if (i == 0) {
                        std::lock_guard<std::mutex> lock(logMutex);
                        std::cout << "Producer " << p << " item processed\n";
                    }
                });
            }
        });
    }

    for (auto& t : producers)
        t.join();

    std::cout << "All producers finished submitting\n\n";

    // The pool keeps consuming after producers are done; wait for it
    // to finish processing everything that was submitted.
    setTitle("Draining The Pool");

    constexpr int totalItems = producerCount * itemsPerProducer;
    while (processedCount.load(std::memory_order_relaxed) < totalItems)
        std::this_thread::yield();

    std::cout << "Processed : " << processedCount.load() << " / " << totalItems << "\n";
}

REGISTER_EXAMPLE_SUITE();
