// Detail::Task benchmark suite.
// Measures construction, move, and invocation cost for both the
// inline (SBO) and heap-fallback storage paths.
//
// Covers:
// - construction cost (SBO vs heap fallback)
// - move construction cost (SBO vs heap fallback)
// - invocation cost (SBO vs heap fallback)

#include <common/framework.h>

#include <ThreadPoolPro/Detail/Task.h>

#include <array>

using namespace ThreadPoolPro::Detail;

// Small lambda: fits inline (SBO) storage.
static void bench_construct_small_sbo() {
    int x = 0;

    auto fn = [&] {
        Task t([&x] { ++x; });
        doNotOptimize(t);
    };
    BENCH("Task construct (SBO)", LARGE, fn);
}

// Large lambda: exceeds SboCapacity (48 bytes), falls back to heap.
static void bench_construct_large_heap() {
    std::array<int, 16> payload{};

    auto fn = [&] {
        Task t([payload] { doNotOptimize(payload); });
        doNotOptimize(t);
    };
    BENCH("Task construct (heap fallback)", LARGE, fn);
}

// Measures move construction for an SBO-stored task.
static void bench_move_construct_sbo() {
    auto fn = [&] {
        Task a([] {});
        Task b(std::move(a));
        doNotOptimize(b);
    };
    BENCH("Task move construct (SBO)", LARGE, fn);
}

// Measures move construction for a heap-stored task.
static void bench_move_construct_heap() {
    std::array<int, 16> payload{};

    auto fn = [&] {
        Task a([payload] { doNotOptimize(payload); });
        Task b(std::move(a));
        doNotOptimize(b);
    };
    BENCH("Task move construct (heap fallback)", LARGE, fn);
}

// Measures invocation cost for an SBO-stored task.
static void bench_invoke_sbo() {
    int x = 0;
    Task t([&x] { ++x; });

    auto fn = [&] { t(); };
    BENCH("Task invoke (SBO)", LARGE, fn);

    doNotOptimize(x);
}

// Measures invocation cost for a heap-stored task.
static void bench_invoke_heap() {
    std::array<int, 16> payload{};
    Task t([payload] { doNotOptimize(payload); });

    auto fn = [&] { t(); };
    BENCH("Task invoke (heap fallback)", LARGE, fn);
}

// Executes all Task benchmark cases.
static void run_benchmarks() {
    bench_construct_small_sbo();
    std::cout << "\n";

    bench_construct_large_heap();
    std::cout << "\n";

    bench_move_construct_sbo();
    std::cout << "\n";

    bench_move_construct_heap();
    std::cout << "\n";

    bench_invoke_sbo();
    std::cout << "\n";

    bench_invoke_heap();
}

REGISTER_BENCH_SUITE();
