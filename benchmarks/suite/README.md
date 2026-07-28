# Benchmark Suite

This document describes the benchmark categories under `suite/` — what
each one measures, and the individual benchmarks it contains.

Every benchmark compares PulseThreadPool against oneTBB — `task_arena` +
`task_group`, the industry-standard C++ work-stealing scheduler
PulseThreadPool is modeled after.

Some benchmarks have no meaningful oneTBB equivalent — TBB exposes no
public type-erased callable wrapper comparable to `Task`, no public
work-stealing deque, no pause/resume concept, and no per-arena
introspection into queue depth or active/idle thread counts. Those run
through `BENCH_SOLO()` instead of `BENCH()`, timing PulseThreadPool
alone. Sweeps across a varying parameter (worker count) use
`BENCH_CUSTOM()` instead.

---

## Access

Benchmarks Task invocation cost and already-running pool state —
outside of, and independent from, actual task submission.

### Benchmarks

- `invoke.cpp` — `operator()` on a Task holding a small (SBO-resident)
  callable, `operator()` on a Task holding a large (heap-allocated)
  callable (solo)
- `state_query.cpp` — `activeTaskCount()` / `queuedTasks()` under load,
  `idleThreadCount()`, `isPaused()` / `isStopped()` / `empty()` (solo)

---

## Core

Benchmarks the fundamental, most frequently exercised operations —
submitting tasks and the work-stealing queue primitives underneath
them.

### Benchmarks

- `enqueue.cpp` — `enqueue()` + `future::get()` for a single task,
  against oneTBB's `task_group::run()` + captured-variable idiom
- `steal.cpp` — `pushBottom()` + `popBottom()` on the owning thread
  (uncontended), `pushBottom()` while a second thread continuously
  steals (contended) (solo)
- `detach.cpp` — `detach()` for a single task, `detach()` for a batch
  of 64 tasks + drain, against oneTBB's `task_group::run()`

---

## Lifecycle

Benchmarks object lifetime operations — pool construction/destruction
and Task's move semantics. Task is deliberately non-copyable, so
there's no copy_semantics counterpart here.

### Benchmarks

- `construct_destroy.cpp` — `ThreadPool(n)` construction + destruction,
  against `tbb::task_arena(n)` with eager `initialize()`
- `move_semantics.cpp` — move-constructing a Task holding an
  SBO-resident callable, move-constructing a Task holding a
  heap-allocated callable (solo)

---

## Scaling

Benchmarks how per-operation cost changes as backlog depth or worker
count grows, independent of any fixed-size repeat count — these sweep
a structural parameter rather than iteration volume.

### Benchmarks

- `queue_depth.cpp` — `pushBottom()` cost at increasing backlog depth
  (0 / 1,024 / 65,536 entries), exercising `WorkStealingQueue`'s
  buffer-growth path via `Buffer::grow()` (solo)
- `worker_count.cpp` — `detach()` / `task_group::run()` batch
  submit-and-drain throughput at 1, 2, 4, 8, 16, and 32 workers

---

## Utility

Benchmarks pool control and error-handling paths that don't belong to
any of the categories above — pausing/resuming, and running tasks that
throw.

### Benchmarks

- `pause_resume.cpp` — `pause()` + `resume()` cycle on an idle pool
  (solo)
- `exception_path.cpp` — `detach()` on a task that throws uncaught,
  exercising ThreadPool's own `exceptionCounter_` path (solo);
  `detach()` on a task that catches its own exception, matched against
  oneTBB with identical catch-inside-task semantics
