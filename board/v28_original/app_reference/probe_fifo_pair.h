#ifndef PROBE_FIFO_PAIR_H
#define PROBE_FIFO_PAIR_H
#include <stdint.h>
void probe_fifo_pair_word(uint8_t pattern_word, int16_t raw,
                          int16_t accel[3], int16_t gyro[3],
                          uint8_t *accel_mask, uint8_t *gyro_mask);
#endif
