// ThreadPool throughput benchmark suite.
// Measures steady-state tasks/sec across thread counts and task granularity.
//
// Covers:
// - throughput scaling with thread count (1, 2, 4, 8 workers)
// - throughput at different task granularities (no-op, light, heavy work)
//
// Note: this measures total wall time for a fixed batch rather than a
// per-call average, so it uses manual timing instead of BENCH().

#include <common/framework.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace ThreadPoolPro;

// Runs taskCount copies of workFn across a pool of threadCount workers
// and returns the achieved throughput in tasks/sec.
static double measureThroughput(std::size_t threadCount, std::size_t taskCount, auto&& workFn) {
    ThreadPool pool{threadCount};
    std::atomic<std::size_t> counter{0};

    auto start = steady_clock::now();

    for (std::size_t i = 0; i < taskCount; ++i)
        pool.detach([&counter, &workFn] {
            workFn();
            counter.fetch_add(1, std::memory_order_relaxed);
        });

    while (counter.load(std::memory_order_relaxed) < taskCount)
        std::this_thread::yield();

    auto elapsed = duration<double>(steady_clock::now() - start).count();
    return static_cast<double>(taskCount) / elapsed;
}

// Measures throughput scaling as thread count increases.
static void bench_throughput_by_thread_count() {
    constexpr std::size_t taskCount = 500'000;
    const std::vector<std::size_t> threadCounts{1, 2, 4, 8};

    auto trivialWork = [] {};

    std::cout << "Throughput by thread count (" << taskCount << " no-op tasks)\n";
    for (auto tc : threadCounts) {
        double tps = measureThroughput(tc, taskCount, trivialWork);
        std::cout << "  " << tc << " thread(s): "
                   << static_cast<long long>(tps) << " tasks/sec\n";
    }
}

// Measures throughput at different task granularities on a fixed thread count.
static void bench_throughput_by_granularity() {
    const std::size_t threadCount = std::thread::hardware_concurrency();
    constexpr std::size_t taskCount = 200'000;

    auto noop = [] {};
    auto light = [] {
        volatile int x = 0;
        for (int i = 0; i < 100; ++i) x += i;
    };
    auto heavy = [] {
        volatile int x = 0;
        for (int i = 0; i < 10'000; ++i) x += i;
    };

    double tpsNoop  = measureThroughput(threadCount, taskCount, noop);
    double tpsLight = measureThroughput(threadCount, taskCount, light);
    double tpsHeavy = measureThroughput(threadCount, taskCount, heavy);

    std::cout << "Throughput by task granularity (" << threadCount << " threads)\n";
    std::cout << "  No-op:       " << static_cast<long long>(tpsNoop)  << " tasks/sec\n";
    std::cout << "  Light (100): " << static_cast<long long>(tpsLight) << " tasks/sec\n";
    std::cout << "  Heavy (10k): " << static_cast<long long>(tpsHeavy) << " tasks/sec\n";
}

// Executes all throughput benchmark cases.
static void run_benchmarks() {
    bench_throughput_by_thread_count();
    std::cout << "\n";

    bench_throughput_by_granularity();
}

REGISTER_BENCH_SUITE();
