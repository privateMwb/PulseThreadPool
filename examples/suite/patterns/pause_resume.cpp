// Pausing and resuming task execution.
//
// Demonstrates:
// - isPaused() before and after pause()
// - Already-running tasks finishing while paused
// - Newly submitted tasks waiting for resume() before they start
// - resume() releasing the queued work

#include <support/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(2);

    setTitle("Pausing");

    std::cout << "isPaused before: " << pool.isPaused() << "\n";

    pool.pause();

    std::cout << "isPaused after : " << pool.isPaused() << "\n\n";

    // Tasks submitted while paused are queued, not started.
    setTitle("Submitting while paused");

    std::atomic<int> started{0};

    for (int i = 0; i < 4; ++i) {
        pool.detach([&started] { ++started; });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "started while paused: " << started.load() << " (expected 0)\n";
    std::cout << "queuedTasks          : " << pool.queuedTasks() << "\n\n";

    // resume() lets workers pick the queued tasks back up.
    setTitle("Resuming");

    pool.resume();
    pool.waitIdle();

    std::cout << "started after resume: " << started.load() << " (expected 4)\n";
}

REGISTER_EXAMPLE_SUITE();
