#include <nuttx/config.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include "probe_ui.h"
#include "probe_ui_beat_mailbox.h"
#include "probe_ui_refresh_policy.h"

#define PROBE_UI_RING_DEPTH 16

enum probe_ui_mode_e
{
  PROBE_UI_MODE_GAME = 0,
  PROBE_UI_MODE_BEAT
};

enum probe_ui_update_e
{
  PROBE_UI_UPDATE_GAME = 0,
  PROBE_UI_UPDATE_BEAT
};

struct probe_ui_update
{
  enum probe_ui_update_e type;
  union
  {
    struct probe_ui_result game;
    struct probe_ui_beat_event beat;
  } data;
};

static struct probe_ui_update g_ui_ring[PROBE_UI_RING_DEPTH];
static struct probe_ui_beat_mailbox g_ui_beat_mailbox;
static uint32_t g_ui_head;
static uint32_t g_ui_tail;
static uint32_t g_ui_full;
static pthread_t g_ui_thread;
static probe_ui_ble_connected_t g_connected;
static bool g_imu_available;
static bool g_started;
static bool g_stop_requested;
static bool g_exited;
static bool g_initialized;
static enum probe_ui_mode_e g_mode;
static enum probe_ui_refresh_mode_e g_refresh_mode;
static uint32_t g_ui_submitted;
static uint32_t g_ui_consumed;
static uint32_t g_ui_logical_beats;
static uint32_t g_ui_stop_drops;
static uint32_t g_ui_init_refresh_calls;
static uint64_t g_ui_init_refresh_total_us;
static uint32_t g_ui_init_refresh_max_us;
static uint32_t g_ui_runtime_refresh_calls;
static uint64_t g_ui_runtime_refresh_total_us;
static uint32_t g_ui_runtime_refresh_max_us;
static uint32_t g_ui_runtime_flush_calls;
static uint64_t g_ui_runtime_flush_total_us;
static uint32_t g_ui_runtime_flush_max_us;
static uint32_t g_ui_maintenance_calls;
static uint64_t g_ui_maintenance_total_us;
static uint32_t g_ui_maintenance_max_us;
static uint32_t g_ui_refresh_deadline_misses;
/*
 * LVGL invokes the display callbacks synchronously from the UI thread in this
 * configuration, so these timing counters have one writer.  A 32-bit version
 * makes the group readable as a coherent snapshot without requiring the
 * unavailable 64-bit libatomic helpers on Cortex-M33.
 */
static volatile uint32_t g_ui_stats_version;
static bool g_ui_initializing;
static uint64_t g_ui_refresh_started_us;
static uint64_t g_ui_flush_started_us;

static uint64_t probe_ui_monotonic_ns(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    {
      return 0;
    }

  return (uint64_t)ts.tv_sec * UINT64_C(1000000000) +
         (uint64_t)ts.tv_nsec;
}

static uint64_t probe_ui_monotonic_us(void)
{
  return probe_ui_monotonic_ns() / UINT64_C(1000);
}

static void probe_ui_stats_write_begin(void)
{
  __atomic_fetch_add(&g_ui_stats_version, 1, __ATOMIC_RELEASE);
}

static void probe_ui_stats_write_end(void)
{
  __atomic_fetch_add(&g_ui_stats_version, 1, __ATOMIC_RELEASE);
}

static void probe_ui_update_max(uint32_t *maximum, uint32_t elapsed)
{
  if (elapsed > *maximum)
    {
      *maximum = elapsed;
    }
}

