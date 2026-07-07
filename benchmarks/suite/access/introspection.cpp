// ThreadPool introspection benchmark suite.
// Measures the per-call cost of the read-only runtime statistics and
// state query methods.
//
// Covers:
// - activeTaskCount()
// - queuedTasks()
// - idleThreadCount()
// - empty()
// - isPaused()
// - isStopped()
// - threadCount()
// - exceptionCount()

#include <common/framework.h>

using namespace ThreadPoolPro;

static void bench_active_task_count() {
    ThreadPool pool{4};
    auto fn = [&] { doNotOptimize(pool.activeTaskCount()); };
    BENCH("activeTaskCount()", LARGE, fn);
}

static void bench_queued_tasks() {
    ThreadPool pool{4};
    auto fn = [&] { doNotOptimize(pool.queuedTasks()); };
    BENCH("queuedTasks()", LARGE, fn);
}

static void bench_idle_thread_count() {
    ThreadPool pool{4};
    auto fn = [&] { doNotOptimize(pool.idleThreadCount()); };
    BENCH("idleThreadCount()", LARGE, fn);
}

static void bench_empty() {
    ThreadPool pool{4};
    auto fn = [&] { doNotOptimize(pool.empty()); };
    BENCH("empty()", LARGE, fn);
}

static void bench_is_paused() {
    ThreadPool pool{4};
    auto fn = [&] { doNotOptimize(pool.isPaused()); };
    BENCH("isPaused()", LARGE, fn);
}

static void bench_is_stopped() {
    ThreadPool pool{4};
    auto fn = [&] { doNotOptimize(pool.isStopped()); };
    BENCH("isStopped()", LARGE, fn);
}

static void bench_thread_count() {
    ThreadPool pool{4};
    auto fn = [&] { doNotOptimize(pool.threadCount()); };
    BENCH("threadCount()", LARGE, fn);
}

static void bench_exception_count() {
    ThreadPool pool{4};
    auto fn = [&] { doNotOptimize(pool.exceptionCount()); };
    BENCH("exceptionCount()", LARGE, fn);
}

// Executes all introspection benchmark cases.
static void run_benchmarks() {
    bench_active_task_count();
    std::cout << "\n";

    bench_queued_tasks();
    std::cout << "\n";

    bench_idle_thread_count();
    std::cout << "\n";

    bench_empty();
    std::cout << "\n";

    bench_is_paused();
    std::cout << "\n";

    bench_is_stopped();
    std::cout << "\n";

    bench_thread_count();
    std::cout << "\n";

    bench_exception_count();
}

REGISTER_BENCH_SUITE();
