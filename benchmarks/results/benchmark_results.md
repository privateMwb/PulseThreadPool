# ThreadPoolProBenchmark Results

## Detach

| Test | Iteration | ThreadPoolPro | oneTBB | Δ |
|---|---|---|---|---|
| Detach Single Task | 10K | 4.42 ms | 7.01 ms | +58.6% |
| Detach Single Task | 100K | 48.18 ms | 86.20 ms | +78.9% |
| Detach Single Task | 1M | 440.66 ms | 880.67 ms | +99.9% |
| Detach Batch + Drain (64 Tasks) | 10K | 651.92 ms | 265.96 ms | -59.2% |
| Detach Batch + Drain (64 Tasks) | 100K | 6.76 s | 2.56 s | -62.2% |
| Detach Batch + Drain (64 Tasks) | 1M | 131.49 s | 48.30 s | -63.3% |
