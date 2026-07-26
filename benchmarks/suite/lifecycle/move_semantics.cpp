// PulseThreadPool Lifecycle Benchmark Suite
// Measures Task's move construction cost, in isolation from any pool.
//
// Covers:
// - move-constructing a Task holding an SBO-resident callable
// - move-constructing a Task holding a heap-allocated callable
//
// Task is deliberately non-copyable, so there's no copy_semantics
// counterpart here. No oneTBB equivalent exists — TBB has no comparable
// type-erased, relocatable callable wrapper — so both cases run through
// BENCH_SOLO().

#include <support/framework.h>

#include <array>

using namespace ThreadPoolPro::Detail;

// Measures move-constructing a Task whose callable fits inline storage.
static void bench_move_small() {
    auto ptp = [] {
        Task source([] {});
        Task moved(std::move(source));
    };

    BENCH_SOLO("move construct (SBO)", ptp);
}

// Measures move-constructing a Task whose callable was heap-allocated.
static void bench_move_large() {
    std::array<int, 16> payload{};

    auto ptp = [&] {
        Task source([payload] { doNotOptimize(payload); });
        Task moved(std::move(source));
    };

    BENCH_SOLO("move construct (heap)", ptp);
}

// Executes all move-semantics benchmark cases.
static void run_benchmarks() {
    bench_move_small();
    std::cout << "\n";

    bench_move_large();
}

REGISTER_BENCH_SUITE();
