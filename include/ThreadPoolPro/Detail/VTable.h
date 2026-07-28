/**
 * @file VTable.h
 * @brief Type-erased callable operations used by Detail::Task.
 *
 * Contains the function-pointer table and per-type factory that give
 * Detail::Task its type erasure, without the extra allocation and
 * indirection layers of `std::function`.
 */

#pragma once

// clang-format off
#include <functional> // std::invoke
#include <new>        // placement new and __STDCPP_DEFAULT_NEW_ALIGNMENT__
#include <utility>    // std::move
// clang-format on

namespace ThreadPoolPro::Detail {

/**
 * @brief Type-erased "how to operate on this callable" table for a single
 * concrete callable type `F`, addressed through an untyped `void*`.
 * @details One instance exists per instantiated `F` (see `getVTable()`),
 * shared by every `Task` holding that type — so type erasure costs a
 * single pointer per `Task` rather than a heap-allocated wrapper per
 * instance, which is the main overhead `std::function` pays that this
 * design avoids.
 */
struct VTable {
    /// @brief Invokes the callable stored at `p`. `p` points at either
    /// `Task`'s inline storage or its heap allocation, depending on
    /// which the owning `Task` selected at construction.
    void (*invoke_)(void* p);

    /// @brief Move-constructs the callable at `src` into raw storage at
    /// `dst`. Used only for the inline-storage case, when moving a
    /// `Task` requires relocating the callable itself.
    void (*moveTo_)(void* src, void* dst) noexcept;

    /// @brief Destroys (but does not free) the callable at `p`. Used for
    /// the inline-storage case, where the backing memory is owned by the
    /// enclosing `Task`, not by this table.
    void (*destroy_)(void* p) noexcept;

    /// @brief Destroys and frees a heap-allocated callable at `p`, using
    /// the same allocation alignment it was originally created with.
    /// Used only for the heap-storage case.
    void (*heapDelete_)(void* p) noexcept;
};

/**
 * @brief Returns the process-wide shared vtable for callable type `F`.
 * @tparam F Decayed callable type the returned table operates on.
 * @return Pointer to a function-local `static constexpr` `VTable`,
 * distinct per instantiation of `F` and stable for the lifetime of the
 * program, so `Task` can store just this one pointer instead of a copy
 * of the table.
 */
template <typename F> inline const VTable* getVTable() noexcept {
    static constexpr VTable table{
        [](void* p) { std::invoke(*static_cast<F*>(p)); },
        [](void* src, void* dst) noexcept { ::new (dst) F(std::move(*static_cast<F*>(src))); },
        [](void* p) noexcept { static_cast<F*>(p)->~F(); },
        // LCOV_EXCL_START
        // heapDelete_ is only ever invoked through Task::reset() when
        // isHeap_ is true (see Task.cpp). For every F small and
        // nothrow-movable enough to use inline storage, this
        // instantiation is unreachable by design — it exists only so
        // heap-stored F's share the same VTable layout. Coverage here
        // is per-F-instantiation, so it reads as "missing" for every
        // inline type even though the heap path itself is fully tested.
        [](void* p) noexcept {
            static_cast<F*>(p)->~F();

            if constexpr (alignof(F) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
                ::operator delete(p, std::align_val_t(alignof(F)));
            else
                ::operator delete(p);
        }};
        // LCOV_EXCL_STOP

    return &table;
}

} // namespace ThreadPoolPro::Detail

/// @brief Short alias so this library can be used as `rain::ThreadPool`,
/// while its true namespace (and all internal diagnostics) remains
/// `ThreadPoolPro`. Repeated identically in every header of this project.
namespace rain = ThreadPoolPro;
