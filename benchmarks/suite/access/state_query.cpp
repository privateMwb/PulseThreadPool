// PulseThreadPool Access Benchmark Suite
// Measures the cost of reading already-running pool state.
//
// Covers:
// - activeTaskCount() / queuedTasks() under load
// - idleThreadCount()
// - isPaused() / isStopped() / empty()
//
// No oneTBB equivalent exists for most of these — tbb::task_arena
// exposes no per-arena queue depth, active-task count, or idle-thread
// introspection. These run through BENCH_SOLO() rather than BENCH().

#include <support/framework.h>

using namespace ThreadPoolPro;

// Measures activeTaskCount() and queuedTasks() while the pool is
// continuously busy running long-lived tasks in the background.
static void bench_task_counts() {
    ThreadPool pool(4);

    for (int i = 0; i < 4; ++i)
        pool.detach([] { std::this_thread::sleep_for(std::chrono::seconds(30)); });

    auto ptp = [&] {
        doNotOptimize(pool.activeTaskCount());
        doNotOptimize(pool.queuedTasks());
    };

    BENCH_SOLO("active/queued counts", ptp);

    pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks);
}

// Measures idleThreadCount() on a pool with no work queued.
static void bench_idle_count() {
    ThreadPool pool(4);

    auto ptp = [&] { doNotOptimize(pool.idleThreadCount()); };

    BENCH_SOLO("idle thread count", ptp);
}

// Measures the cheap boolean state queries together.
static void bench_bool_state() {
    ThreadPool pool(4);

    auto ptp = [&] {
        doNotOptimize(pool.isPaused());
        doNotOptimize(pool.isStopped());
        doNotOptimize(pool.empty());
    };

    BENCH_SOLO("paused/stopped/empty", ptp);
}

// Executes all state-query benchmark cases.
static void run_benchmarks() {
    bench_task_counts();
    std::cout << "\n";

    bench_idle_count();
    std::cout << "\n";

    bench_bool_state();
}

REGISTER_BENCH_SUITE();
