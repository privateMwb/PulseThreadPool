// PulseThreadPool Access Benchmark Suite
// Measures Task invocation overhead in isolation, outside of any pool.
//
// Covers:
// - operator() on a Task holding a small (SBO-resident) callable
// - operator() on a Task holding a large (heap-allocated) callable
//
// No oneTBB equivalent exists at this granularity — TBB exposes no
// public type-erased callable wrapper comparable to Task, so these run
// through BENCH_SOLO() rather than BENCH().

#include <support/framework.h>

#include <array>

using namespace ThreadPoolPro::Detail;

// Measures operator() on a Task whose callable fits inline storage.
static void bench_invoke_small() {
    int counter = 0;
    Task task([&counter] { ++counter; });

    auto ptp = [&] { task(); };

    BENCH_SOLO("invoke small (SBO)", ptp);

    doNotOptimize(counter);
}

// Measures operator() on a Task whose callable is too large for inline
// storage and was heap-allocated at construction.
static void bench_invoke_large() {
    std::array<int, 16> payload{};
    int counter = 0;
    Task task([&counter, payload] {
        ++counter;
        doNotOptimize(payload);
    });

    auto ptp = [&] { task(); };

    BENCH_SOLO("invoke large (heap)", ptp);

    doNotOptimize(counter);
}

// Executes all invocation benchmark cases.
static void run_benchmarks() {
    bench_invoke_small();
    std::cout << "\n";

    bench_invoke_large();
}

REGISTER_BENCH_SUITE();
