# Benchmark Suite

This document describes the benchmark categories under `suite/` — what
each one measures, and the individual benchmarks it contains.

Every benchmark compares LRUCache against stdLRU — a std::list +
std::unordered_map implementation, the conventional way an LRU cache is
built in C++. A category can support more than one standard for
comparison, but for now each category is benchmarked against a single
standard.

Every `BENCH()` call, in every category below, is automatically repeated
at three iteration tiers — SMALL (10K), MEDIUM (100K), and LARGE (1M) —
to smooth out timing noise and show whether relative performance holds
steady as call volume increases. This applies uniformly across the whole
suite; it is not specific to any one category. The **Scaling** category
below measures something different: how per-operation cost changes as
capacity itself grows or shrinks, independent of iteration count.

Some benchmarks have no meaningful stdLRU equivalent — stdLRU tracks no
hit/miss statistics and has no recency-order introspection. Those run
through `BENCH_SOLO()` instead of `BENCH()`, timing LRUCache alone.

---

## Access

Benchmarks read and lookup operations on already-constructed data —
retrieving values by key, indexed access, and existence checks.

### Benchmarks



---

## Core

Benchmarks the fundamental, most frequently exercised operations —
parsing raw input into the in-memory structure, serializing it back
to text, and equality comparison between instances.

### Benchmarks



---

## Lifecycle

Benchmarks object lifetime operations — construction, destruction,
copying, and moving — across the different value kinds a JSON value
can hold (null, bool, number, short/long string, array, object).

### Benchmarks



---

## Scaling

Benchmarks how per-operation cost changes as the *size of the input
data* grows — for example, an array or object with an increasing
number of elements, or JSON with increasing nesting depth. This is a
separate axis from the SMALL/MEDIUM/LARGE iteration tiers described
above: those repeat the same fixed-size operation more times, while
Scaling grows the operation itself and observes the resulting cost.

### Benchmarks



---

## Utility

Benchmarks helper and miscellaneous operations that don't belong to
any of the categories above — pretty-printing, string formatting,
and similar non-core utilities.

### Benchmarks


