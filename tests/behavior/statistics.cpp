// ThreadPool runtime statistics test suite.
//
// Coverage:
// - activeTaskCount() reflects tasks currently executing
// - queuedTasks() reflects tasks submitted but not yet picked up
// - empty() reflects whether any work is outstanding
// - idleThreadCount() reflects workers currently waiting for work
// - activeTaskCount() and queuedTasks() are both accurate under load together

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

// Verifies activeTaskCount() reflects the number of currently running tasks.
static void active_task_count_reflects_running_tasks() {
    constexpr int workers = 4;
    ThreadPool pool{workers};

    std::atomic<int> arrived{0};
    std::atomic<bool> release{false};

    for (int i = 0; i < workers; ++i) {
        pool.detach([&arrived, &release] {
            arrived.fetch_add(1, std::memory_order_relaxed);
            while (!release.load(std::memory_order_relaxed))
                std::this_thread::yield();
        });
    }

    CHK(waitUntil([&] { return arrived.load() == workers; }));
    CHK(pool.activeTaskCount() == static_cast<std::size_t>(workers));

    release.store(true, std::memory_order_relaxed);
    CHK(waitUntil([&] { return pool.activeTaskCount() == 0; }));
}

// Verifies queuedTasks() and empty() reflect a pending backlog.
static void queued_tasks_reflects_backlog() {
    ThreadPool pool{1};
    std::atomic<bool> release{false};
    std::atomic<bool> blockerStarted{false};

    pool.detach([&release, &blockerStarted] {
        blockerStarted.store(true, std::memory_order_relaxed);
        while (!release.load(std::memory_order_relaxed))
            std::this_thread::yield();
    });

    CHK(waitUntil([&] { return blockerStarted.load(); }));

    constexpr int backlog = 10;
    std::atomic<int> completed{0};
    for (int i = 0; i < backlog; ++i)
        pool.detach([&completed] { completed.fetch_add(1, std::memory_order_relaxed); });

    CHK(pool.queuedTasks() == static_cast<std::size_t>(backlog));
    CHK(pool.empty() == false);

    release.store(true, std::memory_order_relaxed);
    CHK(waitUntil([&] { return completed.load() == backlog; }));
    CHK(waitUntil([&] { return pool.queuedTasks() == 0; }));
    CHK(pool.empty() == true);
}

// Verifies idleThreadCount() converges to the full worker count when idle.
static void idle_thread_count_reflects_idle_workers() {
    ThreadPool pool{4};

    CHK(waitUntil([&] { return pool.idleThreadCount() == pool.threadCount(); }));
}

// Verifies activeTaskCount() and queuedTasks() are both accurate at once under load.
static void active_and_queued_under_load_together() {
    constexpr int workers    = 2;
    constexpr int totalTasks = 6;

    ThreadPool pool{workers};

    std::atomic<int> arrived{0};
    std::atomic<bool> release{false};

    for (int i = 0; i < totalTasks; ++i) {
        pool.detach([&arrived, &release] {
            arrived.fetch_add(1, std::memory_order_relaxed);
            while (!release.load(std::memory_order_relaxed))
                std::this_thread::yield();
        });
    }

    CHK(waitUntil([&] { return arrived.load() == workers; }));

    CHK(pool.activeTaskCount() == static_cast<std::size_t>(workers));
    CHK(pool.queuedTasks()     == static_cast<std::size_t>(totalTasks - workers));

    release.store(true, std::memory_order_relaxed);
    CHK(waitUntil([&] { return arrived.load() == totalTasks; }));
    CHK(waitUntil([&] { return pool.activeTaskCount() == 0; }));
    CHK(waitUntil([&] { return pool.queuedTasks()     == 0; }));
}

// Executes all runtime statistics test cases.
static void run_tests() {
    RUN(active_task_count_reflects_running_tasks);
    RUN(queued_tasks_reflects_backlog);
    RUN(idle_thread_count_reflects_idle_workers);
    RUN(active_and_queued_under_load_together);
}

REGISTER_TEST_SUITE();
