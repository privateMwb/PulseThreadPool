# ThreadPoolProBenchmark Results

## Steal

| Test | Iteration | ThreadPoolPro | oneTBB | Δ |
|---|---|---|---|---|
| PushBottom + PopBottom (uncontended) | 10K | 819.69 us |
| PushBottom + PopBottom (uncontended) | 100K | 8.28 ms |
| PushBottom + PopBottom (uncontended) | 1M | 82.49 ms |
| PushBottom (contended By Steal) | 10K | 2.42 ms |
| PushBottom (contended By Steal) | 100K | 26.18 ms |
| PushBottom (contended By Steal) | 1M | 268.23 ms |
