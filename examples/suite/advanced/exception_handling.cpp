// Exception handling across enqueue() and detach().
//
// Demonstrates:
// - An enqueue()'d exception rethrown on get(), preserving its type
// - Catching a specific exception type versus std::exception
// - A detach()'d exception never propagating, only counted
// - Other tasks continuing normally after one throws

#include <support/framework.h>

#include <stdexcept>

using namespace ThreadPoolPro;

namespace {
struct CustomError : std::runtime_error {
    explicit CustomError(const std::string& msg) : std::runtime_error(msg) {}
};
} // namespace

static void run_examples() {

    ThreadPool pool(4);

    // get() rethrows exactly what the task threw — the exception type
    // survives the trip through the Future.
    setTitle("Exact exception type preserved");

    Detail::Future<int> f = pool.enqueue([]() -> int { throw CustomError("custom failure"); });

    try {
        f.get();
    } catch (const CustomError& e) {
        std::cout << "caught CustomError: " << e.what() << "\n\n";
    }

    // A generic std::exception& catch still works, since CustomError
    // derives from std::runtime_error.
    setTitle("Catching by base class");

    Detail::Future<int> g = pool.enqueue([]() -> int { throw CustomError("still custom"); });

    try {
        g.get();
    } catch (const std::exception& e) {
        std::cout << "caught std::exception: " << e.what() << "\n\n";
    }

    // detach() has nowhere to rethrow to, so the exception is
    // swallowed and only exceptionCount() moves.
    setTitle("detach() swallows, doesn't propagate");

    std::size_t before = pool.exceptionCount();
    pool.detach([]() { throw CustomError("never seen"); });
    pool.waitIdle();

    std::cout << "exceptionCount before: " << before << "\n";
    std::cout << "exceptionCount after : " << pool.exceptionCount() << "\n\n";

    // One task throwing doesn't stop the pool or corrupt other tasks.
    setTitle("Pool keeps running after a throw");

    Detail::Future<int> ok = pool.enqueue([] { return 100; });
    std::cout << "unrelated task result: " << ok.get() << "\n";
}

REGISTER_EXAMPLE_SUITE();
