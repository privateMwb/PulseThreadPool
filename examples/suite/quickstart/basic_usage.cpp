// Basic ThreadPool usage.
//
// Demonstrates:
// - Constructing a pool with a fixed thread count
// - Submitting work with enqueue() and reading back a result
// - Submitting work with detach() when no result is needed
// - Waiting for all queued work to finish with waitIdle()
// - Shutting the pool down

#include <support/framework.h>

using namespace ThreadPoolPro;
using namespace ThreadPoolPro::Detail;

static void run_examples() {

    // A pool is constructed with a fixed number of worker threads.
    setTitle("Construction");

    ThreadPool pool(4);

    std::cout << "threadCount: " << pool.threadCount() << "\n";
    std::cout << "isPaused   : " << pool.isPaused() << "\n";
    std::cout << "isStopped  : " << pool.isStopped() << "\n\n";

    // enqueue() submits a callable and returns a Future for its result.
    setTitle("Enqueue with a result");

    Future<int> result = pool.enqueue([](int a, int b) { return a + b; }, 2, 3);

    std::cout << "result: " << result.get() << "\n\n";

    // detach() submits a callable whose result is discarded — cheaper
    // than enqueue() when there's nothing to wait on.
    setTitle("Detach without a result");

    pool.detach([] { std::cout << "detached task ran\n"; });

    pool.waitIdle();
    std::cout << "\n";

    // waitIdle() blocks until every submitted task has finished.
    setTitle("Waiting for idle");

    for (int i = 0; i < 5; ++i) {
        pool.detach([i] { std::cout << "task " << i << " ran\n"; });
    }

    pool.waitIdle();

    std::cout << "queuedTasks    : " << pool.queuedTasks() << "\n";
    std::cout << "activeTaskCount: " << pool.activeTaskCount() << "\n\n";

    // shutdown() stops the pool; the destructor would call it anyway,
    // but calling it explicitly here makes the point in the trace clear.
    setTitle("Shutdown");

    pool.shutdown();

    std::cout << "isStopped after shutdown: " << pool.isStopped() << "\n";
}

REGISTER_EXAMPLE_SUITE();
