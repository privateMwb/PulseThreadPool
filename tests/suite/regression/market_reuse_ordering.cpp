// PulseThreadPool ThreadMarket reuse-ordering regression test.
//
// Regression coverage:
// - MarketThread::loop() must mark itself idle (hasWork_ = false) and
//   call returnToIdle() BEFORE notify_all() wakes any waiting
//   waitDone() caller. If notify happened first, a racing lease() could
//   run before the thread was visible in idleThreads_, missing the
//   reuse and spawning a brand-new OS thread instead — this was the
//   actual cause of construct/destroy being pathologically slow. Not
//   something a plain correctness assertion can catch (results are
//   still correct either way) — this checks wall-clock time as a
//   coarse smoke signal, since real OS thread creation is orders of
//   magnitude slower than reusing an already-parked thread.

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies many rapid ThreadPool construct/destroy cycles stay fast,
// which only holds if worker threads are actually being reused rather
// than a fresh OS thread spawned on every cycle.
static void rapid_pool_cycles_stay_fast() {
    constexpr int cycles = 200;

    // Warm the market up first so the timed loop below starts from an
    // already-populated idle pool, isolating reuse speed from the
    // one-time cost of spawning the very first threads.
    // clang-format off
    {
        ThreadPool warmup(4);
    }
    // clang-format on

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < cycles; ++i) {
        ThreadPool pool(4);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Reusing parked threads: microseconds per cycle. Spawning a fresh
    // OS thread per cycle: at least an order of magnitude slower. This
    // bound is deliberately generous to avoid flaking on a loaded CI box.
    CHK(elapsedMs < 2000);
}

// Executes all ThreadMarket reuse-ordering regression test cases.
static void run_tests() {
    RUN(rapid_pool_cycles_stay_fast);
}

REGISTER_TEST_SUITE();
