// ThreadPool task submission benchmark suite.
// Measures the raw cost of submitting a task from an external
// (non-worker) thread while the pool is live and draining concurrently.
//
// Covers:
// - detach() overhead for a small (SBO) callable
// - enqueue() overhead for a small (SBO) callable
// - enqueue() overhead with forwarded arguments
// - detach() overhead for a large (heap-fallback) callable

#include <common/framework.h>

#include <array>

using namespace ThreadPoolPro;

// Measures detach() for a callable that fits inline (SBO) storage.
static void bench_detach_small_callable() {
    ThreadPool pool{4};

    auto fn = [&] {
        pool.detach([] {});
    };
    BENCH("detach() small callable", SMALL, fn);

    pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks);
}

// Measures enqueue() for a callable that fits inline (SBO) storage.
static void bench_enqueue_small_callable() {
    ThreadPool pool{4};

    auto fn = [&] {
        auto future = pool.enqueue([] {});
        doNotOptimize(future);
    };
    BENCH("enqueue() small callable", SMALL, fn);

    pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks);
}

// Measures enqueue() overhead with forwarded arguments.
static void bench_enqueue_with_arguments() {
    ThreadPool pool{4};

    auto fn = [&] {
        auto future = pool.enqueue([](int a, int b) { return a + b; }, 2, 3);
        doNotOptimize(future);
    };
    BENCH("enqueue() with arguments", SMALL, fn);

    pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks);
}

// Measures detach() for a callable large enough to fall back to the heap
// (SboCapacity is 48 bytes; this capture exceeds it).
static void bench_detach_large_callable() {
    ThreadPool pool{4};
    std::array<int, 16> payload{};

    auto fn = [&] {
        pool.detach([payload] { doNotOptimize(payload); });
    };
    BENCH("detach() large callable (heap path)", SMALL, fn);

    pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks);
}

// Executes all task submission benchmark cases.
static void run_benchmarks() {
    bench_detach_small_callable();
    std::cout << "\n";

    bench_enqueue_small_callable();
    std::cout << "\n";

    bench_enqueue_with_arguments();
    std::cout << "\n";

    bench_detach_large_callable();
}

REGISTER_BENCH_SUITE();
