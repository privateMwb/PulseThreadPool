// ThreadPool submission contention benchmark suite.
// Measures how submission throughput scales as more concurrent
// producer threads hammer the pool at once.
//
// Covers:
// - throughput scaling with producer thread count (detach())
// - enqueue() vs detach() throughput under fixed contention
//
// Note: these measure total wall time for a fixed batch rather than a
// per-call average, so they use manual timing instead of BENCH().

#include <common/framework.h>

#include <atomic>
#include <future>
#include <thread>
#include <vector>

using namespace ThreadPoolPro;

// Spawns producerCount threads, each submitting tasksPerProducer tasks via
// detach(), and returns the achieved submission+execution throughput.
static double measureContentionThroughput(std::size_t producerCount,
                                            std::size_t tasksPerProducer,
                                            std::size_t poolThreads) {
    ThreadPool pool{poolThreads};
    std::atomic<std::size_t> counter{0};
    std::size_t total = producerCount * tasksPerProducer;

    auto start = steady_clock::now();

    std::vector<std::thread> producers;
    for (std::size_t p = 0; p < producerCount; ++p) {
        producers.emplace_back([&pool, &counter, tasksPerProducer] {
            for (std::size_t i = 0; i < tasksPerProducer; ++i)
                pool.detach([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
        });
    }

    for (auto& t : producers)
        t.join();

    while (counter.load(std::memory_order_relaxed) < total)
        std::this_thread::yield();

    auto elapsed = duration<double>(steady_clock::now() - start).count();
    return static_cast<double>(total) / elapsed;
}

// Same as above, but using enqueue() and waiting on each producer's futures.
static double measureContentionThroughputEnqueue(std::size_t producerCount,
                                                   std::size_t tasksPerProducer,
                                                   std::size_t poolThreads) {
    ThreadPool pool{poolThreads};
    std::size_t total = producerCount * tasksPerProducer;

    auto start = steady_clock::now();

    std::vector<std::thread> producers;
    for (std::size_t p = 0; p < producerCount; ++p) {
        producers.emplace_back([&pool, tasksPerProducer] {
            std::vector<std::future<void>> futures;
            futures.reserve(tasksPerProducer);

            for (std::size_t i = 0; i < tasksPerProducer; ++i)
                futures.push_back(pool.enqueue([] {}));

            for (auto& f : futures)
                f.get();
        });
    }

    for (auto& t : producers)
        t.join();

    auto elapsed = duration<double>(steady_clock::now() - start).count();
    return static_cast<double>(total) / elapsed;
}

// Measures submission throughput scaling as producer thread count increases.
static void bench_contention_by_producer_count() {
    constexpr std::size_t tasksPerProducer = 50'000;
    const std::size_t poolThreads = std::thread::hardware_concurrency();
    const std::vector<std::size_t> producerCounts{1, 2, 4, 8};

    std::cout << "Submission throughput by producer count ("
              << tasksPerProducer << " tasks/producer, "
              << poolThreads << " pool threads)\n";

    for (auto pc : producerCounts) {
        double tps = measureContentionThroughput(pc, tasksPerProducer, poolThreads);
        std::cout << "  " << pc << " producer(s): "
                  << static_cast<long long>(tps) << " tasks/sec\n";
    }
}

// Compares enqueue() and detach() throughput under fixed contention.
static void bench_enqueue_vs_detach_under_contention() {
    constexpr std::size_t producerCount    = 8;
    constexpr std::size_t tasksPerProducer = 20'000;
    const std::size_t poolThreads = std::thread::hardware_concurrency();

    double tpsDetach  = measureContentionThroughput(producerCount, tasksPerProducer, poolThreads);
    double tpsEnqueue = measureContentionThroughputEnqueue(producerCount, tasksPerProducer, poolThreads);

    std::cout << "enqueue() vs detach() under contention ("
              << producerCount << " producers, "
              << tasksPerProducer << " tasks/producer)\n";
    std::cout << "  detach():  " << static_cast<long long>(tpsDetach)  << " tasks/sec\n";
    std::cout << "  enqueue(): " << static_cast<long long>(tpsEnqueue) << " tasks/sec\n";
}

// Executes all contention benchmark cases.
static void run_benchmarks() {
    bench_contention_by_producer_count();
    std::cout << "\n";

    bench_enqueue_vs_detach_under_contention();
}

REGISTER_BENCH_SUITE();
