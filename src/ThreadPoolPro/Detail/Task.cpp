// ============================================================
// Task.cpp
// Implementation for ThreadPoolPro::Detail::Task.
// ============================================================
//
// Sections:
//   1. Move Semantics
//   2. Destructor
//   3. Invocation
//   4. State Queries
//   5. Internal Helpers
//
// ============================================================

#include <ThreadPoolPro/Detail/Task.h>

#include <cassert>
#include <new>

namespace ThreadPoolPro::Detail {


    // ============================================================
    //  Section 1 — Move Semantics
    // ============================================================

    Task::Task(Task&& other) noexcept
        : vtable_{ other.vtable_ }
        , isHeap_{ other.isHeap_ }
    {
        if (!vtable_)
            return;

        if (isHeap_) {
            heapPtr_ = other.heapPtr_;
            other.heapPtr_ = nullptr;
        }
        else {
            vtable_->moveTo_(other.inlineStorage_, inlineStorage_);
            vtable_->destroy_(other.inlineStorage_);
        }

        other.vtable_ = nullptr;
        other.isHeap_ = false;
    }

    Task& Task::operator=(Task&& other) noexcept {
        if (this == &other)
            return *this;

        reset();

        vtable_ = other.vtable_;
        isHeap_ = other.isHeap_;

        if (vtable_) {
            if (isHeap_) {
                heapPtr_ = other.heapPtr_;
                other.heapPtr_ = nullptr;
            }
            else {
                vtable_->moveTo_(other.inlineStorage_, inlineStorage_);
                vtable_->destroy_(other.inlineStorage_);
            }
        }

        other.vtable_ = nullptr;
        other.isHeap_ = false;

        return *this;
    }


    // ============================================================
    //  Section 2 — Destructor
    // ============================================================

    Task::~Task() noexcept {
        reset();
    }


    // ============================================================
    //  Section 3 — Invocation
    // ============================================================

    void Task::operator()() {
        assert(vtable_ && "Attempted to invoke an empty Task.");

        if (!vtable_)
            return;

        vtable_->invoke_(target());
    }


    // ============================================================
    //  Section 4 — State Queries
    // ============================================================

    Task::operator bool() const noexcept {
        return vtable_ != nullptr;
    }


    // ============================================================
    //  Section 5 — Internal Helpers
    // ============================================================

    void* Task::target() noexcept {
        return isHeap_
            ? heapPtr_
            : static_cast<void*>(inlineStorage_);
    }

    void Task::reset() noexcept {
        if (!vtable_)
            return;

        if (isHeap_) {
            vtable_->destroy_(heapPtr_);
            ::operator delete(heapPtr_);
            heapPtr_ = nullptr;
        }
        else {
            vtable_->destroy_(inlineStorage_);
        }

        vtable_ = nullptr;
        isHeap_ = false;
    }

} // namespace ThreadPoolPro::Detail