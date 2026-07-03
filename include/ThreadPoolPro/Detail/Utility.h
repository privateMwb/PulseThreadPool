#pragma once

#include <cstddef>
#include <new>

namespace ThreadPoolPro::Detail {

// Hardware cache-line size used to reduce false sharing.
#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t CacheLineSize =
    std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t CacheLineSize = 64;
#endif

// Inline storage capacity used by Detail::Task.
inline constexpr std::size_t SboCapacity = 48;

} // namespace ThreadPoolPro::Detail