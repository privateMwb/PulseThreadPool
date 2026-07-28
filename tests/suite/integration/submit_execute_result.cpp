// PulseThreadPool end-to-end enqueue() result test suite.
//
// Coverage:
// - A task submitted via enqueue() is picked up by a worker, executed,
//   and its result observed through the returned Future
// - Many concurrently enqueued tasks each deliver their own correct,
//   independent result

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies a task submitted via enqueue() is picked up by a worker,
// executed, and its result observed through the returned Future.
static void enqueue_result_flows_end_to_end() {
    ThreadPool pool(4);
    auto future = pool.enqueue([](int a, int b) { return a * b; }, 6, 7);
    CHK(future.get() == 42);
}

// Verifies multiple concurrently enqueued tasks each deliver their own
// correct, independent result.
static void multiple_enqueued_results_are_independent() {
    ThreadPool pool(4);
    std::vector<Detail::Future<int>> futures;

    for (int i = 0; i < 50; ++i)
        futures.push_back(pool.enqueue([](int value) { return value * value; }, i));

    for (int i = 0; i < 50; ++i)
        CHK(futures[static_cast<std::size_t>(i)].get() == i * i);
}

// Executes all end-to-end enqueue() result test cases.
static void run_tests() {
    RUN(enqueue_result_flows_end_to_end);
    RUN(multiple_enqueued_results_are_independent);
}

REGISTER_TEST_SUITE();
