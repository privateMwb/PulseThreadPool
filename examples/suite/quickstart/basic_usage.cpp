// Basic ThreadPool example.
//
// Demonstrates:
// - construction with a fixed thread count
// - submitting a task with enqueue() and reading its result
// - submitting a fire-and-forget task with detach()
// - checking runtime state while work is outstanding
// - shutting the pool down

#include <common/framework.h>

#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {
    // Construct a pool with a fixed number of worker threads.
    setTitle("Construction");

    ThreadPool pool{4};

    std::cout << "Thread count : " << pool.threadCount() << "\n";
    std::cout << "Is stopped   : " << pool.isStopped()   << "\n\n";

    // Submit a task and read its result through the returned future.
    setTitle("Enqueue");

    auto future = pool.enqueue([](int a, int b) { return a + b; }, 2, 3);
    std::cout << "Result : " << future.get() << "\n\n";

    // Submit a fire-and-forget task with detach().
    setTitle("Detach");

    pool.detach([] {
        std::cout << "Running on a worker thread\n";
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "\n";

    // Inspect runtime state while work is outstanding.
    setTitle("Runtime State");

    for (int i = 0; i < 10; ++i)
        pool.detach([] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); });

    std::cout << "Active tasks : " << pool.activeTaskCount() << "\n";
    std::cout << "Queued tasks : " << pool.queuedTasks()     << "\n";
    std::cout << "Idle threads : " << pool.idleThreadCount() << "\n\n";

    // Shut the pool down, draining any remaining work.
    setTitle("Shutdown");

    pool.shutdown(ThreadPool::ShutdownMode::FinishTasks);
    std::cout << "Is stopped : " << pool.isStopped() << "\n";
}

REGISTER_EXAMPLE_SUITE();
