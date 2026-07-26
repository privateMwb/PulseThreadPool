// PulseThreadPool Scaling Benchmark Suite
// Measures how pushBottom() cost changes as the queue's backlog grows,
// independent of the SMALL/MEDIUM/LARGE iteration tiers applied
// uniformly elsewhere in this suite — this is about the size of a
// single un-drained backlog, not how many times an operation repeats.
//
// Covers:
// - pushBottom() cost at increasing backlog depth, exercising
//   WorkStealingQueue's buffer-growth path (Buffer::grow())
//
// This is specific to this library's internal buffer-growth behavior —
// no oneTBB equivalent applies, so this runs through BENCH_SOLO().

#include <support/framework.h>

using namespace ThreadPoolPro::Detail;

// Measures pushBottom() cost once the queue already holds `backlog`
// un-drained tasks, forcing repeated buffer growth as backlog increases.
static void bench_push_at_backlog(std::size_t backlog) {
    WorkStealingQueue queue;

    for (std::size_t i = 0; i < backlog; ++i)
        queue.pushBottom(Task([] {}));

    auto ptp = [&] { queue.pushBottom(Task([] {})); };

    std::string label = "pushBottom at backlog " + std::to_string(backlog);
    BENCH_SOLO(label.c_str(), ptp);

    while (queue.popBottom().has_value()) {
    }
}

// Executes the backlog-depth sweep.
static void run_benchmarks() {
    for (std::size_t backlog : {std::size_t{0}, std::size_t{1024}, std::size_t{65536}}) {
        bench_push_at_backlog(backlog);
        std::cout << "\n";
    }
}

REGISTER_BENCH_SUITE();
