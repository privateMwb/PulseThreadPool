// Calling get() more than once on the same Future.
//
// Demonstrates:
// - get() consuming the Future, same contract as std::future::get()
// - A second get() on that same Future throwing std::logic_error
// - valid() as the check to make before a second get()
// - Moving a Future transferring ownership rather than copying it

#include <support/framework.h>

#include <stdexcept>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(2);

    // get() is a one-shot operation — it moves the result out of the
    // shared state and releases it.
    setTitle("Second get() throws");

    Detail::Future<int> f = pool.enqueue([] { return 5; });

    std::cout << "first get(): " << f.get() << "\n";

    try {
        f.get();
    } catch (const std::logic_error& e) {
        std::cout << "caught: " << e.what() << "\n\n";
    }

    // valid() catches this before it becomes an exception at all.
    setTitle("Checking valid() first");

    Detail::Future<int> g = pool.enqueue([] { return 9; });
    g.get();

    if (g.valid()) {
        std::cout << "unreachable: get() already consumed g\n";
    } else {
        std::cout << "valid() is false, skipping the second get()\n\n";
    }

    // Future can't be copied, only moved — moving transfers the one
    // owning reference rather than duplicating a get()-able handle.
    setTitle("Moving doesn't duplicate ownership");

    Detail::Future<int> original = pool.enqueue([] { return 7; });
    Detail::Future<int> moved = std::move(original);

    std::cout << "original.valid(): " << original.valid() << "\n";
    std::cout << "moved.valid()   : " << moved.valid() << "\n";
    std::cout << "moved.get()     : " << moved.get() << "\n";
}

REGISTER_EXAMPLE_SUITE();
