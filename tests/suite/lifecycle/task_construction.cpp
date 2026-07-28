// PulseThreadPool Task construction test suite.
//
// Coverage:
// - A small callable is constructed and invoked correctly (inline SBO path)
// - A large callable is constructed and invoked correctly (heap path)

#include <support/framework.h>

using namespace ThreadPoolPro::Detail;

// Verifies a small callable is constructed and invoked correctly.
static void construct_small_callable_invokes() {
    bool ran = false;
    Task task([&ran]() { ran = true; });
    Task moved(std::move(task));
    moved();
    CHK(ran);
}

// Verifies a large callable is constructed and invoked correctly.
static void construct_large_callable_invokes() {
    std::byte padding[128]{};
    bool ran = false;

    Task task([&ran, padding]() {
        (void)padding;
        ran = true;
    });

    task();
    CHK(ran);
}

// Executes all Task construction test cases.
static void run_tests() {
    RUN(construct_small_callable_invokes);
    RUN(construct_large_callable_invokes);
}

REGISTER_TEST_SUITE();
