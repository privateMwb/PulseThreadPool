// Blocking future inside pool pitfall example.
//
// Demonstrates:
// - a task that submits a subtask and blocks on its future, on a pool
//   too small to make progress -> deadlock
// - detecting this safely with wait_for() instead of an unbounded get()
// - the fix: size the pool with headroom, or avoid blocking inside a task

#include <common/framework.h>

#include <chrono>
#include <future>

using namespace ThreadPoolPro;

static void run_examples() {
    // A single-thread pool has no spare worker to run a task submitted
    // from inside another task. If that outer task blocks waiting for
    // the subtask's result, it can never make progress: it's occupying
    // the only worker that could have run the subtask.
    setTitle("The Pitfall: Undersized Pool");

    ThreadPool tinyPool{1};

    auto outerFuture = tinyPool.enqueue([&tinyPool] {
        auto innerFuture = tinyPool.enqueue([] { return 1; });

        // DANGER: innerFuture.get() here would block forever on this
        // single-thread pool, since this very call is occupying the
        // only worker that could run the inner task. wait_for() with a
        // timeout is used instead so this demo can detect the stall
        // safely rather than hanging.
        auto status = innerFuture.wait_for(std::chrono::milliseconds(200));
        return status == std::future_status::ready;
    });

    bool madeProgress = outerFuture.get();
    std::cout << "Inner task completed in time : " << madeProgress
              << " (false means it stalled, as expected)\n\n";

    // The fix: give the pool enough headroom for the nesting depth you
    // actually use, so a blocked worker always leaves another one free.
    setTitle("The Fix: Headroom In The Pool");

    ThreadPool roomyPool{4};

    auto fixedOuter = roomyPool.enqueue([&roomyPool] {
        auto fixedInner = roomyPool.enqueue([] { return 1; });
        return fixedInner.get(); // safe: another worker is free to run it
    });

    std::cout << "Result : " << fixedOuter.get() << "\n";
}

REGISTER_EXAMPLE_SUITE();
