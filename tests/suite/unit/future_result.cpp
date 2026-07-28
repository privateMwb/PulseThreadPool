// PulseThreadPool Future/ResultState test suite.
//
// Coverage:
// - get() returns a value published via setValue()
// - get() rethrows an exception published via setException()
// - get() on an empty Future throws
// - the void specialization publishes completion without a value
// - get() actually blocks (spin -> yield -> park) until another thread
//   publishes, for both the value-carrying and void specializations
// - setException() on a non-default, non-void T (std::thread::id, the
//   type ThreadPool::enqueue() actually returns it as elsewhere in the
//   suite) is exercised at least once

#include <support/framework.h>

using namespace ThreadPoolPro::Detail;

// Verifies get() returns a value published via setValue().
static void get_returns_published_value() {
    auto* state = new ResultState<int>();
    state->setValue(42);
    state->release(); // simulates the task closure's share being released
    Future<int> future(state);
    CHK(future.get() == 42);
}

// Verifies get() rethrows an exception published via setException().
static void get_rethrows_published_exception() {
    auto* state = new ResultState<int>();
    state->setException(std::make_exception_ptr(std::runtime_error("boom")));
    state->release();
    Future<int> future(state);
    CHK_THROWS(future.get(), std::runtime_error);
}

// Verifies get() on a default-constructed (empty) Future throws.
static void get_on_empty_future_throws() {
    Future<int> future;
    CHK_THROWS(future.get(), std::logic_error);
}

// Verifies the void specialization publishes completion without a value.
static void void_specialization_completes() {
    auto* state = new ResultState<void>();
    state->setValue();
    state->release();
    Future<void> future(state);
    future.get(); // must not throw
    CHK(!future.valid());
}

// Verifies get() actually blocks until another thread publishes the
// value, rather than only being tested with the result already
// available. A short producer-side delay ensures wait() runs long
// enough to fall through its spin tier into the yield and park tiers,
// rather than always resolving on the very first ready_ check.
static void get_blocks_until_published_from_another_thread() {
    auto* state = new ResultState<int>();

    std::thread producer([state]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        state->setValue(99);
        state->release();
    });

    Future<int> future(state);
    CHK(future.get() == 99);

    producer.join();
}

// Same as above, for the void specialization's wait() path.
static void void_get_blocks_until_published_from_another_thread() {
    auto* state = new ResultState<void>();

    std::thread producer([state]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        state->setValue();
        state->release();
    });

    Future<void> future(state);
    future.get(); // must not throw, must actually wait for the producer

    producer.join();
}

// Verifies setException()/get() works for a non-default, non-void T
// too (std::thread::id, the type actually returned by the pool-reuse
// enqueue() tests elsewhere in the suite) — those tests only ever
// exercise the success path, so this is the only place that publishes
// an exception through a ResultState<std::thread::id>.
static void get_rethrows_published_exception_thread_id() {
    auto* state = new ResultState<std::thread::id>();
    state->setException(std::make_exception_ptr(std::runtime_error("boom")));
    state->release();
    Future<std::thread::id> future(state);
    CHK_THROWS(future.get(), std::runtime_error);
}

// Executes all Future/ResultState test cases.
static void run_tests() {
    RUN(get_returns_published_value);
    RUN(get_rethrows_published_exception);
    RUN(get_on_empty_future_throws);
    RUN(void_specialization_completes);
    RUN(get_blocks_until_published_from_another_thread);
    RUN(void_get_blocks_until_published_from_another_thread);
    RUN(get_rethrows_published_exception_thread_id);
}

REGISTER_TEST_SUITE();
