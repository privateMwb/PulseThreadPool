// Pause/resume control example.
//
// Demonstrates:
// - pausing a pool to open a maintenance window
// - submitted tasks queuing up (not executing) while paused
// - an in-flight task still finishing normally when pause() is called
// - resuming to let queued work proceed

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {
    ThreadPool pool{2};
    std::atomic<int> completed{0};

    // Submit an in-flight task before pausing.
    setTitle("Before Maintenance Window");

    pool.detach([&completed] {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        completed.fetch_add(1, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::cout << "Submitted 1 task, letting it start\n\n";

    // Open the maintenance window: new tasks will queue but not run.
    setTitle("Maintenance Window");

    pool.pause();
    std::cout << "Paused : " << pool.isPaused() << "\n";

    for (int i = 0; i < 10; ++i)
        pool.detach([&completed] { completed.fetch_add(1, std::memory_order_relaxed); });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    std::cout << "Completed while paused : " << completed.load()
              << " (the in-flight task still finished; the 10 new ones are waiting)\n\n";

    // Close the maintenance window.
    setTitle("Resuming");

    pool.resume();

    while (completed.load(std::memory_order_relaxed) < 11)
        std::this_thread::yield();

    std::cout << "Completed after resume : " << completed.load() << " / 11\n";
}

REGISTER_EXAMPLE_SUITE();
