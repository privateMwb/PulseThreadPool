// ThreadPool concurrency stress test suite.
//
// Coverage:
// - Multiple producer threads calling enqueue() concurrently, all resolve correctly
// - Multiple producer threads calling detach() concurrently, all execute exactly once
// - A worker spawning a large internal burst forces work-stealing queue growth
// - A high-volume mixed enqueue/detach burst across many producers executes exactly once
// - The pool remains responsive and drains cleanly after a heavy burst

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <vector>

using namespace ThreadPoolPro;

// Spins until the predicate holds or the timeout elapses.
static bool waitUntil(const std::function<bool()>& pred, int timeoutMs = 10000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

// Verifies concurrent producer threads calling enqueue() all resolve correctly.
static void concurrent_producers_enqueue_all_resolve() {
    ThreadPool pool{4};

    constexpr int producerCount     = 8;
    constexpr int tasksPerProducer  = 200;
    std::atomic<int> sum{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < producerCount; ++p) {
        producers.emplace_back([&pool, &sum] {
            std::vector<std::future<void>> futures;
            futures.reserve(tasksPerProducer);

            for (int i = 0; i < tasksPerProducer; ++i)
                futures.push_back(pool.enqueue([&sum] {
                    sum.fetch_add(1, std::memory_order_relaxed);
                }));

            for (auto& f : futures)
                f.get();
        });
    }

    for (auto& t : producers)
        t.join();

    CHK(sum.load() == producerCount * tasksPerProducer);
}

// Verifies concurrent producer threads calling detach() all execute exactly once.
static void concurrent_producers_detach_all_execute() {
    ThreadPool pool{4};

    constexpr int producerCount    = 8;
    constexpr int tasksPerProducer = 500;
    std::atomic<int> counter{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < producerCount; ++p) {
        producers.emplace_back([&pool, &counter] {
            for (int i = 0; i < tasksPerProducer; ++i)
                pool.detach([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
        });
    }

    for (auto& t : producers)
        t.join();

    CHK(waitUntil([&] { return counter.load() == producerCount * tasksPerProducer; }));
}

// Verifies a worker spawning a large internal burst forces work-stealing queue growth
// and every spawned task still executes exactly once.
static void self_submission_burst_triggers_queue_growth() {
    ThreadPool pool{2};
    std::atomic<int> completed{0};
    constexpr int burstSize = 5000;

    auto future = pool.enqueue([&pool, &completed] {
        for (int i = 0; i < burstSize; ++i)
            pool.detach([&completed] { completed.fetch_add(1, std::memory_order_relaxed); });
        return 0;
    });

    future.get();

    CHK(waitUntil([&] { return completed.load() == burstSize; }));
}

// Verifies a high-volume mixed enqueue/detach burst from many producers
// executes every task exactly once.
static void mixed_high_volume_burst_executes_exactly_once() {
    ThreadPool pool{4};

    constexpr int producerCount    = 6;
    constexpr int tasksPerProducer = 1000;
    std::atomic<int> counter{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < producerCount; ++p) {
        producers.emplace_back([&pool, &counter, p] {
            for (int i = 0; i < tasksPerProducer; ++i) {
                if ((i + p) % 2 == 0) {
                    pool.detach([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
                } else {
                    auto future = pool.enqueue([&counter] {
                        counter.fetch_add(1, std::memory_order_relaxed);
                    });
                    future.get();
                }
            }
        });
    }

    for (auto& t : producers)
        t.join();

    CHK(waitUntil([&] { return counter.load() == producerCount * tasksPerProducer; }));
}

// Verifies the pool remains responsive and drains cleanly after a heavy burst.
static void pool_drains_cleanly_after_stress() {
    ThreadPool pool{4};
    std::atomic<int> counter{0};

    constexpr int burstSize = 20000;
    for (int i = 0; i < burstSize; ++i)
        pool.detach([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });

    pool.shutdown(ThreadPool::ShutdownMode::FinishTasks);

    CHK(counter.load() == burstSize);
    CHK(pool.isStopped() == true);
}

// Executes all concurrency stress test cases.
static void run_tests() {
    RUN(concurrent_producers_enqueue_all_resolve);
    RUN(concurrent_producers_detach_all_execute);
    RUN(self_submission_burst_triggers_queue_growth);
    RUN(mixed_high_volume_burst_executes_exactly_once);
    RUN(pool_drains_cleanly_after_stress);
}

REGISTER_TEST_SUITE();
