/**
 * @file Task.cpp
 * @brief Task implementation.
 *
 * Contains the implementation of Detail::Task's move semantics,
 * destructor, invocation, state queries, and internal helpers.
 */

// ============================================================
// Implementation for ThreadPoolPro::Detail::Task.
// ============================================================
//
//  Sections:
//   1. Move Semantics
//   2. Destructor
//   3. Invocation
//   4. State Queries
//   5. Internal Helpers
//
// ============================================================

// clang-format off
#include <ThreadPoolPro/Detail/Task.h> // Task — the class this file implements

#include <new>       // placement new
#include <stdexcept> // std::logic_error
// clang-format on

namespace ThreadPoolPro::Detail {

// ============================================================
//  Section 1 — Move Semantics
// ============================================================

Task::Task(Task&& other) noexcept : vtable_{other.vtable_}, isHeap_{other.isHeap_} {
    if (!vtable_)
        return;

    if (isHeap_) {
        heapPtr_ = other.heapPtr_;
        other.heapPtr_ = nullptr;
    } else {
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
        } else {
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
    // An empty Task should never reach here in correct operation.
    // Unlike assert(), this check isn't stripped in release builds:
    // silently no-op-ing would make a lost/corrupted task invisible.
    // Throwing lets it surface as a counted exception in the pool's
    // worker loop instead of vanishing without a trace.
    if (!vtable_)
        throw std::logic_error("Task::operator(): attempted to invoke an empty Task");

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
    return isHeap_ ? heapPtr_ : static_cast<void*>(inlineStorage_);
}

void Task::reset() noexcept {
    if (!vtable_)
        return;

    if (isHeap_) {
        vtable_->heapDelete_(heapPtr_);
        heapPtr_ = nullptr;
    } else {
        vtable_->destroy_(inlineStorage_);
    }

    vtable_ = nullptr;
    isHeap_ = false;
}

} // namespace ThreadPoolPro::Detail

