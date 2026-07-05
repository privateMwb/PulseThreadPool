// ThreadPool execution and work-stealing test suite.
//
// Coverage:
// - Every submitted task executes exactly once
// - External bursts get distributed across multiple worker threads
// - Tasks submitted from inside a running task get stolen by idle workers
// - A large, imbalanced burst of tasks still completes promptly

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace ThreadPoolPro;

// Spins until the predicate holds or the timeout elapses.
static bool waitUntil(const std::function<bool()>& pred, int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

// Verifies every submitted task executes exactly once, with no drops or duplicates.
static void all_tasks_execute_exactly_once() {
    ThreadPool pool{4};

    constexpr int taskCount = 500;
    std::vector<std::atomic<int>> counters(taskCount);

    std::vector<std::future<void>> futures;
    futures.reserve(taskCount);

    for (int i = 0; i < taskCount; ++i)
        futures.push_back(pool.enqueue([&counters, i] {
            counters[i].fetch_add(1, std::memory_order_relaxed);
        }));

    for (auto& f : futures)
        f.get();

    bool allExactlyOnce = true;
    for (auto& c : counters)
        if (c.load() != 1) { allExactlyOnce = false; break; }

    CHK(allExactlyOnce == true);
}

// Verifies a burst of externally submitted tasks is distributed across workers.
static void work_stealing_distributes_across_workers() {
    ThreadPool pool{4};

    std::mutex idsMutex;
    std::set<std::thread::id> ids;

    constexpr int taskCount = 200;
    std::vector<std::future<void>> futures;
    futures.reserve(taskCount);

    for (int i = 0; i < taskCount; ++i) {
        futures.push_back(pool.enqueue([&idsMutex, &ids] {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            std::lock_guard<std::mutex> lock(idsMutex);
            ids.insert(std::this_thread::get_id());
        }));
    }

    for (auto& f : futures)
        f.get();

    CHK(ids.size() > 1);
}

// Verifies tasks submitted from inside a running task get stolen by idle workers.
static void internally_submitted_tasks_get_stolen() {
    ThreadPool pool{4};

    std::mutex idsMutex;
    std::set<std::thread::id> ids;
    std::atomic<int> completed{0};

    constexpr int subTaskCount = 40;

    auto future = pool.enqueue([&pool, &idsMutex, &ids, &completed] {
        for (int i = 0; i < subTaskCount; ++i) {
            pool.detach([&idsMutex, &ids, &completed] {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                {
                    std::lock_guard<std::mutex> lock(idsMutex);
                    ids.insert(std::this_thread::get_id());
                }
                completed.fetch_add(1, std::memory_order_relaxed);
            });
        }
        return 0;
    });

    future.get();

    CHK(waitUntil([&] { return completed.load() == subTaskCount; }));
    CHK(ids.size() > 1);
}

// Verifies a large, imbalanced burst of tasks still completes promptly.
static void heavy_imbalance_completes_promptly() {
    ThreadPool pool{4};

    constexpr int taskCount = 2000;
    std::atomic<int> counter{0};

    for (int i = 0; i < taskCount; ++i)
        pool.detach([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });

    CHK(waitUntil([&] { return counter.load() == taskCount; }, 5000));
}

// Executes all execution and work-stealing test cases.
static void run_tests() {
    RUN(all_tasks_execute_exactly_once);
    RUN(work_stealing_distributes_across_workers);
    RUN(internally_submitted_tasks_get_stolen);
    RUN(heavy_imbalance_completes_promptly);
}

REGISTER_TEST_SUITE();
