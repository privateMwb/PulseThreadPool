// Exception monitoring example.
//
// Demonstrates:
// - enqueue() surfaces a task's exception through its future
// - detach() exceptions are only visible in aggregate via exceptionCount()
// - combining both to monitor pool health alongside per-task error handling

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {
    ThreadPool pool{4};

    // enqueue(): the exception is available on the future, per task.
    setTitle("Per-Task Errors (enqueue)");

    auto future = pool.enqueue([] () -> int {
        throw std::runtime_error("invalid input");
    });

    try {
        future.get();
    } catch (const std::exception& e) {
        std::cout << "Caught directly : " << e.what() << "\n";
    }

    std::cout << "exceptionCount() : " << pool.exceptionCount()
              << " (enqueue() exceptions don't count here)\n\n";

    // detach(): the exception never reaches the caller. It's only
    // visible as an increment to exceptionCount().
    setTitle("Aggregate Health (detach)");

    constexpr int taskCount = 20;
    for (int i = 0; i < taskCount; ++i) {
        pool.detach([i] {
            if (i % 5 == 0)
                throw std::runtime_error("simulated failure");
        });
    }

    while (pool.queuedTasks() > 0 || pool.activeTaskCount() > 0)
        std::this_thread::yield();

    std::cout << "Submitted        : " << taskCount << "\n";
    std::cout << "exceptionCount() : " << pool.exceptionCount()
              << " (this is your only signal for detach() failures)\n\n";

    // A monitoring loop would poll exceptionCount() periodically and
    // alert if it grows unexpectedly, since no individual failure is
    // otherwise reported for fire-and-forget work.
    setTitle("Monitoring Pattern");

    std::size_t lastCount = pool.exceptionCount();
    std::cout << "Baseline exceptionCount() : " << lastCount << "\n";

    for (int i = 0; i < 5; ++i)
        pool.detach([] { throw std::runtime_error("more failures"); });

    while (pool.queuedTasks() > 0 || pool.activeTaskCount() > 0)
        std::this_thread::yield();

    std::size_t newCount = pool.exceptionCount();
    std::cout << "New exceptionCount()      : " << newCount << "\n";
    std::cout << "Delta                     : " << (newCount - lastCount) << "\n";
}

REGISTER_EXAMPLE_SUITE();
