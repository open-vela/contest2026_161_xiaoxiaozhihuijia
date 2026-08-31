#include <nuttx/config.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include "probe_ui.h"

#define PROBE_UI_RING_DEPTH 16

static struct probe_ui_result g_ui_ring[PROBE_UI_RING_DEPTH];
static uint32_t g_ui_head;
static uint32_t g_ui_tail;
static uint32_t g_ui_full;
static pthread_t g_ui_thread;
static probe_ui_ble_connected_t g_connected;
static bool g_imu_available;
static bool g_started;

static bool probe_ui_take(struct probe_ui_result *result)
{
  uint32_t tail = __atomic_load_n(&g_ui_tail, __ATOMIC_RELAXED);
  uint32_t head = __atomic_load_n(&g_ui_head, __ATOMIC_ACQUIRE);

  if (tail == head)
    {
      return false;
    }

  memcpy(result, &g_ui_ring[tail % PROBE_UI_RING_DEPTH], sizeof(*result));
  __atomic_store_n(&g_ui_tail, tail + 1, __ATOMIC_RELEASE);
  return true;
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

static void *probe_ui_thread(void *arg)
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t init_result;
  struct probe_ui_result result;
  lv_obj_t *title;
  lv_obj_t *judgement;
  lv_obj_t *combo;
  lv_obj_t *imu;
  lv_obj_t *ble;
  char text[64];
  bool connected = false;
  bool previous_connected = false;

  (void)arg;
  printf("openvela_ble_probe: UI thread entered\n");
  if (lv_is_initialized())
    {
      printf("openvela_ble_probe: UI unavailable: LVGL already initialized\n");
      return NULL;
    }

  lv_init();
  lv_nuttx_dsc_init(&info);
  info.fb_path = "/dev/lcd0";
  info.input_path = "/dev/input0";
  memset(&init_result, 0, sizeof(init_result));
  lv_nuttx_init(&info, &init_result);
  if (init_result.disp == NULL || init_result.indev == NULL)
    {
      printf("openvela_ble_probe: UI initialization failed"
             " lcd=/dev/lcd0 input=/dev/input0\n");
      lv_deinit();
      return NULL;
    }

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
      return NULL;
    }

  lv_label_set_text(title, "Motion Game");
  lv_label_set_text(judgement, "READY");
  lv_label_set_text(combo, "Combo: 0");
  lv_label_set_text(imu, g_imu_available ? "IMU: samples=0 gaps=0" :
                                           "IMU: unavailable");
  lv_label_set_text(ble, "BLE: disconnected");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_align(judgement, LV_ALIGN_CENTER, 0, -30);
  lv_obj_align(combo, LV_ALIGN_CENTER, 0, 5);
  lv_obj_align(imu, LV_ALIGN_CENTER, 0, 38);
  lv_obj_align(ble, LV_ALIGN_BOTTOM_MID, 0, -12);
  printf("openvela_ble_probe: UI initialized\n");

  for (;;)
    {
      while (probe_ui_take(&result))
        {
          lv_label_set_text(judgement,
                            probe_ui_judgement_text(result.judgement));
          snprintf(text, sizeof(text), "Combo: %lu",
                   (unsigned long)result.combo);
          lv_label_set_text(combo, text);
          if (g_imu_available)
            {
              snprintf(text, sizeof(text), "IMU: samples=%llu gaps=%lu",
                       (unsigned long long)result.imu_samples,
                       (unsigned long)result.imu_gaps);
              lv_label_set_text(imu, text);
            }
        }

      connected = g_connected != NULL && g_connected();
      if (connected != previous_connected)
        {
          lv_label_set_text(ble, connected ? "BLE: connected" :
                                             "BLE: disconnected");
          previous_connected = connected;
        }

      {
        uint32_t idle = lv_timer_handler();
        idle = idle == 0 ? 1 : idle;
        if (idle > 20)
          {
            idle = 20;
          }

        usleep(idle * 1000);
      }
    }

  return NULL;
}

int probe_ui_start(probe_ui_ble_connected_t connected, bool imu_available)
{
  int ret;

  if (__atomic_exchange_n(&g_started, true, __ATOMIC_ACQ_REL))
    {
      return 0;
    }

  g_connected = connected;
  g_imu_available = imu_available;
  ret = pthread_create(&g_ui_thread, NULL, probe_ui_thread, NULL);
  if (ret != 0)
    {
      __atomic_store_n(&g_started, false, __ATOMIC_RELEASE);
      return -ret;
    }

  pthread_detach(g_ui_thread);
  return 0;
}

bool probe_ui_submit(const struct probe_ui_result *result)
{
  uint32_t head = __atomic_load_n(&g_ui_head, __ATOMIC_RELAXED);
  uint32_t tail = __atomic_load_n(&g_ui_tail, __ATOMIC_ACQUIRE);

  if (!__atomic_load_n(&g_started, __ATOMIC_ACQUIRE))
    {
      return false;
    }

  if (head - tail >= PROBE_UI_RING_DEPTH)
    {
      __atomic_fetch_add(&g_ui_full, 1, __ATOMIC_RELAXED);
      return false;
    }

  memcpy(&g_ui_ring[head % PROBE_UI_RING_DEPTH], result, sizeof(*result));
  __atomic_store_n(&g_ui_head, head + 1, __ATOMIC_RELEASE);
  return true;
}
