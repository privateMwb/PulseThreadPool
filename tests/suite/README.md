# Test Suite

This document describes the test categories under `suite/` — what each
one verifies, and the individual test files it contains.

Unlike the benchmark suite, tests validate the library's own
correctness directly — there is no reference implementation to compare
against, so results are simply pass or fail.

Every test suite registers itself automatically via
`REGISTER_TEST_SUITE()` at startup, and is assigned a sequential id
within its category (e.g. `U1`, `U2` for Unit; `L1`, `L2` for
Lifecycle) — there's no suite list to maintain by hand. This applies
uniformly across every category below.

---

## Concurrency

Verifies thread-safety — concurrent submission, stealing, and shutdown
from multiple threads, and correctness under simultaneous access to
the pool and its internal queues.

### Tests

- `concurrent_shutdown.cpp` — Two threads calling shutdown() concurrently with different modes resolve to exactly one mode, cleanly
- `concurrent_steal.cpp` — Multiple thieves race the owner's popBottom(); every pushed task is delivered exactly once, none lost or duplicated
- `concurrent_submit.cpp` — Many external threads calling detach() simultaneously all have their tasks executed exactly once
- `self_detach_race.cpp` — A task calling shutdown() on its own pool from a worker thread doesn't deadlock

---

## Integration

Verifies multiple components working together end-to-end — task
submission through to result delivery, draining, and pool
lifecycle — rather than a single function in isolation.

### Tests

- `exception_isolation.cpp` — A throwing detach()ed task doesn't stop others and is counted via exceptionCount(); a throwing enqueue()d task's exception stays confined to its own Future
- `pause_resume_cycle.cpp` — Pausing mid-batch blocks new tasks from starting; resuming drains the rest of the batch
- `pool_reuse_market.cpp` — Many successive pool construct/run/destroy cycles, each reusing worker threads returned by the previous cycle, execute correctly every time
- `steal_under_load.cpp` — A heavily imbalanced burst of recursively-submitted tasks on one worker's queue is drained correctly, with more than one worker helping
- `submit_execute_result.cpp` — A task submitted via enqueue() is picked up, executed, and its result observed through Future; many concurrently enqueued tasks each deliver independent results
- `wait_idle_drain.cpp` — A thread blocked in waitIdle() helps drain the pool itself rather than sitting idle

---

## Lifecycle

Verifies object lifetime and resource-reuse behavior — construction,
destruction, moving, and worker-thread reuse across `ThreadPool` and
its internal `Task`/`WorkStealingQueue`.

### Tests

- `market_thread_reuse.cpp` — A worker OS thread returned to the market by one pool's destructor is reused by the very next pool that leases one, instead of a fresh thread being spawned
- `pool_construction.cpp` — A requested thread count of 0 is clamped to 1
- `pool_shutdown.cpp` — The destructor and shutdown(FinishTasks) let already-queued tasks finish; shutdown(DiscardTasks) drops tasks that hadn't started; only the first shutdown() call's mode takes effect
- `queue_construction.cpp` — Destroying an empty queue is safe; the destructor releases any task still queued; growth past initial capacity preserves already-queued tasks
- `task_construction.cpp` — Small (inline SBO) and large (heap-allocated) callables are constructed and invoked correctly
- `task_move.cpp` — Move construction and move assignment transfer the callable exactly once, for both inline- and heap-stored callables

---

## Regression

Verifies that a specific, previously fixed bug stays fixed. One test
per resolved issue, added at the time the fix lands.

### Tests

- `market_reuse_ordering.cpp` — MarketThread::loop() must mark itself idle and return to the market before notify_all(), or a racing lease() could miss the reuse and spawn a new OS thread; checked via wall-clock time across many rapid construct/destroy cycles
- `market_thread_order.cpp` — thread_ must be declared after job_/hasWork_/exiting_/id_, since its constructor starts the OS thread immediately and that thread touches those members right away; stressed via rapid construct/destroy cycles to give a thread sanitizer a chance to flag any regression
- `shutdown_state_race.cpp` — runState_ is a single atomic RunState rather than a separate stop flag plus a plain enum; many threads race shutdown() with different modes across many separate pools to catch any regression back to an unguarded write

---

## Unit

Verifies individual functions or methods in isolation — the smallest
testable unit of behavior, independent of the categories above.

### Tests

- `buffer.cpp` — at()'s writable, mask-wrapped indexing; grow() doubles capacity, copies the live [top, bottom) range, and leaves the original buffer untouched
- `detach.cpp` — A detached task actually executes; a thrown exception is counted rather than propagated; detach() rejects new work once shutdown() has been called
- `enqueue.cpp` — enqueue()'s return value and thrown exceptions flow through Future::get(); enqueue() rejects new work after shutdown(); Future is no longer valid() after get() consumes it
- `future_result.cpp` — Future/ResultState directly: value and exception publishing, get() actually blocking (spin → yield → park) until another thread publishes, the void specialization, and a non-default/non-void T
- `pause_resume.cpp` — isPaused() reflects pause()/resume(); a task submitted while paused doesn't start until resume()
- `pool_observers.cpp` — threadCount() matches the constructor argument; a fresh pool reports empty and idle; activeTaskCount() reflects a task blocked mid-execution; isStopped() reflects shutdown()
- `queue.cpp` — pushBottom()/popBottom() round-trip in LIFO order; steal() takes from the oldest-pushed end; a stolen task runs exactly once; size() tracks pushBottom(), popBottom(), and steal()
- `task_invoke.cpp` — operator() invokes the wrapped callable and throws on an empty Task; operator bool() reflects whether a callable is held
- `vtable_ops.cpp` — invoke_/moveTo_/destroy_ round-trip for a small, inline-sized callable; invoke_/heapDelete_ and moveTo_/destroy_ for a large, heap-sized callable — driven directly against VTable rather than through Task
