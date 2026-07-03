// ============================================================
// Task.tpp
// Template implementations for ThreadPoolPro::Detail::Task.
// ============================================================
//
// Sections:
//   1. Constructors
//
// This file contains only template member implementations.
// Non-template members are implemented in Task.cpp.
//
// ============================================================

namespace ThreadPoolPro::Detail {


// ============================================================
//  Section 1 — Constructors
// ============================================================

template<typename F>
Task::Task(F&& f) {
	using Decayed = std::decay_t<F>;

	vtable_ = getVTable<Decayed>();

	if constexpr (sizeof(Decayed) <= SboCapacity
		&& alignof(Decayed) <= alignof(std::max_align_t)
		&& std::is_nothrow_move_constructible_v<Decayed>) {
		isHeap_ = false;
		::new (static_cast<void*>(inlineStorage_)) Decayed(std::forward<F>(f));
	} else {
		isHeap_  = true;
		heapPtr_ = new Decayed(std::forward<F>(f));
	}
}

} // namespace ThreadPoolPro::Detail