// ThreadPool dispatch latency benchmark suite.
// Measures submit-to-start latency, both on an idle pool and under a
// heavy backlog (tail latency).
//
// Covers:
// - average and worst-case latency when the pool is otherwise idle
// - p50 / p99 / worst-case latency for a task submitted behind a
//   20,000-task backlog
//
// Note: these measurements need per-task timestamps and percentile
// math rather than a simple repeated-call average, so they use the
// lower-level printing helpers directly instead of the BENCH() macro.

#include <common/framework.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

using namespace ThreadPoolPro;

// Measures submit-to-start latency when the pool has no other work.
static void bench_dispatch_latency_idle() {
    ThreadPool pool{4};

    constexpr int sampleCount = 2000;
    std::vector<long long> latenciesNs(sampleCount);

    for (int i = 0; i < sampleCount; ++i) {
        auto submitTime = steady_clock::now();

        auto future = pool.enqueue([submitTime, &latenciesNs, i] {
            auto startTime = steady_clock::now();
            latenciesNs[i] = duration_cast<nanoseconds>(startTime - submitTime).count();
        });

        future.get();
    }

    long long total = 0, worst = 0;
    for (auto v : latenciesNs) {
        total += v;
        worst  = std::max(worst, v);
    }

    auto avg = nanoseconds(total / sampleCount);
    auto max = nanoseconds(worst);

    setSubHeader("Dispatch Latency (idle pool)");
    std::cout << "  Average: " << timeColor(avg, formatDuration(avg)) << "\n";
    std::cout << "  Worst:   " << timeColor(max, formatDuration(max)) << "\n";
}

// Measures submit-to-start latency for tasks submitted behind a
// large existing backlog (tail latency under load).
static void bench_dispatch_latency_under_load() {
    ThreadPool pool{4};

    constexpr int backlogSize = 20'000;
    for (int i = 0; i < backlogSize; ++i)
        pool.detach([] {});

    constexpr int sampleCount = 200;
    std::vector<long long> latenciesNs(sampleCount);
    std::atomic<int> probesDone{0};

    for (int i = 0; i < sampleCount; ++i) {
        auto submitTime = steady_clock::now();

        pool.detach([submitTime, &latenciesNs, i, &probesDone] {
            auto startTime = steady_clock::now();
            latenciesNs[i] = duration_cast<nanoseconds>(startTime - submitTime).count();
            probesDone.fetch_add(1, std::memory_order_relaxed);
        });
    }

    while (probesDone.load(std::memory_order_relaxed) < sampleCount)
        std::this_thread::yield();

    std::sort(latenciesNs.begin(), latenciesNs.end());

    auto p50   = nanoseconds(latenciesNs[sampleCount / 2]);
    auto p99   = nanoseconds(latenciesNs[static_cast<std::size_t>(sampleCount * 0.99)]);
    auto worst = nanoseconds(latenciesNs.back());

    setSubHeader("Dispatch Latency (behind a 20k-task backlog)");
    std::cout << "  p50:   " << timeColor(p50, formatDuration(p50)) << "\n";
    std::cout << "  p99:   " << timeColor(p99, formatDuration(p99)) << "\n";
    std::cout << "  Worst: " << timeColor(worst, formatDuration(worst)) << "\n";
}

// Executes all dispatch latency benchmark cases.
static void run_benchmarks() {
    bench_dispatch_latency_idle();
    std::cout << "\n";

    bench_dispatch_latency_under_load();
}

REGISTER_BENCH_SUITE();
