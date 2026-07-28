// Calling get() on a Future that owns no state.
//
// Demonstrates:
// - A default-constructed Future throwing std::logic_error on get()
// - A moved-from Future being just as empty as a default-constructed one
// - valid() as the guard to use before trusting a Future came from enqueue()
// - This is distinct from double_get.cpp: here the Future was never valid at all

#include <support/framework.h>

#include <stdexcept>
#include <utility>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(2);

    // A default-constructed Future has no shared state to wait on.
    setTitle("Default-constructed Future");

    Detail::Future<int> empty;

    std::cout << "valid(): " << empty.valid() << "\n";

    try {
        empty.get();
    } catch (const std::logic_error& e) {
        std::cout << "caught: " << e.what() << "\n\n";
    }

    // A moved-from Future is left in the same empty state — the move
    // transferred ownership away, it didn't leave a usable copy behind.
    setTitle("Moved-from Future");

    Detail::Future<int> source = pool.enqueue([] { return 3; });
    Detail::Future<int> destination = std::move(source);

    std::cout << "source.valid()     : " << source.valid() << "\n";
    std::cout << "destination.valid(): " << destination.valid() << "\n";

    try {
        source.get();
    } catch (const std::logic_error& e) {
        std::cout << "caught: " << e.what() << "\n\n";
    }

    destination.get(); // fine — destination still owns the real state

    // Guarding with valid() avoids the exception entirely when a
    // Future's provenance isn't certain (e.g. it passed through
    // several functions before reaching this point).
    setTitle("Guarding with valid()");

    Detail::Future<int> maybeEmpty;

    if (maybeEmpty.valid()) {
        std::cout << maybeEmpty.get() << "\n";
    } else {
        std::cout << "skipped get() on an empty Future\n";
    }
}

REGISTER_EXAMPLE_SUITE();
