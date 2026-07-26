// PulseThreadPool Utility Benchmark Suite
// Measures the cost of running a task that throws.
//
// Covers:
// - detach() on a task that throws uncaught, letting ThreadPool's own
//   catch-and-count path (exceptionCounter_) handle it
// - the same workload with matched semantics against oneTBB: each task
//   catches its own exception and increments a shared counter, so both
//   sides observably run all N tasks to completion
//
// The first case has no fair oneTBB comparison: tbb::task_group cancels
// remaining submitted tasks after the first uncaught exception it
// observes, while ThreadPool counts every one and keeps running the
// rest — timing "run N throwing tasks" against each other would
// silently compare two different workloads (TBB wouldn't actually run
// all N), so it runs through BENCH_SOLO(). The second case sidesteps
// that mismatch by catching inside the task itself on both sides, so
// neither library's cancellation/counting behavior is in play — what's
// left to measure is genuinely comparable: the cost of throwing and
// catching an exception once per task, at the language level, under
// each scheduler.

#include <support/framework.h>

#include <ThreadPoolPro/ThreadPool.h>

#include <tbb/task_arena.h>
#include <tbb/task_group.h>

#include <atomic>
#include <stdexcept>

using namespace ThreadPoolPro;

constexpr int kWorkers = 4;

// Measures detach() throughput when every task throws uncaught,
// exercising ThreadPool's own exceptionCounter_ path. No oneTBB
// counterpart — see the file-level comment.
static void bench_uncaught_exception() {
    ThreadPool pool(kWorkers);

    auto ptp = [&] { pool.detach([] { throw std::runtime_error("bench"); }); };

    BENCH_SOLO("detach (uncaught exception)", ptp);

    pool.waitIdle();

    doNotOptimize(pool.exceptionCount());
}

// Measures throw-and-catch cost per task, with both sides swallowing
// the exception inside the task itself so all N tasks genuinely run on
// both sides — a fair comparison against oneTBB.
static void bench_caught_exception() {
    ThreadPool pool(kWorkers);

    tbb::task_arena arena(kWorkers);
    arena.initialize();
    tbb::task_group tg;

    std::atomic<long long> ptpCounter{0};
    std::atomic<long long> otbbCounter{0};

    auto ptp = [&] {
        pool.detach([&] {
            try {
                throw std::runtime_error("bench");
            } catch (...) {
                ptpCounter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    };

    auto otbb = [&] {
        arena.execute([&] {
            tg.run([&] {
                try {
                    throw std::runtime_error("bench");
                } catch (...) {
                    otbbCounter.fetch_add(1, std::memory_order_relaxed);
                }
            });
        });
    };

    BENCH("detach (caught exception)", ptp, otbb);

    pool.waitIdle();
    arena.execute([&] { tg.wait(); });

    doNotOptimize(ptpCounter.load());
    doNotOptimize(otbbCounter.load());
}

// Executes all exception-path benchmark cases.
static void run_benchmarks() {
    bench_uncaught_exception();
    std::cout << "\n";

    bench_caught_exception();
}

REGISTER_BENCH_SUITE();
