// Detail::Task test suite.
//
// Coverage:
// - a default-constructed Task is empty (falsy)
// - a constructed Task holds and invokes its callable
// - both small (inline/SBO) and large (heap-fallback) callables execute correctly
// - move construction transfers the callable and empties the source
// - move assignment transfers the callable and destroys the previous target
// - the destructor releases captured resources
// - an exception thrown by the callable propagates out of operator()

#include <common/framework.h>

#include <ThreadPoolPro/Detail/Task.h>

#include <array>
#include <memory>
#include <stdexcept>

using namespace ThreadPoolPro::Detail;

// Verifies a default-constructed Task is empty.
static void default_constructed_task_is_empty() {
    Task t;

    CHK(static_cast<bool>(t) == false);
}

// Verifies a constructed Task holds a truthy state and invokes its callable.
static void task_executes_stored_callable() {
    bool ran = false;
    Task t([&ran] { ran = true; });

    CHK(static_cast<bool>(t) == true);
    t();
    CHK(ran == true);
}

// Verifies a small callable (fits inline storage) executes correctly.
static void small_callable_executes_correctly() {
    int value = 0;
    Task t([&value] { value = 42; });

    t();
    CHK(value == 42);
}

// Verifies a large callable (falls back to heap storage) executes correctly.
static void large_callable_executes_correctly() {
    std::array<int, 32> payload{};
    payload[0] = 1;
    payload[31] = 99;
    int result = 0;

    Task t([payload, &result] {
        result = payload[0] + payload[31];
    });

    t();
    CHK(result == 100);
}

// Verifies move construction transfers the callable and empties the source.
static void move_construction_transfers_callable() {
    bool ran = false;
    Task a([&ran] { ran = true; });
    Task b(std::move(a));

    CHK(static_cast<bool>(a) == false);
    CHK(static_cast<bool>(b) == true);

    b();
    CHK(ran == true);
}

// Verifies move assignment transfers the callable and destroys the previous target.
static void move_assignment_transfers_and_replaces_target() {
    auto tracker = std::make_shared<int>(0);

    bool aRan = false;
    Task a([&aRan] { aRan = true; });
    Task b([tracker] { (*tracker)++; });

    b = std::move(a);

    CHK(tracker.use_count() == 1); // b's original callable was destroyed
    CHK(static_cast<bool>(a) == false);

    b();
    CHK(aRan == true);
    CHK(*tracker == 0);
}

// Verifies the destructor releases captured resources.
static void destructor_releases_captured_resources() {
    auto tracker = std::make_shared<int>(0);
    CHK(tracker.use_count() == 1);

    {
        Task t([tracker] { (void)tracker; });
        CHK(tracker.use_count() == 2);
    }

    CHK(tracker.use_count() == 1);
}

// Verifies an exception thrown by the callable propagates out of operator().
static void exception_propagates_through_invocation() {
    Task t([] { throw std::runtime_error("boom"); });

    bool caught = false;
    try {
        t();
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()) == "boom";
    }

    CHK(caught == true);
}

// Executes all Task test cases.
static void run_tests() {
    RUN(default_constructed_task_is_empty);
    RUN(task_executes_stored_callable);
    RUN(small_callable_executes_correctly);
    RUN(large_callable_executes_correctly);
    RUN(move_construction_transfers_callable);
    RUN(move_assignment_transfers_and_replaces_target);
    RUN(destructor_releases_captured_resources);
    RUN(exception_propagates_through_invocation);
}

REGISTER_TEST_SUITE();
