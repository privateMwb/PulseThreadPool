// Recursive parallelism (divide-and-conquer) example.
//
// Demonstrates:
// - a task splitting its own work into subtasks
// - subtasks submitted from inside a running task (stealable by idle workers)
// - recursion bottoming out at a sequential base case
//
// Caveat: this recursively calls future.get() from inside a worker task,
// which blocks that worker until the subtask completes elsewhere. It
// relies on other idle workers stealing the pending subtasks to make
// progress. With a very small pool or very deep recursion, this pattern
// can starve or deadlock — see pitfall/blocking_future_inside_pool.cpp.

#include <common/framework.h>

#include <future>
#include <numeric>
#include <vector>

using namespace ThreadPoolPro;

// Recursively sums a range, splitting into two subtasks above a
// threshold and falling back to a sequential sum below it.
static long long parallelSum(ThreadPool& pool, const std::vector<int>& data,
                              std::size_t begin, std::size_t end) {
    constexpr std::size_t threshold = 500'000;

    if (end - begin <= threshold)
        return std::accumulate(data.begin() + begin, data.begin() + end, 0LL);

    std::size_t mid = begin + (end - begin) / 2;

    // The right half is submitted as a subtask; the left half continues
    // recursively on this same worker (which may itself submit further
    // subtasks). Idle workers can steal any of these subtasks.
    auto rightFuture = pool.enqueue([&pool, &data, mid, end] {
        return parallelSum(pool, data, mid, end);
    });

    long long leftSum = parallelSum(pool, data, begin, mid);

    return leftSum + rightFuture.get();
}

static void run_examples() {
    ThreadPool pool{32};

    setTitle("Divide And Conquer");

    constexpr std::size_t dataSize = 2'000'000;
    std::vector<int> data(dataSize);
    std::iota(data.begin(), data.end(), 1);

    std::cout << "Elements : " << dataSize << "\n\n";

    setTitle("Result");

    // Kick off the recursion from a top-level task so the first split
    // also runs on a worker rather than blocking the calling thread.
    auto future = pool.enqueue([&pool, &data] {
        return parallelSum(pool, data, 0, data.size());
    });

    std::cout << "Sum : " << future.get() << "\n";
}

REGISTER_EXAMPLE_SUITE();
