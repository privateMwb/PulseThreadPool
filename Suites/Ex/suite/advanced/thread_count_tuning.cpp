// Thread count tuning example.
//
// Demonstrates:
// - CPU-bound work: matching thread count to hardware_concurrency()
//   is usually optimal; oversubscribing adds overhead without benefit
// - I/O-bound work: oversubscribing beyond hardware_concurrency() can
//   improve throughput, since threads spend most of their time blocked

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

// Simulates CPU-bound work: a busy computation with no blocking.
static void cpuBoundWork() {
    volatile long x = 0;
    for (int i = 0; i < 200'000; ++i)
        x += i;
}

// Simulates I/O-bound work: mostly blocked waiting, not computing.
static void ioBoundWork() {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

// Runs taskCount copies of workFn on a pool of the given size and
// returns the elapsed wall time in milliseconds.
template<typename WorkFn>
static double runBatch(std::size_t threadCount, int taskCount, WorkFn&& workFn) {
    ThreadPool pool{threadCount};
    std::atomic<int> completed{0};

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < taskCount; ++i)
        pool.detach([&completed, &workFn] {
            workFn();
            completed.fetch_add(1, std::memory_order_relaxed);
        });

    while (completed.load(std::memory_order_relaxed) < taskCount)
        std::this_thread::yield();

    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

static void run_examples() {
    const std::size_t cores = std::thread::hardware_concurrency();

    // CPU-bound: matching the core count is usually the sweet spot.
    setTitle("CPU-Bound Workload");

    constexpr int cpuTasks = 200;
    double cpuAtCores        = runBatch(cores,     cpuTasks, cpuBoundWork);
    double cpuOversubscribed = runBatch(cores * 4, cpuTasks, cpuBoundWork);

    std::cout << cores     << " threads : " << cpuAtCores        << " ms\n";
    std::cout << cores * 4 << " threads : " << cpuOversubscribed
              << " ms (extra threads mostly add overhead)\n\n";

    // I/O-bound: oversubscribing lets more blocked-on-I/O work overlap.
    setTitle("I/O-Bound Workload");

    constexpr int ioTasks = 200;
    double ioAtCores        = runBatch(cores,     ioTasks, ioBoundWork);
    double ioOversubscribed = runBatch(cores * 4, ioTasks, ioBoundWork);

    std::cout << cores     << " threads : " << ioAtCores        << " ms\n";
    std::cout << cores * 4 << " threads : " << ioOversubscribed
              << " ms (overlapping blocked time improves throughput)\n";
}

REGISTER_EXAMPLE_SUITE();
