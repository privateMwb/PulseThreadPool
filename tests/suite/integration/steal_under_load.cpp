// PulseThreadPool steal-under-load test suite.
//
// Coverage:
// - A burst of tasks pushed onto one worker's local queue (by a task
//   recursively submitting from inside itself) gets drained correctly,
//   with other idle workers actually helping rather than the one
//   overloaded worker draining it alone

#include <support/framework.h>

#include <set>

using namespace ThreadPoolPro;

// Verifies a heavily imbalanced burst of tasks on one worker's local
// queue is drained correctly, with more than one worker helping.
static void steal_drains_unevenly_loaded_worker() {
    ThreadPool pool(4);
    constexpr int childCount = 500;
    std::atomic<int> completed{0};
    std::mutex idsMutex;
    std::set<std::thread::id> idsSeen;

    pool.detach([&pool, &completed, &idsMutex, &idsSeen]() {
        for (int i = 0; i < childCount; ++i) {
            pool.detach([&completed, &idsMutex, &idsSeen]() {
                // A tiny bit of real work per child. Without this, the
                // owning worker can pop and finish all `childCount`
                // tasks off its own local queue (LIFO) faster than a
                // parked idle worker can be woken and start stealing —
                // the wakeup (wakeToken_.notify_one() -> scheduler
                // context switch) can cost more than these near-instant
                // tasks do, especially on schedulers with higher wake
                // latency (e.g. Android/Termux, where this assertion
                // was observed to fail). This isn't padding for its own
                // sake: it widens the burst just enough that stealing
                // is deterministically exercised instead of the
                // assertion depending on OS scheduling luck.
                std::atomic<int> spin{0};
                while (spin.fetch_add(1, std::memory_order_relaxed) < 2000) {}

                {
                    std::lock_guard<std::mutex> lock(idsMutex);
                    idsSeen.insert(std::this_thread::get_id());
                }
                completed.fetch_add(1);
            });
        }
    });

    pool.waitIdle();

    CHK(completed.load() == childCount);
    CHK(idsSeen.size() > 1); // more than one worker helped drain the burst
}

// Executes all steal-under-load test cases.
static void run_tests() {
    RUN(steal_drains_unevenly_loaded_worker);
}

REGISTER_TEST_SUITE();
