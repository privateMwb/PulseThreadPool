# ThreadPoolProBenchmark Results

## Exception Path

| Test | Iteration | ThreadPoolPro | oneTBB | Δ |
|---|---|---|---|---|
| Detach (uncaught Exception) | 10K | 5.26 ms |
| Detach (uncaught Exception) | 100K | 63.74 ms |
| Detach (uncaught Exception) | 1M | 771.19 ms |
| Detach (caught Exception) | 10K | 11.10 ms | 18.54 ms | +67.0% |
| Detach (caught Exception) | 100K | 65.12 ms | 70.61 ms | +8.4% |
| Detach (caught Exception) | 1M | 534.77 ms | 761.50 ms | +42.4% |

## Pause Resume

| Test | Iteration | ThreadPoolPro | oneTBB | Δ |
|---|---|---|---|---|
| Pause + Resume Cycle | 10K | 34.05 ms |
| Pause + Resume Cycle | 100K | 96.81 ms |
| Pause + Resume Cycle | 1M | 883.18 ms |
