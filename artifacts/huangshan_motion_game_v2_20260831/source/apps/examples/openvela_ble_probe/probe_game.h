#ifndef __APPS_EXAMPLES_OPENVELA_BLE_PROBE_PROBE_GAME_H
#define __APPS_EXAMPLES_OPENVELA_BLE_PROBE_PROBE_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define PROBE_IMU_SAMPLE_FLAG_ESTIMATED_TIME (1u << 0)
#define PROBE_IMU_SAMPLE_FLAG_GAP            (1u << 1)

struct probe_imu_sample
{
  uint64_t monotonic_us;
  uint32_t sequence;
  int16_t gyro[3];
  int16_t accel[3];
  uint16_t flags;
};

struct probe_game_command
{
  uint32_t connection_generation;
  uint8_t gesture;
  uint8_t confidence;
  uint8_t flags;
  uint8_t battery;
};

struct probe_game_result
{
  uint64_t monotonic_us;
  uint32_t connection_generation;
  uint8_t gesture;
  uint8_t confidence;
  uint8_t flags;
  uint8_t battery;
  uint32_t game_session;
  uint32_t combo;
  int32_t timing_error_ms;
};

struct probe_game_imu_stats
{
  uint32_t ring_full;
  uint32_t gap_samples;
};

int probe_game_start(void);
bool probe_game_submit_imu(const struct probe_imu_sample *sample);
bool probe_game_submit_command(const struct probe_game_command *command);
bool probe_game_submit_judgement(const struct probe_game_result *result);
bool probe_game_take_ble_result(struct probe_game_result *result);
void probe_game_get_imu_stats(struct probe_game_imu_stats *stats);
void probe_local_feedback(const struct probe_game_result *result);

#endif
