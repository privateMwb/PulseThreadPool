// Fire-and-forget work with detach().
//
// Demonstrates:
// - Submitting a callable with no future attached
// - Capturing state by value for a detached task
// - An exception thrown by a detached task being counted, not thrown
// - waitIdle() as the way to know detached work has actually finished

#include <support/framework.h>

#include <stdexcept>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(4);

    // detach() is cheaper than enqueue() when there's nothing to wait
    // on — no Future, no shared state allocation.
    setTitle("Basic detach");

    pool.detach([] { std::cout << "ran with no result\n"; });

    pool.waitIdle();
    std::cout << "\n";

    // Captured state travels with the task exactly like enqueue().
    setTitle("Capturing state");

    for (int i = 0; i < 3; ++i) {
        pool.detach([i] { std::cout << "detached task " << i << "\n"; });
    }

    pool.waitIdle();
    std::cout << "\n";

    // A detached task's exception can't be rethrown anywhere — there's
    // no Future to carry it — so it's counted instead.
    setTitle("Exceptions are counted, not thrown");

    std::size_t before = pool.exceptionCount();

    pool.detach([] { throw std::runtime_error("boom"); });
    pool.waitIdle();

    std::cout << "exceptionCount before: " << before << "\n";
    std::cout << "exceptionCount after : " << pool.exceptionCount() << "\n";
}

REGISTER_EXAMPLE_SUITE();
