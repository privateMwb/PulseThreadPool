// PulseThreadPool pause/resume cycle test suite.
//
// Coverage:
// - Pausing mid-batch stops any new task from starting; resuming lets
//   the rest of the batch run to completion

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies pausing mid-batch blocks new tasks from starting, and
// resuming drains the rest of the batch.
static void pause_mid_batch_then_resume_drains_rest() {
    ThreadPool pool(1);
    std::atomic<int> completed{0};
    std::atomic<bool> release{false};
    std::atomic<bool> started{false};

    // Occupy the single worker so the rest of the batch queues up.
    pool.detach([&release, &started]() {
        started.store(true);
        while (!release.load())
            std::this_thread::yield();
    });

    while (!started.load())
        std::this_thread::yield();

    pool.pause();
    for (int i = 0; i < 10; ++i)
        pool.detach([&completed]() { completed.fetch_add(1); });

    release.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHK(completed.load() == 0); // still paused after the blocking task released

    pool.resume();
    pool.waitIdle();
    CHK(completed.load() == 10);
}

// Executes all pause/resume cycle test cases.
static void run_tests() {
    RUN(pause_mid_batch_then_resume_drains_rest);
}

REGISTER_TEST_SUITE();
