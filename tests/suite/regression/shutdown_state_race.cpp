// PulseThreadPool shutdown-state regression test.
//
// Regression coverage:
// - runState_ is a single atomic RunState, not a separate stop flag
//   plus a plain enum. With the old design, two threads calling
//   shutdown() concurrently with different modes were a data race on
//   that plain enum — only one CAS winner may ever write it now. This
//   repeatedly races many threads against a single shutdown() call, on
//   many separate pools, to give a thread sanitizer many chances to
//   flag any regression back to the unguarded write.

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies many threads racing shutdown() concurrently, repeated across
// many separate pools, always resolves cleanly with no crash and no
// inconsistent isStopped() observation.
static void many_way_shutdown_race_resolves_cleanly() {
    constexpr int iterations = 20;
    constexpr int racerCount = 4;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        ThreadPool pool(2);

        std::vector<std::thread> racers;
        for (int r = 0; r < racerCount; ++r) {
            auto mode = (r % 2 == 0) ? ThreadPool::ShutdownMode::FinishTasks
                                     : ThreadPool::ShutdownMode::DiscardTasks;
            racers.emplace_back([&pool, mode]() { pool.shutdown(mode); });
        }

        for (auto& racer : racers)
            racer.join();

        CHK(pool.isStopped());
    }
}

// Executes all shutdown-state regression test cases.
static void run_tests() {
    RUN(many_way_shutdown_race_resolves_cleanly);
}

REGISTER_TEST_SUITE();
