// PulseThreadPool Core Benchmark Suite
// Measures fire-and-forget task submission against oneTBB.
//
// Covers:
// - detach() for a single task, no result observed
// - the direct oneTBB equivalent: task_group::run()
//
// This is the cleanest apples-to-apples comparison in the suite: both
// detach() and task_group::run() are "submit and don't wait" by design.

#include <support/framework.h>

#include <tbb/task_arena.h>
#include <tbb/task_group.h>

using namespace ThreadPoolPro;

constexpr int kWorkers = 4;

// Measures submitting a single no-op task without observing a result.
static void bench_detach_single() {
    ThreadPool pool(kWorkers);

    tbb::task_arena arena(kWorkers);
    arena.initialize();
    tbb::task_group tg;

    auto ptp = [&] { pool.detach([] {}); };

    auto otbb = [&] { arena.execute([&] { tg.run([] {}); }); };

    BENCH("detach single task", ptp, otbb);
    
    arena.execute([&] { tg.wait(); });
}

// Measures submitting a batch of tasks, then waiting for the whole
// batch to finish — the realistic way detach()/run() are actually used.
static void bench_detach_batch_drain() {
    constexpr int kBatch = 64;

    ThreadPool pool(kWorkers);

    tbb::task_arena arena(kWorkers);
    arena.initialize();
    tbb::task_group tg;

    auto ptp = [&] {
        for (int i = 0; i < kBatch; ++i)
            pool.detach([] {});

        pool.waitIdle();
    };

    auto otbb = [&] {
        arena.execute([&] {
            for (int i = 0; i < kBatch; ++i)
                tg.run([] {});
        });
        arena.execute([&] { tg.wait(); });
    };

    BENCH("detach batch + drain (64 tasks)", ptp, otbb);
}

// Executes all detach benchmark cases.
static void run_benchmarks() {
    bench_detach_single();
    std::cout << "\n";

    bench_detach_batch_drain();
}

REGISTER_BENCH_SUITE();
