// PulseThreadPool pause()/resume() test suite.
//
// Coverage:
// - isPaused() reflects pause()/resume() calls
// - A task submitted while paused doesn't start until resume()

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies isPaused() reflects pause()/resume() calls.
static void pause_resume_toggles_state() {
    ThreadPool pool(2);
    CHK(!pool.isPaused());
    pool.pause();
    CHK(pool.isPaused());
    pool.resume();
    CHK(!pool.isPaused());
}

// Verifies a task submitted while paused doesn't start until resume().
static void paused_task_waits_for_resume() {
    ThreadPool pool(2);
    pool.pause();

    std::atomic<bool> ran{false};
    pool.detach([&ran]() { ran.store(true); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHK(!ran.load());

    pool.resume();
    pool.waitIdle();
    CHK(ran.load());
}

// Executes all pause()/resume() test cases.
static void run_tests() {
    RUN(pause_resume_toggles_state);
    RUN(paused_task_waits_for_resume);
}

REGISTER_TEST_SUITE();
