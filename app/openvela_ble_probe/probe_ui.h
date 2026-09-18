#ifndef __APPS_EXAMPLES_OPENVELA_BLE_PROBE_PROBE_UI_H
#define __APPS_EXAMPLES_OPENVELA_BLE_PROBE_PROBE_UI_H

#include <stdbool.h>
#include <stdint.h>

#define PROBE_UI_REFRESH_LIMIT_HZ 24u

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

struct probe_ui_beat_event
{
  uint32_t beat_count;
  uint32_t source_sequence;
  uint64_t beat_time_us;
  uint64_t imu_samples;
  uint32_t imu_gaps;
};

struct probe_ui_stats
{
  uint32_t submitted;
  uint32_t consumed;
  uint32_t dropped;
  uint32_t logical_beats;
  uint32_t init_refresh_calls;
  uint64_t init_refresh_total_us;
  uint32_t init_refresh_max_us;
  uint32_t runtime_refresh_calls;
  uint64_t runtime_refresh_total_us;
  uint32_t runtime_refresh_max_us;
  uint32_t runtime_flush_calls;
  uint64_t runtime_flush_total_us;
  uint32_t runtime_flush_max_us;
  uint32_t maintenance_calls;
  uint64_t maintenance_total_us;
  uint32_t maintenance_max_us;
  uint32_t refresh_deadline_misses;
  uint32_t stop_drops;
  bool initialized;
  bool exited;
};

enum probe_ui_refresh_mode_e
{
  PROBE_UI_REFRESH_FULL = 0,
  PROBE_UI_REFRESH_PAUSED,
  PROBE_UI_REFRESH_24HZ
};

typedef bool (*probe_ui_ble_connected_t)(void);

int probe_ui_start(probe_ui_ble_connected_t connected, bool imu_available);
bool probe_ui_submit(const struct probe_ui_result *result);
int probe_ui_start_beat(probe_ui_ble_connected_t connected,
                        bool imu_available,
                        enum probe_ui_refresh_mode_e refresh_mode);
bool probe_ui_submit_beat(const struct probe_ui_beat_event *event);
uint32_t probe_ui_dropped(void);
void probe_ui_get_stats(struct probe_ui_stats *stats);
void probe_ui_request_stop(void);
bool probe_ui_has_exited(void);
int probe_ui_get_sched(int *policy, int *priority);
int probe_ui_join(void);
const char *probe_ui_refresh_mode_name(enum probe_ui_refresh_mode_e mode);

#endif
