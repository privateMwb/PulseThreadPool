// Submitting a batch of independent work.
//
// Demonstrates:
// - Submitting every item of a batch before reading any result
// - Collecting futures back into an ordered container
// - Summing results once every future is ready
// - Sizing the batch relative to threadCount()

#include <support/framework.h>

#include <numeric>
#include <vector>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(4);

    // Submit first, in order — each enqueue() returns immediately with
    // a Future, it doesn't wait for the task to run.
    setTitle("Submitting the batch");

    constexpr int itemCount = 10;
    std::vector<Detail::Future<int>> futures;
    futures.reserve(itemCount);

    for (int i = 0; i < itemCount; ++i) {
        futures.push_back(pool.enqueue([](int i) { return i * i; }, i));
    }

    std::cout << "submitted " << itemCount << " items across "
              << pool.threadCount() << " threads\n\n";

    // Collecting is a second, separate pass — order is preserved
    // because futures[i] always corresponds to item i.
    setTitle("Collecting results in order");

    std::vector<int> results;
    results.reserve(itemCount);

    for (auto& f : futures) {
        results.push_back(f.get());
    }

    for (int r : results) {
        std::cout << r << " ";
    }
    std::cout << "\n\n";

    // Once collected, ordinary algorithms work on the results as usual.
    setTitle("Reducing the results");

    int total = std::accumulate(results.begin(), results.end(), 0);
    std::cout << "sum of squares: " << total << "\n";
}

REGISTER_EXAMPLE_SUITE();
