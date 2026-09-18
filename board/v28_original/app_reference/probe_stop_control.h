#ifndef __APPS_EXAMPLES_OPENVELA_BLE_PROBE_STOP_CONTROL_H
#define __APPS_EXAMPLES_OPENVELA_BLE_PROBE_STOP_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

enum probe_stop_step_e
{
  PROBE_STOP_STEP_PRODUCER = 1u << 0,
  PROBE_STOP_STEP_CONSUMER = 1u << 1,
  PROBE_STOP_STEP_UI = 1u << 2,
  PROBE_STOP_STEP_COMMUNICATION = 1u << 3,
  PROBE_STOP_STEP_INPUT = 1u << 4,
  PROBE_STOP_STEP_BLE = 1u << 5,
  PROBE_STOP_STEP_PRE_BLE_STATS = 1u << 6
};

struct probe_stop_control
{
  uint64_t requested_us;
  uint64_t deadline_us;
  uint32_t required_steps;
  uint32_t completed_steps;
  uint32_t duplicate_requests;
  bool requested;
};

bool probe_stop_control_request(struct probe_stop_control *control,
                                uint64_t now_us, uint32_t timeout_ms,
                                uint32_t required_steps);
void probe_stop_control_complete(struct probe_stop_control *control,
                                 uint32_t step);
bool probe_stop_control_is_complete(const struct probe_stop_control *control);
bool probe_stop_control_is_expired(const struct probe_stop_control *control,
                                   uint64_t now_us);
uint64_t probe_stop_control_elapsed_us(const struct probe_stop_control *control,
                                       uint64_t now_us);

#endif