static void probe_ui_display_event(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);
  uint64_t now_us = probe_ui_monotonic_us();
  uint32_t elapsed;

  if (code == LV_EVENT_REFR_START)
    {
      g_ui_refresh_started_us = now_us;
      return;
    }

  if (code == LV_EVENT_FLUSH_START)
    {
      g_ui_flush_started_us = now_us;
      return;
    }

  if (code == LV_EVENT_FLUSH_FINISH)
    {
      elapsed = now_us >= g_ui_flush_started_us ?
                (uint32_t)(now_us - g_ui_flush_started_us) : 0;
      if (!g_ui_initializing)
        {
          probe_ui_stats_write_begin();
          g_ui_runtime_flush_calls++;
          g_ui_runtime_flush_total_us += elapsed;
          probe_ui_update_max(&g_ui_runtime_flush_max_us, elapsed);
          probe_ui_stats_write_end();
        }

      return;
    }

  if (code == LV_EVENT_REFR_READY)
    {
      elapsed = now_us >= g_ui_refresh_started_us ?
                (uint32_t)(now_us - g_ui_refresh_started_us) : 0;
      if (g_ui_initializing)
        {
          probe_ui_stats_write_begin();
          g_ui_init_refresh_calls++;
          g_ui_init_refresh_total_us += elapsed;
          probe_ui_update_max(&g_ui_init_refresh_max_us, elapsed);
          probe_ui_stats_write_end();
        }
      else
        {
          probe_ui_stats_write_begin();
          g_ui_runtime_refresh_calls++;
          g_ui_runtime_refresh_total_us += elapsed;
          probe_ui_update_max(&g_ui_runtime_refresh_max_us, elapsed);
          probe_ui_stats_write_end();
        }
    }
}

static uint32_t probe_ui_run_maintenance(void)
{
  uint64_t before = probe_ui_monotonic_us();
  uint32_t idle = lv_timer_handler();
  uint64_t after = probe_ui_monotonic_us();
  uint32_t elapsed = after >= before ? (uint32_t)(after - before) : 0;

  if (!g_ui_initializing)
    {
      probe_ui_stats_write_begin();
      g_ui_maintenance_calls++;
      g_ui_maintenance_total_us += elapsed;
      probe_ui_update_max(&g_ui_maintenance_max_us, elapsed);
      probe_ui_stats_write_end();
    }

  return idle;
}

static bool probe_ui_take(struct probe_ui_update *update)
{
  uint32_t tail = __atomic_load_n(&g_ui_tail, __ATOMIC_RELAXED);
  uint32_t head = __atomic_load_n(&g_ui_head, __ATOMIC_ACQUIRE);

  if (tail == head)
    {
      return false;
    }

  memcpy(update, &g_ui_ring[tail % PROBE_UI_RING_DEPTH], sizeof(*update));
  __atomic_store_n(&g_ui_tail, tail + 1, __ATOMIC_RELEASE);
  return true;
}

static bool probe_ui_queue_empty(void)
{
  bool ring_empty = __atomic_load_n(&g_ui_tail, __ATOMIC_RELAXED) ==
                    __atomic_load_n(&g_ui_head, __ATOMIC_ACQUIRE);
  bool beats_consumed = probe_ui_beat_mailbox_empty(&g_ui_beat_mailbox);

  return ring_empty && (g_mode != PROBE_UI_MODE_BEAT || beats_consumed);
}

static bool probe_ui_take_latest_beat(struct probe_ui_beat_event *beat,
                                      uint32_t *submitted)
{
  return probe_ui_beat_mailbox_take_latest(&g_ui_beat_mailbox, beat,
                                            submitted);
}

static const char *probe_ui_judgement_text(enum probe_judgement_e judgement)
{
  switch (judgement)
    {
      case PROBE_JUDGEMENT_PERFECT:
        return "PERFECT";
      case PROBE_JUDGEMENT_GOOD:
        return "GOOD";
      case PROBE_JUDGEMENT_MISS:
        return "MISS";
      default:
        return "READY";
    }
}

const char *probe_ui_refresh_mode_name(enum probe_ui_refresh_mode_e mode)
{
  switch (mode)
    {
      case PROBE_UI_REFRESH_FULL:
        return "full";
      case PROBE_UI_REFRESH_PAUSED:
        return "paused";
      case PROBE_UI_REFRESH_24HZ:
        return "24hz";
      default:
        return "invalid";
    }
}

static void probe_ui_apply_beat(lv_obj_t *judgement, lv_obj_t *combo,
                                lv_obj_t *imu,
                                const struct probe_ui_beat_event *beat)
{
  char text[64];

  lv_label_set_text(judgement, "Last: BEAT");
  snprintf(text, sizeof(text), "Beats: %lu",
           (unsigned long)beat->beat_count);
  lv_label_set_text(combo, text);
  if (g_imu_available)
    {
      snprintf(text, sizeof(text), "IMU: samples=%llu gaps=%lu",
               (unsigned long long)beat->imu_samples,
               (unsigned long)beat->imu_gaps);
      lv_label_set_text(imu, text);
    }
}

