// PulseThreadPool waitIdle()-helps-drain test suite.
//
// Coverage:
// - A thread blocked in waitIdle() helps drain the pool itself
//   (via fetchTaskExternal()) instead of sitting idle while waiting

#include <support/framework.h>

#include <set>

using namespace ThreadPoolPro;

// Verifies the thread calling waitIdle() actually helps execute queued
// tasks rather than just waiting on the single worker to finish them.
static void wait_idle_thread_helps_drain() {
    ThreadPool pool(1);
    constexpr int taskCount = 200;
    std::atomic<int> completed{0};
    std::mutex idsMutex;
    std::set<std::thread::id> idsSeen;

    for (int i = 0; i < taskCount; ++i) {
        pool.detach([&completed, &idsMutex, &idsSeen]() {
            {
                std::lock_guard<std::mutex> lock(idsMutex);
                idsSeen.insert(std::this_thread::get_id());
            }
            completed.fetch_add(1);
        });
    }

    pool.waitIdle();

    CHK(completed.load() == taskCount);
    CHK(idsSeen.count(std::this_thread::get_id()) == 1); // the waiting thread itself helped
}

// Executes all waitIdle()-helps-drain test cases.
static void run_tests() {
    RUN(wait_idle_thread_helps_drain);
}

REGISTER_TEST_SUITE();
