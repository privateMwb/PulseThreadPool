// Dangling capture pitfall example.
//
// Demonstrates:
// - why capturing a local variable BY REFERENCE in a detached task is
//   dangerous when the enclosing scope may exit before the task runs
// - the safe fix: capture by value (or share ownership for larger objects)
//
// The broken version below is shown only as commented-out code; it is
// deliberately not compiled or run, since triggering it is undefined
// behavior with unpredictable (and sometimes silent) failure.

#include <common/framework.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace ThreadPoolPro;

// --- THE PITFALL (do not do this) -----------------------------------------
//
// void submitBroken(ThreadPool& pool) {
//     int requestId = 42;
//
//     // requestId is a stack local. This function can return, destroying
//     // requestId, before a worker thread ever runs the task below.
//     pool.detach([&requestId] {
//         // By the time this runs, requestId may already be destroyed.
//         // Reading it here is undefined behavior: a garbage value, a
//         // crash, or worst of all, output that happens to look correct
//         // until timing or optimization levels change.
//         std::cout << "Request: " << requestId << "\n";
//     });
// }
//
// ---------------------------------------------------------------------------

// The fix: capture by value so the task owns its own copy, independent
// of the caller's stack frame.
static void submitFixed(ThreadPool& pool, int requestId) {
    pool.detach([requestId] {
        std::cout << "Request: " << requestId << "\n";
    });
}

static void run_examples() {
    setTitle("The Fix: Capture By Value");

    ThreadPool pool{2};

    submitFixed(pool, 42);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "\n";

    setTitle("Extending Lifetime For Larger Objects");

    // For objects too expensive to copy per task, share ownership
    // instead of capturing a raw reference, so the object outlives
    // the task no matter when it actually runs.
    auto sharedData = std::make_shared<std::string>("payload");

    pool.detach([sharedData] {
        std::cout << "Data: " << *sharedData << "\n";
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

REGISTER_EXAMPLE_SUITE();
