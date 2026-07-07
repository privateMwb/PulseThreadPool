// Fan-out / fan-in pattern example.
//
// Demonstrates:
// - splitting a workload into independent chunks (fan-out)
// - submitting one task per chunk
// - gathering and reducing the chunk results (fan-in)

#include <common/framework.h>

#include <future>
#include <numeric>
#include <vector>

using namespace ThreadPoolPro;

// Sums a contiguous range of a vector.
static long long sumRange(const std::vector<int>& data, std::size_t begin, std::size_t end) {
    return std::accumulate(data.begin() + begin, data.begin() + end, 0LL);
}

static void run_examples() {
    ThreadPool pool{4};

    // Build a dataset to sum in parallel.
    setTitle("Workload");

    constexpr std::size_t dataSize = 1'000'000;
    std::vector<int> data(dataSize);
    std::iota(data.begin(), data.end(), 1);

    std::cout << "Elements : " << dataSize << "\n\n";

    // Fan-out: split the data into chunks, one task per chunk.
    setTitle("Fan-Out");

    constexpr std::size_t chunkCount = 8;
    std::size_t chunkSize = dataSize / chunkCount;

    std::vector<std::future<long long>> futures;
    futures.reserve(chunkCount);

    for (std::size_t c = 0; c < chunkCount; ++c) {
        std::size_t begin = c * chunkSize;
        std::size_t end   = (c == chunkCount - 1) ? dataSize : begin + chunkSize;

        futures.push_back(pool.enqueue([&data, begin, end] {
            return sumRange(data, begin, end);
        }));
    }

    std::cout << "Submitted " << chunkCount << " chunk tasks\n\n";

    // Fan-in: gather each chunk's partial result and combine them.
    setTitle("Fan-In");

    long long total = 0;
    for (auto& f : futures)
        total += f.get();

    std::cout << "Total : " << total << "\n";
}

REGISTER_EXAMPLE_SUITE();
