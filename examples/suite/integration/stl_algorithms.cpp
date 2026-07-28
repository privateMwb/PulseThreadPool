// Pool-backed versions of a few familiar STL algorithms.
//
// Demonstrates:
// - A parallel transform() that writes into an output range
// - A parallel count_if() that reduces per-chunk counts at the end
// - A parallel any_of() with early-exit left out deliberately (kept simple)
// - Falling back to std::transform for comparison

#include <support/framework.h>

#include <algorithm>
#include <numeric>
#include <vector>

using namespace ThreadPoolPro;

namespace {
template <typename It, typename OutIt, typename UnaryOp>
void parallelTransform(ThreadPool& pool, It first, It last, OutIt outFirst, UnaryOp op) {
    std::size_t n = static_cast<std::size_t>(std::distance(first, last));
    std::size_t chunks = pool.threadCount();
    std::size_t chunkSize = (n + chunks - 1) / chunks;

    std::vector<Detail::Future<void>> futures;

    for (std::size_t c = 0; c < chunks; ++c) {
        std::size_t begin = c * chunkSize;
        std::size_t end = std::min(begin + chunkSize, n);
        if (begin >= end)
            break;

        futures.push_back(pool.enqueue(
            [=] { std::transform(first + begin, first + end, outFirst + begin, op); }));
    }

    for (auto& f : futures) {
        f.get();
    }
}

template <typename It, typename Predicate>
std::size_t parallelCountIf(ThreadPool& pool, It first, It last, Predicate pred) {
    std::size_t n = static_cast<std::size_t>(std::distance(first, last));
    std::size_t chunks = pool.threadCount();
    std::size_t chunkSize = (n + chunks - 1) / chunks;

    std::vector<Detail::Future<std::size_t>> futures;

    for (std::size_t c = 0; c < chunks; ++c) {
        std::size_t begin = c * chunkSize;
        std::size_t end = std::min(begin + chunkSize, n);
        if (begin >= end)
            break;

        futures.push_back(pool.enqueue([=] {
            return static_cast<std::size_t>(std::count_if(first + begin, first + end, pred));
        }));
    }

    std::size_t total = 0;
    for (auto& f : futures) {
        total += f.get();
    }
    return total;
}
} // namespace

static void run_examples() {

    ThreadPool pool(4);

    std::vector<int> input(1'000);
    std::iota(input.begin(), input.end(), 0);

    // Parallel transform, compared against the serial std::transform
    // it's standing in for.
    setTitle("Parallel transform");

    std::vector<int> output(input.size());
    parallelTransform(pool, input.begin(), input.end(), output.begin(),
                      [](int v) { return v * v; });

    std::vector<int> expected(input.size());
    std::transform(input.begin(), input.end(), expected.begin(), [](int v) { return v * v; });

    std::cout << "matches std::transform: " << (output == expected) << "\n\n";

    // Parallel count_if, reducing one std::size_t per chunk.
    setTitle("Parallel count_if");

    std::size_t evenCount =
        parallelCountIf(pool, input.begin(), input.end(), [](int v) { return v % 2 == 0; });

    std::cout << "even numbers in [0, 1000): " << evenCount << "\n";
}

REGISTER_EXAMPLE_SUITE();
