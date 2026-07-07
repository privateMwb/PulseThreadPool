// Pipeline pattern example.
//
// Demonstrates:
// - chaining task stages, where each stage submits the next on completion
// - passing results from one stage to the next without blocking the caller
// - signaling final completion through a promise/future

#include <common/framework.h>

#include <future>
#include <memory>
#include <string>

using namespace ThreadPoolPro;

// Stage 1: produce raw data.
static int loadStage() {
    return 42;
}

// Stage 2: transform the data.
static int transformStage(int value) {
    return value * 2;
}

// Stage 3: format the final result.
static std::string finalizeStage(int value) {
    return "Result: " + std::to_string(value);
}

static void run_examples() {
    ThreadPool pool{4};

    setTitle("Chained Pipeline");

    auto donePromise = std::make_shared<std::promise<std::string>>();
    auto doneFuture   = donePromise->get_future();

    // Stage 1 submits stage 2 on completion, which submits stage 3 on
    // completion. None of these stages block waiting for the next one.
    pool.detach([&pool, donePromise] {
        int loaded = loadStage();
        std::cout << "Stage 1 (load)      -> " << loaded << "\n";

        pool.detach([&pool, donePromise, loaded] {
            int transformed = transformStage(loaded);
            std::cout << "Stage 2 (transform) -> " << transformed << "\n";

            pool.detach([donePromise, transformed] {
                std::string finalResult = finalizeStage(transformed);
                std::cout << "Stage 3 (finalize)  -> " << finalResult << "\n";

                donePromise->set_value(finalResult);
            });
        });
    });

    std::cout << "\n";

    // The caller only waits on the final stage's result, not any
    // intermediate stage.
    setTitle("Final Result");

    std::cout << doneFuture.get() << "\n";
}

REGISTER_EXAMPLE_SUITE();
