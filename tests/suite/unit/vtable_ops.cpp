// PulseThreadPool VTable operation test suite.
//
// Coverage:
// - invoke_, moveTo_, and destroy_ round-trip correctly for a small,
//   inline-sized callable
// - invoke_ and heapDelete_ correctly destroy and free a large,
//   heap-sized callable
//
// These drive VTable's function pointers directly rather than through
// Task, since Task only ever calls moveTo_/destroy_ for inline-stored
// callables and heapDelete_ for heap-stored ones — never both for the
// same F. Testing here at the VTable level, independent of which
// storage strategy Task would pick for a given F, is what lets both
// paths be exercised without contorting Task's own tests.

#include <support/framework.h>

#include <cstddef>
#include <new>

using namespace ThreadPoolPro::Detail;

namespace {

// Small enough for inline storage. Tracks invocation and destruction
// counts so moveTo_/destroy_/invoke_ can each be verified directly.
struct VTableSmallCallable {
    int* invokes;
    int* destructions;

    VTableSmallCallable(int* i, int* d) noexcept : invokes{i}, destructions{d} {}

    VTableSmallCallable(VTableSmallCallable&& other) noexcept
        : invokes{other.invokes}, destructions{other.destructions} {
        other.invokes = nullptr;
        other.destructions = nullptr;
    }

    VTableSmallCallable(const VTableSmallCallable&) = delete;

    ~VTableSmallCallable() {
        if (destructions)
            ++(*destructions);
    }

    void operator()() const {
        if (invokes)
            ++(*invokes);
    }
};

// Padded well past Task's inline capacity. Never moved here — this
// test only exercises the heap-delete path, which is all a heap-stored
// F ever needs from VTable.
struct VTableLargeCallable {
    int* invokes;
    int* destructions;
    std::byte padding[64]{};

    VTableLargeCallable(int* i, int* d) noexcept : invokes{i}, destructions{d} {}

    VTableLargeCallable(VTableLargeCallable&&) = delete;
    VTableLargeCallable(const VTableLargeCallable&) = delete;

    ~VTableLargeCallable() {
        if (destructions)
            ++(*destructions);
    }

    void operator()() const {
        if (invokes)
            ++(*invokes);
    }
};

} // namespace

// Verifies invoke_, moveTo_, and destroy_ round-trip correctly for a
// small, inline-sized callable, mirroring the exact sequence Task's
// move constructor uses: moveTo_ into the new slot, then destroy_ the
// old one.
static void small_type_operations_round_trip() {
    int invokes = 0;
    int destructions = 0;

    alignas(std::max_align_t) std::byte src[sizeof(VTableSmallCallable)];
    alignas(std::max_align_t) std::byte dst[sizeof(VTableSmallCallable)];

    ::new (static_cast<void*>(src)) VTableSmallCallable(&invokes, &destructions);

    const VTable* vtable = getVTable<VTableSmallCallable>();

    vtable->invoke_(src);
    CHK(invokes == 1);

    vtable->moveTo_(src, dst);
    vtable->destroy_(src);
    CHK(destructions == 1); // only the moved-from husk destructed so far

    vtable->invoke_(dst);
    CHK(invokes == 2);

    vtable->destroy_(dst);
    CHK(destructions == 2);
}

// Verifies invoke_ and heapDelete_ correctly invoke, destroy, and free
// a large, heap-sized callable.
static void large_type_heap_delete_frees_callable() {
    int invokes = 0;
    int destructions = 0;

    void* p = ::operator new(sizeof(VTableLargeCallable));
    ::new (p) VTableLargeCallable(&invokes, &destructions);

    const VTable* vtable = getVTable<VTableLargeCallable>();

    vtable->invoke_(p);
    CHK(invokes == 1);

    vtable->heapDelete_(p);
    CHK(destructions == 1);
}

// Executes all VTable operation test cases.
static void run_tests() {
    RUN(small_type_operations_round_trip);
    RUN(large_type_heap_delete_frees_callable);
}

REGISTER_TEST_SUITE();