static void probe_ui_apply_game(lv_obj_t *judgement, lv_obj_t *combo,
                                lv_obj_t *imu,
                                const struct probe_ui_result *game)
{
  char text[64];

  lv_label_set_text(judgement,
                    probe_ui_judgement_text(game->judgement));
  snprintf(text, sizeof(text), "Combo: %lu", (unsigned long)game->combo);
  lv_label_set_text(combo, text);
  if (g_imu_available)
    {
      snprintf(text, sizeof(text), "IMU: samples=%llu gaps=%lu",
               (unsigned long long)game->imu_samples,
               (unsigned long)game->imu_gaps);
      lv_label_set_text(imu, text);
    }
}

static void *probe_ui_thread(void *arg)
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t init_result;
  struct probe_ui_update update;
  struct probe_ui_update latest;
  struct probe_ui_refresh_policy refresh_policy;
  lv_obj_t *title;
  lv_obj_t *judgement;
  lv_obj_t *combo;
  lv_obj_t *imu;
  lv_obj_t *ble;
  bool connected = false;
  bool previous_connected = false;
  bool latest_valid = false;
  bool display_dirty = false;
  unsigned int initial_calls = 0;
  uint32_t beat_submitted;

  (void)arg;
  memset(&init_result, 0, sizeof(init_result));
  memset(&refresh_policy, 0, sizeof(refresh_policy));
  printf("openvela_ble_probe: UI thread entered mode=%s\n",
         probe_ui_refresh_mode_name(g_refresh_mode));
  if (lv_is_initialized())
    {
      printf("openvela_ble_probe: UI unavailable: LVGL already initialized\n");
      goto exited;
    }

  lv_init();
  lv_nuttx_dsc_init(&info);
  info.fb_path = "/dev/lcd0";
  info.input_path = "/dev/input0";
  lv_nuttx_init(&info, &init_result);
  if (init_result.disp == NULL || init_result.indev == NULL)
    {
      printf("openvela_ble_probe: UI initialization failed"
             " lcd=/dev/lcd0 input=/dev/input0\n");
      /* Release whichever NuttX display/input objects were created before
       * tearing down LVGL itself.  This also runs the LCD driver's display
       * ownership callback for a display-only partial initialization.
       */
      lv_nuttx_deinit(&init_result);
      lv_deinit();
      goto exited;
    }

  lv_display_add_event_cb(init_result.disp, probe_ui_display_event,
                          LV_EVENT_ALL, NULL);
  title = lv_label_create(lv_screen_active());
  judgement = lv_label_create(lv_screen_active());
  combo = lv_label_create(lv_screen_active());
  imu = lv_label_create(lv_screen_active());
  ble = lv_label_create(lv_screen_active());
  if (title == NULL || judgement == NULL || combo == NULL || imu == NULL ||
      ble == NULL)
    {
      printf("openvela_ble_probe: UI label creation failed\n");
      lv_nuttx_deinit(&init_result);
      lv_deinit();
      goto exited;
    }

  lv_label_set_text(title, g_mode == PROBE_UI_MODE_BEAT ?
                            "Beat Detector" : "Motion Game");
  lv_label_set_text(judgement,
                    g_mode == PROBE_UI_MODE_BEAT ?
                      (g_refresh_mode == PROBE_UI_REFRESH_PAUSED ?
                         "REFRESH PAUSED" : "RUNNING") : "READY");
  lv_label_set_text(combo, g_mode == PROBE_UI_MODE_BEAT ?
                            "Beats: 0" : "Combo: 0");
  lv_label_set_text(imu, g_imu_available ? "IMU: samples=0 gaps=0" :
                                           "IMU: unavailable");
  lv_label_set_text(ble, "BLE: disconnected");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_align(judgement, LV_ALIGN_CENTER, 0, -30);
  lv_obj_align(combo, LV_ALIGN_CENTER, 0, 5);
  lv_obj_align(imu, LV_ALIGN_CENTER, 0, 38);
  lv_obj_align(ble, LV_ALIGN_BOTTOM_MID, 0, -12);
  __atomic_store_n(&g_initialized, true, __ATOMIC_RELEASE);
  printf("openvela_ble_probe: UI initialized refresh=%s limit_hz=%u\n",
         probe_ui_refresh_mode_name(g_refresh_mode),
         g_refresh_mode == PROBE_UI_REFRESH_24HZ ? PROBE_UI_LIMIT_HZ : 0);

  while (initial_calls < 3)
    {
      (void)probe_ui_run_maintenance();
      initial_calls++;
      usleep(20000);
    }

  if (g_refresh_mode == PROBE_UI_REFRESH_24HZ)
    {
      lv_timer_t *refresh_timer = lv_display_get_refr_timer(init_result.disp);

      if (refresh_timer != NULL)
        {
          lv_timer_pause(refresh_timer);
        }

      probe_ui_refresh_policy_start(&refresh_policy,
                                    probe_ui_monotonic_ns());
    }

  g_ui_initializing = false;
  for (;;)
    {
      if (g_mode == PROBE_UI_MODE_BEAT &&
          probe_ui_take_latest_beat(&latest.data.beat, &beat_submitted))
        {
          latest.type = PROBE_UI_UPDATE_BEAT;
          probe_ui_beat_mailbox_consume(&g_ui_beat_mailbox,
                                        beat_submitted,
                                        latest.data.beat.beat_count);
          if (g_refresh_mode == PROBE_UI_REFRESH_FULL)
            {
              probe_ui_apply_beat(judgement, combo, imu,
                                  &latest.data.beat);
            }
          else if (g_refresh_mode == PROBE_UI_REFRESH_24HZ)
            {
              latest_valid = true;
              display_dirty = true;
            }
        }

      while (probe_ui_take(&update))
        {
          __atomic_fetch_add(&g_ui_consumed, 1, __ATOMIC_RELAXED);
          if (update.type == PROBE_UI_UPDATE_BEAT)
            {
              __atomic_store_n(&g_ui_logical_beats,
                               update.data.beat.beat_count,
                               __ATOMIC_RELAXED);
            }

          if (g_refresh_mode == PROBE_UI_REFRESH_PAUSED)
            {
              continue;
            }

          if (g_refresh_mode == PROBE_UI_REFRESH_24HZ)
            {
              latest = update;
              latest_valid = true;
              display_dirty = true;
              continue;
            }

          if (update.type == PROBE_UI_UPDATE_BEAT)
            {
              probe_ui_apply_beat(judgement, combo, imu,
                                  &update.data.beat);
            }
          else
            {
              probe_ui_apply_game(judgement, combo, imu,
                                  &update.data.game);
            }
        }

      if (__atomic_load_n(&g_stop_requested, __ATOMIC_ACQUIRE) &&
          probe_ui_queue_empty())
        {
          break;
        }

      if (g_refresh_mode == PROBE_UI_REFRESH_PAUSED)
        {
          usleep(20000);
          continue;
        }

      connected = g_connected != NULL && g_connected();
      if (connected != previous_connected)
        {
          if (g_refresh_mode == PROBE_UI_REFRESH_FULL)
            {
              lv_label_set_text(ble, connected ? "BLE: connected" :
                                                   "BLE: disconnected");
            }
          else
            {
              display_dirty = true;
            }

          previous_connected = connected;
        }

      if (g_refresh_mode == PROBE_UI_REFRESH_24HZ)
        {
          uint32_t missed = 0;
          uint64_t now_ns;
          uint32_t idle;

          idle = probe_ui_run_maintenance();
          now_ns = probe_ui_monotonic_ns();
          if (display_dirty &&
              probe_ui_refresh_policy_due(&refresh_policy, now_ns, &missed))
            {
              if (latest_valid)
                {
                  if (latest.type == PROBE_UI_UPDATE_BEAT)
                    {
                      probe_ui_apply_beat(judgement, combo, imu,
                                          &latest.data.beat);
                    }
                  else
                    {
                      probe_ui_apply_game(judgement, combo, imu,
                                          &latest.data.game);
                    }

                  latest_valid = false;
                }

              lv_label_set_text(ble, connected ? "BLE: connected" :
                                                   "BLE: disconnected");
              probe_ui_stats_write_begin();
              g_ui_refresh_deadline_misses += missed;
              probe_ui_stats_write_end();
              lv_refr_now(init_result.disp);
              display_dirty = false;
            }

          idle = idle == 0 ? 1 : idle;
          if (idle > 10)
            {
              idle = 10;
            }

          usleep(idle * 1000);
        }
      else
        {
          uint32_t idle = probe_ui_run_maintenance();

          idle = idle == 0 ? 1 : idle;
          if (idle > 20)
            {
              idle = 20;
            }

          usleep(idle * 1000);
        }
    }

  lv_nuttx_deinit(&init_result);
  lv_deinit();

