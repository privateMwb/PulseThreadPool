// PulseThreadPool Task move semantics test suite.
//
// Coverage:
// - Move construction transfers the callable exactly once
// - Move assignment transfers the callable exactly once
// - Move construction correctly transfers a heap-allocated callable

#include <support/framework.h>

using namespace ThreadPoolPro::Detail;

namespace {

// A callable whose destructor counts how many times it runs, used to
// verify Task's move constructor/assignment transfer ownership exactly
// once rather than double-destroying or leaking the wrapped callable.
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

// Padded past SboCapacity (48 bytes) so Task heap-allocates it instead
// of storing it inline.
struct LargeCountedCallable : CountedCallable {
    using CountedCallable::CountedCallable;
    std::byte padding[64]{};
};

} // namespace

// Verifies move construction transfers the callable exactly once.
static void move_construct_transfers_callable() {
    int destructions = 0;
    {
        Task original{CountedCallable(&destructions)};
        Task moved(std::move(original));

        CHK(!static_cast<bool>(original));
        CHK(static_cast<bool>(moved));

        moved();
    }
    CHK(destructions == 1);
}

// Verifies move assignment transfers the callable exactly once.
static void move_assign_transfers_callable() {
    int destructions = 0;
    {
        Task original{CountedCallable(&destructions)};
        Task target;
        target = std::move(original);

        CHK(!static_cast<bool>(original));
        CHK(static_cast<bool>(target));

        target();
    }
    CHK(destructions == 1);
}

// Verifies move construction correctly transfers a heap-allocated callable.
static void move_construct_transfers_heap_callable() {
    int destructions = 0;
    {
        Task original{LargeCountedCallable(&destructions)};
        Task moved(std::move(original));

        CHK(!static_cast<bool>(original));
        CHK(static_cast<bool>(moved));

        moved();
    }
    CHK(destructions == 1);
}

// Executes all Task move semantics test cases.
static void run_tests() {
    RUN(move_construct_transfers_callable);
    RUN(move_assign_transfers_callable);
    RUN(move_construct_transfers_heap_callable);
}

REGISTER_TEST_SUITE();
