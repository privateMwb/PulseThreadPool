// Submitting after the pool has begun shutting down.
//
// Demonstrates:
// - enqueue() throwing std::runtime_error once shutdown() has run
// - detach() throwing the same way
// - isStopped() as the check to make before submitting more work
// - This being true even while shutdown()'s FinishTasks queue is still draining

#include <support/framework.h>

#include <stdexcept>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(2);

    // Once shutdown() has been called, the pool no longer accepts new
    // work — even mid-drain, with FinishTasks tasks still running.
    setTitle("enqueue() after shutdown()");

    pool.shutdown();

    try {
        Detail::Future<int> f = pool.enqueue([] { return 1; });
        f.get();
    } catch (const std::runtime_error& e) {
        std::cout << "caught: " << e.what() << "\n\n";
    }

    // detach() rejects new work the same way enqueue() does.
    setTitle("detach() after shutdown()");

    try {
        pool.detach([] { std::cout << "never runs\n"; });
    } catch (const std::runtime_error& e) {
        std::cout << "caught: " << e.what() << "\n\n";
    }

    // isStopped() is the check to make first, rather than relying on
    // catching the exception as normal control flow.
    setTitle("Checking isStopped() first");

    ThreadPool other(2);
    other.shutdown();

    if (other.isStopped()) {
        std::cout << "pool is stopped, skipping submission\n";
    } else {
        other.detach([] { std::cout << "would have run\n"; });
    }
}

REGISTER_EXAMPLE_SUITE();
