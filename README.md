# ThreadPoolPro

<p align="center">
  <img src="https://img.shields.io/github/v/release/privateMwb/ThreadPoolPro?style=for-the-badge&logo=github&color=yellow" alt="Version">
  <img src="https://img.shields.io/badge/License-MIT-orange?style=for-the-badge" alt="License - MIT">
  <img src="https://img.shields.io/badge/C%2B%2B-23-blue?style=for-the-badge&logo=c%2B%2B" alt="C++ - 23">
</p>

<p align="center">
  <a href="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/build.yml">
    <img src="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/build.yml/badge.svg" alt="Build and Test">
  </a>
  <a href="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/benchmark.yml">
    <img src="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/benchmark.yml/badge.svg" alt="Benchmarks">
  </a>
  <a href="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/coverage.yml">
    <img src="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/coverage.yml/badge.svg" alt="Coverage">
  </a>
  <a href="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/sanitizers.yml">
    <img src="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/sanitizers.yml/badge.svg" alt="Sanitizers">
  </a>
  <a href="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/clang-tidy.yml">
    <img src="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/clang-tidy.yml/badge.svg" alt="Clang Tidy">
  </a>
  <a href="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/clang-format.yml">
    <img src="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/clang-format.yml/badge.svg" alt="Clang Format">
  </a>
  <a href="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/docs.yml">
    <img src="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/docs.yml/badge.svg" alt="Documentation">
  </a>
  <a href="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/release.yml">
    <img src="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/release.yml/badge.svg" alt="Release">
  </a>
  <a href="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/packaging.yml">
    <img src="https://github.com/privateMwb/ThreadPoolPro/actions/workflows/packaging.yml/badge.svg" alt="Packaging">
  </a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/GCC-support-B46F1B?style=flat&logo=gnu" alt="GCC - support">
  <img src="https://img.shields.io/badge/Clang-support-045891?style=flat&logo=llvm" alt="Clang - support">
  <img src="https://img.shields.io/badge/MSVC-support-5C2D91?style=flat" alt="MSVC - support">
  <img src="https://img.shields.io/badge/AppleClang-support-000000?style=flat&logo=apple" alt="AppleClang - support">
</p>

ThreadPoolPro is a work-stealing C++ thread pool for modern C++ — lock-free per-worker Chase-Lev deques, a type-erased `Task` with small-buffer optimization instead of `std::function`'s per-instance heap allocation, and a lightweight `Future` replacing `std::packaged_task`/`std::future`'s general-purpose shared state.

