#pragma once

#include <new>
#include <utility>
#include <type_traits>
#include <functional>

namespace ThreadPoolPro::Detail {

// Type-erased operations for invoking, moving, and destroying a callable.
struct VTable {
    void (*invoke_)(void*);
    void (*moveTo_)(void*, void*) noexcept;
    void (*destroy_)(void*) noexcept;

    // Destroys and frees a heap-allocated callable, using the
    // allocation alignment it was originally created with.
    void (*heapDelete_)(void*) noexcept;
};

// Returns the shared vtable for the specified callable type.
template<typename F>
inline const VTable* getVTable() noexcept {
    static constexpr VTable table{
        [](void* p) {
            std::invoke(*static_cast<F*>(p));
        },
        [](void* src, void* dst) noexcept {
            ::new (dst) F(std::move(*static_cast<F*>(src)));
        },
        [](void* p) noexcept {
            static_cast<F*>(p)->~F();
        },
        [](void* p) noexcept {
            static_cast<F*>(p)->~F();

            if constexpr (alignof(F) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
                ::operator delete(p, std::align_val_t(alignof(F)));
            else
                ::operator delete(p);
        }
    };

    return &table;
}

} // namespace ThreadPoolPro::Detail