// PulseThreadPool Task invocation test suite.
//
// Coverage:
// - operator() invokes the wrapped callable
// - operator() on an empty Task throws
// - operator bool() reflects whether a callable is held

#include <support/framework.h>

using namespace ThreadPoolPro::Detail;

// Verifies operator() invokes the wrapped callable.
static void invoke_runs_callable() {
    bool ran = false;
    Task task([&ran]() { ran = true; });
    Task moved(std::move(task));
    moved();
    CHK(ran);
}

// Verifies operator() on an empty Task throws.
static void invoke_on_empty_throws() {
    Task task;
    CHK_THROWS(task(), std::logic_error);
}

// Verifies operator bool() reports whether a callable is held.
static void bool_conversion_reflects_state() {
    Task empty;
    CHK(!static_cast<bool>(empty));

    Task holding([]() {});
    CHK(static_cast<bool>(holding));

    Task moved(std::move(holding));
    moved();
}

// Executes all Task invocation test cases.
static void run_tests() {
    RUN(invoke_runs_callable);
    RUN(invoke_on_empty_throws);
    RUN(bool_conversion_reflects_state);
}

REGISTER_TEST_SUITE();
