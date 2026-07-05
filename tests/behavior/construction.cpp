// ThreadPool constructor and initial state test suite.
//
// Coverage:
// - Construction with a specified thread count
// - Zero thread count is clamped to one worker
// - Default constructor uses hardware_concurrency
// - Single-thread pool construction
// - Initial task state (active/queued/exceptions/idle)
// - Initial control state (paused/stopped)
// - Immediate destruction with no submitted tasks

#include <common/framework.h>

using namespace ThreadPoolPro;

// Verifies construction with the requested thread count.
static void basic_construction() {
    ThreadPool pool{4};

    CHK(pool.threadCount()     == 4);
    CHK(pool.activeTaskCount() == 0);
    CHK(pool.queuedTasks()     == 0);
}

// Verifies a zero thread count is clamped to one worker.
static void zero_thread_count() {
    ThreadPool pool{0};

    CHK(pool.threadCount() == 1);
}

// Verifies the default constructor produces at least one worker.
static void default_thread_count() {
    ThreadPool pool;

    CHK(pool.threadCount() >= 1);
}

// Verifies a single-thread pool constructs correctly.
static void single_thread_pool() {
    ThreadPool pool{1};

    CHK(pool.threadCount() == 1);
}

// Verifies a newly constructed pool starts with no active or queued tasks.
static void initial_task_state() {
    ThreadPool pool{4};

    CHK(pool.activeTaskCount() == 0);
    CHK(pool.queuedTasks()     == 0);
    CHK(pool.empty()           == true);
    CHK(pool.exceptionCount()  == 0);
    CHK(pool.idleThreadCount() <= pool.threadCount());
}

// Verifies a newly constructed pool is neither paused nor stopped.
static void initial_control_state() {
    ThreadPool pool{4};

    CHK(pool.isPaused()  == false);
    CHK(pool.isStopped() == false);
}

// Verifies a pool with no submitted tasks destructs immediately without hanging.
static void immediate_destruction() {
    {
        ThreadPool pool{4};
    }

    CHK(true);
}

// Executes all constructor and initial state test cases.
static void run_tests() {
    RUN(basic_construction);
    RUN(zero_thread_count);
    RUN(default_thread_count);
    RUN(single_thread_pool);
    RUN(initial_task_state);
    RUN(initial_control_state);
    RUN(immediate_destruction);
}

REGISTER_TEST_SUITE();
