// A closer look at ShutdownMode::DiscardTasks.
//
// Demonstrates:
// - Already-running tasks still finish under DiscardTasks
// - Only the still-queued tasks are abandoned
// - queuedTasks() dropping to zero the moment shutdown() returns
// - Only the first shutdown() call's mode taking effect

#include <support/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {

    // A single worker makes the running/queued split easy to see: one
    // task is in flight while the rest are still waiting behind it.
    setTitle("Running tasks finish, queued ones don't");

    {
        ThreadPool pool(1);
        std::atomic<int> started{0};
        std::atomic<int> finished{0};

        pool.detach([&started, &finished] {
            ++started;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            ++finished;
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        for (int i = 0; i < 5; ++i) {
            pool.detach([&started, &finished] {
                ++started;
                ++finished;
            });
        }

        std::cout << "queuedTasks before shutdown: " << pool.queuedTasks() << "\n";

        pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks);

        std::cout << "started : " << started.load() << " (the in-flight task)\n";
        std::cout << "finished: " << finished.load() << "\n";
        std::cout << "queuedTasks after shutdown : " << pool.queuedTasks() << "\n\n";
    }

    // Only the first shutdown() call's mode has any effect — a later
    // call, even with a different mode, is a no-op.
    setTitle("First shutdown() call wins");

    {
        ThreadPool pool(2);
        std::atomic<int> completed{0};

        for (int i = 0; i < 4; ++i) {
            pool.detach([&completed] { ++completed; });
        }

        pool.shutdown(ThreadPool::ShutdownMode::FinishTasks);
        pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks); // no-op, already stopped

        std::cout << "completed : " << completed.load() << " / 4\n";
        std::cout << "isStopped : " << pool.isStopped() << "\n";
    }
}

REGISTER_EXAMPLE_SUITE();
