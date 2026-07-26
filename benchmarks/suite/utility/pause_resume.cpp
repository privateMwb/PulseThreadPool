// PulseThreadPool Utility Benchmark Suite
// Measures pause()/resume() overhead.
//
// Covers:
// - pause() on a pool with no active work
// - resume() (which must wake every idle worker)
//
// No oneTBB equivalent exists — tbb::task_arena has no pause/resume
// concept — so this runs through BENCH_SOLO().

#include <support/framework.h>

using namespace ThreadPoolPro;

constexpr int kWorkers = 4;

// Measures a pause()/resume() cycle on an idle pool.
static void bench_pause_resume_cycle() {
    ThreadPool pool(kWorkers);

    auto ptp = [&] {
        pool.pause();
        pool.resume();
    };

    BENCH_SOLO("pause + resume cycle", ptp);
}

// Executes all pause/resume benchmark cases.
static void run_benchmarks() {
    bench_pause_resume_cycle();
}

REGISTER_BENCH_SUITE();
