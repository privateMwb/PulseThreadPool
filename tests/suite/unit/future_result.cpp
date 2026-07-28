// PulseThreadPool Future/ResultState test suite.
//
// Coverage:
// - get() returns a value published via setValue()
// - get() rethrows an exception published via setException()
// - get() on an empty Future throws
// - the void specialization publishes completion without a value

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

// Executes all Future/ResultState test cases.
static void run_tests() {
    RUN(get_returns_published_value);
    RUN(get_rethrows_published_exception);
    RUN(get_on_empty_future_throws);
    RUN(void_specialization_completes);
}

REGISTER_TEST_SUITE();
