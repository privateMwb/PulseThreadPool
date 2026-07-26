// Detail::WorkStealingQueue test suite.
//
// Coverage:
// - pushBottom() / popBottom() round-trips a task
// - popBottom() and steal() on an empty queue return nullopt
// - popBottom() returns tasks in LIFO order (owner side)
// - steal() returns tasks in FIFO order (thief side)
// - size() reflects the current pending count
// - growth beyond initial capacity preserves all tasks
// - concurrent popBottom() vs steal() under contention: no drops, no duplicates

#include <common/framework.h>

#include <ThreadPoolPro/Detail/WorkStealingQueue.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace ThreadPoolPro::Detail;

// Verifies a pushed task can be popped back and executed.
static void push_then_pop_returns_task() {
    WorkStealingQueue q;
    bool ran = false;

    q.pushBottom(Task([&ran] { ran = true; }));

    auto task = q.popBottom();
    CHK(task.has_value());
    (*task)();
    CHK(ran == true);
}

// Verifies popBottom() on an empty queue returns nullopt.
static void pop_on_empty_queue_returns_nullopt() {
    WorkStealingQueue q;

    CHK(q.popBottom().has_value() == false);
}

// Verifies steal() on an empty queue returns nullopt.
static void steal_on_empty_queue_returns_nullopt() {
    WorkStealingQueue q;

    CHK(q.steal().has_value() == false);
}

// Verifies popBottom() retrieves tasks in LIFO order.
static void pop_returns_lifo_order() {
    WorkStealingQueue q;
    std::vector<int> order;

    for (int i = 0; i < 5; ++i)
        q.pushBottom(Task([&order, i] { order.push_back(i); }));

    for (int i = 0; i < 5; ++i) {
        auto task = q.popBottom();
        CHK(task.has_value());
        (*task)();
    }

    std::vector<int> expected{4, 3, 2, 1, 0};
    CHK(order == expected);
}

// Verifies steal() retrieves tasks in FIFO order.
static void steal_returns_fifo_order() {
    WorkStealingQueue q;
    std::vector<int> order;

    for (int i = 0; i < 5; ++i)
        q.pushBottom(Task([&order, i] { order.push_back(i); }));

    for (int i = 0; i < 5; ++i) {
        auto task = q.steal();
        CHK(task.has_value());
        (*task)();
    }

    std::vector<int> expected{0, 1, 2, 3, 4};
    CHK(order == expected);
}

// Verifies size() reflects the current pending count.
static void size_reflects_pending_count() {
    WorkStealingQueue q;

    CHK(q.size() == 0);

    q.pushBottom(Task([] {}));
    q.pushBottom(Task([] {}));
    CHK(q.size() == 2);

    (void)q.popBottom();
    CHK(q.size() == 1);
}

// Verifies growth beyond the initial capacity preserves every task exactly once.
static void growth_beyond_initial_capacity() {
    WorkStealingQueue q{4};

    constexpr int taskCount = 500;
    std::vector<std::atomic<int>> counters(taskCount);

    for (int i = 0; i < taskCount; ++i)
        q.pushBottom(Task([&counters, i] { counters[i].fetch_add(1, std::memory_order_relaxed); }));

    int popped = 0;
    while (auto task = q.popBottom()) {
        (*task)();
        ++popped;
    }

    CHK(popped == taskCount);

    bool allExactlyOnce = true;
    for (auto& c : counters)
        if (c.load() != 1) { allExactlyOnce = false; break; }

    CHK(allExactlyOnce == true);
}

// Verifies concurrent popBottom() (owner) and steal() (thieves) never drop
// or duplicate a task, even under heavy contention.
static void concurrent_steal_vs_pop_no_duplicates_no_drops() {
    constexpr int taskCount  = 20000;
    constexpr int thiefCount = 4;

    WorkStealingQueue q;
    std::vector<std::atomic<int>> counters(taskCount);

    for (int i = 0; i < taskCount; ++i)
        q.pushBottom(Task([&counters, i] { counters[i].fetch_add(1, std::memory_order_relaxed); }));

    std::atomic<int> totalRetrieved{0};

    auto thief = [&] {
        while (true) {
            auto task = q.steal();
            if (task) {
                (*task)();
                totalRetrieved.fetch_add(1, std::memory_order_relaxed);
            } else if (totalRetrieved.load(std::memory_order_relaxed) >= taskCount) {
                return;
            } else {
                std::this_thread::yield();
            }
        }
    };

    std::vector<std::thread> thieves;
    for (int i = 0; i < thiefCount; ++i)
        thieves.emplace_back(thief);

    while (auto task = q.popBottom()) {
        (*task)();
        totalRetrieved.fetch_add(1, std::memory_order_relaxed);
    }

    for (auto& t : thieves)
        t.join();

    CHK(totalRetrieved.load() == taskCount);

    bool allExactlyOnce = true;

for (std::size_t i = 0; i < counters.size(); ++i) {
    int n = counters[i].load();

    if (n != 1) {
        std::cout << i << " -> " << n << '\n';
        allExactlyOnce = false;
    }
}
    CHK(allExactlyOnce == true);
}

// Executes all WorkStealingQueue test cases.
static void run_tests() {
    RUN(push_then_pop_returns_task);
    RUN(pop_on_empty_queue_returns_nullopt);
    RUN(steal_on_empty_queue_returns_nullopt);
    RUN(pop_returns_lifo_order);
    RUN(steal_returns_fifo_order);
    RUN(size_reflects_pending_count);
    RUN(growth_beyond_initial_capacity);
    RUN(concurrent_steal_vs_pop_no_duplicates_no_drops);
}

REGISTER_TEST_SUITE();
