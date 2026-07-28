// PulseThreadPool WorkStealingQueue lifecycle test suite.
//
// Coverage:
// - Destroying an empty queue is safe
// - The destructor releases any task still queued at destruction time
// - Pushing past the initial capacity grows the buffer without losing
//   or corrupting already-queued tasks

#include <support/framework.h>

using namespace ThreadPoolPro::Detail;

namespace {

// A callable whose destructor counts how many times it runs, used to
// verify the queue's destructor actually destroys any Task it still
// holds rather than leaking it.
struct CountedCallable {
    int* destructions;

    explicit CountedCallable(int* d) noexcept : destructions{d} {}

    CountedCallable(CountedCallable&& other) noexcept : destructions{other.destructions} {
        other.destructions = nullptr;
    }

    CountedCallable(const CountedCallable&) = delete;

    ~CountedCallable() {
        if (destructions)
            ++(*destructions);
    }

    void operator()() const {}
};

} // namespace

// Verifies destroying an empty queue is safe.
static void destroying_empty_queue_is_safe() {
    WorkStealingQueue queue;
    CHK(queue.size() == 0);
}

// Verifies the destructor releases any task still queued at destruction.
static void destructor_releases_queued_task() {
    int destructions = 0;
    {
        WorkStealingQueue queue(4);
        queue.pushBottom(Task(CountedCallable(&destructions)));
    }
    CHK(destructions == 1);
}

// Verifies pushing past the initial capacity grows the buffer without
// losing or corrupting already-queued tasks.
static void growth_preserves_queued_tasks() {
    WorkStealingQueue queue(4);
    std::vector<int> observed;

    for (int i = 0; i < 20; ++i)
        queue.pushBottom(Task([i, &observed]() { observed.push_back(i); }));

    CHK(queue.size() == 20);

    while (auto task = queue.popBottom())
        (*task)();

    std::vector<int> expected;
    for (int i = 19; i >= 0; --i)
        expected.push_back(i);

    CHK(observed == expected);
}

// Executes all WorkStealingQueue lifecycle test cases.
static void run_tests() {
    RUN(destroying_empty_queue_is_safe);
    RUN(destructor_releases_queued_task);
    RUN(growth_preserves_queued_tasks);
}

REGISTER_TEST_SUITE();
