# Example Suite

This document describes the example categories under `suite/` — what
each one demonstrates, and the individual example files it contains.

Unlike the test suite, an example doesn't assert correctness — it
demonstrates real usage of the library, including deliberate misuse
where instructive (see Misuse), so the reader sees both the correct
pattern and the mistake it guards against.

Every example file ends with `REGISTER_EXAMPLE_SUITE()`, which derives
the suite's category from its containing directory and assigns it a
sequential id within that category. This applies uniformly across
every category below.

---

## Advanced

Demonstrates deeper mechanics of the pool — exception propagation
versus swallowing, the runtime stats counters, the difference between
the two shutdown modes, and the observable effect of work stealing.

### Examples

- `discard_shutdown.cpp` — shutdown(DiscardTasks): in-flight tasks still finish, queued ones don't; only the first shutdown() call's mode takes effect
- `exception_handling.cpp` — get() rethrowing a task's exact exception type, catching by base class, and detach() swallowing exceptions into exceptionCount() instead
- `runtime_stats.cpp` — threadCount(), activeTaskCount(), queuedTasks(), idleThreadCount(), and exceptionCount() as the pool runs, drains, and accumulates
- `worker_stealing.cpp` — a single task fanning children onto its own worker's queue, and the wall-clock gap between four workers and one

---

## Integration

Demonstrates interoperability with the rest of a codebase — building
parallel algorithm primitives on top of enqueue()/detach(), chaining
task results into pipelines, and a producer/consumer pattern.

### Examples

- `parallel_for.cpp` — a parallelFor() built from enqueue(), splitting a range into per-thread chunks and joining on their futures
- `pipeline_stages.cpp` — chaining a task's result into the next stage, both sequentially and inside a single task, then several pipelines run as phases
- `producer_consumer.cpp` — detach() as a lightweight consumer per produced item, with waitIdle() as the join point
- `stl_algorithms.cpp` — pool-backed parallelTransform() and parallelCountIf(), checked against their serial std:: equivalents

---

## Misuse

Demonstrates common mistakes and the exceptions they lead to, alongside
the correct pattern — including the one call (waitIdle() from a pool's
own worker) that's unsafe enough to be shown but not exercised.

### Examples

- `double_get.cpp` — a second get() on the same Future throwing std::logic_error; valid() as the guard, and what moving actually transfers
- `empty_future_get.cpp` — get() on a default-constructed or moved-from Future throwing std::logic_error
- `submit_after_shutdown.cpp` — enqueue()/detach() throwing std::runtime_error once shutdown() has run; isStopped() as the check to make first
- `worker_self_join.cpp` — shutdown() called safely from a worker's own task, and why waitIdle() from a worker is the one call to avoid

---

## Patterns

Demonstrates common usage idioms built on top of the core API —
submitting and collecting a batch, pausing and resuming work, waiting
for the pool to drain, and a running task submitting further work.

### Examples

- `batch_enqueue.cpp` — submitting a batch before reading any result, then collecting futures back in order
- `nested_submit.cpp` — a running task detaching, enqueueing, and fanning out further work onto its own pool
- `pause_resume.cpp` — pause() holding new tasks back, isPaused(), and resume() releasing the queue
- `wait_idle.cpp` — queuedTasks()/activeTaskCount() while busy, waitIdle() blocking until drained, empty() as a non-blocking check

---

## Quickstart

Demonstrates fundamental, everyday usage — construction, submitting
work with and without a result, and shutting the pool down.

### Examples

- `basic_usage.cpp` — construction, enqueue()/detach(), waitIdle(), threadCount(), shutdown()
- `detach_task.cpp` — fire-and-forget submission, capturing state, and exceptionCount() instead of a thrown exception
- `enqueue_future.cpp` — Future's get(), valid(), collecting several futures, and exception propagation
- `pool_shutdown.cpp` — FinishTasks vs DiscardTasks, isStopped(), and the destructor's implicit shutdown()