exited:
  __atomic_store_n(&g_initialized, false, __ATOMIC_RELEASE);
  __atomic_store_n(&g_exited, true, __ATOMIC_RELEASE);
  return NULL;
}

static int probe_ui_start_mode(probe_ui_ble_connected_t connected,
                               bool imu_available,
                               enum probe_ui_mode_e mode,
                               enum probe_ui_refresh_mode_e refresh_mode)
{
  int ret;

  if (__atomic_exchange_n(&g_started, true, __ATOMIC_ACQ_REL))
    {
      return 0;
    }

  g_connected = connected;
  g_imu_available = imu_available;
  g_mode = mode;
  g_refresh_mode = refresh_mode;
  g_ui_head = 0;
  g_ui_tail = 0;
  g_ui_full = 0;
  probe_ui_beat_mailbox_init(&g_ui_beat_mailbox);
  g_ui_submitted = 0;
  g_ui_consumed = 0;
  g_ui_logical_beats = 0;
  g_ui_stop_drops = 0;
  g_ui_init_refresh_calls = 0;
  g_ui_init_refresh_total_us = 0;
  g_ui_init_refresh_max_us = 0;
  g_ui_runtime_refresh_calls = 0;
  g_ui_runtime_refresh_total_us = 0;
  g_ui_runtime_refresh_max_us = 0;
  g_ui_runtime_flush_calls = 0;
  g_ui_runtime_flush_total_us = 0;
  g_ui_runtime_flush_max_us = 0;
  g_ui_maintenance_calls = 0;
  g_ui_maintenance_total_us = 0;
  g_ui_maintenance_max_us = 0;
  g_ui_refresh_deadline_misses = 0;
  g_ui_stats_version = 0;
  g_ui_initializing = true;
  __atomic_store_n(&g_stop_requested, false, __ATOMIC_RELEASE);
  __atomic_store_n(&g_exited, false, __ATOMIC_RELEASE);
  __atomic_store_n(&g_initialized, false, __ATOMIC_RELEASE);
  ret = pthread_create(&g_ui_thread, NULL, probe_ui_thread, NULL);
  if (ret != 0)
    {
      __atomic_store_n(&g_started, false, __ATOMIC_RELEASE);
      __atomic_store_n(&g_exited, true, __ATOMIC_RELEASE);
      return -ret;
    }

