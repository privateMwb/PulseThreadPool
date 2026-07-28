// PulseThreadPool concurrent-submission test suite.
//
// Coverage:
// - Many external threads calling detach() on the same pool
//   simultaneously all have their tasks executed exactly once

#include <support/framework.h>

using namespace ThreadPoolPro;

// Verifies many concurrent external producers submitting into the pool
// all have their tasks executed exactly once.
static void concurrent_submit_runs_every_task() {
    ThreadPool pool(4);
    constexpr int producerCount = 8;
    constexpr int perProducer = 200;
    std::atomic<int> completed{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < producerCount; ++p) {
        producers.emplace_back([&pool, &completed]() {
            for (int i = 0; i < perProducer; ++i)
                pool.detach([&completed]() { completed.fetch_add(1); });
        });
    }

    for (auto& producer : producers)
        producer.join();

    pool.waitIdle();
    CHK(completed.load() == producerCount * perProducer);
}

// Executes all concurrent-submission test cases.
static void run_tests() {
    RUN(concurrent_submit_runs_every_task);
}

REGISTER_TEST_SUITE();
