// ThreadPool pause and resume test suite.
//
// Coverage:
// - pause() prevents new tasks from being picked up
// - resume() wakes workers and lets picked-up tasks run
// - an in-flight task keeps running to completion after pause() is called
// - isPaused() reflects the current control state
// - repeated pause/resume cycles behave correctly

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

using namespace ThreadPoolPro;

// Spins until the predicate holds or the timeout elapses.
static bool waitUntil(const std::function<bool()>& pred, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

// Verifies pause() prevents newly submitted tasks from being picked up.
static void pause_prevents_new_task_pickup() {
    ThreadPool pool{4};
    pool.pause();

    std::atomic<int> counter{0};
    constexpr int taskCount = 20;
    for (int i = 0; i < taskCount; ++i)
        pool.detach([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHK(counter.load() == 0);

    pool.resume();
    CHK(waitUntil([&] { return counter.load() == taskCount; }));
}

// Verifies an in-flight task keeps running to completion after pause() is called,
// while subsequently submitted tasks wait until resume().
static void in_flight_task_completes_after_pause() {
    ThreadPool pool{1};
    std::atomic<bool> longTaskDone{false};
    std::atomic<bool> nextTaskRan{false};

    pool.detach([&longTaskDone] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        longTaskDone.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    pool.pause();

    pool.detach([&nextTaskRan] { nextTaskRan.store(true); });

    CHK(waitUntil([&] { return longTaskDone.load(); }));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHK(nextTaskRan.load() == false);

    pool.resume();
    CHK(waitUntil([&] { return nextTaskRan.load(); }));
}

// Verifies isPaused() reflects the current control state.
static void is_paused_reflects_state() {
    ThreadPool pool{2};

    CHK(pool.isPaused() == false);

    pool.pause();
    CHK(pool.isPaused() == true);

    pool.resume();
    CHK(pool.isPaused() == false);
}

// Verifies repeated pause/resume cycles behave correctly.
static void multiple_pause_resume_cycles() {
    ThreadPool pool{2};
    std::atomic<int> counter{0};

    for (int cycle = 0; cycle < 3; ++cycle) {
        pool.pause();
        pool.detach([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        int before = counter.load();

        pool.resume();
        CHK(waitUntil([&] { return counter.load() == before + 1; }));
    }

    CHK(counter.load() == 3);
}

// Executes all pause and resume test cases.
static void run_tests() {
    RUN(pause_prevents_new_task_pickup);
    RUN(in_flight_task_completes_after_pause);
    RUN(is_paused_reflects_state);
    RUN(multiple_pause_resume_cycles);
}

REGISTER_TEST_SUITE();
