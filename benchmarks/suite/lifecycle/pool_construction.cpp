// ThreadPool construction benchmark suite.
// Measures pool startup/teardown cost across different thread counts.
//
// Covers:
// - construction + teardown with a single worker thread
// - construction + teardown with four worker threads
// - construction + teardown with hardware_concurrency() worker threads
// - default constructor

#include <common/framework.h>

#include <thread>

using namespace ThreadPoolPro;

// Unlike lightweight container benchmarks, constructing a ThreadPool spawns
// real OS threads, so iteration counts are far smaller than the
// SMALL/MEDIUM/LARGE presets (which assume near-free operations).
constexpr std::size_t POOL_ITER = 200;

// Measures construction and teardown with a single worker thread.
static void bench_construct_single_thread() {
    auto fn = [&] {
        ThreadPool pool{1};
        doNotOptimize(pool);
    };
    BENCH("ThreadPool construct (1 thread)", POOL_ITER, fn);
}

// Measures construction and teardown with four worker threads.
static void bench_construct_four_threads() {
    auto fn = [&] {
        ThreadPool pool{4};
        doNotOptimize(pool);
    };
    BENCH("ThreadPool construct (4 threads)", POOL_ITER, fn);
}

// Measures construction and teardown with hardware_concurrency() worker threads.
static void bench_construct_hardware_concurrency() {
    const std::size_t n = std::thread::hardware_concurrency();

    auto fn = [&] {
        ThreadPool pool{n};
        doNotOptimize(pool);
    };
    BENCH("ThreadPool construct (hardware_concurrency)", POOL_ITER, fn);
}

// Measures the default constructor.
static void bench_default_construct() {
    auto fn = [&] {
        ThreadPool pool;
        doNotOptimize(pool);
    };
    BENCH("ThreadPool default construct", POOL_ITER, fn);
}

// Executes all construction benchmark cases.
static void run_benchmarks() {
    bench_construct_single_thread();
    std::cout << "\n";

    bench_construct_four_threads();
    std::cout << "\n";

    bench_construct_hardware_concurrency();
    std::cout << "\n";

    bench_default_construct();
}

REGISTER_BENCH_SUITE();
