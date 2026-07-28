// PulseThreadPool Buffer test suite.
//
// Coverage:
// - at() returns a writable reference to the requested slot
// - at() wraps a logical index into range via the capacity mask
// - grow() doubles capacity
// - grow() copies the live [top, bottom) pointer range into the new buffer
// - grow() leaves the original buffer untouched

#include <support/framework.h>

using namespace ThreadPoolPro::Detail;

// Verifies at() returns a writable reference to the requested slot.
static void at_returns_writable_slot() {
    Buffer buffer(8);
    Task* task = reinterpret_cast<Task*>(0x1);
    buffer.at(3) = task;
    CHK(buffer.at(3) == task);
}

// Verifies at() wraps a logical index into range via the capacity mask.
static void at_wraps_via_mask() {
    Buffer buffer(8);
    Task* task = reinterpret_cast<Task*>(0x2);
    buffer.at(2) = task;
    CHK(buffer.at(10) == task); // 10 & 7 == 2
}

// Verifies grow() doubles capacity.
static void grow_doubles_capacity() {
    Buffer buffer(4);
    std::unique_ptr<Buffer> grown(buffer.grow(0, 0));
    CHK(grown->capacity_ == 8);
}

// Verifies grow() copies the live [top, bottom) pointer range into the new buffer.
static void grow_copies_live_range() {
    Buffer buffer(4);
    Task* a = reinterpret_cast<Task*>(0x1);
    Task* b = reinterpret_cast<Task*>(0x2);
    buffer.at(0) = a;
    buffer.at(1) = b;

    std::unique_ptr<Buffer> grown(buffer.grow(2, 0));
    CHK(grown->at(0) == a);
    CHK(grown->at(1) == b);
}

// Verifies grow() leaves the original buffer untouched.
static void grow_does_not_mutate_original() {
    Buffer buffer(4);
    Task* a = reinterpret_cast<Task*>(0x3);
    buffer.at(0) = a;

    std::unique_ptr<Buffer> grown(buffer.grow(1, 0));
    CHK(buffer.at(0) == a);
    CHK(buffer.capacity_ == 4);
}

// Executes all Buffer test cases.
static void run_tests() {
    RUN(at_returns_writable_slot);
    RUN(at_wraps_via_mask);
    RUN(grow_doubles_capacity);
    RUN(grow_copies_live_range);
    RUN(grow_does_not_mutate_original);
}

REGISTER_TEST_SUITE();
