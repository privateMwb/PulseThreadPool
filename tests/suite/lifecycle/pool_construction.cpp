// PulseThreadPool ThreadPool construction test suite.
//
// Coverage:
// - A requested thread count of 0 is clamped to 1

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies a requested thread count of 0 is clamped to 1.
static void zero_thread_count_clamped_to_one() {
    ThreadPool pool(0);
    CHK(pool.threadCount() == 1);
}

// Executes all ThreadPool construction test cases.
static void run_tests() {
    RUN(zero_thread_count_clamped_to_one);
}

REGISTER_TEST_SUITE();
