# PulseThreadPool

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)](https://en.cppreference.com/w/cpp/23)
[![Status](https://img.shields.io/badge/status-learning%20project-green)](https://github.com/privateMwb/PulseThreadPool)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**PulseThreadPool** is a from-scratch, production-oriented thread pool written in modern C++23. It was built as a deep dive into concurrent scheduler design — per-worker work-stealing deques, small-buffer-optimized task storage, manual vtable type erasure, and the memory-ordering discipline a lock-free scheduler needs to survive real contention.

---

## Table of Contents

- [Overview](#overview)
- [Motivation](#motivation)
- [Features](#features)
- [Quick Start](#quick-start)
- [Core API](#core-api)
- [Design Overview](#design-overview)
- [Complexity](#complexity)
- [Benchmarks](#benchmarks)
- [Project Structure](#project-structure)
- [Building from Source](#building-from-source)
- [Known Limitations](#known-limitations)
- [License](#license)

---

## Overview

`ThreadPoolPro::ThreadPool` is a fixed-size worker pool built around a Chase-Lev work-stealing deque per worker, backed by a small-buffer-optimized, move-only task type. It focuses on the problems a production scheduler actually has to solve internally:

- Lock-free push/pop on the owning worker's own queue, with lock-free stealing from idle workers
- Zero-allocation task storage for small callables (SBO), heap fallback only when needed
- A single mutex-guarded injection queue for the one path that genuinely needs it: submission from non-worker threads
- Correct memory ordering around the classic Chase-Lev "last element" contention case
- Two independent submission APIs (`enqueue()` for a result, `detach()` for fire-and-forget) with two different exception-visibility contracts

On top of this foundation, PulseThreadPool adds pause/resume control, two shutdown modes, and a small set of runtime introspection methods, plus a benchmark suite covering construction, submission, dispatch latency, stealing, contention, and throughput.

---

## Motivation

This project was built to understand, in depth:

- Work-stealing deque design (Chase-Lev) and exactly where its lock-free guarantees actually hold
- Where a "torn read" on a contended slot is benign (trivial, pointer-sized payloads) versus where it's a real data race (non-trivial, move-only payloads) — and what that means for algorithm ordering
- Small-buffer optimization for type-erased callables, and the manual function-pointer vtable that avoids `virtual` dispatch
- Correct wake/notify discipline around a condition variable shared with atomic predicate state
- The real difference between "drain everything" and "discard everything" shutdown semantics, and how easy it is to accidentally implement neither
- Exception propagation differences between a future-returning API and a fire-and-forget one
- Rigorous benchmarking of a concurrent structure, including learning to distrust benchmark numbers that don't reproduce

---

## Features

| Feature | Description |
|---|---|
| Per-worker work-stealing deque | Each worker owns a lock-free Chase-Lev deque; idle workers steal from busy ones instead of contending on one global queue |
| SBO task storage | Callables up to 48 bytes are stored inline with no heap allocation; larger callables fall back to the heap automatically |
| Manual vtable type erasure | `Detail::Task` dispatches through a function-pointer table instead of `virtual`, avoiding vtable-pointer indirection through a base class |
| Two submission APIs | `enqueue()` returns a `std::future` for the result; `detach()` is a lower-overhead fire-and-forget path |
| Pause / resume | Stop picking up new work without tearing the pool down; in-flight tasks always run to completion |
| Two shutdown modes | `FinishTasks` drains everything queued; `DiscardTasks` abandons it |
| Cache-line-padded control state | `stopRequested_`, `paused_`, and the runtime counters each sit on their own cache line to avoid false sharing |
| Runtime introspection | `activeTaskCount()`, `queuedTasks()`, `idleThreadCount()`, `exceptionCount()`, `empty()`, and more |

---

## Quick Start

### Basic usage

```cpp
#include <ThreadPoolPro/ThreadPool.h>

using namespace ThreadPoolPro;

int main() {
    ThreadPool pool{4};

    auto future = pool.enqueue([](int a, int b) { return a + b; }, 2, 3);
    int result = future.get(); // 5

    pool.detach([] {
        // fire-and-forget, no future overhead
    });

    pool.shutdown(); // drains remaining work by default
}
```

### Pause / resume

```cpp
#include <ThreadPoolPro/ThreadPool.h>

using namespace ThreadPoolPro;

int main() {
    ThreadPool pool{4};

    pool.pause();               // stop picking up new work
    pool.detach([] { /* ... */ }); // queues, doesn't run yet
    pool.resume();               // let it proceed
}
```

### Shutdown modes

```cpp
#include <ThreadPoolPro/ThreadPool.h>

using namespace ThreadPoolPro;

int main() {
    ThreadPool pool{2};

    for (int i = 0; i < 100; ++i)
        pool.detach([] { /* ... */ });

    pool.shutdown(ThreadPool::ShutdownMode::DiscardTasks); // abandons what's still queued
}
```

### Recursive submission

```cpp
#include <ThreadPoolPro/ThreadPool.h>

using namespace ThreadPoolPro;

int main() {
    ThreadPool pool{4};

    auto future = pool.enqueue([&pool] {
        pool.detach([] { /* subtask, stealable by idle workers */ });
        return 1;
    });

    future.get();
}
```

---

## Core API

### Constructors & destructor

```cpp
explicit ThreadPool(std::size_t threadCount = std::thread::hardware_concurrency());
~ThreadPool();
```

### Execution control

```cpp
void pause() noexcept;
void resume() noexcept;
void shutdown(ShutdownMode mode = ShutdownMode::FinishTasks) noexcept;
```

### Task submission

```cpp
template<typename F, typename... Args>
[[nodiscard]] auto enqueue(F&& task, Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>;

template<typename F>
void detach(F&& task);
```

### Runtime statistics

```cpp
[[nodiscard]] std::size_t activeTaskCount() const noexcept;
[[nodiscard]] std::size_t queuedTasks()     const noexcept;
[[nodiscard]] std::size_t threadCount()     const noexcept;
[[nodiscard]] std::size_t exceptionCount()  const noexcept;
[[nodiscard]] std::size_t idleThreadCount() const noexcept;
[[nodiscard]] bool        empty()           const noexcept;
```

### State queries

```cpp
[[nodiscard]] bool isPaused()  const noexcept;
[[nodiscard]] bool isStopped() const noexcept;
```

---

## Design Overview

`ThreadPool` owns a fixed set of `Worker`s, each pairing a `Detail::WorkStealingQueue` with its own `std::thread`, plus a single mutex-guarded injection queue for external submission.

### Internal layout

```
ThreadPool
 ├── workers_[0] : { WorkStealingQueue, std::thread }
 ├── workers_[1] : { WorkStealingQueue, std::thread }
 ├── ...
 ├── injectionQueue_  (mutex-guarded, external submission only)
 ├── stopRequested_ / paused_ / activeTasks_ / pendingTasks_ / idleWorkers_ / exceptionCounter_
 │     (each alignas(CacheLineSize) — no false sharing between them)
 └── currentWorker_ / currentWorkerIndex_  (thread_local, set inside workerLoop)
```

- **`Detail::Task`** — a move-only, type-erased callable wrapper. Callables ≤ 48 bytes are placed in inline storage (`inlineStorage_`); larger ones are heap-allocated. Dispatch goes through a `Detail::VTable` of three function pointers (`invoke_`, `moveTo_`, `destroy_`) instead of a virtual base class.
- **`Detail::WorkStealingQueue`** — a Chase-Lev deque. The owning worker pushes/pops from the bottom without any lock; other workers steal from the top. Growth allocates a new `Buffer`, and the *old* buffer is retained in `retiredBuffers_` rather than freed immediately, since a thief may still be mid-read against it.
- **`submit()`** (internal) — if called from inside a worker (`currentWorker_ != nullptr`), pushes directly onto that worker's own queue (lock-free). If called from any other thread, it goes through the mutex-guarded `injectionQueue_` instead.
- **`workerLoop()`** — each worker tries its own queue, then steals from the others, then checks the injection queue, in that order, before parking on a condition variable if nothing is available.

### The one contended case in the deque

For most of the deque's operations there's no contention to worry about: a thief only ever touches the *top* index, and the owner only ever touches the *bottom* index, and those two never overlap except in one specific case — when exactly one element remains (`top == bottom`). Both `popBottom()` and `steal()` handle that case by attempting a CAS on the shared top index **before** touching the slot's data, so only the thread that wins the CAS ever reads or moves the `Task` stored there. Reading the data before resolving that CAS — the more obvious-looking implementation — is safe for trivial, pointer-sized payloads (the classic Chase-Lev use case) but is a genuine data race for a non-trivial, move-only type like `Task`, since its move constructor mutates the source object.

### Exception handling model

- **`enqueue()`** wraps the callable in a `std::packaged_task`; any exception it throws is captured into the associated future and surfaces at `future.get()`. It is **not** counted by `exceptionCount()`.
- **`detach()`** runs the callable directly inside the worker's `try`/`catch`; any exception increments `exceptionCounter_` and is otherwise silently discarded. There is no way to recover the exception itself through this path.

This asymmetry is intentional but easy to forget — see [Known Limitations](#known-limitations).

---

## Complexity

| Operation | Complexity | Notes |
|---|---|---|
| `WorkStealingQueue::pushBottom` | O(1) amortized | Lock-free; may trigger a buffer growth |
| `WorkStealingQueue::popBottom` | O(1) | Lock-free; CAS only in the single-element contended case |
| `WorkStealingQueue::steal` | O(1) | Lock-free; always CAS-gated |
| `enqueue()` / `detach()` (from a worker) | O(1) amortized | Direct `pushBottom` on the caller's own queue |
| `enqueue()` / `detach()` (external thread) | O(1) + lock | Goes through the mutex-guarded injection queue |
| `fetchTask()` | O(workerCount) worst case | Own queue → steal from each other worker → injection queue |
| Introspection methods | O(1) | Single relaxed atomic load each |
| `shutdown()` | O(pending tasks) for `FinishTasks`, O(1) signal + join for `DiscardTasks` | Join cost is proportional to remaining in-flight work only in `FinishTasks` mode |

---

## Benchmarks

All times are total elapsed time for the listed iteration count, measured on the development machine.

> Compiled with `-std=c++23`, `-O3`. Results may vary depending on hardware, scheduler noise, and compiler optimizations.

<details>
<summary>Show benchmark results</summary>

#### Introspection

```
----------------------------------------------------------------------
Introspection                           Time           Iteration
----------------------------------------------------------------------
ActiveTaskCount()                       2.16 ms         1000000

QueuedTasks()                           2.76 ms         1000000

IdleThreadCount()                       1.68 ms         1000000

Empty()                                 2.16 ms         1000000

IsPaused()                              2.77 ms         1000000

IsStopped()                             2.27 ms         1000000

ThreadCount()                           2.61 ms         1000000

ExceptionCount()                        2.74 ms         1000000
----------------------------------------------------------------------
```

#### Task

```
----------------------------------------------------------------------
Task                                    Time           Iteration
----------------------------------------------------------------------
Task construct (SBO)                    10.83 ms        1000000

Task construct (heap fallback)          233.64 ms       1000000

Task move construct (SBO)               19.34 ms        1000000

Task move construct (heap fallback)     146.45 ms       1000000

Task invoke (SBO)                       7.90 ms         1000000

Task invoke (heap fallback)             5.67 ms         1000000
----------------------------------------------------------------------
```

#### Pool Construction

```
----------------------------------------------------------------------
Pool Construction                       Time           Iteration
----------------------------------------------------------------------
ThreadPool construct (1 thread)         63.70 ms        200

ThreadPool construct (4 threads)        172.16 ms       200

ThreadPool construct (hardware Concurrency)306.80 ms       200

ThreadPool default construct            336.54 ms       200
----------------------------------------------------------------------
```

#### Dispatch Latency

```
----------------------------------------------------------------------
Dispatch Latency                        Time           Iteration
----------------------------------------------------------------------
Dispatch Latency (idle pool)
  Average: 52.69 us
  Worst:   2.01 ms

Dispatch Latency (behind a 20k-task backlog)
  p50:   4.31 us
  p99:   38.38 us
  Worst: 41.54 us
----------------------------------------------------------------------
```

#### Submission

```
----------------------------------------------------------------------
Submission                              Time           Iteration
----------------------------------------------------------------------
Detach() small callable                 342.67 ms       100000

Enqueue() small callable                607.08 ms       100000

Enqueue() with arguments                551.13 ms       100000

Detach() large callable (heap path)     616.22 ms       100000
----------------------------------------------------------------------
```

#### Work Stealing

```
----------------------------------------------------------------------
Work Stealing                           Time           Iteration
----------------------------------------------------------------------
WorkStealingQueue push+pop (uncontended)433.03 ms       1000000

WorkStealingQueue steal (uncontended)   504.82 ms       1000000

Balanced load (external submission)
  Total: 671.17 ms

Imbalanced load (single-worker origin, requires stealing)
  Total: 509.12 ms
----------------------------------------------------------------------
```

#### Contention

```
----------------------------------------------------------------------
Contention                              Time           Iteration
----------------------------------------------------------------------
Submission throughput by producer count (50000 tasks/producer, 8 pool threads)
  1 producer(s): 122284 tasks/sec
  2 producer(s): 175825 tasks/sec
  4 producer(s): 21417 tasks/sec
  8 producer(s): 600357 tasks/sec

enqueue() vs detach() under contention (8 producers, 20000 tasks/producer)
  detach():  643262 tasks/sec
  enqueue(): 353473 tasks/sec
----------------------------------------------------------------------
```

#### Throughput

```
----------------------------------------------------------------------
Throughput                              Time           Iteration
----------------------------------------------------------------------
Throughput by thread count (500000 no-op tasks)
  1 thread(s): 1095007 tasks/sec
  2 thread(s): 380622 tasks/sec
  4 thread(s): 159919 tasks/sec
  8 thread(s): 118785 tasks/sec

Throughput by task granularity (8 threads)
  No-op:       174284 tasks/sec
  Light (100): 294153 tasks/sec
  Heavy (10k): 108517 tasks/sec
----------------------------------------------------------------------
```

#### Summary

**Where the design pays off:**

- SBO task construction (`10.83 ms` vs `233.64 ms` for the heap path, per 1,000,000 calls) — roughly a 21x gap, which is exactly the allocation cost the inline storage exists to avoid.
- `detach()` is meaningfully cheaper than `enqueue()` for submission (`342.67 ms` vs `607.08 ms` per 100,000 calls) — the difference is the `packaged_task`/future machinery `enqueue()` carries and `detach()` doesn't.
- Introspection methods are effectively free (~2–3 ns/call across the board), consistent with single relaxed atomic loads.
- Dispatch latency behind a 20k-task backlog is very low (p50 `4.31 us`, p99 `38.38 us`) — a busy worker just pulls the next queued item with no wake-up cost.

**Where the numbers are inconsistent — unresolved:**

- Idle-pool dispatch latency (`52.69 us` average) is *higher* than the p99 under a 20k-task backlog (`38.38 us`). This direction makes sense (waking a parked thread costs more than a busy worker grabbing its next item), but the gap is worth re-measuring with more samples before treating either number as precise.
- The imbalanced work-stealing case finishes faster than the balanced case (`509.12 ms` vs `671.17 ms`), which reads like "stealing beats balanced load" but isn't a fair comparison — the two scenarios submit through different paths (external mutex-guarded injection vs. internal lock-free `pushBottom`), so the gap more likely reflects submission-path cost than stealing effectiveness.
- The 4-producer contention result (`21417 tasks/sec`) is roughly 8x worse than 2 producers and 28x worse than 8 — not a plausible scaling curve, and almost certainly a one-off measurement artifact rather than real behavior. Needs to be rerun several times before drawing any conclusion from it.
- No-op throughput at 8 threads disagrees between two tables measuring nominally the same thing: `118785 tasks/sec` in the by-thread-count table vs. `174284 tasks/sec` in the by-granularity table. Neither should be cited as an exact figure until this is reconciled.
- Throughput *dropping* as thread count increases for no-op tasks (`1095007` → `118785` tasks/sec from 1 to 8 threads) is real and expected, not a regression — with zero actual work per task, more threads only adds coordination overhead (wake/notify traffic, atomic cache-line bouncing) with nothing to parallelize against it.

| Category | Takeaway |
|---|---|
| Task construction | SBO path ~21x cheaper than heap fallback |
| Submission | `detach()` ~1.8x cheaper than `enqueue()` |
| Introspection | Effectively free (~2–3 ns/call) |
| Dispatch latency | Very low under load; idle-wake cost dominates the idle case |
| Work-stealing throughput comparison | Confounded by differing submission paths — not a clean read on stealing itself |
| 4-producer contention result | Outlier, not reproduced elsewhere — needs rerun |
| No-op throughput at 8 threads | Two tables disagree — needs reconciliation |
| No-op throughput vs. thread count | Expected: overhead-bound workload gets worse with more threads |

**Use a large pool when:** work is I/O-bound or genuinely parallelizable — the granularity benchmark shows light real work (`294153 tasks/sec`) benefiting from more threads, unlike no-op tasks.

**Keep the pool small, or batch tasks, when:** individual tasks are trivial — coordination overhead dominates at that granularity regardless of thread count.

</details>

---

## Project Structure

```
PulseThreadPool/
├── include/
│   └── ThreadPoolPro/
│       ├── ThreadPool.h
│       ├── ThreadPool.tpp
│       └── Detail/
│           ├── Utility.h
│           ├── VTable.h
│           ├── Task.h
│           ├── Task.tpp
│           ├── Buffer.h
│           └── WorkStealingQueue.h
│
├── tests/
│   ├── component/
│   └── behavior/
│
├── benchmarks/
│   ├── access/
│   ├── auxiliary/
│   ├── construction/
│   ├── core/
│   └── scaling/
│
├── examples/
│   ├── quickstart/
│   ├── integration/
│   ├── patterns/
│   ├── advanced/
│   └── pitfall/
│
├── cmake/
│   └── ThreadPoolProConfig.cmake.in
│
├── .gitignore
├── CMakeLists.txt
├── README.md
└── LICENSE
```

---

## Building from Source

### Requirements

- GCC 13+ or Clang with C++23 support
- CMake 3.20+

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Run tests

```bash
./tests
```

### Run benchmarks

```bash
./benchmarks
```

### Run examples

```bash
./example_basic_usage
./example_with_futures
./example_fan_out_fan_in
./example_pause_resume_control
./example_dangling_capture
```

---

## Known Limitations

- **`enqueue()` and `detach()` have different exception-visibility contracts.** An exception thrown inside `enqueue()`'s callable is captured by the `packaged_task` and surfaces at `future.get()` — it never touches `exceptionCount()`. An exception thrown inside `detach()`'s callable is caught by the worker loop, silently discarded, and only visible as an increment to `exceptionCount()`. There's currently no way to recover the actual exception from a `detach()`-submitted task.
- **No backpressure by default.** `enqueue()`/`detach()` will accept work faster than the pool can drain it indefinitely; `queuedTasks()` and `idleThreadCount()` are exposed so callers can build their own throttling, but the pool doesn't do it for you.
- **Blocking on a future from inside a task can deadlock on an undersized pool.** If a task calls `.get()` on a subtask's future and there's no idle worker free to run that subtask, the pool makes no progress. This is inherent to blocking inside a work-stealing scheduler, not specific to this implementation — size the pool with headroom for your actual nesting depth.
- **Some benchmark numbers don't yet reproduce cleanly** — see the [Benchmarks summary](#benchmarks) for the specific outlier (4-producer contention) and the inconsistent no-op throughput figures across two tables. Treat those specific numbers as directional, not exact, until rerun.
- **`WorkStealingQueue`'s old buffers are retained, not freed, on growth.** After a growth event, the previous (smaller) buffer is kept alive in `retiredBuffers_` rather than deleted immediately, since a concurrent thief may still be mid-read against it. This avoids a use-after-free but means buffer memory isn't reclaimed until the queue itself is destroyed — acceptable given growth is geometric and rare, but worth knowing if a worker's queue grows very large and then stays alive for a long time.

### Fixed during development

- The Chase-Lev deque's contended "last element" case (`top == bottom`) previously read and moved the `Task` out of the shared slot *before* resolving the ownership CAS, in both `popBottom()` and `steal()`. Since `Task`'s move constructor mutates its source, two threads racing for that slot could both move-construct from the same object concurrently — a genuine data race that surfaced as duplicated and dropped task executions under the concurrency stress tests. Both functions now resolve the CAS first and only the winner ever touches the slot's data.
- `WorkStealingQueue::pushBottom()` previously retired the *new* (just-grown) buffer into `retiredBuffers_` while also storing its raw pointer in `buffer_`, and separately leaked the *old* buffer that should have been retired instead. The result was a double-free of the new buffer at queue destruction and a leak of every buffer replaced by a growth event. Retirement now correctly targets the old buffer.
- `workerLoop()` previously always attempted to fetch and run another task before ever checking `stopRequested_`, meaning `ShutdownMode::DiscardTasks` behaved identically to `FinishTasks` — it drained the full queue instead of discarding it. The loop now checks for a discard-mode shutdown before each fetch attempt.

---

## License

Licensed under the [MIT License](LICENSE) — free to use, modify, and distribute for educational and personal purposes.