  return 0;
}

int probe_ui_start(probe_ui_ble_connected_t connected, bool imu_available)
{
  return probe_ui_start_mode(connected, imu_available, PROBE_UI_MODE_GAME,
                             PROBE_UI_REFRESH_FULL);
}

int probe_ui_start_beat(probe_ui_ble_connected_t connected,
                        bool imu_available,
                        enum probe_ui_refresh_mode_e refresh_mode)
{
  return probe_ui_start_mode(connected, imu_available, PROBE_UI_MODE_BEAT,
                             refresh_mode);
}

bool probe_ui_submit(const struct probe_ui_result *result)
{
  struct probe_ui_update update;
  uint32_t head = __atomic_load_n(&g_ui_head, __ATOMIC_RELAXED);
  uint32_t tail = __atomic_load_n(&g_ui_tail, __ATOMIC_ACQUIRE);

  if (!__atomic_load_n(&g_started, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&g_stop_requested, __ATOMIC_ACQUIRE))
    {
      return false;
    }

  if (head - tail >= PROBE_UI_RING_DEPTH)
    {
      __atomic_fetch_add(&g_ui_full, 1, __ATOMIC_RELAXED);
      return false;
    }

  update.type = PROBE_UI_UPDATE_GAME;
  memcpy(&update.data.game, result, sizeof(*result));
  memcpy(&g_ui_ring[head % PROBE_UI_RING_DEPTH], &update, sizeof(update));
  __atomic_store_n(&g_ui_head, head + 1, __ATOMIC_RELEASE);
  __atomic_fetch_add(&g_ui_submitted, 1, __ATOMIC_RELAXED);
  return true;
}

