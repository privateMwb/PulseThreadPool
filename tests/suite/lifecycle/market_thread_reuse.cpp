// PulseThreadPool worker-thread reuse across ThreadPool lifetimes.
//
// Coverage:
// - A worker OS thread returned to the market by one ThreadPool's
//   destructor is reused by the very next ThreadPool that leases one,
//   instead of a fresh OS thread being spawned.

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies a destroyed pool's worker thread is handed straight to the
// next pool that leases one, rather than spawning a new OS thread.
static void worker_thread_is_reused_across_pools() {
    std::thread::id firstThreadId;

    {
        ThreadPool pool(1);
        auto future = pool.enqueue([]() { return std::this_thread::get_id(); });
        firstThreadId = future.get();
    }

    std::thread::id secondThreadId;
    {
        ThreadPool pool(1);
        auto future = pool.enqueue([]() { return std::this_thread::get_id(); });
        secondThreadId = future.get();
    }

    CHK(firstThreadId == secondThreadId);
}

// Executes all worker-thread reuse test cases.
static void run_tests() {
    RUN(worker_thread_is_reused_across_pools);
}

REGISTER_TEST_SUITE();
