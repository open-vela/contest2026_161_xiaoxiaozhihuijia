# Huangshan Motion Game v2 Verification — 2026-08-31

The 3000-event reliability run passed: `passed=3000`, `failed=0`,
`retry_proofs=600`, and `retransmissions_seen=600`.

- Runtime was approximately 4,758,490 ms and crossed the legacy `2^32 us`
  wrap point.
- The tail recorded `imu_time_us=4,764,381,124`.
- Gesture counters: `enqueued=3000`, `acked=3000`, `full=0`, `dropped=0`,
  `tx_status_nonzero=0`.
- FIFO counters: `overrun=0`, `recovery=0`, `gap=0`, `unexpected=0`,
  `i2c_errors=0`, `imu_ring_full=0`.
- IMU FIFO reads used 24-byte chunks.
- The final active disconnect with `reason=0x13` occurred after the test had
  ended and is not counted as an anomaly.
- First-event latency: median 252.71 ms, p95 322.98 ms, maximum 920.24 ms.
  The maximum is a future performance-optimization item and does not affect
  reliability acceptance.

This verification conclusion applies only to a flashed device whose firmware
SHA-256 matches the frozen SHA-256
`aa7ce08ca7460c8effdd78ad410631d277c5bee438c1ff0cb5c21da9c8228f3b`.