bool probe_ui_submit_beat(const struct probe_ui_beat_event *event)
{
  if (!__atomic_load_n(&g_started, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&g_stop_requested, __ATOMIC_ACQUIRE) ||
      g_mode != PROBE_UI_MODE_BEAT)
    {
      return false;
    }

  probe_ui_beat_mailbox_publish(&g_ui_beat_mailbox, event);
  return true;
}

uint32_t probe_ui_dropped(void)
{
  return __atomic_load_n(&g_ui_full, __ATOMIC_RELAXED);
}

void probe_ui_get_stats(struct probe_ui_stats *stats)
{
  uint32_t before;
  uint32_t after;
  uint32_t beat_submitted;
  uint32_t beat_consumed;
  uint32_t beat_logical;

  if (stats == NULL)
    {
      return;
    }

  memset(stats, 0, sizeof(*stats));
  if (g_mode == PROBE_UI_MODE_BEAT)
    {
      probe_ui_beat_mailbox_get(&g_ui_beat_mailbox, &beat_submitted,
                                &beat_consumed, &beat_logical);
      stats->submitted = beat_submitted;
      stats->consumed = beat_consumed;
      stats->logical_beats = beat_logical;
    }
  else
    {
      stats->submitted = __atomic_load_n(&g_ui_submitted,
                                          __ATOMIC_RELAXED);
      stats->consumed = __atomic_load_n(&g_ui_consumed,
                                         __ATOMIC_RELAXED);
      stats->logical_beats = __atomic_load_n(&g_ui_logical_beats,
                                              __ATOMIC_RELAXED);
    }
  stats->dropped = __atomic_load_n(&g_ui_full, __ATOMIC_RELAXED);
  do
    {
      before = __atomic_load_n(&g_ui_stats_version, __ATOMIC_ACQUIRE);
      if ((before & 1u) != 0)
        {
          continue;
        }

      stats->init_refresh_calls = g_ui_init_refresh_calls;
      stats->init_refresh_total_us = g_ui_init_refresh_total_us;
      stats->init_refresh_max_us = g_ui_init_refresh_max_us;
      stats->runtime_refresh_calls = g_ui_runtime_refresh_calls;
      stats->runtime_refresh_total_us = g_ui_runtime_refresh_total_us;
      stats->runtime_refresh_max_us = g_ui_runtime_refresh_max_us;
      stats->runtime_flush_calls = g_ui_runtime_flush_calls;
      stats->runtime_flush_total_us = g_ui_runtime_flush_total_us;
      stats->runtime_flush_max_us = g_ui_runtime_flush_max_us;
      stats->maintenance_calls = g_ui_maintenance_calls;
      stats->maintenance_total_us = g_ui_maintenance_total_us;
      stats->maintenance_max_us = g_ui_maintenance_max_us;
      stats->refresh_deadline_misses = g_ui_refresh_deadline_misses;
      after = __atomic_load_n(&g_ui_stats_version, __ATOMIC_ACQUIRE);
    }
  while (before != after || (after & 1u) != 0);
  stats->stop_drops = __atomic_load_n(&g_ui_stop_drops,
                                      __ATOMIC_RELAXED);
  stats->initialized = __atomic_load_n(&g_initialized, __ATOMIC_ACQUIRE);
  stats->exited = __atomic_load_n(&g_exited, __ATOMIC_ACQUIRE);
}

void probe_ui_request_stop(void)
{
  __atomic_store_n(&g_stop_requested, true, __ATOMIC_RELEASE);
}

bool probe_ui_has_exited(void)
{
  return !__atomic_load_n(&g_started, __ATOMIC_ACQUIRE) ||
         __atomic_load_n(&g_exited, __ATOMIC_ACQUIRE);
}

int probe_ui_get_sched(int *policy, int *priority)
{
  struct sched_param param;
  int value;
  int ret;
  if (policy == NULL || priority == NULL || !g_started) return EINVAL;
  ret = pthread_getschedparam(g_ui_thread, &value, &param);
  if (ret == 0)
    {
      *policy = value;
      *priority = param.sched_priority;
    }
  return ret;
}

int probe_ui_join(void)
{
  int ret;

  if (!__atomic_load_n(&g_started, __ATOMIC_ACQUIRE))
    {
      return 0;
    }

  ret = pthread_join(g_ui_thread, NULL);
  if (ret == 0)
    {
      __atomic_store_n(&g_started, false, __ATOMIC_RELEASE);
    }

  return ret == 0 ? 0 : -ret;
}
