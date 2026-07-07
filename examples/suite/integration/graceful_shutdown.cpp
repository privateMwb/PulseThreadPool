// Graceful shutdown example.
//
// Demonstrates:
// - shutdown(FinishTasks): drains all queued work before returning
// - shutdown(DiscardTasks): abandons work still queued
// - the destructor performing a FinishTasks shutdown by default
// - shutdown() being safe to call more than once

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {
    // FinishTasks waits for every queued task to complete.
    setTitle("Shutdown (FinishTasks)");

    {
        ThreadPool pool{2};
        std::atomic<int> completed{0};

        for (int i = 0; i < 50; ++i)
            pool.detach([&completed] { completed.fetch_add(1, std::memory_order_relaxed); });

        pool.shutdown(ThreadPool::ShutdownMode::FinishTasks);

        std::cout << "Completed : " << completed.load() << " / 50\n\n";
    }

    // DiscardTasks abandons whatever is still queued at the moment shutdown is called.
    setTitle("Shutdown (DiscardTasks)");

    {
        ThreadPool pool{1};
        std::atomic<int> completed{0};

        // Occupy the only worker for a moment so the rest of the batch
        // piles up in the queue instead of running immediately.
        pool.detach([&completed] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            completed.fetch_add(1, std::memory_order_relaxed);
        });

        for (int i = 0; i < 50; ++i)
            pool.detach([&completed] { completed.fetch_add(1, std::memory_order_relaxed); });

        pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks);

        std::cout << "Completed : " << completed.load() << " / 51 (the rest were discarded)\n\n";
    }

    // The destructor performs a FinishTasks shutdown by default.
    setTitle("Destructor Drains By Default");

    std::atomic<int> completed{0};
    {
        ThreadPool pool{2};
        for (int i = 0; i < 20; ++i)
            pool.detach([&completed] { completed.fetch_add(1, std::memory_order_relaxed); });
    }
    std::cout << "Completed : " << completed.load() << " / 20\n\n";

    // Calling shutdown() more than once is safe.
    setTitle("Idempotent Shutdown");

    ThreadPool pool{2};
    pool.shutdown();
    pool.shutdown();

    std::cout << "Is stopped : " << pool.isStopped() << "\n";
}

REGISTER_EXAMPLE_SUITE();
