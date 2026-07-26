// PulseThreadPool Core Benchmark Suite
// Measures the Chase-Lev queue primitives directly, bypassing ThreadPool.
//
// Covers:
// - pushBottom() + popBottom() on the owning thread (uncontended)
// - steal() from a second thread while the owner keeps pushing/popping
//
// No oneTBB equivalent exists — TBB's internal work-stealing deque isn't
// public API, so there's nothing to compare against at this level. Both
// cases run through BENCH_SOLO().

#include <support/framework.h>

#include <atomic>
#include <thread>

using namespace ThreadPoolPro::Detail;

// Measures a push immediately followed by a pop on the same (owning)
// thread — the uncontended fast path with no atomic RMW required.
static void bench_push_pop_uncontended() {
    WorkStealingQueue queue;

    auto ptp = [&] {
        queue.pushBottom(Task([] {}));
        auto task = queue.popBottom();
        doNotOptimize(task.has_value());
    };

    BENCH_SOLO("pushBottom + popBottom (uncontended)", ptp);
}

// Measures pushBottom() while a second thread continuously steals —
// the contended cross-thread path.
static void bench_steal_contended() {
    WorkStealingQueue queue;
    std::atomic<bool> stop{false};
    std::atomic<long long> stolen{0};

    std::thread thief([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            if (auto task = queue.steal())
                stolen.fetch_add(1, std::memory_order_relaxed);
            else
                std::this_thread::yield();
        }
    });

    auto ptp = [&] { queue.pushBottom(Task([] {})); };

    BENCH_SOLO("pushBottom (contended by steal)", ptp);

    stop.store(true, std::memory_order_relaxed);
    thief.join();
    doNotOptimize(stolen.load());
}

// Executes all queue-primitive benchmark cases.
static void run_benchmarks() {
    bench_push_pop_uncontended();
    std::cout << "\n";

    bench_steal_contended();
}

REGISTER_BENCH_SUITE();
