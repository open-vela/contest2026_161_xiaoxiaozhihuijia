#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "probe_game.h"
#include "probe_ui.h"

#define PROBE_IMU_RING_DEPTH     32
#define PROBE_COMMAND_RING_DEPTH 16
#define PROBE_RESULT_RING_DEPTH  16

static struct probe_imu_sample g_imu_ring[PROBE_IMU_RING_DEPTH];
static struct probe_game_command g_command_ring[PROBE_COMMAND_RING_DEPTH];
static struct probe_game_result g_result_ring[PROBE_RESULT_RING_DEPTH];
static uint32_t g_imu_head;
static uint32_t g_imu_tail;
static uint32_t g_command_head;
static uint32_t g_command_tail;
static uint32_t g_result_head;
static uint32_t g_result_tail;
static uint32_t g_imu_ring_full;
static uint32_t g_imu_gap_samples;
static uint32_t g_command_ring_full;
static uint32_t g_result_ring_full;
static uint32_t g_ui_ring_full;
static sem_t g_game_sem;
static pthread_t g_game_thread;
static bool g_started;

static bool probe_game_take_imu(struct probe_imu_sample *sample)
{
  uint32_t tail = __atomic_load_n(&g_imu_tail, __ATOMIC_RELAXED);
  uint32_t head = __atomic_load_n(&g_imu_head, __ATOMIC_ACQUIRE);

  if (tail == head)
    {
      return false;
    }

  memcpy(sample, &g_imu_ring[tail % PROBE_IMU_RING_DEPTH], sizeof(*sample));
  __atomic_store_n(&g_imu_tail, tail + 1, __ATOMIC_RELEASE);
  return true;
}

static bool probe_game_take_command(struct probe_game_command *command)
{
  uint32_t tail = __atomic_load_n(&g_command_tail, __ATOMIC_RELAXED);
  uint32_t head = __atomic_load_n(&g_command_head, __ATOMIC_ACQUIRE);

  if (tail == head)
    {
      return false;
    }

  memcpy(command, &g_command_ring[tail % PROBE_COMMAND_RING_DEPTH],
         sizeof(*command));
  __atomic_store_n(&g_command_tail, tail + 1, __ATOMIC_RELEASE);
  return true;
}

static enum probe_judgement_e probe_test_judgement(uint8_t gesture)
{
  /* Test-only visible mapping; this is not an action recognition algorithm. */

  if (gesture == 1)
    {
      return PROBE_JUDGEMENT_GOOD;
    }

  if (gesture == 2)
    {
      return PROBE_JUDGEMENT_MISS;
    }

  return PROBE_JUDGEMENT_PERFECT;
}

static void probe_game_publish_result(const struct probe_game_result *result,
                                      uint64_t imu_samples,
                                      uint32_t imu_gaps)
{
  struct probe_ui_result ui;

  ui.judgement = probe_test_judgement(result->gesture);
  ui.combo = result->combo;
  ui.imu_samples = imu_samples;
  ui.imu_gaps = imu_gaps;

  probe_local_feedback(result);
  if (!probe_ui_submit(&ui))
    {
      __atomic_fetch_add(&g_ui_ring_full, 1, __ATOMIC_RELAXED);
    }

  if (!probe_game_submit_judgement(result))
    {
      __atomic_fetch_add(&g_result_ring_full, 1, __ATOMIC_RELAXED);
    }
}

static void *probe_game_thread(void *arg)
{
  struct probe_imu_sample sample;
  struct probe_game_command command;
  struct probe_game_result result;
  uint64_t imu_samples = 0;
  uint64_t last_sample_us = 0;
  uint32_t imu_gaps = 0;
  uint32_t game_session = 1;
  uint32_t combo = 0;

  (void)arg;
  printf("openvela_ble_probe: game thread entered\n");

  for (;;)
    {
      while (sem_wait(&g_game_sem) < 0 && errno == EINTR)
        {
        }

      while (probe_game_take_imu(&sample))
        {
          imu_samples++;
          last_sample_us = sample.monotonic_us;
          if ((sample.flags & PROBE_IMU_SAMPLE_FLAG_GAP) != 0)
            {
              imu_gaps++;
            }
        }

      while (probe_game_take_command(&command))
        {
          memset(&result, 0, sizeof(result));
          result.monotonic_us = last_sample_us;
          result.connection_generation = command.connection_generation;
          result.gesture = command.gesture;
          result.confidence = command.confidence;
          result.flags = command.flags;
          result.battery = command.battery;
          result.game_session = game_session;
          result.timing_error_ms = 0;
          if (probe_test_judgement(command.gesture) == PROBE_JUDGEMENT_MISS)
            {
              combo = 0;
            }
          else
            {
              combo++;
            }

          result.combo = combo;
          probe_game_publish_result(&result, imu_samples, imu_gaps);
          printf("openvela_ble_probe: game test judgement gesture=%u"
                 " combo=%lu imu_time_us=%" PRIu64 "\n",
                 command.gesture, (unsigned long)combo,
                 last_sample_us);
        }
    }

  return NULL;
}

