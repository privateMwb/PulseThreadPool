// Futures integration example.
//
// Demonstrates:
// - collecting multiple futures and gathering their results
// - combining results from independent tasks
// - propagating an exception through a future

#include <common/framework.h>

#include <future>
#include <numeric>
#include <stdexcept>
#include <vector>

using namespace ThreadPoolPro;

static void run_examples() {
    ThreadPool pool{4};

    // Submit several independent tasks and collect their futures.
    setTitle("Gathering Results");

    std::vector<std::future<int>> futures;
    for (int i = 1; i <= 5; ++i)
        futures.push_back(pool.enqueue([i] { return i * i; }));

    std::vector<int> results;
    for (auto& f : futures)
        results.push_back(f.get());

    std::cout << "Squares : ";
    for (int r : results)
        std::cout << r << ' ';
    std::cout << "\n\n";

    // Combine results from two independent tasks.
    setTitle("Combining Results");

    auto sumFuture = pool.enqueue([&results] {
        return std::accumulate(results.begin(), results.end(), 0);
    });
    auto countFuture = pool.enqueue([&results] {
        return results.size();
    });

    std::cout << "Sum   : " << sumFuture.get()   << "\n";
    std::cout << "Count : " << countFuture.get() << "\n\n";

    // A future propagates any exception thrown inside its task.
    setTitle("Exception Propagation");

    auto failingFuture = pool.enqueue([] () -> int {
        throw std::runtime_error("division by zero");
    });

    try {
        failingFuture.get();
    } catch (const std::exception& e) {
        std::cout << "Caught : " << e.what() << "\n";
    }
}

REGISTER_EXAMPLE_SUITE();
