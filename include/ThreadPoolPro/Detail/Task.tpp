/**
 * @file Task.tpp
 * @brief Task template implementation.
 *
 * Contains the implementation of Detail::Task's templated constructor.
 * Non-template members are implemented in Task.cpp.
 */

// ============================================================
// Template implementation for ThreadPoolPro::Detail::Task.
// ============================================================
//
//  Sections:
//   1. Constructors
//
// ============================================================

namespace ThreadPoolPro::Detail {

// ============================================================
//  Section 1 — Constructors
// ============================================================

template <typename F> Task::Task(F&& f) {
    using Decayed = std::decay_t<F>;

    vtable_ = getVTable<Decayed>();

    if constexpr (sizeof(Decayed) <= SboCapacity && alignof(Decayed) <= alignof(std::max_align_t) &&
                  std::is_nothrow_move_constructible_v<Decayed>) {
        // Fits inline: no allocation at all beyond whatever f's own
        // constructor required.
        isHeap_ = false;
        ::new (static_cast<void*>(inlineStorage_)) Decayed(std::forward<F>(f));
    } else {
        // Too large, over-aligned, or not nothrow-movable: fall back to
        // a single heap allocation. Not nothrow-movable is disqualifying
        // even if small, because Task's own move constructor/assignment
        // are noexcept and must be able to relocate an inline callable
        // without risking an exception mid-move.
        isHeap_ = true;

        if constexpr (alignof(Decayed) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            heapPtr_ = ::operator new(sizeof(Decayed), std::align_val_t(alignof(Decayed)));
        else
            heapPtr_ = ::operator new(sizeof(Decayed));

        ::new (heapPtr_) Decayed(std::forward<F>(f));
    }
}

} // namespace ThreadPoolPro::Detail
