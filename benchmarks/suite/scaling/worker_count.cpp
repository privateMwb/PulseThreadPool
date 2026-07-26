// PulseThreadPool Scaling Benchmark Suite
// Measures how per-task submission cost changes as worker count grows,
// against oneTBB — the "varying worker counts" axis called for in the
// original performance review.
//
// Covers:
// - detach() / task_group::run() throughput at 1, 2, 4, 8, 16, and 32
//   workers, submitting a fixed-size batch and draining it each time

#include <support/framework.h>

#include <ThreadPoolPro/ThreadPool.h>

#include <tbb/task_arena.h>
#include <tbb/task_group.h>

using namespace ThreadPoolPro;

constexpr int kBatch = 256;

// Measures batch submission + drain at a given worker count.
static void bench_at_worker_count(int workers) {
    ThreadPool pool(static_cast<std::size_t>(workers));

    tbb::task_arena arena(workers);
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

    std::string label = "worker count " + std::to_string(workers);
    BENCH(label.c_str(), ptp, otbb);
}

// Executes the worker-count sweep.
static void run_benchmarks() {
    for (int workers : {1, 2, 4, 8, 16, 32}) {
        bench_at_worker_count(workers);
        std::cout << "\n";
    }
}

REGISTER_BENCH_SUITE();
