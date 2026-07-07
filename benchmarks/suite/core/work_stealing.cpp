// ThreadPool work-stealing benchmark suite.
// Measures raw WorkStealingQueue push/pop/steal cost in isolation,
// then compares pool throughput under balanced vs. imbalanced load
// to show stealing's effect in practice.
//
// Covers:
// - uncontended push+pop cost (owner-only)
// - uncontended steal cost
// - pool throughput: externally submitted (naturally balanced) load
// - pool throughput: single-worker-origin (imbalanced) load requiring stealing
//
// Note: the throughput comparison measures total wall time for a fixed
// batch rather than a per-call average, so it uses the lower-level
// printing helpers directly instead of BENCH().

#include <common/framework.h>

#include <ThreadPoolPro/Detail/Task.h>
#include <ThreadPoolPro/Detail/WorkStealingQueue.h>

#include <atomic>
#include <thread>

using namespace ThreadPoolPro;
using namespace ThreadPoolPro::Detail;

// Measures uncontended push+pop cost on a single thread.
static void bench_push_pop_uncontended() {
    WorkStealingQueue q;

    auto fn = [&] {
        q.pushBottom(Task([] {}));
        auto t = q.popBottom();
        doNotOptimize(t);
    };
    BENCH("WorkStealingQueue push+pop (uncontended)", LARGE, fn);
}

// Measures uncontended steal cost on a single thread.
static void bench_steal_uncontended() {
    WorkStealingQueue q;

    auto fn = [&] {
        q.pushBottom(Task([] {}));
        auto t = q.steal();
        doNotOptimize(t);
    };
    BENCH("WorkStealingQueue steal (uncontended)", LARGE, fn);
}

// Measures total wall time to drain a batch of externally submitted tasks,
// which spread across workers naturally via the injection queue.
static void bench_balanced_external_load() {
    constexpr int taskCount = 200'000;

    ThreadPool pool{4};
    std::atomic<int> counter{0};

    auto start = steady_clock::now();

    for (int i = 0; i < taskCount; ++i)
        pool.detach([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });

    while (counter.load(std::memory_order_relaxed) < taskCount)
        std::this_thread::yield();

    auto elapsed = duration_cast<nanoseconds>(steady_clock::now() - start);

    setSubHeader("Balanced load (external submission)");
    std::cout << "  Total: " << timeColor(elapsed, formatDuration(elapsed)) << "\n";
}

// Measures total wall time to drain a batch of tasks that all originate
// from a single worker, forcing the other workers to steal in order to help.
static void bench_imbalanced_single_worker_origin() {
    constexpr int taskCount = 200'000;

    ThreadPool pool{4};
    std::atomic<int> counter{0};

    auto start = steady_clock::now();

    auto future = pool.enqueue([&pool, &counter] {
        for (int i = 0; i < taskCount; ++i)
            pool.detach([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
        return 0;
    });
    future.get();

    while (counter.load(std::memory_order_relaxed) < taskCount)
        std::this_thread::yield();

    auto elapsed = duration_cast<nanoseconds>(steady_clock::now() - start);

    setSubHeader("Imbalanced load (single-worker origin, requires stealing)");
    std::cout << "  Total: " << timeColor(elapsed, formatDuration(elapsed)) << "\n";
}

// Executes all work-stealing benchmark cases.
static void run_benchmarks() {
    bench_push_pop_uncontended();
    std::cout << "\n";

    bench_steal_uncontended();
    std::cout << "\n";

    bench_balanced_external_load();
    std::cout << "\n";

    bench_imbalanced_single_worker_origin();
}

REGISTER_BENCH_SUITE();
