# ThreadPoolProBenchmark Results

## Detach

| Test | Iteration | ThreadPoolPro | oneTBB | Δ |
|---|---|---|---|---|
| Detach Single Task | 10K | 5.78 ms | 7.50 ms | +29.7% |
| Detach Single Task | 100K | 50.65 ms | 91.25 ms | +80.2% |
| Detach Single Task | 1M | 518.68 ms | 905.45 ms | +74.6% |
| Detach Batch + Drain (64 Tasks) | 10K | 441.95 ms | 255.24 ms | -42.2% |
| Detach Batch + Drain (64 Tasks) | 100K | 4.33 s | 2.56 s | -40.9% |
| Detach Batch + Drain (64 Tasks) | 1M | 119.36 s | 30.52 s | -74.4% |
