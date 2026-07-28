// Observing the effect of work stealing.
//
// Demonstrates:
// - A single task loading its own worker's local queue with children
// - Idle workers stealing those children instead of sitting idle
// - The resulting wall-clock time being far below "one worker, serial"
// - The same batch run on a single-thread pool, for comparison

#include <support/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

namespace {
constexpr int taskCount = 200;
constexpr auto workDuration = std::chrono::milliseconds(2);

void spinFor(std::chrono::milliseconds d) {
    auto end = std::chrono::steady_clock::now() + d;
    while (std::chrono::steady_clock::now() < end) {
    }
}
} // namespace

static void run_examples() {

    // One task pushes every child into its own worker's local queue in
    // one go. Left alone, that worker would run all 200 serially — but
    // idle workers steal from the far end of that queue as soon as they
    // run out of their own work.
    setTitle("Fan-out onto four workers");

    {
        ThreadPool pool(4);
        std::atomic<int> completed{0};

        auto start = std::chrono::steady_clock::now();

        pool.detach([&pool, &completed] {
            for (int i = 0; i < taskCount; ++i) {
                pool.detach([&completed] {
                    spinFor(workDuration);
                    ++completed;
                });
            }
        });

        pool.waitIdle();
        auto elapsed = std::chrono::steady_clock::now() - start;

        std::cout << "completed: " << completed.load() << " / " << taskCount << "\n";
        std::cout << "elapsed  : "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                  << " ms\n\n";
    }

    // Same batch, one worker only — nothing to steal it away to, so
    // this is the serial baseline the run above beats.
    setTitle("Same batch, single worker");

    {
        ThreadPool pool(1);
        std::atomic<int> completed{0};

        auto start = std::chrono::steady_clock::now();

        pool.detach([&pool, &completed] {
            for (int i = 0; i < taskCount; ++i) {
                pool.detach([&completed] {
                    spinFor(workDuration);
                    ++completed;
                });
            }
        });

        pool.waitIdle();
        auto elapsed = std::chrono::steady_clock::now() - start;

        std::cout << "completed: " << completed.load() << " / " << taskCount << "\n";
        std::cout << "elapsed  : "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                  << " ms (expected roughly 4x the four-worker run)\n";
    }
}

REGISTER_EXAMPLE_SUITE();
