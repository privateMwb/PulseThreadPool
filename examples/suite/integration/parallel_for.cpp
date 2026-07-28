// A parallel-for built on top of the pool.
//
// Demonstrates:
// - Splitting a range into contiguous chunks, one per worker thread
// - Each chunk running as its own enqueue()'d task
// - Waiting on every chunk's future before reading the result
// - Sizing chunk count from threadCount() rather than hardcoding it

#include <support/framework.h>

#include <numeric>
#include <vector>

using namespace ThreadPoolPro;

namespace {
// Splits [0, n) into `chunks` contiguous, roughly-equal ranges and runs
// `body(begin, end)` for each one on the pool, returning once every
// chunk has finished.
template <typename Body>
void parallelFor(ThreadPool& pool, std::size_t n, std::size_t chunks, Body body) {
    std::vector<Detail::Future<void>> futures;
    futures.reserve(chunks);

    std::size_t chunkSize = (n + chunks - 1) / chunks;

    for (std::size_t c = 0; c < chunks; ++c) {
        std::size_t begin = c * chunkSize;
        std::size_t end = std::min(begin + chunkSize, n);
        if (begin >= end)
            break;

        futures.push_back(pool.enqueue([begin, end, &body] { body(begin, end); }));
    }

    for (auto& f : futures) {
        f.get();
    }
}
} // namespace

static void run_examples() {

    ThreadPool pool(4);

    // Fill a vector in parallel, one contiguous chunk per worker.
    setTitle("Parallel fill");

    constexpr std::size_t n = 1'000;
    std::vector<int> data(n);

    parallelFor(pool, n, pool.threadCount(), [&data](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            data[i] = static_cast<int>(i);
        }
    });

    std::cout << "data[0]   : " << data[0] << "\n";
    std::cout << "data[999] : " << data[999] << "\n\n";

    // Each worker accumulates its own chunk's sum into a private slot —
    // no shared counter, so no synchronization needed inside the loop.
    setTitle("Parallel reduce");

    std::vector<long long> partialSums(pool.threadCount(), 0);

    parallelFor(pool, n, pool.threadCount(), [&](std::size_t begin, std::size_t end) {
        // Recover which chunk this is from its offset into data.
        std::size_t chunkSize = (n + pool.threadCount() - 1) / pool.threadCount();
        std::size_t chunk = begin / chunkSize;

        long long sum = 0;
        for (std::size_t i = begin; i < end; ++i) {
            sum += data[i];
        }
        partialSums[chunk] = sum;
    });

    long long total = std::accumulate(partialSums.begin(), partialSums.end(), 0LL);
    std::cout << "sum 0..999: " << total << "\n";
}

REGISTER_EXAMPLE_SUITE();
