// PulseThreadPool repeated pool-lifecycle test suite.
//
// Coverage:
// - Many successive construct/run/destroy cycles, each reusing worker
//   threads returned to ThreadMarket by the previous cycle, execute
//   correctly every time

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies many successive pool construct/run/destroy cycles each
// execute their full batch of tasks correctly.
static void repeated_pool_cycles_execute_correctly() {
    for (int cycle = 0; cycle < 50; ++cycle) {
        ThreadPool pool(4);
        std::atomic<int> completed{0};

        for (int i = 0; i < 20; ++i)
            pool.detach([&completed]() { completed.fetch_add(1); });

        pool.waitIdle();
        CHK(completed.load() == 20);
    }
}

// Executes all repeated pool-lifecycle test cases.
static void run_tests() {
    RUN(repeated_pool_cycles_execute_correctly);
}

REGISTER_TEST_SUITE();
