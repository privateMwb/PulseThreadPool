// Shutting a pool down.
//
// Demonstrates:
// - isStopped() before and after shutdown()
// - ShutdownMode::FinishTasks draining the queue before stopping
// - ShutdownMode::DiscardTasks abandoning what's still queued
// - The destructor calling shutdown() automatically if it wasn't already

#include <support/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {

    // shutdown() defaults to FinishTasks: every already-queued task
    // still runs before the pool stops accepting new state changes.
    setTitle("FinishTasks (the default)");

    {
        ThreadPool pool(2);
        std::atomic<int> completed{0};

        for (int i = 0; i < 10; ++i) {
            pool.detach([&completed] { ++completed; });
        }

        pool.shutdown(ThreadPool::ShutdownMode::FinishTasks);

        std::cout << "isStopped     : " << pool.isStopped() << "\n";
        std::cout << "tasks completed: " << completed.load() << " / 10\n\n";
    }

    // DiscardTasks stops as soon as possible — tasks still sitting in
    // the queue are abandoned rather than run.
    setTitle("DiscardTasks");

    {
        ThreadPool pool(1);
        std::atomic<int> completed{0};

        for (int i = 0; i < 10; ++i) {
            pool.detach([&completed] {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                ++completed;
            });
        }

        pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks);

        std::cout << "isStopped      : " << pool.isStopped() << "\n";
        std::cout << "tasks completed: " << completed.load() << " (likely < 10)\n\n";
    }

    // If shutdown() is never called explicitly, the destructor calls it
    // (with FinishTasks) on the way out.
    setTitle("Implicit shutdown via destructor");

    {
        ThreadPool pool(2);
        pool.detach([] { std::cout << "ran before implicit shutdown\n"; });
    } // ~ThreadPool() drains and joins here.

    std::cout << "pool destroyed\n";
}

REGISTER_EXAMPLE_SUITE();
