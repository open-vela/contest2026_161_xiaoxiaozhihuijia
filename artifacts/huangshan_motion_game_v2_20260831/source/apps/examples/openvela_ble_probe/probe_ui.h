#ifndef __APPS_EXAMPLES_OPENVELA_BLE_PROBE_PROBE_UI_H
#define __APPS_EXAMPLES_OPENVELA_BLE_PROBE_PROBE_UI_H

#include <stdbool.h>
#include <stdint.h>

enum probe_judgement_e
{
  PROBE_JUDGEMENT_READY = 0,
  PROBE_JUDGEMENT_PERFECT,
  PROBE_JUDGEMENT_GOOD,
  PROBE_JUDGEMENT_MISS
};

struct probe_ui_result
{
  enum probe_judgement_e judgement;
  uint32_t combo;
  uint64_t imu_samples;
  uint32_t imu_gaps;
};

typedef bool (*probe_ui_ble_connected_t)(void);

int probe_ui_start(probe_ui_ble_connected_t connected, bool imu_available);
bool probe_ui_submit(const struct probe_ui_result *result);

#endif
