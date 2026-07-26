// PulseThreadPool Lifecycle Benchmark Suite
// Measures pool startup/teardown cost against oneTBB.
//
// Covers:
// - ThreadPool(n) construction + destruction
// - the equivalent oneTBB cost: task_arena(n) + eager initialize()
//
// tbb::task_arena is lazy by default — it doesn't spin up worker
// threads until first used. initialize() is called explicitly here so
// both sides pay for eager worker-thread creation, matching what
// ThreadPool's constructor always does.

#include <support/framework.h>

#include <tbb/task_arena.h>

using namespace ThreadPoolPro;

constexpr int kWorkers = 4;

// Measures full construction and destruction of a pool sized to kWorkers.
static void bench_construct_destroy() {
    auto ptp = [] { ThreadPool pool(kWorkers); };

    auto otbb = [] {
        tbb::task_arena arena(kWorkers);
        arena.initialize();
    };

    BENCH("construct + destroy", ptp, otbb);
}

// Executes all lifecycle construction benchmark cases.
static void run_benchmarks() {
    bench_construct_destroy();
}

REGISTER_BENCH_SUITE();