int probe_game_start(void)
{
  int ret;

  if (__atomic_exchange_n(&g_started, true, __ATOMIC_ACQ_REL))
    {
      return 0;
    }

  if (sem_init(&g_game_sem, 0, 0) < 0)
    {
      __atomic_store_n(&g_started, false, __ATOMIC_RELEASE);
      return -errno;
    }

  ret = pthread_create(&g_game_thread, NULL, probe_game_thread, NULL);
  if (ret != 0)
    {
      sem_destroy(&g_game_sem);
      __atomic_store_n(&g_started, false, __ATOMIC_RELEASE);
      return -ret;
    }

  pthread_detach(g_game_thread);
  return 0;
}

bool probe_game_submit_imu(const struct probe_imu_sample *sample)
{
  uint32_t head = __atomic_load_n(&g_imu_head, __ATOMIC_RELAXED);
  uint32_t tail = __atomic_load_n(&g_imu_tail, __ATOMIC_ACQUIRE);

  if (!__atomic_load_n(&g_started, __ATOMIC_ACQUIRE))
    {
      return false;
    }

  if (head - tail >= PROBE_IMU_RING_DEPTH)
    {
      __atomic_fetch_add(&g_imu_ring_full, 1, __ATOMIC_RELAXED);
      return false;
    }

  memcpy(&g_imu_ring[head % PROBE_IMU_RING_DEPTH], sample, sizeof(*sample));
  __atomic_store_n(&g_imu_head, head + 1, __ATOMIC_RELEASE);
  if ((sample->flags & PROBE_IMU_SAMPLE_FLAG_GAP) != 0)
    {
      __atomic_fetch_add(&g_imu_gap_samples, 1, __ATOMIC_RELAXED);
    }

  sem_post(&g_game_sem);
  return true;
}

bool probe_game_submit_command(const struct probe_game_command *command)
{
  uint32_t head = __atomic_load_n(&g_command_head, __ATOMIC_RELAXED);
  uint32_t tail = __atomic_load_n(&g_command_tail, __ATOMIC_ACQUIRE);

  if (!__atomic_load_n(&g_started, __ATOMIC_ACQUIRE))
    {
      return false;
    }

  if (head - tail >= PROBE_COMMAND_RING_DEPTH)
    {
      __atomic_fetch_add(&g_command_ring_full, 1, __ATOMIC_RELAXED);
      return false;
    }

  memcpy(&g_command_ring[head % PROBE_COMMAND_RING_DEPTH], command,
         sizeof(*command));
  __atomic_store_n(&g_command_head, head + 1, __ATOMIC_RELEASE);
  sem_post(&g_game_sem);
  return true;
}

bool probe_game_submit_judgement(const struct probe_game_result *result)
{
  uint32_t head = __atomic_load_n(&g_result_head, __ATOMIC_RELAXED);
  uint32_t tail = __atomic_load_n(&g_result_tail, __ATOMIC_ACQUIRE);

  if (!__atomic_load_n(&g_started, __ATOMIC_ACQUIRE))
    {
      return false;
    }

  if (head - tail >= PROBE_RESULT_RING_DEPTH)
    {
      return false;
    }

  memcpy(&g_result_ring[head % PROBE_RESULT_RING_DEPTH], result,
         sizeof(*result));
  __atomic_store_n(&g_result_head, head + 1, __ATOMIC_RELEASE);
  return true;
}

bool probe_game_take_ble_result(struct probe_game_result *result)
{
  uint32_t tail = __atomic_load_n(&g_result_tail, __ATOMIC_RELAXED);
  uint32_t head = __atomic_load_n(&g_result_head, __ATOMIC_ACQUIRE);

  if (tail == head)
    {
      return false;
    }

  memcpy(result, &g_result_ring[tail % PROBE_RESULT_RING_DEPTH],
         sizeof(*result));
  __atomic_store_n(&g_result_tail, tail + 1, __ATOMIC_RELEASE);
  return true;
}

void probe_game_get_imu_stats(struct probe_game_imu_stats *stats)
{
  stats->ring_full = __atomic_load_n(&g_imu_ring_full, __ATOMIC_RELAXED);
  stats->gap_samples = __atomic_load_n(&g_imu_gap_samples,
                                       __ATOMIC_RELAXED);
}

void probe_local_feedback(const struct probe_game_result *result)
{
  /* Intentionally empty: PA32 polarity/RGB mapping and motor hardware are
   * not established for this board revision.
   */

  (void)result;
}
