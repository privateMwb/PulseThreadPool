/**
 * @file Utility.h
 * @brief Shared low-level constants used throughout PulseThreadPool.
 *
 * Contains the hardware cache-line size used to prevent false sharing on
 * hot atomics, and the inline storage capacity used by Detail::Task's
 * small-buffer optimization.
 */

#pragma once

// clang-format off
#include <cstddef> // std::size_t
#include <new>     // std::hardware_destructive_interference_size
#include <thread>  // std::this_thread::yield — cpuRelax()'s portable fallback
// clang-format on

#if defined(_MSC_VER)
#include <intrin.h> // _mm_pause
#endif

namespace ThreadPoolPro::Detail {

/**
 * @brief Hardware destructive-interference size, used to align hot
 * members so independent atomics never share a cache line.
 * @details Falls back to the common 64-byte line size on standard
 * library implementations that don't yet define
 * `__cpp_lib_hardware_interference_size` (e.g. some libc++ versions).
 */
#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t CacheLineSize = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t CacheLineSize = 64;
#endif

/**
 * @brief Inline storage capacity, in bytes, used by Detail::Task's
 * small-buffer optimization.
 * @details Callables whose decayed type fits within this many bytes (and
 * is nothrow-move-constructible) are stored directly inside the `Task`
 * object rather than heap-allocated. 48 bytes comfortably fits a
 * `std::packaged_task<R()>` capturing lambda plus a small argument tuple
 * for most real-world call signatures, while keeping `sizeof(Task)`
 * itself cache-line-friendly.
 */
inline constexpr std::size_t SboCapacity = 48;

/**
 * @brief Maximum number of retired `Task` heap allocations a single
 * WorkStealingQueue will keep on its owner-thread-only free list for
 * reuse before falling back to actually freeing the memory.
 * @details Bounds the worst case where a queue is drained once (e.g. a
 * large burst followed by long-term idling) into permanently retaining
 * this much recycled memory. 4096 nodes is a few hundred KB at most —
 * negligible next to the throughput win of skipping malloc/free on the
 * steady-state push/pop path — while still capping unbounded growth.
 */
inline constexpr std::size_t TaskFreeListCapacity = 4096;

/**
 * @brief Cheap "I'm spinning" hint for busy-wait loops.
 * @details Tells the CPU this is a spin loop so it can de-pipeline the
 * retry (e.g. `pause` on x86, `yield` on AArch64) without actually
 * yielding the core to the scheduler. Falls back to
 * `std::this_thread::yield()` on architectures with no such
 * instruction — see `waitUntil()` in ThreadPool.tpp, which spins on
 * this before ever parking on the wake token.
 */
inline void cpuRelax() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    asm volatile("yield" ::: "memory");
#else
    std::this_thread::yield();
#endif
}

/**
 * @brief Number of `cpuRelax()`-spin iterations `waitUntil()` tries
 * before falling back to `std::atomic::wait()` (an actual park/syscall).
 * @details Sized so the spin phase costs on the order of a few
 * microseconds total — negligible next to the microsecond-to-millisecond
 * wake latency a full park/unparked round trip costs on a loaded
 * system. This is what lets a burst of single, near-instant tasks (the
 * common `enqueue()`/`detach()` pattern) get picked up without ever
 * touching the OS scheduler, matching how other work-stealing runtimes
 * (e.g. oneTBB) avoid parking their workers between small, closely-spaced
 * tasks.
 */
inline constexpr int WaitSpinIterations = 1000;

/**
 * @brief Number of additional `std::this_thread::yield()` iterations
 * `waitUntil()` tries after `WaitSpinIterations` pure `cpuRelax()`
 * spins, before finally parking.
 * @details Defaults to 0 (disabled). Measured: `yield()` is not a cheap
 * "step aside" — under oversubscription (e.g. this pool's workers
 * sharing cores with another thread pool, or simply more worker threads
 * than logical cores) it can cost multiple real context switches before
 * the scheduler gets back around to the thread whose action the
 * predicate is actually waiting on, which delays exactly the progress
 * it's meant to encourage. Left as a tunable in case a given deployment
 * benefits from a small nonzero value (e.g. a genuinely idle machine
 * where yielding is nearly free), but pure spin -> park is the safer
 * default.
 */
inline constexpr int WaitYieldIterations = 0;

} // namespace ThreadPoolPro::Detail

/// @brief Short alias so this library can be used as `rain::ThreadPool`,
/// while its true namespace (and all internal diagnostics) remains
/// `ThreadPoolPro`. Repeated identically in every header of this project.
namespace rain = ThreadPoolPro;
