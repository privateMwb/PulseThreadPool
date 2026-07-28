// Submitting work with enqueue().
//
// Demonstrates:
// - Getting a Future back from enqueue()
// - Passing arguments through to the callable
// - Collecting several futures before reading any of them
// - valid() before and after get()
// - An exception thrown by the task surfacing through get()

#include <support/framework.h>

#include <stdexcept>
#include <vector>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(4);

    // enqueue() forwards any extra arguments straight into the callable.
    setTitle("Result with arguments");

    Detail::Future<int> squared = pool.enqueue([](int n) { return n * n; }, 7);

    std::cout << "squared: " << squared.get() << "\n\n";

    // Futures don't have to be read in submission order — submit a
    // batch first, then collect them afterwards.
    setTitle("Collecting several futures");

    std::vector<Detail::Future<int>> futures;
    for (int i = 0; i < 5; ++i) {
        futures.push_back(pool.enqueue([](int i) { return i * i; }, i));
    }

    for (auto& f : futures) {
        std::cout << f.get() << " ";
    }
    std::cout << "\n\n";

    // valid() reports whether a Future still owns a shared state —
    // it goes false the moment get() consumes it.
    setTitle("valid() before and after get()");

    Detail::Future<int> once = pool.enqueue([] { return 42; });

    std::cout << "valid before get(): " << once.valid() << "\n";
    once.get();
    std::cout << "valid after get() : " << once.valid() << "\n\n";

    // A task's exception is stored, not swallowed — get() rethrows it
    // on the calling thread.
    setTitle("Exception propagation");

    Detail::Future<int> failing = pool.enqueue([]() -> int { throw std::runtime_error("boom"); });

    try {
        failing.get();
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";
    }

    pool.waitIdle();
}

REGISTER_EXAMPLE_SUITE();
