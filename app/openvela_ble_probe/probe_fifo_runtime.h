#ifndef PROBE_FIFO_RUNTIME_H
#define PROBE_FIFO_RUNTIME_H
#include <stdbool.h>
#include <stdint.h>
#include "probe_fifo_time_estimator.h"
void probe_fifo_runtime_recover(uint8_t *gyro_mask, uint8_t *accel_mask,
                                bool *gyro_ready, int16_t gyro[3],
                                int16_t accel[3], bool *gap_pending,
                                struct probe_fifo_time_estimator *time_est,
                                uint32_t hz);
#endif
