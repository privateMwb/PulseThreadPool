#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "Utility.h"
#include "VTable.h"

namespace ThreadPoolPro::Detail {

// Move-only task wrapper with small-buffer optimization.
//
// Stores small callable objects inline and falls back to heap
// allocation for larger callables.
class Task {
private:

    // Internal helpers.
    [[nodiscard]] void* target() noexcept;
    void reset() noexcept;

    // Inline storage for small callables.
    alignas(std::max_align_t) std::byte inlineStorage_[SboCapacity];

    // Heap storage for large callables.
    void* heapPtr_ = nullptr;

    // Type-erased callable operations.
    const VTable* vtable_ = nullptr;

    // Indicates whether the callable is heap allocated.
    bool isHeap_ = false;

public:

    // Constructors and destructor.
    Task() noexcept = default;

    template<typename F>
    Task(F&& f);

    Task(Task&& other) noexcept;
    Task& operator=(Task&& other) noexcept;

    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    ~Task() noexcept;

    // Task execution.
    void operator()();

    // State queries.
    [[nodiscard]] explicit operator bool() const noexcept;
};

} // namespace ThreadPoolPro::Detail

#include "Task.tpp"