## 📑 Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Project Structure](#project-structure)
- [Development](#development)
- [Benchmarks](#benchmarks)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [Changelog](#changelog)
- [License](#license)

## <a id="features"></a>✨ Features

- **Lock-free work-stealing deques** — each worker owns a Chase-Lev deque (`WorkStealingQueue`); local push/pop costs no atomic read-modify-write in the uncontended case, and idle workers steal from one another via a randomized victim offset before falling back to the injection queues, so concurrently-idle workers don't all serialize on the same low-index victim.
- **Type-erased, SBO'd `Task`** — callables that fit inline (48 bytes, nothrow-movable) are stored directly in the `Task` object via a per-type `VTable`, not heap-allocated the way `std::function` wraps every callable; only large or throw-on-move callables fall back to a single heap allocation.
- **Sharded injection queues** — submissions from non-worker threads round-robin across many mutex-guarded shards instead of one shared queue, so producer contention doesn't become the dominant cost as worker count grows.
- **Lightweight `Future`/`ResultState`** — a single, purpose-sized heap allocation per `enqueue()` (a value slot, an exception_ptr, and a completion flag), not `std::packaged_task`'s larger general-purpose shared state.
- **`ThreadMarket`** — a process-wide pool of persistent OS threads leased out to each `ThreadPool` and returned on shutdown instead of spawned/joined per pool, so constructing and destroying pools repeatedly doesn't repeatedly pay OS thread-creation cost.
- **Atomic wake-token synchronization** — worker wakeup uses a C++20 atomic wait/notify token with a spin → yield → park backoff, not a `condition_variable` + mutex pair, so `submit()`'s hot path never has to acquire a lock just to notify.
- **Pause/resume and dual shutdown modes** — `pause()`/`resume()` block new task execution without discarding queued work; `shutdown()` supports both finishing already-queued tasks (`FinishTasks`) and discarding them (`DiscardTasks`), and safely self-detaches a worker that calls `shutdown()` on its own pool from within a running task.
- **`waitIdle()` that pitches in** — a thread blocked waiting for the pool to drain helps steal and execute tasks itself rather than sitting idle, instead of leaving the pool's own workers as the only executors.

## <a id="requirements"></a>📋 Requirements

- A C++23-conformant compiler (tested: GCC, Clang, MSVC, AppleClang)
- CMake 3.20+

## <a id="installation"></a>📦 Installation

**From source:**

```bash
git clone https://github.com/privateMwb/ThreadPoolPro.git
cd ThreadPoolPro
cmake -B build \
  -DBUILD_TESTS=OFF \
  -DBUILD_BENCHMARKS=OFF \
  -DBUILD_REGRESSION=OFF \
  -DBUILD_EXAMPLES=OFF
cmake --install build
```

Then, in your own `CMakeLists.txt`:

```cmake
find_package(ThreadPoolPro CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE ThreadPoolPro::ThreadPoolPro)
```

> vcpkg and Conan packages are built and verified (recipe in
> `packaging/recipes/threadpoolpro/`, port in
> `packaging/vcpkg/ports/threadpoolpro/`), but not yet published to the
> public registries. This section will be updated once they are.

## <a id="quick-start"></a>🚀 Quick Start

```cpp
#include <ThreadPoolPro/ThreadPool.h>

int main() {
    rain::ThreadPool pool(4);

    // enqueue() returns a Future for the result.
    auto future = pool.enqueue([](int a, int b) { return a + b; }, 2, 3);
    int sum = future.get(); // 5

    // detach() is cheaper when you don't need the result at all.
    pool.detach([] { /* fire and forget */ });

    // Block until every submitted task has finished.
    pool.waitIdle();
}
```

An exception thrown by a task is rethrown from `get()`, not swallowed:

```cpp
auto future = pool.enqueue([]() -> int { throw std::runtime_error("boom"); });

try {
    future.get();
} catch (const std::runtime_error& e) {
    std::cerr << e.what() << '\n';
}
```

Pausing execution and choosing how shutdown handles queued work:

```cpp
pool.pause();
// ... no new task starts until resume() ...
pool.resume();

pool.shutdown(rain::ThreadPool::ShutdownMode::DiscardTasks);
```

## <a id="project-structure"></a>🗂️ Project Structure

```
ThreadPoolPro/
├── include/
│   └── ThreadPoolPro/
│       ├── ThreadPool.h
│       ├── ThreadPool.tpp
│       └── Detail/
│           ├── Buffer.h
│           ├── Future.h
│           ├── Task.h
│           ├── Task.tpp
│           ├── ThreadMarket.h
│           ├── Utility.h
│           ├── VTable.h
│           └── WorkStealingQueue.h
│
├── src/
│   └── ThreadPoolPro/
│       ├── ThreadPool.cpp
│       └── Detail/
│           ├── Buffer.cpp
│           ├── ThreadMarket.cpp
│           ├── Task.cpp
│           └── WorkStealingQueue.cpp
│
├── tests/
│   ├── support/
│   ├── suite/
│   ├── test_main.cpp
│   └── CMakeLists.txt
│
├── benchmarks/
│   ├── support/
│   ├── suite/
│   ├── results/
│   ├── bench_main.cpp
│   └── CMakeLists.txt
│
├── examples/
│   ├── support/
│   ├── suite/
│   ├── example_main.cpp
│   └── CMakeLists.txt
│
├── regression/
│   ├── support/
│   ├── regression_main.cpp
│   └── CMakeLists.txt
│
├── packaging/
│   ├── README.md
│   ├── recipes/
│   │   └── threadpoolpro/
│   ├── vcpkg/
│   │   └── ports/
│   │       └── threadpoolpro/
│   └── vcpkg-smoke-test/
│
├── scripts/
│   └── update_package_files.py
│
├── .github/
│   ├── releases/
│   └── workflows/
│
├── cmake/
│   └── ThreadPoolProConfig.cmake.in
│
├── docs/
│   ├── Doxyfile
│   └── README.md
│
├── .gitignore
├── CMakeLists.txt
├── README.md
└── LICENSE
```

## <a id="development"></a>🛠️ Development

The from-source install above builds the library only. To work on
ThreadPoolPro itself — running tests, benchmarks, or the regression
tool — build with everything enabled (the default):

```bash
cmake -B build
cmake --build build
```

**Run the test suite:**

```bash
ctest --test-dir build
```

**Run benchmarks and check for regressions:**

```bash
./build/benchmarks
./build/regression                  # latest baseline vs. benchmarks/results/benchmark_results.json
./build/regression v1.2.0           # a specific baseline vs. current
./build/regression v1.2.0 v1.4.0    # two baselines against each other
```

`regression` picks the latest baseline by semantic version (`v1.10.0`
correctly outranks `v1.9.0`), not alphabetical filename order, and
auto-names its output (`regression_v1.2.0_vs_current.md`/`.json`, etc.).

See [packaging/README.md](packaging/README.md) for notes on verifying the vcpkg
port and Conan recipe locally.

## <a id="benchmarks"></a>📊 Benchmarks

Measured against oneTBB, same build, at 10K / 100K / 1M iterations
(1k–100k for worker-count scaling). Full results:
`benchmarks/results/v1_0_0.md`.

| Operation                       | ThreadPoolPro (1M) | oneTBB (1M) | Δ      |
|-----------------------------------|---------------------|-------------|--------|
| `detach()` (caught exception)    | 114.18 ms           | 133.30 ms   | +16.7% |
| `detach()` (single task)         | 376.76 ms           | 157.34 ms   | -58.2% |
| `enqueue()` + result             | 941.76 ms           | 168.78 ms   | -82.1% |
| Batch `detach()` + drain (64)    | 50.60 s              | 3.44 s      | -93.2% |
| Construct + Destroy              | 10.63 s              | 621.59 ms   | -94.2% |
| Worker Count 32 (100k tasks)     | 26.38 s              | 1.30 s      | -95.1% |
| Worker Count 8 (100k tasks)      | 92.94 s              | 1.29 s      | -98.6% |

Correctness and safety hold up — the trade-off this release makes is
speed. ThreadPoolPro trails oneTBB, a mature production scheduler with
years of tuning behind it, on nearly every apples-to-apples throughput
comparison, sometimes substantially so at higher worker counts and
batch sizes. The one measured exception is the caught-exception path
under `detach()`, where ThreadPoolPro is modestly faster. Closing this
gap is expected follow-up work, not a claim this release makes.

## <a id="documentation"></a>📖 Documentation

Full API reference, generated with Doxygen from `docs/Doxyfile`:

**https://privateMwb.github.io/ThreadPoolPro/**

## <a id="contributing"></a>🤝 Contributing

Issues and pull requests are welcome. Before submitting a PR:

- Run the test suite (`ctest --test-dir build`)
- If you're changing a hot path, run `./build/regression` and mention
  the results in your PR description

## <a id="changelog"></a>📝 Changelog

See the [Releases](https://github.com/privateMwb/ThreadPoolPro/releases)
page for version history and release notes.

## <a id="license"></a>📄 License

MIT — see [LICENSE](LICENSE) for details.
