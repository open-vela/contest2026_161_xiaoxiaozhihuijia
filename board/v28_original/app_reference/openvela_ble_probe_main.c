#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <poll.h>
#include <termios.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/i2c/i2c_master.h>
#include <arch/board/board.h>

#include "advertiser_data.h"
#include "bluetooth.h"
#include "bt_adapter.h"
#include "bt_gatts.h"
#include "bt_le_advertiser.h"
#include "probe_game.h"
#include "probe_ui.h"
#include "beat_rank1.h"
#include "probe_fifo_time_estimator.h"
#include "probe_fifo_pair.h"
#include "probe_fifo_runtime.h"
#include "probe_stop_control.h"
#include "../../frameworks/connectivity/bluetooth/service/common/service_trace.h"
extern int bt_sal_le_cleanup_result(void);
extern bool bt_sal_le_host_initialized(void);
#include "probe_app_input.h"
#include "probe_rank_interval.h"
#include "rank1_benchmark.h"

extern void sf32lb52_bt_diag_dump_acl_tx(void);

#define PROBE_SERVICE_UUID 0xfff0
#define PROBE_RX_UUID      0xfff1
#define PROBE_TX_UUID      0xfff2
#define MIN_BLE_STARTUP_UPTIME_MS 6000ULL
#define PROBE_RX_QUEUE_DEPTH 32
#define PROBE_RX_FRAME_MAX   250
/* Validated host data-window ceiling; WNR remains an unreliable transport. */
#define PROBE_VALIDATED_HOST_DATA_WINDOW 30
#define PROBE_DATA_HEADER_SIZE 5
#define PROBE_VALIDATED_DATA_PAYLOAD 180
#define BLE_PROBE_ENABLE_AUTO_2M_PHY 0
#define PROBE_ADV_RESTART_MAX_ATTEMPTS 3
#define PROBE_RELIABLE39_REQUEST_TYPE  0x20
#define PROBE_RELIABLE39_RESPONSE_TYPE 0xa0
#define PROBE_RELIABLE39_PAYLOAD_SIZE  39
#define PROBE_RELIABLE39_CRC_OFFSET    44
#define PROBE_RELIABLE39_FRAME_SIZE    46
#define BLE_PROBE_GESTURE_TEST_INJECT  1 /* Product firmware: set to 0. */
#define PROBE_GESTURE_TIME_SYNC_TYPE   0x40
#define PROBE_GESTURE_ACK_TYPE         0x41
#define PROBE_GESTURE_INJECT_TYPE      0x42
#define PROBE_GESTURE_QUEUE_DEPTH      8
#define PROBE_GESTURE_EVENT_SIZE       16
#define PROBE_GESTURE_MAX_SENDS        3
#define PROBE_GESTURE_WORKER_POLL_MS   50
#define PROBE_STOP_DEFAULT_TIMEOUT_MS  8000u
#define PROBE_BLE_ADAPTER_WAIT_TIMEOUT_MS 10000u
#define PROBE_CAPTURE_START_TYPE        0x30
#define PROBE_CAPTURE_STOP_TYPE         0x31
#define PROBE_CAPTURE_SAMPLE_TYPE       0x02
#define PROBE_CAPTURE_CONTROL_TYPE      0x03
#define PROBE_CAPTURE_PROTOCOL_VERSION  1
#define PROBE_CAPTURE_SAMPLE_SIZE       17
#define PROBE_CAPTURE_CONTROL_MAX_SIZE  20
#define PROBE_CAPTURE_QUEUE_DEPTH       256
#ifndef PROBE_EXPERIMENT_BUILD_ID
#define PROBE_EXPERIMENT_BUILD_ID       "rank1_v28_original_20260917"
#endif
/* A/B diagnostic only.  Set to 1 at compile time to preserve capture
 * control ACKs while suppressing capture sample production and sending.
 */
#ifndef BLE_PROBE_CAPTURE_ACK_ONLY
#  define BLE_PROBE_CAPTURE_ACK_ONLY    0
#endif
#ifndef BLE_PROBE_SERIAL_ONLY
#  define BLE_PROBE_SERIAL_ONLY         0
#endif
#ifndef BLE_PROBE_ONBOARD_BEAT
#  define BLE_PROBE_ONBOARD_BEAT         0
#endif
#ifndef BLE_PROBE_ONBOARD_BEAT_DIAG
#  define BLE_PROBE_ONBOARD_BEAT_DIAG    0
#endif
#ifndef BLE_PROBE_JOINT_UI_BLE
#  define BLE_PROBE_JOINT_UI_BLE          0
#endif
#if BLE_PROBE_JOINT_UI_BLE && BLE_PROBE_SERIAL_ONLY
#  error "joint UI/BLE mode and serial-only mode are mutually exclusive"
#endif
#if BLE_PROBE_JOINT_UI_BLE && !BLE_PROBE_ONBOARD_BEAT
#  error "joint UI/BLE mode requires the onboard rank1 detector"
#endif
#define PROBE_RANK_PIPELINE_ENABLED \
  (BLE_PROBE_ONBOARD_BEAT && \
   (BLE_PROBE_SERIAL_ONLY || BLE_PROBE_JOINT_UI_BLE))
#define PROBE_CAPTURE_LOG_LIMIT         8
#define PROBE_IMU_I2C_DEV              "/dev/i2c2"
#define PROBE_IMU_I2C_FREQUENCY        400000
#define PROBE_IMU_WHO_AM_I_REG         0x0f
#define PROBE_IMU_WHO_AM_I_VALUE       0x6a
#define PROBE_IMU_ADDR                 0x6a
#define PROBE_IMU_DRDY_PULSE_CFG_G     0x0b
#define PROBE_IMU_INT1_CTRL             0x0d
#define PROBE_IMU_CTRL1_XL             0x10
#define PROBE_IMU_CTRL2_G              0x11
#define PROBE_IMU_CTRL3_C              0x12
#define PROBE_IMU_OUTX_L_G             0x22
#define PROBE_IMU_CTRL1_XL_104HZ_4G    0x48
#define PROBE_IMU_CTRL2_G_104HZ_1000DPS 0x48
#define PROBE_IMU_CTRL3_C_BDU_IF_INC   0x44
#define PROBE_IMU_RAW_SAMPLE_SIZE      12
#define PROBE_IMU_RAW_TARGET_HZ        104
#define PROBE_IMU_FIFO_WATCHDOG_MS      250 /* FTH IRQ is the primary wakeup. */
#define BLE_PROBE_IMU_DRDY_START_ON_TX_SUBSCRIBE 1

/* LSM6DS3TR-C FIFO definitions are local to this hardware test. */

#define PROBE_IMU_FIFO_CTRL1            0x06
#define PROBE_IMU_FIFO_CTRL2            0x07
#define PROBE_IMU_FIFO_CTRL3            0x08
#define PROBE_IMU_FIFO_CTRL4            0x09
#define PROBE_IMU_FIFO_CTRL5            0x0a
#define PROBE_IMU_FIFO_STATUS1          0x3a
#define PROBE_IMU_FIFO_DATA_OUT_L       0x3e
#define PROBE_IMU_FIFO_WATERMARK_WORDS  120
#define PROBE_IMU_FIFO_CTRL3_GY_XL      0x09
#define PROBE_IMU_FIFO_BYPASS           0x00
#define PROBE_IMU_FIFO_104HZ_CONTINUOUS 0x26
#define PROBE_IMU_INT1_DRDY_XL          0x01
#define PROBE_IMU_INT1_DRDY_G           0x02
#define PROBE_IMU_INT1_FIFO_FTH         0x08
#define PROBE_IMU_INT1_FIFO_OVR         0x10
#define PROBE_IMU_INT1_FIFO_FULL        0x20
#define PROBE_IMU_FIFO_PATTERN_WORDS    6
#define PROBE_IMU_FIFO_NEAR_FULL_WORDS  2000
#define PROBE_IMU_FIFO_SMART_FULL_LIMIT 2
#define PROBE_IMU_FIFO_NEAR_FULL_LIMIT  3
#define PROBE_IMU_FIFO_PATTERN_ERR_LIMIT 6
#define PROBE_IMU_FIFO_RECOVERY_RETRIES 3
#define PROBE_IMU_FIFO_RECOVERY_DELAY_US 1000000
#define PROBE_IMU_FIFO_READ_MAX_BYTES   24
#define PROBE_IMU_FIFO_READ_CHUNK_BYTES 24

_Static_assert(PROBE_IMU_FIFO_READ_CHUNK_BYTES > 0,
               "FIFO read chunk must not be empty");
_Static_assert((PROBE_IMU_FIFO_READ_CHUNK_BYTES % 2) == 0,
               "FIFO read chunk must contain whole words");
_Static_assert(PROBE_IMU_FIFO_READ_CHUNK_BYTES <=
               PROBE_IMU_FIFO_READ_MAX_BYTES,
               "FIFO read chunk exceeds its fixed buffer");

_Static_assert(PROBE_DATA_HEADER_SIZE + PROBE_VALIDATED_DATA_PAYLOAD <=
               PROBE_RX_FRAME_MAX,
               "validated DATA frame must fit in an RX queue slot");
_Static_assert(PROBE_VALIDATED_HOST_DATA_WINDOW <= PROBE_RX_QUEUE_DEPTH,
               "validated host data window must fit in the RX queue");
_Static_assert(PROBE_RELIABLE39_FRAME_SIZE <= PROBE_RX_FRAME_MAX,
               "reliable39 frame must fit in an RX queue slot");
_Static_assert(PROBE_GESTURE_EVENT_SIZE <= PROBE_RX_FRAME_MAX,
               "gesture event must fit in an RX queue slot");
_Static_assert(PROBE_CAPTURE_SAMPLE_SIZE <= 20,
               "capture sample must fit the confirmed ATT payload");
_Static_assert(PROBE_CAPTURE_CONTROL_MAX_SIZE <= 20,
               "capture control must fit the confirmed ATT payload");

enum probe_attr_handle_e
{
  PROBE_SERVICE_HANDLE = 1,
  PROBE_RX_HANDLE,
  PROBE_TX_HANDLE,
  PROBE_TX_CCCD_HANDLE,
};

static sem_t g_ble_on_sem;
static volatile bool g_ble_on;
static void *g_adapter_callback_cookie;

static bt_instance_t *g_instance;
static bt_advertiser_t *g_advertiser;
static gatts_handle_t g_service_handle;

static advertiser_data_t *g_adv_data;
static advertiser_data_t *g_scan_rsp_data;
/* These builders belong to this application; the framework receives copied
 * payload bytes.  Explicit ownership makes repeated/failed starts idempotent
 * and prevents a second cleanup from freeing an old builder. */
static bool g_adv_data_owned;
static bool g_scan_rsp_data_owned;
static uint8_t *g_adv_payload;
static uint8_t *g_scan_rsp_payload;
static uint16_t g_adv_payload_len;
static uint16_t g_scan_rsp_payload_len;

static void probe_advertiser_data_release(void)
{
  advertiser_data_t *adv_data;
  advertiser_data_t *scan_rsp_data;
  bool adv_owned;
  bool scan_owned;

  /* Detach the application-owned builders before calling into the allocator.
   * This makes the release one-shot even if a failure path is revisited. */
  adv_data = g_adv_data;
  scan_rsp_data = g_scan_rsp_data;
  adv_owned = g_adv_data_owned;
  scan_owned = g_scan_rsp_data_owned;
  g_adv_data = NULL;
  g_scan_rsp_data = NULL;
  g_adv_data_owned = false;
  g_scan_rsp_data_owned = false;
  g_adv_payload = NULL;
  g_scan_rsp_payload = NULL;
  g_adv_payload_len = 0;
  g_scan_rsp_payload_len = 0;

  if (adv_owned && adv_data != NULL)
    {
      advertiser_data_free(adv_data);
    }
  if (scan_owned && scan_rsp_data != NULL)
    {
      advertiser_data_free(scan_rsp_data);
    }
}

static bt_address_t g_peer_addr;
static bool g_peer_connected;
static bool g_notify_enabled;
static bool g_phy_read_pending;
static bool g_phy_update_pending;
static bool g_phy_read_requested;
static bool g_phy_update_requested;
static uint32_t g_connection_generation;
static uint32_t g_capture_start_rx_logs;
static uint32_t g_capture_active_reject_logs;
static uint32_t g_capture_ready_reject_logs;
static uint32_t g_capture_rx_full_logs;
static uint32_t g_capture_rx_stale_logs;
static bool g_adv_start_inflight;
static bool g_adv_start_callback_seen;
static uint8_t g_adv_start_callback_status;
static bool g_adv_restart_pending;
static bool g_adv_restart_delay_armed;
static uint8_t g_adv_restart_attempts;
static uint64_t g_adv_restart_due_ms;
static bool g_imu_detected;
static bool g_imu_raw_launch_pending;
static bool g_imu_raw_launch_attempted;
static bool g_imu_raw_started;
static bool g_imu_raw_launch_check_logged;
static bool g_visible_game_launch_pending;
static bool g_visible_game_launch_attempted;
static sem_t g_imu_drdy_sem;
static uint32_t g_imu_drdy_irq_count;
static bool g_imu_drdy_sem_initialized;


struct probe_rx_slot_s
{
  bt_address_t addr;
  uint32_t connection_generation;
  uint16_t length;
  uint16_t offset;
  uint8_t value[PROBE_RX_FRAME_MAX];
};

static pthread_mutex_t g_state_lock;
static pthread_mutex_t g_monotonic_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_monotonic_extended_valid;
static uint32_t g_monotonic_last_low_us;
static uint64_t g_monotonic_epoch_us;
static uint64_t g_monotonic_last_us;
static sem_t g_rx_sem;
static pthread_t g_communication_thread;
static volatile bool g_communication_running;
static volatile bool g_communication_exited;
static bool g_communication_thread_created;
static struct probe_rx_slot_s g_rx_queue[PROBE_RX_QUEUE_DEPTH];
static uint32_t g_rx_head;
static uint32_t g_rx_tail;
static uint32_t g_rx_enqueued;
static uint32_t g_rx_invalid_length;
static uint32_t g_rx_drop_lock_busy;
static uint32_t g_rx_drop_full;
static uint32_t g_rx_high_water;
static uint32_t g_rx_stale;
static uint32_t g_reliable39_rx_ok;
static uint32_t g_reliable39_bad_length;
static uint32_t g_reliable39_bad_crc;
static uint32_t g_reliable39_duplicates;
static uint32_t g_reliable39_tx_attempted;
static uint32_t g_reliable39_tx_status_nonzero;
static uint32_t g_gesture_rx_head;
static uint32_t g_gesture_rx_tail;
static uint16_t g_gesture_next_packet_id;
static bool g_gesture_time_synced;
static uint64_t g_gesture_sync_unix_ms;
static uint64_t g_gesture_sync_monotonic_us;
static uint32_t g_gesture_enqueued;
static uint32_t g_gesture_queue_full;
static uint32_t g_gesture_acked;
static uint32_t g_gesture_retransmitted;
static uint32_t g_gesture_dropped;
static uint32_t g_gesture_tx_status_nonzero;

struct probe_capture_sample_s
{
  uint32_t connection_generation;
  uint32_t sequence;
  uint32_t uptime_ms;
  uint64_t monotonic_us;
  int16_t accel[3];
  int16_t gyro[3];
  uint8_t flags;
  uint32_t batch_id;
  uint16_t batch_pos;
  uint16_t batch_complete_cycles;
  uint64_t batch_drain_us;
  uint64_t queued_at_us;
  /* Immutable producer context copied before queue publication. */
  uint64_t fifo_status_before_us;
  uint64_t fifo_status_after_us;
  uint64_t pair_complete_us;
  uint64_t estimated_sample_us;
  uint32_t estimator_sequence;
  uint32_t estimator_period_q16;
  int64_t estimator_slew_us;
  uint32_t fifo_depth_words;
  uint32_t remaining_after;
  uint32_t recovery_generation;
  uint8_t producer_context_valid;
};

#define PROBE_LATENCY_EVENT_DEPTH 32
struct probe_latency_event_record
{
  uint32_t session;
  uint32_t event_id;
  uint32_t source_sequence;
  uint64_t beat_us;
  uint64_t confirmation_sample_us;
  uint64_t enqueue_us;
  uint64_t hb_begin_us;
  uint64_t hb_end_us;
  uint64_t detected_us;
  uint64_t nominal_center_us;
  uint64_t nominal_confirmation_us;
  int64_t nominal_delta_us;
  uint32_t batch_id;
  uint32_t recovery_generation;
  uint32_t remaining_after;
  uint32_t fifo_depth_words;
  uint64_t fifo_status_before_us;
  uint64_t fifo_status_after_us;
  uint64_t pair_complete_us;
  uint64_t estimated_sample_us;
  uint32_t estimator_sequence;
  uint32_t estimator_period_q16;
  int64_t estimator_slew_us;
  uint32_t batch_complete_samples;
  uint32_t batch_first_sequence;
  uint32_t batch_last_sequence;
  uint32_t batch_enqueue_success;
  uint32_t batch_enqueue_failed;
  uint64_t batch_end_us;
  uint8_t batch_complete_valid;
  uint8_t batch_assoc_reason;
  uint8_t producer_context_valid;
};
static struct probe_latency_event_record g_latency_events[PROBE_LATENCY_EVENT_DEPTH];
static uint32_t g_latency_event_count;
static uint32_t g_latency_event_dropped;
static volatile bool g_latency_dump_requested;
static volatile bool g_latency_dump_active;
static uint32_t g_batch_association_failures;
#define PROBE_BATCH_ASSOC_PENDING 0
#define PROBE_BATCH_ASSOC_OK 1
#define PROBE_BATCH_ASSOC_NOT_FOUND 2
#define PROBE_BATCH_ASSOC_EVICTED 3
static const char *probe_batch_assoc_reason_name(uint8_t reason)
{
  switch (reason)
    {
      case PROBE_BATCH_ASSOC_OK: return "ASSOCIATED";
      case PROBE_BATCH_ASSOC_NOT_FOUND: return "NOT_FOUND";
      case PROBE_BATCH_ASSOC_EVICTED: return "EVICTED";
      default: return "PENDING";
    }
}
/* v21: formatting is performed outside the rank1 consumer.  The UART
 * mutex serializes this bounded diagnostic writer with event output. */
static pthread_mutex_t g_uart_write_lock = PTHREAD_MUTEX_INITIALIZER;
static sem_t g_latency_dump_sem;
static pthread_t g_latency_dump_thread;
static bool g_latency_dump_thread_created;
static volatile bool g_latency_dump_thread_running;
static volatile bool g_latency_dump_full;


/* Producer-side evidence.  Records are published only after all fields are
 * written; the UART thread consumes a bounded snapshot and never gates FIFO
 * production.  This is deliberately separate from event-only latency data. */
#define PROBE_FIFO_TRACE_DEPTH 512
struct probe_fifo_trace_record
{
  uint32_t session, batch_id, recovery_generation, sequence;
  uint32_t depth_words, remaining_after;
  uint64_t drain_before_us, drain_after_us, complete_us, enqueue_us;
  uint64_t batch_end_us;
  uint64_t sample_us, estimated_sample_us;
  uint32_t estimated_sequence;
  uint32_t period_q16;
  int64_t slew_us;
  uint8_t flags, enqueued, status_valid;
};
static struct probe_fifo_trace_record g_fifo_trace[PROBE_FIFO_TRACE_DEPTH];
static volatile uint32_t g_fifo_trace_count;
static volatile uint32_t g_fifo_trace_dropped;
static volatile uint32_t g_fifo_trace_next;
static uint32_t g_fifo_trace_recovery_generation;
static volatile uint32_t g_fifo_overrun_total;
static volatile uint32_t g_fifo_recovery_attempts_total;
static volatile uint32_t g_fifo_recovery_success_total;
static volatile uint32_t g_fifo_recovery_failure_total;
static volatile uint32_t g_fifo_max_depth_words;
static volatile uint32_t g_fifo_recovery_last_reason;
static volatile uint32_t g_fifo_recovery_last_depth;
static volatile int32_t g_fifo_recovery_last_status_ret;
static volatile uint32_t g_fifo_recovery_last_status_word;
static volatile uint32_t g_fifo_recovery_last_status_valid;
static volatile uint32_t g_fifo_recovery_pre_status_word;
static volatile uint32_t g_fifo_recovery_pre_status_valid;

#define PROBE_BATCH_TRACE_DEPTH 8
struct probe_batch_trace_record
{
  uint32_t session, batch_id, recovery_generation;
  uint64_t status_begin_us, status_end_us, batch_end_us;
  uint64_t previous_batch_end_us, next_batch_begin_us;
  uint32_t depth_words, words_read, complete_samples;
  uint32_t first_sequence, last_sequence;
  uint32_t enqueue_success, enqueue_failed, read_transfers;
  uint64_t read_total_us, read_max_us, batch_elapsed_us;
  int32_t status_ret;
  uint8_t valid, status_valid, has_sequence;
};
#define PROBE_BATCH_ARCHIVE_DEPTH 60
struct probe_batch_archive_record { uint32_t session, batch_id, recovery_generation; uint32_t complete_samples, first_sequence, last_sequence; uint32_t enqueue_success, enqueue_failed; uint64_t batch_end_us; uint8_t valid, has_sequence; };
static struct probe_batch_archive_record g_batch_archive[PROBE_BATCH_ARCHIVE_DEPTH];
static uint32_t g_batch_archive_count;
static uint32_t g_batch_archive_next;
/* Batch summaries are producer-owned records but are read by the consumer
 * and the dump worker.  Keep the short critical sections locked so an event
 * cannot observe a half-written key or summary. */
static pthread_mutex_t g_batch_assoc_lock = PTHREAD_MUTEX_INITIALIZER;
/* Event-owned pending associations.  These are not a second recent-batch
 * ring: each slot names one retained event and is consumed exactly once by
 * the matching batch completion. */
#define PROBE_BATCH_PENDING_DEPTH PROBE_LATENCY_EVENT_DEPTH
struct probe_batch_pending_assoc
{
  uint32_t event_index;
  uint32_t session, batch_id, recovery_generation;
  uint8_t used;
};
static struct probe_batch_pending_assoc g_batch_pending[PROBE_BATCH_PENDING_DEPTH];
static uint32_t g_batch_pending_count;
static uint32_t g_batch_pending_overflow;
static struct probe_batch_trace_record g_batch_trace[PROBE_BATCH_TRACE_DEPTH];
static volatile uint32_t g_batch_trace_count, g_batch_trace_dropped,
                         g_batch_trace_next;

static void probe_batch_trace_add(const struct probe_batch_trace_record *record)
{
  /* The persistent association record is written even while the verbose
   * recent trace is frozen for export.  Export must never make completed
   * batches disappear from event association. */
  pthread_mutex_lock(&g_batch_assoc_lock);
  g_batch_archive[g_batch_archive_next % PROBE_BATCH_ARCHIVE_DEPTH] = (struct probe_batch_archive_record){
    record->session, record->batch_id, record->recovery_generation,
    record->complete_samples, record->first_sequence, record->last_sequence,
    record->enqueue_success, record->enqueue_failed, record->batch_end_us,
    record->valid,
    record->has_sequence};
  g_batch_archive_next++;
  if (g_batch_archive_count < PROBE_BATCH_ARCHIVE_DEPTH)
    g_batch_archive_count++;
  pthread_mutex_unlock(&g_batch_assoc_lock);

  if (__atomic_load_n(&g_latency_dump_active, __ATOMIC_ACQUIRE))
    {
      __atomic_fetch_add(&g_batch_trace_dropped, 1, __ATOMIC_RELAXED);
      return;
    }
  uint32_t next = __atomic_fetch_add(&g_batch_trace_next, 1,
                                     __ATOMIC_RELAXED);
  uint32_t count = __atomic_load_n(&g_batch_trace_count, __ATOMIC_RELAXED);
  g_batch_trace[next % PROBE_BATCH_TRACE_DEPTH] = *record;
  if (count >= PROBE_BATCH_TRACE_DEPTH)
    {
      __atomic_fetch_add(&g_batch_trace_dropped, 1, __ATOMIC_RELAXED);
    }
  else
    {
      __atomic_fetch_add(&g_batch_trace_count, 1, __ATOMIC_RELEASE);
    }
  /* Completed batches have a longer-lived association record than the
   * eight-entry diagnostic ring.  This is written before events can be
   * exported and is independent of FIFO trace retention. */
}

/* Atomically resolve an event against the completed-batch archive, or
 * register it for completion.  The archive lookup and pending insertion must
 * share one critical section: otherwise a producer can commit the batch
 * between two separately locked operations and leave the event PENDING
 * forever. */
static bool probe_batch_associate_or_defer(struct probe_latency_event_record *e,
                                           uint32_t event_index)
{
  uint32_t i;
  bool found = false;
  pthread_mutex_lock(&g_batch_assoc_lock);
  for (i = 0; i < g_batch_archive_count && i < PROBE_BATCH_ARCHIVE_DEPTH; i++)
    {
      uint32_t idx = (g_batch_archive_next + PROBE_BATCH_ARCHIVE_DEPTH -
                      g_batch_archive_count + i) % PROBE_BATCH_ARCHIVE_DEPTH;
      struct probe_batch_archive_record *b = &g_batch_archive[idx];
      if (b->session == e->session && b->batch_id == e->batch_id &&
          b->recovery_generation == e->recovery_generation)
        {
          e->batch_complete_samples = b->complete_samples;
          e->batch_first_sequence = b->first_sequence;
          e->batch_last_sequence = b->last_sequence;
          e->batch_enqueue_success = b->enqueue_success;
          e->batch_enqueue_failed = b->enqueue_failed;
          e->batch_end_us = b->batch_end_us;
          e->batch_complete_valid = b->valid;
          e->batch_assoc_reason = b->valid ? PROBE_BATCH_ASSOC_OK :
                                  PROBE_BATCH_ASSOC_NOT_FOUND;
          if (!b->valid)
            g_batch_association_failures++;
          found = true;
          break;
        }
    }
  if (!found)
    {
      for (i = 0; i < PROBE_BATCH_PENDING_DEPTH; i++)
        if (!g_batch_pending[i].used)
          {
            g_batch_pending[i] = (struct probe_batch_pending_assoc){event_index,
              e->session, e->batch_id, e->recovery_generation, 1};
            g_batch_pending_count++;
            e->batch_assoc_reason = PROBE_BATCH_ASSOC_PENDING;
            pthread_mutex_unlock(&g_batch_assoc_lock);
            return true;
          }
      g_batch_pending_overflow++;
      e->batch_assoc_reason = PROBE_BATCH_ASSOC_EVICTED;
      g_batch_association_failures++;
    }
  pthread_mutex_unlock(&g_batch_assoc_lock);
  return found;
}

static void probe_batch_pending_complete(const struct probe_batch_trace_record *record)
{
  uint32_t i;
  pthread_mutex_lock(&g_batch_assoc_lock);
  for (i = 0; i < PROBE_BATCH_PENDING_DEPTH; i++)
    if (g_batch_pending[i].used &&
        g_batch_pending[i].session == record->session &&
        g_batch_pending[i].batch_id == record->batch_id &&
        g_batch_pending[i].recovery_generation == record->recovery_generation)
      {
        struct probe_latency_event_record *e =
          &g_latency_events[g_batch_pending[i].event_index];
        e->batch_complete_samples = record->complete_samples;
        e->batch_first_sequence = record->first_sequence;
        e->batch_last_sequence = record->last_sequence;
        e->batch_enqueue_success = record->enqueue_success;
        e->batch_enqueue_failed = record->enqueue_failed;
        e->batch_end_us = record->batch_end_us;
        e->batch_complete_valid = record->valid;
        e->batch_assoc_reason = record->valid ? PROBE_BATCH_ASSOC_OK : PROBE_BATCH_ASSOC_NOT_FOUND;
        if (!record->valid)
          g_batch_association_failures++;
        g_batch_pending[i].used = 0;
        if (g_batch_pending_count > 0) g_batch_pending_count--;
      }
  pthread_mutex_unlock(&g_batch_assoc_lock);
}

#if PROBE_RANK_PIPELINE_ENABLED
static pthread_t serial_thread;
static bool g_rank_diag_enabled = BLE_PROBE_ONBOARD_BEAT_DIAG != 0;
#if BLE_PROBE_JOINT_UI_BLE
static pthread_t g_joint_imu_thread;
static pthread_t g_joint_input_thread;
static pthread_t g_joint_ble_cleanup_thread;
static volatile bool g_serial_thread_exited;
static volatile bool g_joint_imu_thread_exited;
static volatile bool g_joint_producer_running;
static volatile bool g_joint_input_running;
static volatile bool g_joint_input_exited;
static volatile bool g_joint_ble_cleanup_exited;
static volatile bool g_joint_stop_requested;
static volatile bool g_joint_stop_in_progress;
static bool g_serial_thread_created;
static bool g_joint_imu_thread_created;
static bool g_joint_input_thread_created;
static bool g_joint_ble_cleanup_thread_created;
static bool g_state_lock_initialized;
static bool g_rx_sem_initialized;
static bool g_ble_on_sem_initialized;
static bool g_adapter_enabled;
static bool g_service_registered;
static bool g_joint_ui_run_valid;
static bool g_joint_capture_started;
static bool g_joint_ready_emitted;
static uint32_t g_joint_run_generation;
static bool g_joint_terminal_saved;
static struct termios g_joint_terminal;
static uint32_t g_joint_stop_timeout_ms = PROBE_STOP_DEFAULT_TIMEOUT_MS;
static uint32_t g_joint_stop_requests;
static uint32_t g_joint_stop_unknown_lines;
static uint32_t g_joint_stop_overlong_lines;
static uint32_t g_joint_stop_sample_drops;
static uint32_t g_joint_stop_event_drops;
static uint32_t g_joint_stop_ui_drops;
static uint64_t g_joint_stop_producer_us;
static uint64_t g_joint_stop_consumer_us;
static uint64_t g_joint_stop_ui_us;
static uint64_t g_joint_stop_communication_us;
static uint64_t g_joint_stop_input_us;
static uint64_t g_joint_stop_ble_us;
static uint32_t g_joint_stop_ble_status;
static uint32_t g_joint_stop_pending_threads;
/* One-shot lifecycle evidence. Bits use probe_stop_step_e values. */
static volatile uint32_t g_joint_stop_seen_steps;
static volatile uint32_t g_joint_stop_exit_begin_steps;
static volatile uint32_t g_joint_stop_exit_done_steps;
static uint64_t g_joint_runtime_started_us;
static struct probe_stop_control g_joint_stop_control;
#define PROBE_STOP_REPORT_MAX 3072
static char g_joint_stop_report[PROBE_STOP_REPORT_MAX];

#define PROBE_BLE_LIFECYCLE_LOG_DEPTH 32
enum probe_ble_lifecycle_stage_e
{
  PROBE_BLE_LC_INSTANCE_CREATE = 1,
  PROBE_BLE_LC_CALLBACK_REGISTER,
  PROBE_BLE_LC_ENABLE_REQUEST,
  PROBE_BLE_LC_ADAPTER_CALLBACK,
  PROBE_BLE_LC_SERVICE_REGISTER,
  PROBE_BLE_LC_ADV_START,
  PROBE_BLE_LC_ADV_STOP,
  PROBE_BLE_LC_DISABLE_REQUEST,
  PROBE_BLE_LC_CALLBACK_UNREGISTER,
  PROBE_BLE_LC_INSTANCE_DELETE,
  PROBE_BLE_LC_STARTUP_TIMEOUT,
};
struct probe_ble_lifecycle_record
{
  uint32_t run;
  uint32_t session;
  uint16_t stage;
  int16_t status;
  int16_t adapter_state;
  uint64_t at_us;
};
static struct probe_ble_lifecycle_record
  g_ble_lifecycle_log[PROBE_BLE_LIFECYCLE_LOG_DEPTH];
static volatile uint32_t g_ble_lifecycle_count;
static volatile uint32_t g_ble_lifecycle_dumped;
#endif
#if BLE_PROBE_SERIAL_ONLY
static pthread_t imu_thread;
static pthread_t beat_thread;
#endif
#define PROBE_SERIAL_QUEUE_DEPTH 64
static struct probe_capture_sample_s g_serial_queue[PROBE_SERIAL_QUEUE_DEPTH];
static volatile uint32_t g_serial_head;
static volatile uint32_t g_serial_tail;
static volatile uint32_t g_serial_queue_full;
static volatile uint32_t g_serial_output;
static volatile uint32_t g_serial_running;
static volatile uint32_t g_serial_session;
static volatile uint32_t g_serial_stop_drops;
static volatile uint32_t g_serial_submit_ok;
static volatile uint32_t g_serial_short_writes;
static volatile uint32_t g_rank_samples_consumed;
static volatile uint32_t g_rank_events;
static volatile uint32_t g_rank_feed_calls;
static volatile uint32_t g_rank_event_written;
static volatile uint32_t g_rank_gap_flags;
static volatile uint64_t g_rank_feed_total_us;
static volatile uint32_t g_rank_feed_max_us;
static volatile uint32_t g_rank_queue_high_water;
static volatile uint32_t g_rank_generated;
static volatile uint32_t g_rank_producer_stats_version;
/* Consumer-owned fallback.  A higher-priority consumer must never spin while
 * the lower-priority producer is inside the producer sequence window. */
static struct probe_rank_counts g_rank_last_consistent_counts;
static volatile uint32_t g_rank_snapshot_fallbacks;
static volatile uint32_t g_rank_sequence_gap;
static volatile uint64_t g_rank_queue_wait_total_us;
static volatile uint32_t g_rank_queue_wait_calls;
static volatile uint32_t g_rank_queue_wait_max_us;
static volatile uint64_t g_rank_feed_elapsed_total_us;
static volatile uint32_t g_rank_feed_elapsed_calls;
static volatile uint32_t g_rank_feed_elapsed_max_us;
static volatile uint64_t g_rank_encode_total_us;
static volatile uint32_t g_rank_encode_calls;
static volatile uint32_t g_rank_encode_max_us;
static volatile uint64_t g_rank_write_total_us;
static volatile uint32_t g_rank_write_calls;
static volatile uint32_t g_rank_write_max_us;
static volatile uint64_t g_rank_ui_submit_total_us;
static volatile uint32_t g_rank_ui_submit_calls;
static volatile uint32_t g_rank_ui_submit_max_us;
static volatile uint64_t g_rank_diag_output_total_us;
static volatile uint32_t g_rank_diag_output_calls;
static volatile uint32_t g_rank_diag_output_max_us;
static uint32_t g_rank_reset_base;
enum probe_sched_mode_e { PROBE_SCHED_A = 0, PROBE_SCHED_B = 1,
                          PROBE_SCHED_C = 2 };
struct probe_sched_result
{
  int query_ret, policy, priority;
};

/* Used by bounded lifecycle diagnostics before the main helper definitions. */
static int probe_get_monotonic_us(uint64_t *out_us);

#if BLE_PROBE_JOINT_UI_BLE
static const char *probe_ble_lifecycle_stage_name(uint16_t stage)
{
  switch (stage)
    {
      case PROBE_BLE_LC_INSTANCE_CREATE: return "instance_create";
      case PROBE_BLE_LC_CALLBACK_REGISTER: return "callback_register";
      case PROBE_BLE_LC_ENABLE_REQUEST: return "enable_request";
      case PROBE_BLE_LC_ADAPTER_CALLBACK: return "adapter_callback";
      case PROBE_BLE_LC_SERVICE_REGISTER: return "service_register";
      case PROBE_BLE_LC_ADV_START: return "adv_start";
      case PROBE_BLE_LC_ADV_STOP: return "adv_stop";
      case PROBE_BLE_LC_DISABLE_REQUEST: return "disable_request";
      case PROBE_BLE_LC_CALLBACK_UNREGISTER: return "callback_unregister";
      case PROBE_BLE_LC_INSTANCE_DELETE: return "instance_delete";
      case PROBE_BLE_LC_STARTUP_TIMEOUT: return "startup_timeout";
      default: return "unknown";
    }
}

static void probe_ble_lifecycle_note(uint16_t stage, int status,
                                     int adapter_state)
{
  uint32_t n;
  uint64_t now_us = 0;

  if (probe_get_monotonic_us(&now_us) < 0)
    {
      now_us = 0;
    }

  n = __atomic_fetch_add(&g_ble_lifecycle_count, 1, __ATOMIC_RELAXED);
  n %= PROBE_BLE_LIFECYCLE_LOG_DEPTH;
  g_ble_lifecycle_log[n].run = g_joint_run_generation;
  g_ble_lifecycle_log[n].session = g_serial_session;
  g_ble_lifecycle_log[n].stage = stage;
  g_ble_lifecycle_log[n].status = (int16_t)status;
  g_ble_lifecycle_log[n].adapter_state = (int16_t)adapter_state;
  g_ble_lifecycle_log[n].at_us = now_us;
}

static void probe_ble_lifecycle_reset(void)
{
  service_trace_reset();
  memset(g_ble_lifecycle_log, 0, sizeof(g_ble_lifecycle_log));
  __atomic_store_n(&g_ble_lifecycle_count, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_ble_lifecycle_dumped, 0, __ATOMIC_RELEASE);
}
#endif

static enum probe_sched_mode_e g_rank_sched_mode = PROBE_SCHED_A;
static int g_rank_sched_setup_ret;
static int g_rank_sched_requested_priority;
static struct probe_sched_result g_rank_sched_consumer;
static struct probe_sched_result g_rank_sched_fifo;
static int g_rank_sched_fifo_requested_priority = -1;
static int g_rank_sched_fifo_setup_ret;
static struct probe_sched_result g_rank_sched_input;
static struct probe_sched_result g_rank_sched_ui;
static struct probe_sched_result g_rank_sched_communication;
static struct probe_rank_interval_log g_rank_interval_log;
static volatile uint32_t g_rank_gen_time_faults;
static volatile uint32_t g_rank_gen_time_dropped;
static uint64_t g_rank_gen_prev_us;
static bool g_rank_gen_prev_valid;
struct rank_gen_fault { uint32_t ps, cs, pb, cb, pc, cc, pw, cw; uint64_t pu, cu, pd, cd; uint16_t pr, cr; uint8_t pf, cf; };
static struct rank_gen_fault g_rank_gen_faults[16];
static volatile uint32_t g_rank_gen_fault_count, g_rank_gen_fault_dumped;
static uint32_t g_rank_gen_prev_seq, g_rank_gen_prev_batch, g_rank_gen_prev_pos;
static uint16_t g_rank_gen_prev_rem, g_rank_gen_prev_cycles;
static uint64_t g_rank_gen_prev_drain;
static uint8_t g_rank_gen_prev_flags;
struct rank_batch_diag { uint32_t id, count, first_seq, last_seq; uint64_t drain_before, first_us, last_us, anchor_us; uint8_t valid; };
static struct rank_batch_diag g_rank_batch_diag;
struct rank_time_fault { uint32_t ps, cs; uint64_t pu, cu; int64_t delta; uint8_t pf, cf; };
static struct rank_time_fault g_rank_faults[16];
static volatile uint32_t g_rank_fault_count, g_rank_fault_dumped;
static volatile uint32_t g_rank_fault_dropped;
static uint32_t g_rank_prev_seq; static uint64_t g_rank_prev_us; static uint8_t g_rank_prev_flags; static bool g_rank_prev_valid;
static sem_t g_serial_sem;
static uint32_t g_beat_count;
#if BLE_PROBE_SERIAL_ONLY
#define PROBE_BEAT_ACK_DEPTH 16
struct probe_beat_ack_s { uint32_t session; uint32_t event; uint8_t status; uint32_t count; };
static struct probe_beat_ack_s g_beat_ack[PROBE_BEAT_ACK_DEPTH];
static uint32_t g_beat_ack_head, g_beat_ack_tail;
#define PROBE_BEAT_RECENT_DEPTH 32
struct probe_time_req_s { uint32_t session, request; uint64_t d2; };
static struct probe_time_req_s g_time_req[8]; static uint32_t g_time_req_head, g_time_req_tail;
static uint32_t g_beat_recent[PROBE_BEAT_RECENT_DEPTH];
static uint32_t g_beat_recent_count, g_beat_recent_next;
static volatile uint32_t g_beat_running;
static struct termios g_serial_termios;
static bool g_serial_termios_saved;
static uint32_t g_beat_errors, g_beat_ack_full;
#endif
static bool g_rank_runtime_started;
#endif

static enum probe_ui_refresh_mode_e g_joint_ui_refresh_mode =
  PROBE_UI_REFRESH_FULL;

#if !BLE_PROBE_CAPTURE_ACK_ONLY
static struct probe_capture_sample_s
  g_capture_queue[PROBE_CAPTURE_QUEUE_DEPTH];
#endif
static uint32_t g_capture_head;
static uint32_t g_capture_tail;
static bool g_capture_active;
static uint32_t g_capture_generation;
static uint32_t g_capture_session_id;
static uint32_t g_capture_generated;
static uint32_t g_capture_enqueued;
static uint32_t g_capture_queue_full;
static uint32_t g_capture_notify_success;
static uint32_t g_capture_notify_failure;
static uint32_t g_capture_last_sequence;
static uint32_t g_capture_gap_samples;
static uint32_t g_capture_fifo_recovery_start;
static uint32_t g_fifo_recovery_total;

static uint8_t g_pong[] = { 'P', 'O', 'N', 'G' };
static uint8_t g_error[] = { 'E', 'R', 'R' };

struct probe_throughput_state_s
{
  bool active;
  bool timing;
  bool have_expected_seq;
  uint32_t test_id;
  uint32_t frames;
  uint32_t payload_bytes;
  uint32_t expected_seq;
  uint32_t missing_frames;
  uint64_t started_us;
};

static struct probe_throughput_state_s g_throughput;

struct probe_reliable39_state_s
{
  bool valid;
  uint32_t connection_generation;
  uint32_t sequence;
  uint8_t response[PROBE_RELIABLE39_FRAME_SIZE];
};

/* This cache is owned exclusively by the communication thread. */

static struct probe_reliable39_state_s g_reliable39;

struct probe_gesture_record_s
{
  uint32_t connection_generation;
  uint16_t packet_id;
  uint8_t event[PROBE_GESTURE_EVENT_SIZE];
  uint8_t send_count;
  uint64_t next_retry_ms;
};

static struct probe_gesture_record_s
  g_gesture_queue[PROBE_GESTURE_QUEUE_DEPTH];

static int probe_parse_startup_uptime(int argc, char *argv[],
                                      uint64_t *target_ms)
{
  const char *cursor;
  const char *timeout_prefix = "--stop-timeout-ms=";
  uint32_t value = 0;
  bool have_uptime = false;
  bool have_ui_mode = false;
  bool have_diag_mode = false;
  bool have_sched_mode = false;
  int i;

  if (argc == 1)
    {
      *target_ms = MIN_BLE_STARTUP_UPTIME_MS;
      return 0;
    }

  *target_ms = MIN_BLE_STARTUP_UPTIME_MS;
  for (i = 1; i < argc; i++)
    {
#if BLE_PROBE_JOINT_UI_BLE
      if (strcmp(argv[i], "--ui-full") == 0 ||
          strcmp(argv[i], "--ui-paused") == 0 ||
          strcmp(argv[i], "--ui-24hz") == 0)
        {
          if (have_ui_mode)
            {
              goto invalid;
            }

          have_ui_mode = true;
          if (strcmp(argv[i], "--ui-paused") == 0)
            {
              g_joint_ui_refresh_mode = PROBE_UI_REFRESH_PAUSED;
            }
          else if (strcmp(argv[i], "--ui-24hz") == 0)
            {
              g_joint_ui_refresh_mode = PROBE_UI_REFRESH_24HZ;
            }
          else
            {
              g_joint_ui_refresh_mode = PROBE_UI_REFRESH_FULL;
            }

          continue;
        }

      if (strncmp(argv[i], timeout_prefix, strlen(timeout_prefix)) == 0)
        {
          uint32_t timeout = 0;

          cursor = argv[i] + strlen(timeout_prefix);
          if (*cursor == '\0')
            {
              goto invalid;
            }

          for (; *cursor != '\0'; cursor++)
            {
              if (*cursor < '0' || *cursor > '9')
                {
                  goto invalid;
                }

              timeout = timeout * 10u + (uint32_t)(*cursor - '0');
              if (timeout > 30000u)
                {
                  goto invalid;
                }
            }

          if (timeout < 1000u)
            {
              goto invalid;
            }

          g_joint_stop_timeout_ms = timeout;
          continue;
        }

      if (strcmp(argv[i], "--diag-on") == 0 ||
          strcmp(argv[i], "--diag-off") == 0)
        {
          if (have_diag_mode)
            {
              goto invalid;
            }

          have_diag_mode = true;
          g_rank_diag_enabled = strcmp(argv[i], "--diag-on") == 0;
          continue;
        }

      if (strcmp(argv[i], "--sched-a") == 0 ||
          strcmp(argv[i], "--sched-b") == 0 ||
          strcmp(argv[i], "--sched-c") == 0 ||
          strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "-b") == 0 ||
          strcmp(argv[i], "-c") == 0)
        {
          if (have_sched_mode) goto invalid;
          have_sched_mode = true;
          g_rank_sched_mode =
            (strcmp(argv[i], "--sched-c") == 0 ||
             strcmp(argv[i], "-c") == 0) ? PROBE_SCHED_C :
            ((strcmp(argv[i], "--sched-b") == 0 ||
              strcmp(argv[i], "-b") == 0) ? PROBE_SCHED_B : PROBE_SCHED_A);
          continue;
        }
#endif

      if (have_uptime || argv[i][0] == '\0')
        {
          goto invalid;
        }

      value = 0;
      for (cursor = argv[i]; *cursor != '\0'; cursor++)
        {
          if (*cursor < '0' || *cursor > '9')
            {
              goto invalid;
            }

          value = value * 10u + (uint32_t)(*cursor - '0');
          if (value > 15000u)
            {
              goto invalid;
            }
        }

      if (value < 5000u || value > 15000u || value % 250u != 0)
        {
          goto invalid;
        }

      *target_ms = value;
      have_uptime = true;
    }

  return 0;

invalid:
  printf("Usage: openvela_ble_probe "
#if BLE_PROBE_JOINT_UI_BLE
         "[--ui-full|--ui-paused|--ui-24hz] "
         "[--diag-on|--diag-off] "
         "[--sched-a|--sched-b|-a|-b] "
         "[--stop-timeout-ms=1000..30000] "
#endif
         "[min_uptime_ms: 5000..15000, step 250]\n");
  return -1;
}

static int probe_wait_for_ble_startup(uint64_t target_ms)
{
  uint64_t now_us;
  uint64_t uptime_ms;
  uint64_t remaining_ms;
  uint64_t sleep_ms;
  bool delay_logged = false;

  for (;;)
    {
#if BLE_PROBE_JOINT_UI_BLE
      if (__atomic_load_n(&g_joint_stop_requested, __ATOMIC_ACQUIRE))
        {
          return 0;
        }
#endif
      if (probe_get_monotonic_us(&now_us) < 0)
        {
          printf("openvela_ble_probe: failed to read system uptime\n");
          return -1;
        }

      uptime_ms = now_us / 1000ULL;
      if (uptime_ms >= target_ms)
        {
          if (!delay_logged)
            {
              printf("openvela_ble_probe: BLE startup target=%llu ms "
                     "already satisfied\n",
                     (unsigned long long)target_ms);
            }

          return 0;
        }

      remaining_ms = target_ms - uptime_ms;
      if (!delay_logged)
        {
          printf("openvela_ble_probe: delaying BLE start %llu ms "
                 "for platform readiness (target=%llu ms)\n",
                 (unsigned long long)remaining_ms,
                 (unsigned long long)target_ms);
          delay_logged = true;
        }

      sleep_ms = remaining_ms > 100u ? 100u : remaining_ms;
      usleep((useconds_t)(sleep_ms * 1000u));
    }
}

static int probe_imu_transfer(int fd, struct i2c_msg_s *messages,
                              size_t count)
{
  struct i2c_transfer_s transfer;

  transfer.msgv = messages;
  transfer.msgc = count;
  return ioctl(fd, I2CIOC_TRANSFER,
               (unsigned long)(uintptr_t)&transfer);
}

static int probe_imu_read(int fd, uint8_t addr, uint8_t reg,
                          uint8_t *value, size_t length)
{
  struct i2c_msg_s messages[2];

  messages[0].frequency = PROBE_IMU_I2C_FREQUENCY;
  messages[0].addr = addr;
  messages[0].flags = I2C_M_NOSTOP;
  messages[0].buffer = &reg;
  messages[0].length = 1;
  messages[1].frequency = PROBE_IMU_I2C_FREQUENCY;
  messages[1].addr = addr;
  messages[1].flags = I2C_M_READ | I2C_M_NOSTART;
  messages[1].buffer = value;
  messages[1].length = length;

  return probe_imu_transfer(fd, messages, 2);
}

static int probe_imu_write_reg(int fd, uint8_t addr, uint8_t reg,
                               uint8_t value)
{
  struct i2c_msg_s message;
  uint8_t data[2] = {reg, value};

  message.frequency = PROBE_IMU_I2C_FREQUENCY;
  message.addr = addr;
  message.flags = 0;
  message.buffer = data;
  message.length = sizeof(data);

  return probe_imu_transfer(fd, &message, 1);
}

static int probe_imu_write_verify(int fd, uint8_t addr, uint8_t reg,
                                  uint8_t value)
{
  uint8_t readback;
  int ret;

  ret = probe_imu_write_reg(fd, addr, reg, value);
  if (ret != 0)
    {
      return ret;
    }

  ret = probe_imu_read(fd, addr, reg, &readback, 1);
  if (ret != 0)
    {
      return ret;
    }

  return readback == value ? 0 : -EIO;
}

static bool probe_detect_imu(void)
{
  static const uint8_t addresses[] = {0x6a, 0x6b};
  uint8_t whoami;
  bool detected = false;
  int fd;
  int ret;
  size_t i;

  fd = open(PROBE_IMU_I2C_DEV, O_RDONLY);
  if (fd < 0)
    {
      printf("openvela_ble_probe: IMU probe open %s failed errno=%d\n",
             PROBE_IMU_I2C_DEV, errno);
      printf("openvela_ble_probe: IMU not detected at 0x6a or 0x6b\n");
      return false;
    }

  for (i = 0; i < sizeof(addresses); i++)
    {
      whoami = 0;
      ret = probe_imu_read(fd, addresses[i], PROBE_IMU_WHO_AM_I_REG,
                           &whoami, 1);
      printf("openvela_ble_probe: IMU probe addr=0x%02x transport=%d"
             " whoami=0x%02x\n",
             addresses[i], ret, whoami);
      if (ret == 0 && whoami == PROBE_IMU_WHO_AM_I_VALUE)
        {
          printf("openvela_ble_probe: IMU detected addr=0x%02x"
                 " whoami=0x6a\n", addresses[i]);
          detected = true;
        }
    }

  close(fd);
  if (!detected)
    {
      printf("openvela_ble_probe: IMU not detected at 0x6a or 0x6b\n");
    }

  return detected;
}

static int probe_get_monotonic_sample(uint64_t *out_us,
                                      struct timespec *raw_time)
{
  struct timespec now;
  uint64_t raw_us;
  uint32_t low_us;
  uint64_t extended_us;
  bool log_enabled = false;
  int error;

  /* The SF32LB52 monotonic source can expose a 32-bit microsecond rollover.
   * Sample and extend that counter under the same short lock so cross-thread
   * ordering cannot join a pre-wrap sample to a post-wrap epoch.  Callers
   * still transfer timestamps through their existing release/acquire SPSC
   * slots rather than sharing mutable 64-bit fields.
   */

  pthread_mutex_lock(&g_monotonic_lock);
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
      error = errno;
      pthread_mutex_unlock(&g_monotonic_lock);
      errno = error;
      return -1;
    }

  raw_us = (uint64_t)now.tv_sec * 1000000ULL +
           (uint64_t)now.tv_nsec / 1000ULL;
  low_us = (uint32_t)raw_us;
  if (!g_monotonic_extended_valid)
    {
      g_monotonic_epoch_us = raw_us & ~UINT64_C(0xffffffff);
      g_monotonic_extended_valid = true;
      log_enabled = true;
    }
  else if (low_us < g_monotonic_last_low_us &&
           g_monotonic_last_low_us - low_us > UINT32_C(0x80000000))
    {
      g_monotonic_epoch_us += UINT64_C(0x100000000);
    }

  extended_us = g_monotonic_epoch_us + low_us;
  if (extended_us < g_monotonic_last_us)
    {
      extended_us = g_monotonic_last_us;
    }

  g_monotonic_last_low_us = low_us;
  g_monotonic_last_us = extended_us;
  *out_us = extended_us;
  if (raw_time != NULL)
    {
      *raw_time = now;
    }

  pthread_mutex_unlock(&g_monotonic_lock);
  if (log_enabled)
    {
      printf("openvela_ble_probe: monotonic 64-bit epoch extension enabled\n");
    }

  return 0;
}

static int probe_get_monotonic_us(uint64_t *out_us)
{
  return probe_get_monotonic_sample(out_us, NULL);
}

static void probe_timespec_add_ms(struct timespec *time, uint32_t ms)
{
  uint64_t nsec = (uint64_t)time->tv_nsec +
                  (uint64_t)ms * 1000000ULL;

  time->tv_sec += (time_t)(nsec / 1000000000ULL);
  time->tv_nsec = (long)(nsec % 1000000000ULL);
}

static void probe_imu_int1_isr(void *arg)
{
  (void)arg;
  __atomic_add_fetch(&g_imu_drdy_irq_count, 1, __ATOMIC_RELAXED);
  sem_post(&g_imu_drdy_sem);
}

static uint16_t probe_imu_fifo_depth(const uint8_t status[4])
{
  return (uint16_t)status[0] | ((uint16_t)(status[1] & 0x07) << 8);
}

static void probe_imu_drain_semaphore(void)
{
  while (sem_trywait(&g_imu_drdy_sem) == 0)
    {
    }
}

/* FIFO worker is the sole producer; the BLE communication pthread is the
 * sole consumer.  This queue is deliberately separate from the Game SPSC.
 */

static bool probe_capture_submit(const struct probe_imu_sample *sample,
                                uint64_t *enqueue_us)
{
#if PROBE_RANK_PIPELINE_ENABLED
  uint32_t head;
  uint32_t tail;
  bool wake_consumer;
  if (!__atomic_load_n(&g_serial_running, __ATOMIC_ACQUIRE))
    {
      __atomic_store_n(&g_serial_submit_ok, 0, __ATOMIC_RELAXED);
      return false;
    }
  head = __atomic_load_n(&g_serial_head, __ATOMIC_RELAXED);
  tail = __atomic_load_n(&g_serial_tail, __ATOMIC_ACQUIRE);
  wake_consumer = head == tail;
  __atomic_fetch_add(&g_rank_producer_stats_version, 1, __ATOMIC_ACQ_REL);
  g_rank_generated++;
  if (head - tail >= PROBE_SERIAL_QUEUE_DEPTH)
    {
      __atomic_fetch_add(&g_serial_queue_full, 1, __ATOMIC_RELAXED);
      __atomic_store_n(&g_serial_submit_ok, 0, __ATOMIC_RELAXED);
      __atomic_fetch_add(&g_rank_producer_stats_version, 1,
                         __ATOMIC_RELEASE);
      return false;
    }
  /* Copy the complete immutable producer context before publishing head.
   * A priority-101 consumer may run immediately after the release store. */
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].sequence = sample->sequence;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].monotonic_us = sample->monotonic_us;
  memcpy(g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].accel, sample->accel,
         sizeof(sample->accel));
  memcpy(g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].gyro, sample->gyro,
         sizeof(sample->gyro));
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].flags = (uint8_t)sample->flags;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].batch_id = sample->batch_id;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].batch_pos = sample->batch_pos;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].batch_complete_cycles = sample->batch_complete_cycles;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].batch_drain_us = sample->batch_drain_us;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].fifo_status_before_us = sample->fifo_status_before_us;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].fifo_status_after_us = sample->fifo_status_after_us;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].pair_complete_us = sample->pair_complete_us;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].estimated_sample_us = sample->estimated_sample_us;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].estimator_sequence = sample->estimator_sequence;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].estimator_period_q16 = sample->estimator_period_q16;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].estimator_slew_us = sample->estimator_slew_us;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].fifo_depth_words = sample->fifo_depth_words;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].remaining_after = sample->remaining_after;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].recovery_generation = sample->recovery_generation;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].producer_context_valid = sample->producer_context_valid;
  g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].uptime_ms =
    (uint32_t)(sample->monotonic_us / 1000ULL);
  if (probe_get_monotonic_us(
        &g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].queued_at_us) < 0)
    {
      g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].queued_at_us = 0;
    }
  if (enqueue_us != NULL)
    {
      *enqueue_us = g_serial_queue[head % PROBE_SERIAL_QUEUE_DEPTH].queued_at_us;
    }
  __atomic_store_n(&g_serial_head, head + 1, __ATOMIC_RELEASE);
  if (head + 1 - tail > g_rank_queue_high_water)
    g_rank_queue_high_water = head + 1 - tail;
  probe_rank_interval_note_depth(&g_rank_interval_log, head + 1 - tail);
  __atomic_fetch_add(&g_rank_producer_stats_version, 1, __ATOMIC_RELEASE);
  __atomic_store_n(&g_serial_submit_ok, 1, __ATOMIC_RELAXED);
  /* A single empty-to-nonempty wake lets the consumer drain without
   * preempting the FIFO worker after every sample. */
  if (wake_consumer)
    sem_post(&g_serial_sem);
  return true;
#elif BLE_PROBE_CAPTURE_ACK_ONLY
  (void)sample;
  if (enqueue_us != NULL) *enqueue_us = 0;
  return false;
#else
  struct probe_capture_sample_s *capture;
  uint32_t generation;
  uint32_t head;
  uint32_t tail;

  if (!__atomic_load_n(&g_capture_active, __ATOMIC_ACQUIRE))
    {
      return false;
    }

  generation = __atomic_load_n(&g_capture_generation, __ATOMIC_RELAXED);
  if (generation !=
      __atomic_load_n(&g_connection_generation, __ATOMIC_ACQUIRE))
    {
      return false;
    }

  __atomic_fetch_add(&g_capture_generated, 1, __ATOMIC_RELAXED);
  __atomic_store_n(&g_capture_last_sequence, sample->sequence,
                   __ATOMIC_RELAXED);
  if ((sample->flags & PROBE_IMU_SAMPLE_FLAG_GAP) != 0)
    {
      __atomic_fetch_add(&g_capture_gap_samples, 1, __ATOMIC_RELAXED);
    }

  head = __atomic_load_n(&g_capture_head, __ATOMIC_RELAXED);
  tail = __atomic_load_n(&g_capture_tail, __ATOMIC_ACQUIRE);
  if (head - tail >= PROBE_CAPTURE_QUEUE_DEPTH)
    {
      __atomic_fetch_add(&g_capture_queue_full, 1, __ATOMIC_RELAXED);
      return false;
    }

  capture = &g_capture_queue[head % PROBE_CAPTURE_QUEUE_DEPTH];
  capture->connection_generation = generation;
  capture->sequence = sample->sequence;
  capture->uptime_ms = (uint32_t)(sample->monotonic_us / 1000ULL);
  memcpy(capture->accel, sample->accel, sizeof(capture->accel));
  capture->flags = (uint8_t)sample->flags;
  if (!__atomic_load_n(&g_capture_active, __ATOMIC_ACQUIRE) ||
      generation !=
        __atomic_load_n(&g_capture_generation, __ATOMIC_RELAXED))
    {
      return false;
    }

  __atomic_store_n(&g_capture_head, head + 1, __ATOMIC_RELEASE);
  __atomic_fetch_add(&g_capture_enqueued, 1, __ATOMIC_RELAXED);
  sem_post(&g_rx_sem);
  if (enqueue_us != NULL) *enqueue_us = 0;
  return true;
#endif
}

#if PROBE_RANK_PIPELINE_ENABLED
static uint16_t probe_serial_crc16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xffff;
  size_t i;
  for (i = 0; i < len; i++)
    {
      int bit;
      crc ^= data[i];
      for (bit = 0; bit < 8; bit++)
        crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
  return crc;
}

#if BLE_PROBE_SERIAL_ONLY
static int probe_serial_parse_beat(const char *line, uint8_t *body,
                                   unsigned *out_len, unsigned *out_crc,
                                   unsigned *hex_ok)
{
  const char *p;
  char *end;
  unsigned len = 0;
  unsigned crc = 0;
  char hex[21] = {0};
  size_t n;
  *hex_ok = 0;
  if (strncmp(line, "@BEAT1,", 7) != 0) return -1;
  p = line + 7;
  len = strtoul(p, &end, 10);
  if (end == p || *end != ',') return -1;
  p = end + 1;
  n = strcspn(p, ",");
  if (n != 20) return -2;
  memcpy(hex, p, 20); hex[20] = 0;
  p += n;
  if (*p != ',') return -1;
  crc = strtoul(p + 1, &end, 16);
  if (end == p + 1 || *end != 0) return -1;
  for (unsigned i = 0; i < 10; i++)
    {
      char pair[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
      char *q; unsigned v = strtoul(pair, &q, 16);
      if (q != pair + 2) return -3;
      body[i] = (uint8_t)v; (*hex_ok)++;
    }
  *out_len = len; *out_crc = crc;
  if (len != 10) return -2;
  if (probe_serial_crc16(body, 10) != (uint16_t)crc) return -4;
  if (body[0] != 1 || body[1] != 1) return -5;
  return 0;
}

static void probe_serial_parse_selftest(void)
{
  static const char v[] = "@BEAT1,10,01013700000001000000,5c6f";
  uint8_t body[10] = {0}; unsigned len = 0, crc = 0, hex_ok = 0;
  int ret = probe_serial_parse_beat(v, body, &len, &crc, &hex_ok);
  uint32_t sid = (uint32_t)body[2] | ((uint32_t)body[3] << 8) |
                 ((uint32_t)body[4] << 16) | ((uint32_t)body[5] << 24);
  uint32_t eid = (uint32_t)body[6] | ((uint32_t)body[7] << 8) |
                 ((uint32_t)body[8] << 16) | ((uint32_t)body[9] << 24);
  printf("BEAT1_PARSE_SELFTEST ret=%d len=%u hex_ok=%u crc=0x%04x rx=0x%04x version=%u event=%u session=%lu event_id=%lu\n",
         ret, len, hex_ok, probe_serial_crc16(body, 10), crc, body[0], body[1],
         (unsigned long)sid, (unsigned long)eid);
}

static void probe_serial_ack(uint8_t status, uint32_t event_id)
{
  uint32_t head = __atomic_load_n(&g_beat_ack_head, __ATOMIC_RELAXED);
  uint32_t tail = __atomic_load_n(&g_beat_ack_tail, __ATOMIC_ACQUIRE);
  if (head - tail >= PROBE_BEAT_ACK_DEPTH)
    { g_beat_ack_full++; return; }
  g_beat_ack[head % PROBE_BEAT_ACK_DEPTH] = (struct probe_beat_ack_s)
    { g_serial_session, event_id, status, g_beat_count };
  __atomic_store_n(&g_beat_ack_head, head + 1, __ATOMIC_RELEASE);
  sem_post(&g_serial_sem);
}

static void *probe_serial_beat_thread(void *arg)
{
  char line[160]; size_t used = 0; struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
  (void)arg;
  while (__atomic_load_n(&g_beat_running, __ATOMIC_ACQUIRE))
    {
      int pr = poll(&pfd, 1, 100);
      if (pr <= 0) continue;
      char ch; ssize_t n = read(STDIN_FILENO, &ch, 1);
      if (n != 1) continue;
      if (ch == '\n' || ch == '\r')
        {
          /* Treat CRLF as one delimiter; do not parse the empty LF line. */
          if (used == 0)
            continue;
          uint8_t body[10]; unsigned len = 0, crc = 0, hex_ok = 0; uint32_t sid, eid;
          line[used] = 0; used = 0;
          if (strncmp(line, "@TSC1,", 6) == 0)
            {
              uint32_t req=0; unsigned rx=0; uint8_t b[4]; uint16_t got;
              if (sscanf(line+6, "%8x,%4hx%n", &req, &got, &rx) == 2 && rx == 13)
                { b[0]=req; b[1]=req>>8; b[2]=req>>16; b[3]=req>>24;
                  if (probe_serial_crc16(b,4) == got && g_time_req_head-g_time_req_tail < 8)
                    { uint64_t d2=0; if (probe_get_monotonic_us(&d2)==0) { g_time_req[g_time_req_head%8]=(struct probe_time_req_s){g_serial_session,req,d2}; __atomic_store_n(&g_time_req_head,g_time_req_head+1,__ATOMIC_RELEASE); sem_post(&g_serial_sem); } }
                }
              continue;
            }
          if (probe_serial_parse_beat(line, body, &len, &crc, &hex_ok) != 0)
            { g_beat_errors++; probe_serial_ack(2, 0); continue; }
          sid = (uint32_t)body[2] | ((uint32_t)body[3] << 8) |
                ((uint32_t)body[4] << 16) | ((uint32_t)body[5] << 24);
          eid = (uint32_t)body[6] | ((uint32_t)body[7] << 8) |
                ((uint32_t)body[8] << 16) | ((uint32_t)body[9] << 24);
          if (sid != g_serial_session) { g_beat_errors++; probe_serial_ack(3, eid); continue; }
          bool duplicate = false;
          for (uint32_t i = 0; i < g_beat_recent_count; i++)
            if (g_beat_recent[i] == eid) duplicate = true;
          if (!duplicate)
            { g_beat_recent[g_beat_recent_next++ % PROBE_BEAT_RECENT_DEPTH] = eid;
              if (g_beat_recent_count < PROBE_BEAT_RECENT_DEPTH) g_beat_recent_count++;
              g_beat_count++; }
          probe_serial_ack(duplicate ? 1 : 0, eid);
        }
      else if (used + 1 < sizeof(line)) line[used++] = ch;
      else { used = 0; g_beat_errors++; probe_serial_ack(2, 0); }
    }
  return NULL;
}

static void probe_serial_restore_terminal(void)
{
  if (g_serial_termios_saved)
    {
      tcsetattr(STDIN_FILENO, TCSANOW, &g_serial_termios);
      g_serial_termios_saved = false;
    }
}
#endif

static const char *probe_sched_mode_name(void)
{
  return g_rank_sched_mode == PROBE_SCHED_C ? "c" :
         (g_rank_sched_mode == PROBE_SCHED_B ? "b" : "a");
}

static void probe_sched_query(pthread_t thread, struct probe_sched_result *out)
{
  struct sched_param param;
  int policy = -1;
  memset(&param, 0, sizeof(param));
  out->query_ret = pthread_getschedparam(thread, &policy, &param);
  out->policy = out->query_ret == 0 ? policy : -1;
  out->priority = out->query_ret == 0 ? param.sched_priority : -1;
}

static void probe_sched_query_ui(struct probe_sched_result *out)
{
  out->policy = -1; out->priority = -1;
  out->query_ret = probe_ui_get_sched(&out->policy, &out->priority);
}

static void probe_rank_counts_snapshot(struct probe_rank_counts *counts)
{
  uint32_t before = 0;
  uint32_t after = 0;
  unsigned int attempt;

  for (attempt = 0; attempt < 3; attempt++)
    {
      before = __atomic_load_n(&g_rank_producer_stats_version,
                               __ATOMIC_ACQUIRE);
      if ((before & 1u) != 0)
        {
          continue;
        }

      counts->generated = __atomic_load_n(&g_rank_generated,
                                          __ATOMIC_RELAXED);
      counts->enqueued = __atomic_load_n(&g_serial_head, __ATOMIC_RELAXED);
      counts->queue_full = __atomic_load_n(&g_serial_queue_full,
                                           __ATOMIC_RELAXED);
      after = __atomic_load_n(&g_rank_producer_stats_version,
                              __ATOMIC_ACQUIRE);
      if (before == after && (after & 1u) == 0)
        {
          goto producer_snapshot_ok;
        }
    }

  /* The producer may have been preempted in its update window.  Return the
   * last snapshot known to be consistent instead of starving that producer.
   * This path affects diagnostics only; it never gates sample consumption. */
  *counts = g_rank_last_consistent_counts;
  __atomic_fetch_add(&g_rank_snapshot_fallbacks, 1, __ATOMIC_RELAXED);
  return;

producer_snapshot_ok:
  counts->consumed = __atomic_load_n(&g_rank_samples_consumed,
                                     __ATOMIC_ACQUIRE);
  counts->gap = __atomic_load_n(&g_rank_gap_flags, __ATOMIC_ACQUIRE);
  counts->sequence_gap = __atomic_load_n(&g_rank_sequence_gap,
                                         __ATOMIC_ACQUIRE);
  counts->hb_calls = __atomic_load_n(&g_rank_feed_elapsed_calls,
                                     __ATOMIC_ACQUIRE);
  counts->events = __atomic_load_n(&g_rank_events, __ATOMIC_ACQUIRE);
  /* Both 64-bit totals are owned by the sole consumer.  This snapshot is
   * taken by that consumer while running, or after it has joined at stop. */
  counts->hb_total_us = g_rank_feed_elapsed_total_us;
  counts->wait_total_us = g_rank_queue_wait_total_us;
  g_rank_last_consistent_counts = *counts;
}

static void probe_fifo_trace_add(uint32_t batch_id, uint32_t sequence,
                                 uint32_t depth_words, uint32_t remaining_after,
                                 uint64_t drain_before_us, uint64_t drain_after_us,
                                 uint64_t complete_us,
                                 uint64_t enqueue_us, uint64_t sample_us,
                                 uint64_t anchor_us, uint32_t estimated_sequence,
                                 uint32_t period_q16, int64_t slew_us,
                                 uint8_t flags, bool enqueued,
                                 uint8_t status_valid, uint64_t batch_end_us)
{
  if (__atomic_load_n(&g_latency_dump_active, __ATOMIC_ACQUIRE))
    {
      __atomic_fetch_add(&g_fifo_trace_dropped, 1, __ATOMIC_RELAXED);
      return;
    }
  uint32_t i = __atomic_fetch_add(&g_fifo_trace_next, 1, __ATOMIC_RELAXED) %
               PROBE_FIFO_TRACE_DEPTH;
  uint32_t valid = __atomic_load_n(&g_fifo_trace_count, __ATOMIC_RELAXED);
  if (valid >= PROBE_FIFO_TRACE_DEPTH)
    {
      __atomic_fetch_add(&g_fifo_trace_dropped, 1, __ATOMIC_RELAXED);
    }
  g_fifo_trace[i] = (struct probe_fifo_trace_record){
    .session = g_serial_session,
    .batch_id = batch_id,
    .recovery_generation = g_fifo_trace_recovery_generation,
    .sequence = sequence,
    .depth_words = depth_words,
    .remaining_after = remaining_after,
    .drain_before_us = drain_before_us,
    .drain_after_us = drain_after_us,
    .complete_us = complete_us,
    .enqueue_us = enqueue_us,
    .sample_us = sample_us,
    .estimated_sample_us = anchor_us,
    .estimated_sequence = estimated_sequence,
    .period_q16 = period_q16,
    .slew_us = slew_us,
    .batch_end_us = batch_end_us,
    .flags = flags,
    .enqueued = enqueued ? 1u : 0u,
    .status_valid = status_valid};
  if (valid < PROBE_FIFO_TRACE_DEPTH)
    __atomic_fetch_add(&g_fifo_trace_count, 1, __ATOMIC_RELEASE);
}

static void probe_latency_dump(void)
{
  uint32_t i;
  /* A full event line includes producer and completed-batch context.  Keep
   * it bounded but large enough that snprintf cannot silently suppress a
   * valid record. */
  char line[1024];
  hb_diag latency_diag;
  struct probe_rank_counts dump_begin_counts;
  struct probe_rank_counts dump_end_counts;
  uint64_t dump_begin_us = 0;
  uint64_t dump_end_us = 0;
  probe_rank_counts_snapshot(&dump_begin_counts);
  uint32_t dump_begin_tail = __atomic_load_n(&g_serial_tail,
                                              __ATOMIC_ACQUIRE);
  (void)probe_get_monotonic_us(&dump_begin_us);
  hb_get_diag(&latency_diag);
  /* Freeze only the short metadata capture window.  Formatting and UART
   * output happen after the flag is released, so the consumer keeps
   * draining samples while a slow host reads the dump. */
  __atomic_store_n(&g_latency_dump_active, true, __ATOMIC_RELEASE);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  (void)snprintf(line, sizeof(line),
    "BTE1 LATENCY_DUMP_BEGIN at_us=%llu generated=%lu enqueued=%lu consumed=%lu queue_full=%lu depth=%lu snapshot_fallbacks=%lu\n",
    (unsigned long long)dump_begin_us,
    (unsigned long)dump_begin_counts.generated,
    (unsigned long)dump_begin_counts.enqueued,
    (unsigned long)dump_begin_counts.consumed,
    (unsigned long)dump_begin_counts.queue_full,
    (unsigned long)(dump_begin_counts.enqueued - dump_begin_tail),
    (unsigned long)__atomic_load_n(&g_rank_snapshot_fallbacks, __ATOMIC_ACQUIRE));
  pthread_mutex_lock(&g_uart_write_lock);
  (void)write(STDOUT_FILENO, line, strlen(line));
  pthread_mutex_unlock(&g_uart_write_lock);
  __atomic_store_n(&g_latency_dump_active, false, __ATOMIC_RELEASE);
  g_batch_association_failures = 0;
  for (i = 0; i < g_latency_event_count; i++)
    {
      struct probe_latency_event_record *r = &g_latency_events[i];
      /* Association is immutable event-owned state.  Export never performs
       * a late lookup in the bounded archive, so coverage and UART timing
       * cannot change a previously resolved event. */
      int64_t a = (int64_t)r->confirmation_sample_us - (int64_t)r->beat_us;
      int64_t s = (int64_t)r->enqueue_us - (int64_t)r->confirmation_sample_us;
      int64_t q = (int64_t)r->hb_begin_us - (int64_t)r->enqueue_us;
      int64_t f = (int64_t)r->detected_us - (int64_t)r->hb_begin_us;
      int64_t sum = a + s + q + f;
      int n = snprintf(line, sizeof(line),
        "BTE1 LATENCY session=%lu event=%lu seq=%lu batch=%lu recovery=%lu beat_us=%llu confirmation_sample_us=%llu enqueue_us=%llu hb_begin_us=%llu hb_end_us=%llu detected_us=%llu algorithm_span_us=%lld sample_age_at_enqueue_us=%lld queue_wait_us=%lld feed_elapsed_us=%lld sum_us=%lld total_us=%lld producer_valid=%u fifo_before_us=%llu fifo_after_us=%llu pair_complete_us=%llu depth_words=%lu remaining_after=%lu estimator_sequence=%lu period_q16=%lu slew_us=%lld batch_complete_valid=%u batch_assoc_reason=%u batch_assoc_reason_name=%s batch_complete_samples=%lu batch_first_seq=%lu batch_last_seq=%lu batch_enqueue_ok=%lu batch_enqueue_failed=%lu batch_end_us=%llu\n",
        (unsigned long)r->session, (unsigned long)r->event_id,
        (unsigned long)r->source_sequence,
        (unsigned long)r->batch_id,
        (unsigned long)r->recovery_generation,
        (unsigned long long)r->beat_us,
        (unsigned long long)r->confirmation_sample_us,
        (unsigned long long)r->enqueue_us,
        (unsigned long long)r->hb_begin_us,
        (unsigned long long)r->hb_end_us,
        (unsigned long long)r->detected_us,
        (long long)a, (long long)s, (long long)q, (long long)f,
        (long long)sum,
        (long long)((int64_t)r->detected_us - (int64_t)r->beat_us),
        (unsigned)r->producer_context_valid,
        (unsigned long long)r->fifo_status_before_us,
        (unsigned long long)r->fifo_status_after_us,
        (unsigned long long)r->pair_complete_us,
        (unsigned long)r->fifo_depth_words,
        (unsigned long)r->remaining_after,
        (unsigned long)r->estimator_sequence,
        (unsigned long)r->estimator_period_q16,
        (long long)r->estimator_slew_us,
        (unsigned)r->batch_complete_valid,
        (unsigned)r->batch_assoc_reason,
        probe_batch_assoc_reason_name(r->batch_assoc_reason),
        (unsigned long)r->batch_complete_samples,
        (unsigned long)r->batch_first_sequence,
        (unsigned long)r->batch_last_sequence,
        (unsigned long)r->batch_enqueue_success,
        (unsigned long)r->batch_enqueue_failed,
        (unsigned long long)r->batch_end_us);
      if (n > 0 && (size_t)n < sizeof(line))
        { pthread_mutex_lock(&g_uart_write_lock); (void)write(STDOUT_FILENO, line, (size_t)n); pthread_mutex_unlock(&g_uart_write_lock); }
    }
  (void)snprintf(line, sizeof(line), "BTE1 LATENCY_SUMMARY records=%lu dropped=%lu hb_feed_calls=%lu evaluations=%lu candidates=%lu gate_accepted=%lu event_generated=%lu event_written=%lu diagnostic_saved=%lu GAP=%lu sequence_gap=%lu reset_gap=%lu reset_sequence=%lu reset_time=%lu reset_session=%lu recovery_generation=%lu fifo_overrun=%lu recovery_attempts=%lu recovery_success=%lu recovery_failure=%lu max_fifo_depth_words=%lu recovery_last_reason=%lu recovery_last_depth=%lu recovery_last_status_ret=%ld recovery_last_status_word=0x%08lx recovery_last_status_valid=%lu recovery_pre_status_word=0x%08lx recovery_pre_status_valid=%lu\n",
                 (unsigned long)g_latency_event_count,
                 (unsigned long)g_latency_event_dropped,
                 (unsigned long)g_rank_feed_calls,
                 (unsigned long)latency_diag.evaluations,
                 (unsigned long)latency_diag.candidates,
                 (unsigned long)g_rank_events,
                 (unsigned long)g_rank_events,
                 (unsigned long)g_rank_event_written,
                 (unsigned long)g_latency_event_count,
                 (unsigned long)g_rank_gap_flags,
                 (unsigned long)g_rank_sequence_gap,
                 (unsigned long)latency_diag.reset_gap,
                 (unsigned long)latency_diag.reset_sequence,
                 (unsigned long)latency_diag.reset_time,
                 (unsigned long)latency_diag.reset_session,
                 (unsigned long)g_fifo_trace_recovery_generation,
                 (unsigned long)__atomic_load_n(&g_fifo_overrun_total, __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_fifo_recovery_attempts_total, __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_fifo_recovery_success_total, __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_fifo_recovery_failure_total, __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_fifo_max_depth_words, __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_fifo_recovery_last_reason, __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_fifo_recovery_last_depth, __ATOMIC_ACQUIRE),
                 (long)__atomic_load_n(&g_fifo_recovery_last_status_ret, __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_fifo_recovery_last_status_word, __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_fifo_recovery_last_status_valid, __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_fifo_recovery_pre_status_word, __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_fifo_recovery_pre_status_valid, __ATOMIC_ACQUIRE));
  pthread_mutex_lock(&g_uart_write_lock); (void)write(STDOUT_FILENO, line, strlen(line)); pthread_mutex_unlock(&g_uart_write_lock);

  (void)snprintf(line, sizeof(line),
                 "BTE1 BATCH_ASSOC_SUMMARY failures=%lu archive_records=%lu pending=%lu pending_overflow=%lu\n",
                 (unsigned long)g_batch_association_failures,
                 (unsigned long)g_batch_archive_count,
                 (unsigned long)g_batch_pending_count,
                 (unsigned long)g_batch_pending_overflow);
  pthread_mutex_lock(&g_uart_write_lock); (void)write(STDOUT_FILENO, line, strlen(line)); pthread_mutex_unlock(&g_uart_write_lock);
  {
    uint32_t t = __atomic_load_n(&g_serial_tail, __ATOMIC_ACQUIRE);
    probe_rank_counts_snapshot(&dump_end_counts);
    (void)snprintf(line, sizeof(line),
      "BTE1 LATENCY_COUNTS generated=%lu enqueued=%lu consumed=%lu queue_full=%lu depth=%lu high_water=%lu\n",
      (unsigned long)dump_end_counts.generated,
      (unsigned long)dump_end_counts.enqueued,
      (unsigned long)dump_end_counts.consumed,
      (unsigned long)dump_end_counts.queue_full,
      (unsigned long)(dump_end_counts.enqueued - t),
      (unsigned long)g_rank_queue_high_water);
    pthread_mutex_lock(&g_uart_write_lock); (void)write(STDOUT_FILENO, line, strlen(line)); pthread_mutex_unlock(&g_uart_write_lock);
  }

  if (__atomic_load_n(&g_latency_dump_full, __ATOMIC_ACQUIRE))
  {
    uint32_t n = __atomic_load_n(&g_fifo_trace_count, __ATOMIC_ACQUIRE);
    uint32_t lim = n < PROBE_FIFO_TRACE_DEPTH ? n : PROBE_FIFO_TRACE_DEPTH;
    uint32_t next = __atomic_load_n(&g_fifo_trace_next, __ATOMIC_ACQUIRE);
    uint32_t j;
    for (j = 0; j < lim; j++)
      {
        const struct probe_fifo_trace_record *r =
          &g_fifo_trace[((n >= PROBE_FIFO_TRACE_DEPTH ? next : 0) + j) %
                        PROBE_FIFO_TRACE_DEPTH];
        int m = snprintf(line, sizeof(line),
          "BTE1 FIFO_TRACE session=%lu batch=%lu recovery=%lu seq=%lu depth_words=%lu remaining_after=%lu status_begin_us=%llu status_end_us=%llu pair_complete_us=%llu enqueue_us=%llu sample_us=%llu estimated_sequence=%lu estimated_sample_us=%llu period_q16=%lu slew_us=%lld flags=%u status_valid=%u enqueued=%u\n",
          (unsigned long)r->session, (unsigned long)r->batch_id,
          (unsigned long)r->recovery_generation, (unsigned long)r->sequence,
          (unsigned long)r->depth_words, (unsigned long)r->remaining_after,
          (unsigned long long)r->drain_before_us,
          (unsigned long long)r->drain_after_us,
          (unsigned long long)r->complete_us,
          (unsigned long long)r->enqueue_us,
          (unsigned long long)r->sample_us,
          (unsigned long)r->estimated_sequence,
          (unsigned long long)r->estimated_sample_us,
          (unsigned long)r->period_q16, (long long)r->slew_us,
          (unsigned)r->flags, (unsigned)r->status_valid,
          (unsigned)r->enqueued);
        if (m > 0 && (size_t)m < sizeof(line))
          (void)write(STDOUT_FILENO, line, (size_t)m);
      }
    (void)snprintf(line, sizeof(line),
      "BTE1 FIFO_TRACE_SUMMARY records=%lu dropped=%lu recovery_generation=%lu\n",
      (unsigned long)lim, (unsigned long)__atomic_load_n(&g_fifo_trace_dropped, __ATOMIC_ACQUIRE),
      (unsigned long)g_fifo_trace_recovery_generation);
    pthread_mutex_lock(&g_uart_write_lock); (void)write(STDOUT_FILENO, line, strlen(line)); pthread_mutex_unlock(&g_uart_write_lock);

    {
      uint32_t bn = __atomic_load_n(&g_batch_trace_count, __ATOMIC_ACQUIRE);
      uint32_t blim = bn < PROBE_BATCH_TRACE_DEPTH ? bn : PROBE_BATCH_TRACE_DEPTH;
      uint32_t bnext = __atomic_load_n(&g_batch_trace_next, __ATOMIC_ACQUIRE);
      uint32_t bj;
      for (bj = 0; bj < blim; bj++)
        {
          const struct probe_batch_trace_record *b =
            &g_batch_trace[((bn >= PROBE_BATCH_TRACE_DEPTH ? bnext : 0) + bj) %
                           PROBE_BATCH_TRACE_DEPTH];
          int bm = snprintf(line, sizeof(line),
            "BTE1 BATCH_TRACE session=%lu batch=%lu recovery=%lu valid=%u status_valid=%u status_ret=%ld status_begin_us=%llu status_end_us=%llu batch_end_us=%llu depth_words=%lu words_read=%lu complete_samples=%lu first_seq=%lu last_seq=%lu enqueue_ok=%lu enqueue_failed=%lu read_transfers=%lu read_total_us=%llu read_max_us=%llu batch_elapsed_us=%llu prev_batch_end_us=%llu next_batch_begin_us=%llu has_sequence=%u\n",
            (unsigned long)b->session, (unsigned long)b->batch_id,
            (unsigned long)b->recovery_generation, (unsigned)b->valid,
            (unsigned)b->status_valid, (long)b->status_ret,
            (unsigned long long)b->status_begin_us,
            (unsigned long long)b->status_end_us,
            (unsigned long long)b->batch_end_us,
            (unsigned long)b->depth_words, (unsigned long)b->words_read,
            (unsigned long)b->complete_samples,
            (unsigned long)b->first_sequence, (unsigned long)b->last_sequence,
            (unsigned long)b->enqueue_success,
            (unsigned long)b->enqueue_failed,
            (unsigned long)b->read_transfers,
            (unsigned long long)b->read_total_us,
            (unsigned long long)b->read_max_us,
            (unsigned long long)b->batch_elapsed_us,
            (unsigned long long)b->previous_batch_end_us,
            (unsigned long long)b->next_batch_begin_us,
            (unsigned)b->has_sequence);
          if (bm > 0 && (size_t)bm < sizeof(line))
            { pthread_mutex_lock(&g_uart_write_lock); (void)write(STDOUT_FILENO, line, (size_t)bm); pthread_mutex_unlock(&g_uart_write_lock); }
        }
      (void)snprintf(line, sizeof(line),
        "BTE1 BATCH_TRACE_SUMMARY records=%lu dropped=%lu\n",
        (unsigned long)blim,
        (unsigned long)__atomic_load_n(&g_batch_trace_dropped, __ATOMIC_ACQUIRE));
      pthread_mutex_lock(&g_uart_write_lock); (void)write(STDOUT_FILENO, line, strlen(line)); pthread_mutex_unlock(&g_uart_write_lock);
    }

    (void)snprintf(line, sizeof(line),
      "BTE1 BATCH_ASSOC_SUMMARY failures=%lu\n",
      (unsigned long)g_batch_association_failures);
      pthread_mutex_lock(&g_uart_write_lock); (void)write(STDOUT_FILENO, line, strlen(line)); pthread_mutex_unlock(&g_uart_write_lock);

    {
      uint32_t h = __atomic_load_n(&g_serial_head, __ATOMIC_ACQUIRE);
      uint32_t t = __atomic_load_n(&g_serial_tail, __ATOMIC_ACQUIRE);
      int m = snprintf(line, sizeof(line),
        "BTE1 LATENCY_COUNTS generated=%lu enqueued=%lu consumed=%lu queue_full=%lu depth=%lu high_water=%lu GAP=%lu sequence_gap=%lu fifo_overrun=%lu fifo_recovery=%lu trace_records=%lu trace_dropped=%lu\n",
        (unsigned long)__atomic_load_n(&g_rank_generated, __ATOMIC_ACQUIRE),
        (unsigned long)h,
        (unsigned long)__atomic_load_n(&g_rank_samples_consumed, __ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&g_serial_queue_full, __ATOMIC_ACQUIRE),
        (unsigned long)(h - t), (unsigned long)g_rank_queue_high_water,
        (unsigned long)g_rank_gap_flags, (unsigned long)g_rank_sequence_gap,
        (unsigned long)g_fifo_overrun_total,
        (unsigned long)__atomic_load_n(&g_fifo_recovery_total, __ATOMIC_ACQUIRE),
        (unsigned long)lim,
        (unsigned long)__atomic_load_n(&g_fifo_trace_dropped, __ATOMIC_ACQUIRE));
      if (m > 0 && (size_t)m < sizeof(line))
        { pthread_mutex_lock(&g_uart_write_lock); (void)write(STDOUT_FILENO, line, (size_t)m); pthread_mutex_unlock(&g_uart_write_lock); }
    }
  }
  __atomic_store_n(&g_latency_dump_active, false, __ATOMIC_RELEASE);
  (void)probe_get_monotonic_us(&dump_end_us);
  {
    probe_rank_counts_snapshot(&dump_end_counts);
    uint32_t end_generated = dump_end_counts.generated;
    uint32_t end_enqueued = dump_end_counts.enqueued;
    uint32_t end_consumed = dump_end_counts.consumed;
    uint32_t end_queue_full = dump_end_counts.queue_full;
    uint32_t end_tail = __atomic_load_n(&g_serial_tail, __ATOMIC_ACQUIRE);
    (void)snprintf(line, sizeof(line),
      "BTE1 LATENCY_DUMP_END at_us=%llu generated=%lu enqueued=%lu consumed=%lu queue_full=%lu depth=%lu delta_generated=%ld delta_enqueued=%ld delta_consumed=%ld delta_queue_full=%ld export_elapsed_us=%llu snapshot_fallbacks=%lu\n",
      (unsigned long long)dump_end_us,
      (unsigned long)end_generated, (unsigned long)end_enqueued,
      (unsigned long)end_consumed, (unsigned long)end_queue_full,
      (unsigned long)(end_enqueued - end_tail),
      (long)(end_generated - dump_begin_counts.generated),
      (long)(end_enqueued - dump_begin_counts.enqueued),
      (long)(end_consumed - dump_begin_counts.consumed),
      (long)(end_queue_full - dump_begin_counts.queue_full),
      (unsigned long long)(dump_end_us >= dump_begin_us ?
                           dump_end_us - dump_begin_us : 0),
      (unsigned long)__atomic_load_n(&g_rank_snapshot_fallbacks, __ATOMIC_ACQUIRE));
      /* snapshot_fallbacks is reported separately from the counter deltas;
       * a fallback is a diagnostic validity limitation, not a sample event. */
    pthread_mutex_lock(&g_uart_write_lock); (void)write(STDOUT_FILENO, line, strlen(line)); pthread_mutex_unlock(&g_uart_write_lock);
  }
}

static void *probe_latency_dump_thread(void *arg)
{
  (void)arg;
  while (__atomic_load_n(&g_latency_dump_thread_running,
                         __ATOMIC_ACQUIRE))
    {
      if (sem_wait(&g_latency_dump_sem) < 0)
        continue;
      if (!__atomic_load_n(&g_latency_dump_thread_running,
                           __ATOMIC_ACQUIRE))
        break;
      if (__atomic_exchange_n(&g_latency_dump_requested, false,
                              __ATOMIC_ACQ_REL))
        probe_latency_dump();
    }
  return NULL;
}

static void *probe_serial_output_thread(void *arg)
{
  struct probe_capture_sample_s sample;
  uint32_t tail;
  uint8_t body[32];
  char hex[55];
  char line[80];
  uint16_t body_len;
  uint16_t crc;
  uint64_t diag_next = 0;
  (void)arg;
  probe_sched_query(pthread_self(), &g_rank_sched_consumer);
#if BLE_PROBE_JOINT_UI_BLE
  __atomic_store_n(&g_serial_thread_exited, false, __ATOMIC_RELEASE);
#endif
  while (__atomic_load_n(&g_serial_running, __ATOMIC_ACQUIRE) ||
         __atomic_load_n(&g_serial_tail, __ATOMIC_RELAXED) !=
         __atomic_load_n(&g_serial_head, __ATOMIC_ACQUIRE)
#if BLE_PROBE_JOINT_UI_BLE
         /* During cooperative stop the producer may have observed the stop
          * flag but still be finishing its current FIFO transaction.  Keep
          * the consumer alive until producer exit is published, otherwise a
          * final sample can remain pending and drain_complete is false. */
         || !__atomic_load_n(&g_joint_imu_thread_exited, __ATOMIC_ACQUIRE)
#endif
#if BLE_PROBE_SERIAL_ONLY
         ||
         __atomic_load_n(&g_beat_ack_tail, __ATOMIC_RELAXED) !=
         __atomic_load_n(&g_beat_ack_head, __ATOMIC_ACQUIRE)
#if !BLE_PROBE_ONBOARD_BEAT
         ||
         __atomic_load_n(&g_time_req_tail, __ATOMIC_RELAXED) !=
         __atomic_load_n(&g_time_req_head, __ATOMIC_ACQUIRE))
#else
         )
#endif
#else
         )
#endif
    {
      if (__atomic_load_n(&g_serial_tail, __ATOMIC_RELAXED) ==
          __atomic_load_n(&g_serial_head, __ATOMIC_ACQUIRE))
        {
          if (sem_wait(&g_serial_sem) < 0)
            continue;
        }
#if BLE_PROBE_SERIAL_ONLY
      uint32_t at = __atomic_load_n(&g_beat_ack_tail, __ATOMIC_RELAXED);
      if (at != __atomic_load_n(&g_beat_ack_head, __ATOMIC_ACQUIRE))
        {
          struct probe_beat_ack_s a = g_beat_ack[at % PROBE_BEAT_ACK_DEPTH];
          char ack[128]; int n = snprintf(ack, sizeof(ack),
            "@BEAT1_ACK,%lu,%lu,%u,%lu\n", (unsigned long)a.session,
            (unsigned long)a.event, a.status, (unsigned long)a.count);
          if (n > 0 && n < (int)sizeof(ack) && write(STDOUT_FILENO, ack, n) != n)
            __atomic_fetch_add(&g_serial_short_writes, 1, __ATOMIC_RELAXED);
          __atomic_store_n(&g_beat_ack_tail, at + 1, __ATOMIC_RELEASE);
          continue;
        }
#if !BLE_PROBE_ONBOARD_BEAT
      uint32_t tt=__atomic_load_n(&g_time_req_tail,__ATOMIC_RELAXED);
      if (tt != __atomic_load_n(&g_time_req_head,__ATOMIC_ACQUIRE))
        { struct probe_time_req_s q=g_time_req[tt%8]; uint64_t d3=0; probe_get_monotonic_us(&d3); char z[128]; int n=snprintf(z,sizeof(z),"@TSC1_ACK,%lu,%lu,%llu,%llu\n",(unsigned long)q.session,(unsigned long)q.request,(unsigned long long)q.d2,(unsigned long long)d3); if(n>0&&n<(int)sizeof(z)&&write(STDOUT_FILENO,z,n)!=n)__atomic_fetch_add(&g_serial_short_writes,1,__ATOMIC_RELAXED); __atomic_store_n(&g_time_req_tail,tt+1,__ATOMIC_RELEASE); continue; }
#endif
#endif
      tail = __atomic_load_n(&g_serial_tail, __ATOMIC_RELAXED);
      if (tail == __atomic_load_n(&g_serial_head, __ATOMIC_ACQUIRE))
        continue;
      memcpy(&sample, &g_serial_queue[tail % PROBE_SERIAL_QUEUE_DEPTH],
             sizeof(sample));
      __atomic_store_n(&g_serial_tail, tail + 1, __ATOMIC_RELEASE);
#if PROBE_RANK_PIPELINE_ENABLED
      {
        hb_sample hs = {0}; hb_event he; char event_line[96]; size_t n;
        uint64_t elapsed_start_us;
        uint64_t elapsed_end_us;
        uint32_t elapsed_us;
        struct probe_ui_stats ui_stats;
        bool ui_submitted;
        memcpy(hs.accel, sample.accel, sizeof(hs.accel));
        memcpy(hs.gyro, sample.gyro, sizeof(hs.gyro));
        hs.session = g_serial_session; hs.sequence = sample.sequence;
        hs.monotonic_us = sample.monotonic_us;
        hs.flags = sample.flags;
        if (sample.queued_at_us != 0 &&
            probe_get_monotonic_us(&elapsed_end_us) == 0 &&
            elapsed_end_us >= sample.queued_at_us)
          {
            elapsed_us = (uint32_t)(elapsed_end_us - sample.queued_at_us);
            g_rank_queue_wait_calls++;
            g_rank_queue_wait_total_us += elapsed_us;
            if (elapsed_us > g_rank_queue_wait_max_us)
              {
                g_rank_queue_wait_max_us = elapsed_us;
              }
            probe_rank_interval_note_wait(&g_rank_interval_log, elapsed_us);
          }
        __atomic_fetch_add(&g_rank_samples_consumed, 1, __ATOMIC_RELAXED);
        if (g_rank_prev_valid && sample.monotonic_us < g_rank_prev_us)
          { uint32_t i = g_rank_fault_count; if (i < 16) g_rank_faults[i] = (struct rank_time_fault){g_rank_prev_seq,sample.sequence,g_rank_prev_us,sample.monotonic_us,(int64_t)sample.monotonic_us-(int64_t)g_rank_prev_us,g_rank_prev_flags,sample.flags}; else g_rank_fault_dropped++; if (i < 16) g_rank_fault_count++; }
        if (g_rank_prev_valid && (uint32_t)(sample.sequence - g_rank_prev_seq) != 1u)
          g_rank_sequence_gap++;
        g_rank_prev_seq = sample.sequence; g_rank_prev_us = sample.monotonic_us; g_rank_prev_flags = sample.flags; g_rank_prev_valid = true;
        if (sample.flags & PROBE_IMU_SAMPLE_FLAG_GAP) g_rank_gap_flags++;
        uint64_t ft0 = (uint64_t)clock_systime_ticks();
        elapsed_start_us = 0;
        elapsed_end_us = 0;
        (void)probe_get_monotonic_us(&elapsed_start_us);
        int hb_result = hb_feed(&hs, &he);
        (void)probe_get_monotonic_us(&elapsed_end_us);
        if (elapsed_start_us != 0 && elapsed_end_us >= elapsed_start_us)
          {
            elapsed_us = (uint32_t)(elapsed_end_us - elapsed_start_us);
            g_rank_feed_elapsed_calls++;
            g_rank_feed_elapsed_total_us += elapsed_us;
            if (elapsed_us > g_rank_feed_elapsed_max_us)
              {
                g_rank_feed_elapsed_max_us = elapsed_us;
              }
            probe_rank_interval_note_hb(&g_rank_interval_log, elapsed_us);
          }
        uint32_t fdu = (uint32_t)((uint64_t)clock_systime_ticks() - ft0);
        g_rank_feed_calls++; g_rank_feed_total_us += fdu;
        if (fdu > g_rank_feed_max_us) g_rank_feed_max_us = fdu;
        if (hb_result == 1)
          {
            struct probe_ui_beat_event ui_event;
            uint32_t event_id;
            __atomic_fetch_add(&g_rank_events, 1, __ATOMIC_RELAXED);
            uint64_t detected_us;
            if (probe_get_monotonic_us(&detected_us) < 0)
              detected_us = hs.monotonic_us;
            event_id = ++g_beat_count;
            if (g_latency_event_count < PROBE_LATENCY_EVENT_DEPTH)
              {
                struct probe_latency_event_record *lr =
                  &g_latency_events[g_latency_event_count++];
                lr->session = g_serial_session;
                lr->event_id = event_id;
                lr->source_sequence = he.source_sequence;
                lr->beat_us = he.beat_time_us;
                lr->confirmation_sample_us = he.confirmation_sample_us;
                lr->enqueue_us = sample.queued_at_us;
                lr->hb_begin_us = elapsed_start_us;
                lr->hb_end_us = elapsed_end_us;
                lr->detected_us = detected_us;
                lr->nominal_center_us =
                  (uint64_t)(he.nominal_center_s * 1000000.0);
                lr->nominal_confirmation_us =
                  (uint64_t)(he.nominal_confirmation_s * 1000000.0);
                lr->nominal_delta_us = (int64_t)
                  ((he.nominal_confirmation_s - he.nominal_center_s) * 1000000.0);
                lr->batch_id = sample.batch_id;
                lr->recovery_generation = sample.recovery_generation;
                lr->remaining_after = sample.remaining_after;
                lr->fifo_depth_words = sample.fifo_depth_words;
                lr->fifo_status_before_us = sample.fifo_status_before_us;
                lr->fifo_status_after_us = sample.fifo_status_after_us;
                lr->pair_complete_us = sample.pair_complete_us;
                lr->estimated_sample_us = sample.estimated_sample_us;
                lr->estimator_sequence = sample.estimator_sequence;
                lr->estimator_period_q16 = sample.estimator_period_q16;
                lr->estimator_slew_us = sample.estimator_slew_us;
                lr->producer_context_valid = sample.producer_context_valid;
                /* Resolve or defer atomically.  A separate archive lookup
                 * followed by pending registration has a race with the
                 * producer committing the batch between those operations. */
                (void)probe_batch_associate_or_defer(lr,
                                                     g_latency_event_count - 1);
              }
            else
              {
                g_latency_event_dropped++;
              }
            elapsed_start_us = detected_us;
            n = hb_encode_event(&he, g_serial_session, event_id,
                                detected_us, event_line, sizeof(event_line));
            (void)probe_get_monotonic_us(&elapsed_end_us);
            if (elapsed_end_us >= elapsed_start_us)
              {
                elapsed_us = (uint32_t)(elapsed_end_us - elapsed_start_us);
                g_rank_encode_calls++;
                g_rank_encode_total_us += elapsed_us;
                if (elapsed_us > g_rank_encode_max_us)
                  {
                    g_rank_encode_max_us = elapsed_us;
                  }
              }
            elapsed_start_us = elapsed_end_us;
            if (n > 0)
              {
                ssize_t written = write(STDOUT_FILENO, event_line, n);
                (void)probe_get_monotonic_us(&elapsed_end_us);
                if (elapsed_start_us != 0 && elapsed_end_us >= elapsed_start_us)
                  {
                    elapsed_us = (uint32_t)(elapsed_end_us - elapsed_start_us);
                    g_rank_write_calls++;
                    g_rank_write_total_us += elapsed_us;
                    if (elapsed_us > g_rank_write_max_us)
                      {
                        g_rank_write_max_us = elapsed_us;
                      }
                  }
                if (written != (ssize_t)n)
                  {
                    __atomic_fetch_add(&g_serial_short_writes, 1,
                                       __ATOMIC_RELAXED);
                  }
                else
                  {
                    g_rank_event_written++;
                  }
              }
#if BLE_PROBE_JOINT_UI_BLE
            memset(&ui_event, 0, sizeof(ui_event));
            ui_event.beat_count = event_id;
            ui_event.source_sequence = he.source_sequence;
            ui_event.beat_time_us = he.beat_time_us;
            ui_event.imu_samples = g_rank_samples_consumed;
            ui_event.imu_gaps = g_rank_gap_flags;
            elapsed_start_us = 0;
            (void)probe_get_monotonic_us(&elapsed_start_us);
            ui_submitted = probe_ui_submit_beat(&ui_event);
            (void)ui_submitted;
            (void)probe_get_monotonic_us(&elapsed_end_us);
            if (elapsed_start_us != 0 && elapsed_end_us >= elapsed_start_us)
              {
                elapsed_us = (uint32_t)(elapsed_end_us - elapsed_start_us);
                g_rank_ui_submit_calls++;
                g_rank_ui_submit_total_us += elapsed_us;
                if (elapsed_us > g_rank_ui_submit_max_us)
                  {
                    g_rank_ui_submit_max_us = elapsed_us;
                  }
              }
#else
            (void)ui_event;
#endif
#if BLE_PROBE_ONBOARD_BEAT_DIAG
            if (g_rank_diag_enabled)
              {
                printf("BTE1 EVENT_DIAG seq=%lu sample_us=%llu beat_us=%llu detected_us=%llu\n",
                  (unsigned long)he.source_sequence,
                  (unsigned long long)he.confirmation_sample_us,
                  (unsigned long long)he.beat_time_us,
                  (unsigned long long)detected_us);
              }
#endif
          }
#if BLE_PROBE_ONBOARD_BEAT_DIAG
        uint64_t now_tick = (uint64_t)clock_systime_ticks();
        if (diag_next == 0) diag_next = now_tick + 500;
        if (g_rank_diag_enabled && now_tick >= diag_next)
#if 1
          {
            uint32_t h = g_serial_head, t = g_serial_tail;
            uint64_t diag_started_us = 0;
            uint64_t diag_finished_us = 0;
            hb_diag d; hb_get_diag(&d);
            probe_ui_get_stats(&ui_stats);
            (void)probe_get_monotonic_us(&diag_started_us);
            printf("BTE1 DIAG ui_refresh=%s ui_refresh_limit_hz=%u generated=%lu enqueued=%lu consumed=%lu queue_full=%lu queue_depth=%lu queue_high_water=%lu sequence_gap=%lu GAP_flag_count=%lu reset_count=%lu reset_gap=%lu reset_sequence=%lu reset_session=%lu reset_time=%lu hb_feed_calls=%lu hb_feed_total_ticks=%llu hb_feed_max_ticks=%lu tick_us=10000 queue_wait_calls=%lu queue_wait_total_us=%llu queue_wait_max_us=%lu hb_elapsed_calls=%lu hb_elapsed_total_us=%llu hb_elapsed_max_us=%lu encode_calls=%lu encode_total_us=%llu encode_max_us=%lu write_calls=%lu write_total_us=%llu write_max_us=%lu diag_output_calls=%lu diag_output_total_us=%llu diag_output_max_us=%lu ui_submit_calls=%lu ui_submit_total_us=%llu ui_submit_max_us=%lu ui_submitted=%lu ui_consumed=%lu ui_dropped=%lu ui_logical_beats=%lu ui_init_refresh_calls=%lu ui_init_refresh_total_us=%llu ui_init_refresh_max_us=%lu ui_runtime_refresh_calls=%lu ui_runtime_refresh_total_us=%llu ui_runtime_refresh_max_us=%lu ui_runtime_flush_calls=%lu ui_runtime_flush_total_us=%llu ui_runtime_flush_max_us=%lu ui_maintenance_calls=%lu ui_maintenance_total_us=%llu ui_maintenance_max_us=%lu ui_refresh_deadline_misses=%lu candidate_count=%lu gate_accepted=%lu event_generated=%lu event_written=%lu short_write=%lu time_faults=%lu time_fault_dropped=%lu gen_time_faults=%lu gen_time_dropped=%lu\n",
              probe_ui_refresh_mode_name(g_joint_ui_refresh_mode),
              g_joint_ui_refresh_mode == PROBE_UI_REFRESH_24HZ ?
                PROBE_UI_REFRESH_LIMIT_HZ : 0,
              (unsigned long)g_rank_generated, (unsigned long)h,
              (unsigned long)g_rank_samples_consumed,
              (unsigned long)g_serial_queue_full, (unsigned long)(h-t),
              (unsigned long)g_rank_queue_high_water,
              (unsigned long)g_rank_sequence_gap,
              (unsigned long)g_rank_gap_flags, (unsigned long)d.resets,
              (unsigned long)d.reset_gap, (unsigned long)d.reset_sequence,
              (unsigned long)d.reset_session, (unsigned long)d.reset_time,
              (unsigned long)g_rank_feed_calls,
              (unsigned long long)g_rank_feed_total_us,
              (unsigned long)g_rank_feed_max_us,
              (unsigned long)g_rank_queue_wait_calls,
              (unsigned long long)g_rank_queue_wait_total_us,
              (unsigned long)g_rank_queue_wait_max_us,
              (unsigned long)g_rank_feed_elapsed_calls,
              (unsigned long long)g_rank_feed_elapsed_total_us,
              (unsigned long)g_rank_feed_elapsed_max_us,
              (unsigned long)g_rank_encode_calls,
              (unsigned long long)g_rank_encode_total_us,
              (unsigned long)g_rank_encode_max_us,
              (unsigned long)g_rank_write_calls,
              (unsigned long long)g_rank_write_total_us,
              (unsigned long)g_rank_write_max_us,
              (unsigned long)g_rank_diag_output_calls,
              (unsigned long long)g_rank_diag_output_total_us,
              (unsigned long)g_rank_diag_output_max_us,
              (unsigned long)g_rank_ui_submit_calls,
              (unsigned long long)g_rank_ui_submit_total_us,
              (unsigned long)g_rank_ui_submit_max_us,
              (unsigned long)ui_stats.submitted,
              (unsigned long)ui_stats.consumed,
              (unsigned long)ui_stats.dropped,
              (unsigned long)ui_stats.logical_beats,
              (unsigned long)ui_stats.init_refresh_calls,
              (unsigned long long)ui_stats.init_refresh_total_us,
              (unsigned long)ui_stats.init_refresh_max_us,
              (unsigned long)ui_stats.runtime_refresh_calls,
              (unsigned long long)ui_stats.runtime_refresh_total_us,
              (unsigned long)ui_stats.runtime_refresh_max_us,
              (unsigned long)ui_stats.runtime_flush_calls,
              (unsigned long long)ui_stats.runtime_flush_total_us,
              (unsigned long)ui_stats.runtime_flush_max_us,
              (unsigned long)ui_stats.maintenance_calls,
              (unsigned long long)ui_stats.maintenance_total_us,
              (unsigned long)ui_stats.maintenance_max_us,
              (unsigned long)ui_stats.refresh_deadline_misses,
              (unsigned long)d.candidates,
              (unsigned long)g_rank_events, (unsigned long)g_rank_events,
              (unsigned long)g_rank_event_written,
              (unsigned long)g_serial_short_writes,
              (unsigned long)g_rank_fault_count,
              (unsigned long)g_rank_fault_dropped,
              (unsigned long)g_rank_gen_time_faults,
              (unsigned long)g_rank_gen_time_dropped);
            for (uint32_t fi=g_rank_fault_dumped;
                 fi<g_rank_fault_count && fi<16; fi++)
              printf("BTE1 TIME_FAULT prev_seq=%lu curr_seq=%lu prev_us=%llu curr_us=%llu delta_us=%lld prev_flags=%u curr_flags=%u\n",
                (unsigned long)g_rank_faults[fi].ps,
                (unsigned long)g_rank_faults[fi].cs,
                (unsigned long long)g_rank_faults[fi].pu,
                (unsigned long long)g_rank_faults[fi].cu,
                (long long)g_rank_faults[fi].delta,
                (unsigned)g_rank_faults[fi].pf,
                (unsigned)g_rank_faults[fi].cf);
            g_rank_fault_dumped = g_rank_fault_count;
            for (uint32_t fi=g_rank_gen_fault_dumped; fi<g_rank_gen_fault_count && fi<16; fi++)
              printf("BTE1 GEN_TIME_FAULT prev_seq=%lu curr_seq=%lu prev_us=%llu curr_us=%llu delta_us=%lld prev_batch=%lu curr_batch=%lu prev_pos=%lu curr_pos=%lu prev_rem=%u curr_rem=%u prev_cycles=%u curr_cycles=%u prev_drain=%llu curr_drain=%llu prev_flags=%u curr_flags=%u\n",
                (unsigned long)g_rank_gen_faults[fi].ps,
                (unsigned long)g_rank_gen_faults[fi].cs,
                (unsigned long long)g_rank_gen_faults[fi].pu,
                (unsigned long long)g_rank_gen_faults[fi].cu,
                (long long)((int64_t)g_rank_gen_faults[fi].cu-(int64_t)g_rank_gen_faults[fi].pu),
                (unsigned long)g_rank_gen_faults[fi].pb,
                (unsigned long)g_rank_gen_faults[fi].cb,
                (unsigned long)g_rank_gen_faults[fi].pc,
                (unsigned long)g_rank_gen_faults[fi].cc,
                (unsigned)g_rank_gen_faults[fi].pw,
                (unsigned)g_rank_gen_faults[fi].cw,
                (unsigned)g_rank_gen_faults[fi].pr,
                (unsigned)g_rank_gen_faults[fi].cr,
                (unsigned long long)g_rank_gen_faults[fi].pd,
                (unsigned long long)g_rank_gen_faults[fi].cd,
                (unsigned)g_rank_gen_faults[fi].pf,
                (unsigned)g_rank_gen_faults[fi].cf);
            g_rank_gen_fault_dumped=g_rank_gen_fault_count;
#if !BLE_PROBE_JOINT_UI_BLE
            printf("BTE1 MATCH evaluations=%lu candidates=%lu\n",
                   (unsigned long)d.evaluations,
                   (unsigned long)d.candidates);
            printf("BTE1 FIFO_BATCH valid=%u id=%lu samples=%lu first_seq=%lu last_seq=%lu first_us=%llu last_us=%llu drain_before=%llu anchor_us=%llu\n",
              (unsigned)g_rank_batch_diag.valid,
              (unsigned long)g_rank_batch_diag.id,(unsigned long)g_rank_batch_diag.count,
              (unsigned long)g_rank_batch_diag.first_seq,(unsigned long)g_rank_batch_diag.last_seq,
              (unsigned long long)g_rank_batch_diag.first_us,(unsigned long long)g_rank_batch_diag.last_us,
              (unsigned long long)g_rank_batch_diag.drain_before,(unsigned long long)g_rank_batch_diag.anchor_us);
#endif
            (void)probe_get_monotonic_us(&diag_finished_us);
            if (diag_started_us != 0 && diag_finished_us >= diag_started_us)
              {
                uint32_t diag_elapsed =
                  (uint32_t)(diag_finished_us - diag_started_us);

                g_rank_diag_output_calls++;
                g_rank_diag_output_total_us += diag_elapsed;
                if (diag_elapsed > g_rank_diag_output_max_us)
                  {
                    g_rank_diag_output_max_us = diag_elapsed;
                  }
              }
            diag_next = now_tick + 500;
          }
#endif
#endif
        {
          struct probe_rank_counts counts;
          uint64_t interval_now_us = 0;
          if (probe_get_monotonic_us(&interval_now_us) == 0)
            {
              probe_rank_counts_snapshot(&counts);
              probe_rank_interval_maybe_close(&g_rank_interval_log,
                                               interval_now_us, &counts,
                                               false);
            }
        }
        continue;
      }
#endif
      body[0] = 2; body[1] = 0x02;
      body[2] = (uint8_t)g_serial_session;
      body[3] = (uint8_t)(g_serial_session >> 8);
      body[4] = (uint8_t)(g_serial_session >> 16);
      body[5] = (uint8_t)(g_serial_session >> 24);
      memcpy(&body[6], &sample.sequence, 4);
      memcpy(&body[10], &sample.uptime_ms, 4);
      memcpy(&body[14], sample.accel, 6);
      memcpy(&body[20], sample.gyro, 6);
      body[26] = sample.flags;
      body_len = 27;
      crc = probe_serial_crc16(body, body_len);
      for (uint16_t i = 0; i < body_len; i++)
        {
          snprintf(&hex[i * 2], 3, "%02x", body[i]);
        }
      hex[body_len * 2] = '\0';
      int line_len = snprintf(line, sizeof(line), "@IMU1,%u,%s,%04x\n",
                              body_len, hex, (unsigned)crc);
      if (line_len > 0 && line_len < (int)sizeof(line))
        {
          ssize_t written = write(STDOUT_FILENO, line, (size_t)line_len);
          if (written != line_len)
            {
              __atomic_fetch_add(&g_serial_short_writes, 1,
                                 __ATOMIC_RELAXED);
            }
        }
      else
        {
          __atomic_fetch_add(&g_serial_short_writes, 1,
                             __ATOMIC_RELAXED);
        }
      __atomic_fetch_add(&g_serial_output, 1, __ATOMIC_RELAXED);
    }
#if BLE_PROBE_JOINT_UI_BLE
  {
    struct probe_rank_counts counts;
    uint64_t interval_now_us = 0;
    if (probe_get_monotonic_us(&interval_now_us) == 0)
      {
        probe_rank_counts_snapshot(&counts);
        probe_rank_interval_maybe_close(&g_rank_interval_log,
                                         interval_now_us, &counts, true);
      }
  }
#endif
#if BLE_PROBE_JOINT_UI_BLE
  if (!__atomic_load_n(&g_serial_running, __ATOMIC_ACQUIRE))
    {
      __atomic_fetch_or(&g_joint_stop_seen_steps,
                        PROBE_STOP_STEP_CONSUMER, __ATOMIC_RELAXED);
      __atomic_fetch_or(&g_joint_stop_exit_begin_steps,
                        PROBE_STOP_STEP_CONSUMER, __ATOMIC_RELAXED);
    }
#endif
  printf("IMU1 STOP session=%lu samples=%lu queue_full=%lu stop_drops=%lu"
         " short_writes=%lu beat_count=%lu",
         (unsigned long)g_serial_session, (unsigned long)g_serial_output,
         (unsigned long)g_serial_queue_full,
         (unsigned long)g_serial_stop_drops,
         (unsigned long)g_serial_short_writes, (unsigned long)g_beat_count);
#if BLE_PROBE_SERIAL_ONLY
  printf(" beat_errors=%lu beat_ack_full=%lu",
         (unsigned long)g_beat_errors, (unsigned long)g_beat_ack_full);
#endif
  printf("\n");
#if PROBE_RANK_PIPELINE_ENABLED
  printf("BTE1 STATS samples_consumed=%lu events=%lu\n",
         (unsigned long)g_rank_samples_consumed,
         (unsigned long)g_rank_events);
#endif
#if BLE_PROBE_JOINT_UI_BLE
  __atomic_fetch_or(&g_joint_stop_exit_done_steps,
                    PROBE_STOP_STEP_CONSUMER, __ATOMIC_RELEASE);
  __atomic_store_n(&g_serial_thread_exited, true, __ATOMIC_RELEASE);
#endif
  return NULL;
}
#endif

static void *probe_imu_raw_thread(void *arg)
{
  static const uint8_t registers[] =
    {
      PROBE_IMU_FIFO_CTRL1,
      PROBE_IMU_FIFO_CTRL2,
      PROBE_IMU_FIFO_CTRL3,
      PROBE_IMU_FIFO_CTRL4,
      PROBE_IMU_FIFO_CTRL5,
      PROBE_IMU_DRDY_PULSE_CFG_G,
      PROBE_IMU_INT1_CTRL,
      PROBE_IMU_CTRL1_XL,
      PROBE_IMU_CTRL2_G,
      PROBE_IMU_CTRL3_C
    };
  bool saved_valid[sizeof(registers)] = {false};
  uint8_t saved[sizeof(registers)] = {0};
  uint8_t gyro_cfg_readback = 0;
  uint8_t accel_cfg_readback = 0;
  uint8_t status[4];
  uint8_t fifo_data[PROBE_IMU_FIFO_READ_MAX_BYTES];
  struct probe_game_imu_stats final_game_imu_stats;
  struct timespec wait_until;
  struct timespec now;
  uint64_t start_us = 0;
  uint64_t end_us = 0;
  uint64_t next_log_us;
  uint32_t imu_sequence = 0;
#if BLE_PROBE_JOINT_UI_BLE
  probe_sched_query(pthread_self(), &g_rank_sched_fifo);
  g_rank_sched_fifo_setup_ret = 0;
  g_rank_sched_fifo_requested_priority = g_rank_sched_fifo.priority;
  if (g_rank_sched_mode == PROBE_SCHED_C)
    {
      struct sched_param fifo_param;
      int fifo_policy = -1;
      int fifo_ret = pthread_getschedparam(pthread_self(), &fifo_policy,
                                           &fifo_param);
      if (fifo_ret == 0)
        {
          /* C is the producer-priority experiment: producer/FIFO is one
           * level above the rank1 consumer (101), while remaining below the
           * BLE service loop (103).  The normal producer starts at 100. */
          g_rank_sched_fifo_requested_priority = fifo_param.sched_priority + 2;
          if (g_rank_sched_fifo_requested_priority >=
              CONFIG_BLUETOOTH_SERVICE_LOOP_THREAD_PRIORITY)
            fifo_ret = EINVAL;
          else
            {
              fifo_param.sched_priority = g_rank_sched_fifo_requested_priority;
              fifo_ret = pthread_setschedparam(pthread_self(), fifo_policy,
                                               &fifo_param);
            }
        }
      g_rank_sched_fifo_setup_ret = fifo_ret;
      probe_sched_query(pthread_self(), &g_rank_sched_fifo);
      if (fifo_ret != 0 || g_rank_sched_fifo.query_ret != 0 ||
          g_rank_sched_fifo.priority != g_rank_sched_fifo_requested_priority)
        {
          printf("openvela_ble_probe: sched-c FIFO priority setup failed"
                 " setup_ret=%d query_ret=%d requested=%d actual=%d\n",
                 fifo_ret, g_rank_sched_fifo.query_ret,
                 g_rank_sched_fifo_requested_priority,
                 g_rank_sched_fifo.priority);
          __atomic_store_n(&g_joint_stop_requested, true, __ATOMIC_RELEASE);
          return NULL;
        }
    }
#endif
  bool imu_gap_pending = false;
  int16_t gyro[3] = {0};
  int16_t accel[3] = {0};
  uint32_t fifo_wake_count = 0;
  uint32_t fifo_batch_id = 0;
  uint32_t batch_recovery_generation = 0;
  uint32_t batch_complete_samples = 0;
  uint32_t batch_last_complete_sequence = 0;
  uint64_t previous_batch_end_us = 0;
  struct probe_fifo_time_estimator time_est;
  probe_fifo_time_estimator_init(&time_est, PROBE_IMU_RAW_TARGET_HZ);
  uint32_t fifo_words_read = 0;
  uint32_t fifo_data_read_transfers = 0;
  uint32_t fifo_data_read_words = 0;
  uint32_t fifo_data_read_max_bytes = 0;
  uint32_t gyro_samples = 0;
  uint32_t accel_samples = 0;
  uint32_t paired_samples = 0;
  uint32_t startup_discarded_words = 0;
  uint32_t fifo_watermark_events = 0;
  uint32_t fifo_overrun = 0;
  uint32_t fifo_smart_full = 0;
  uint32_t max_fifo_depth_words = 0;
  uint32_t unexpected_frames = 0;
  uint32_t i2c_errors = 0;
  uint32_t timeouts = 0;
  uint32_t timeout_status_polls = 0;
  uint32_t fifo_recovery_attempts = 0;
  uint32_t fifo_recovery_success = 0;
  uint32_t fifo_recovery_failed = 0;
  uint32_t fifo_irq_count;
  uint16_t end_depth_words = 0;
  uint8_t expected_pattern = 0;
  uint8_t gyro_axis_mask = 0;
  uint8_t accel_axis_mask = 0;
  uint8_t discard_remaining = 0;
  uint8_t smart_full_streak = 0;
  uint8_t near_full_streak = 0;
  uint8_t pattern_error_streak = 0;
  uint8_t pattern_valid_streak = 0;
  uint8_t int1_fifo_only;
  bool pattern_aligned = false;
  bool expected_valid = false;
  bool gyro_ready = false;
  bool irq_enabled = false;
  bool semaphore_initialized = false;
  bool fifo_started = false;
  bool ready_published = false;
  int restore_status = 0;
  int cleanup_ret;
  int wait_status;
  int fd;
  int ret;
  size_t i;

  (void)arg;
#if BLE_PROBE_JOINT_UI_BLE
  __atomic_store_n(&g_joint_imu_thread_exited, false, __ATOMIC_RELEASE);
#endif
  printf("openvela_ble_probe: IMU FIFO thread entered\n");
  __atomic_store_n(&g_imu_drdy_irq_count, 0, __ATOMIC_RELAXED);
  fd = open(PROBE_IMU_I2C_DEV, O_RDONLY);
  if (fd < 0)
    {
      printf("openvela_ble_probe: IMU raw open %s failed errno=%d\n",
             PROBE_IMU_I2C_DEV, errno);
#if BLE_PROBE_JOINT_UI_BLE
      __atomic_store_n(&g_joint_stop_requested, true, __ATOMIC_RELEASE);
      __atomic_store_n(&g_joint_imu_thread_exited, true, __ATOMIC_RELEASE);
#endif
      return NULL;
    }

  for (i = 0; i < sizeof(registers); i++)
    {
      ret = probe_imu_read(fd, PROBE_IMU_ADDR, registers[i], &saved[i], 1);
      if (ret != 0)
        {
          printf("openvela_ble_probe: IMU raw save reg=0x%02x failed"
                 " status=%d\n", registers[i], ret);
          goto restore;
        }

      saved_valid[i] = true;
    }

  ret = board_imu_int1_disable();
  if (ret != 0)
    {
      printf("openvela_ble_probe: IMU FIFO initial IRQ disable failed"
             " status=%d\n", ret);
      goto restore;
    }

  ret = probe_imu_write_verify(fd, PROBE_IMU_ADDR, PROBE_IMU_FIFO_CTRL5,
                               PROBE_IMU_FIFO_BYPASS);
  if (ret == 0)
    {
      ret = probe_imu_write_verify(fd, PROBE_IMU_ADDR, PROBE_IMU_CTRL3_C,
                                   PROBE_IMU_CTRL3_C_BDU_IF_INC);
    }

  if (ret == 0)
    {
      ret = probe_imu_write_verify(fd, PROBE_IMU_ADDR, PROBE_IMU_CTRL1_XL,
                                   PROBE_IMU_CTRL1_XL_104HZ_4G);
    }

  if (ret == 0)
    {
      ret = probe_imu_write_verify(fd, PROBE_IMU_ADDR, PROBE_IMU_CTRL2_G,
                                   PROBE_IMU_CTRL2_G_104HZ_1000DPS);
    }

  if (ret == 0)
    {
      ret = probe_imu_write_verify(fd, PROBE_IMU_ADDR,
                                   PROBE_IMU_FIFO_CTRL1, 0x78);
    }

  if (ret == 0)
    {
      ret = probe_imu_write_verify(fd, PROBE_IMU_ADDR,
                                   PROBE_IMU_FIFO_CTRL2, 0x00);
    }

  if (ret == 0)
    {
      ret = probe_imu_write_verify(fd, PROBE_IMU_ADDR,
                                   PROBE_IMU_FIFO_CTRL3,
                                   PROBE_IMU_FIFO_CTRL3_GY_XL);
    }

  if (ret == 0)
    {
      ret = probe_imu_write_verify(fd, PROBE_IMU_ADDR,
                                   PROBE_IMU_FIFO_CTRL4, 0x00);
    }

  if (ret == 0)
    {
      uint8_t int1_value = saved[6] &
        ~(PROBE_IMU_INT1_DRDY_XL | PROBE_IMU_INT1_DRDY_G |
          PROBE_IMU_INT1_FIFO_FTH | PROBE_IMU_INT1_FIFO_OVR |
          PROBE_IMU_INT1_FIFO_FULL);

      int1_value |= PROBE_IMU_INT1_FIFO_FTH;
      int1_fifo_only = int1_value;
      ret = probe_imu_write_verify(fd, PROBE_IMU_ADDR,
                                   PROBE_IMU_INT1_CTRL, int1_value);
    }

  if (ret != 0)
    {
      printf("openvela_ble_probe: IMU FIFO configure failed status=%d\n",
             ret);
      goto restore;
    }

  if (sem_init(&g_imu_drdy_sem, 0, 0) < 0)
    {
      printf("openvela_ble_probe: IMU FIFO semaphore init failed errno=%d\n",
             errno);
      goto restore;
    }

  semaphore_initialized = true;
  g_imu_drdy_sem_initialized = true;

  ret = board_imu_int1_enable(probe_imu_int1_isr, NULL);
  if (ret != 0)
    {
      printf("openvela_ble_probe: IMU FIFO IRQ enable failed status=%d\n",
             ret);
      goto restore;
    }

  irq_enabled = true;
  ret = probe_imu_write_verify(fd, PROBE_IMU_ADDR, PROBE_IMU_FIFO_CTRL5,
                               PROBE_IMU_FIFO_104HZ_CONTINUOUS);
  if (ret != 0)
    {
      printf("openvela_ble_probe: IMU FIFO start failed status=%d\n", ret);
      goto stop_irq;
    }

  fifo_started = true;

  ret = probe_imu_read(fd, PROBE_IMU_ADDR, PROBE_IMU_CTRL1_XL,
                       &accel_cfg_readback, 1);
  if (ret == 0)
    {
      ret = probe_imu_read(fd, PROBE_IMU_ADDR, PROBE_IMU_CTRL2_G,
                           &gyro_cfg_readback, 1);
    }
  if (ret != 0 || accel_cfg_readback != PROBE_IMU_CTRL1_XL_104HZ_4G ||
      gyro_cfg_readback != PROBE_IMU_CTRL2_G_104HZ_1000DPS)
    {
      printf("openvela_ble_probe: IMU config readback failed status=%d"
             " ctrl1_xl=0x%02x ctrl2_g=0x%02x\n", ret,
             accel_cfg_readback, gyro_cfg_readback);
      goto stop_irq;
    }

#if PROBE_RANK_PIPELINE_ENABLED
#if BLE_PROBE_ONBOARD_BEAT
#if BLE_PROBE_JOINT_UI_BLE
  if (g_joint_input_thread_created)
    probe_sched_query(g_joint_input_thread, &g_rank_sched_input);
  if (g_communication_thread_created)
    probe_sched_query(g_communication_thread, &g_rank_sched_communication);
  probe_sched_query_ui(&g_rank_sched_ui);
#endif
  printf("@BTE1,READY,1,%lu,build_id=%s,model=%s,clock=device_monotonic_us,stride=%u,diag=%u,mode=%s,ui_refresh=%s,ui_refresh_limit_hz=%u,sched_mode=%s,consumer_policy=%d,consumer_priority=%d,consumer_requested=%d,sched_setup_ret=%d,sched_query_ret=%d,fifo_policy=%d,fifo_priority=%d,fifo_requested=%d,fifo_setup_ret=%d,fifo_query_ret=%d,input_policy=%d,input_priority=%d,input_query_ret=%d,ui_policy=%d,ui_priority=%d,ui_query_ret=%d,comm_policy=%d,comm_priority=%d,comm_query_ret=%d,ble_service_priority=%d,system_work_priority=%d,hpwork_priority=%d,stop=stop,stop_timeout_ms=%lu\n",
         (unsigned long)g_serial_session, PROBE_EXPERIMENT_BUILD_ID,
         HB_VERSION, (unsigned)HB_EVAL_STRIDE,
         (unsigned)g_rank_diag_enabled,
         BLE_PROBE_JOINT_UI_BLE ? "rank1_ui_ble" : "rank1_serial",
         probe_ui_refresh_mode_name(g_joint_ui_refresh_mode),
         g_joint_ui_refresh_mode == PROBE_UI_REFRESH_24HZ ?
           PROBE_UI_REFRESH_LIMIT_HZ : 0,
         probe_sched_mode_name(), g_rank_sched_consumer.policy,
         g_rank_sched_consumer.priority, g_rank_sched_requested_priority,
         g_rank_sched_setup_ret, g_rank_sched_consumer.query_ret,
         g_rank_sched_fifo.policy, g_rank_sched_fifo.priority,
         g_rank_sched_fifo_requested_priority, g_rank_sched_fifo_setup_ret,
         g_rank_sched_fifo.query_ret,
         g_rank_sched_input.policy, g_rank_sched_input.priority,
         g_rank_sched_input.query_ret,
         g_rank_sched_ui.policy, g_rank_sched_ui.priority,
         g_rank_sched_ui.query_ret,
         g_rank_sched_communication.policy,
         g_rank_sched_communication.priority,
         g_rank_sched_communication.query_ret,
         CONFIG_BLUETOOTH_SERVICE_LOOP_THREAD_PRIORITY,
         CONFIG_SYSTEM_WORKQUEUE_PRIORITY, CONFIG_SCHED_HPWORKPRIORITY,
         (unsigned long)g_joint_stop_timeout_ms);
#else
  printf("@IMU1,READY,1,%lu,104,4g,time=sample_monotonic_ms"
         ",gyro_fs_dps=1000,gyro_sensitivity_mdps_lsb=35"
         ",gyro_odr_hz=104,ctrl2_g=0x%02x,beat_protocol=BEAT1"
         ",build_id=%s\n", (unsigned long)g_serial_session,
         gyro_cfg_readback, PROBE_EXPERIMENT_BUILD_ID);
#endif
#endif
  ready_published = true;
#if BLE_PROBE_JOINT_UI_BLE
  g_joint_ready_emitted = true;
#endif

  printf("openvela_ble_probe: IMU FIFO config watermark_words=120"
         " fifo_ctrl3=0x09 fifo_ctrl5=0x26\n");

  if (probe_get_monotonic_us(&start_us) < 0)
    {
      printf("openvela_ble_probe: IMU DRDY clock_gettime failed errno=%d\n",
             errno);
      goto stop_irq;
    }

  next_log_us = start_us + 10000000ULL;

  /* This producer is intentionally long-lived.  The cleanup path below is
   * retained for configuration, I/O, or a future explicit-stop failure.
   */

  for (;;)
    {
#if BLE_PROBE_JOINT_UI_BLE
      if (!__atomic_load_n(&g_joint_producer_running, __ATOMIC_ACQUIRE))
        {
          __atomic_fetch_or(&g_joint_stop_seen_steps,
                            PROBE_STOP_STEP_PRODUCER, __ATOMIC_RELAXED);
          __atomic_fetch_or(&g_joint_stop_exit_begin_steps,
                            PROBE_STOP_STEP_PRODUCER, __ATOMIC_RELAXED);
          break;
        }
#elif PROBE_RANK_PIPELINE_ENABLED
      if (!__atomic_load_n(&g_serial_running, __ATOMIC_ACQUIRE))
        {
          break;
        }
#endif
#if !PROBE_RANK_PIPELINE_ENABLED
      struct probe_game_imu_stats game_imu_stats;
#endif
      uint64_t now_us;
      uint16_t recovery_depth = 0;
      const char *recovery_reason = NULL;
      bool status_ready = false;
      bool wake_watermark_seen = false;
      bool wake_smart_full_seen = false;
      bool wake_overrun_seen = false;

      if (probe_get_monotonic_sample(&now_us, &now) < 0)
        {
          printf("openvela_ble_probe: IMU FIFO clock read failed errno=%d\n",
                 errno);
          break;
        }

      if (now_us >= next_log_us)
        {
#if !PROBE_RANK_PIPELINE_ENABLED
          probe_game_get_imu_stats(&game_imu_stats);
          fifo_irq_count = __atomic_load_n(&g_imu_drdy_irq_count,
                                           __ATOMIC_RELAXED);
          printf("openvela_ble_probe: IMU FIFO progress elapsed_ms=%llu"
                 " fifo_irq_count=%lu fifo_wake_count=%lu"
                 " fifo_words_read=%lu gyro_samples=%lu accel_samples=%lu"
                 " paired_six_axis_samples=%lu fifo_overrun=%lu"
                 " fifo_smart_full=%lu max_fifo_depth_words=%lu"
                 " unexpected=%lu i2c_errors=%lu timeouts=%lu"
                 " timeout_status_polls=%lu fifo_recovery_attempts=%lu"
                 " fifo_recovery_success=%lu fifo_recovery_failed=%lu"
                 " imu_ring_full=%lu imu_gap_samples=%lu"
                 " fifo_data_read_transfers=%lu fifo_data_read_words=%lu"
                 " fifo_data_read_max_bytes=%lu"
                 " fifo_data_read_chunk_bytes=%u\n",
                 (unsigned long long)((now_us - start_us) / 1000),
                 (unsigned long)fifo_irq_count,
                 (unsigned long)fifo_wake_count,
                 (unsigned long)fifo_words_read,
                 (unsigned long)gyro_samples,
                 (unsigned long)accel_samples,
                 (unsigned long)paired_samples,
                 (unsigned long)fifo_overrun,
                 (unsigned long)fifo_smart_full,
                 (unsigned long)max_fifo_depth_words,
                 (unsigned long)unexpected_frames,
                 (unsigned long)i2c_errors, (unsigned long)timeouts,
                 (unsigned long)timeout_status_polls,
                 (unsigned long)fifo_recovery_attempts,
                 (unsigned long)fifo_recovery_success,
                 (unsigned long)fifo_recovery_failed,
                 (unsigned long)game_imu_stats.ring_full,
                 (unsigned long)game_imu_stats.gap_samples,
                 (unsigned long)fifo_data_read_transfers,
                 (unsigned long)fifo_data_read_words,
                 (unsigned long)fifo_data_read_max_bytes,
                 PROBE_IMU_FIFO_READ_CHUNK_BYTES);
#endif
          next_log_us = now_us + 10000000ULL;
        }

      wait_until = now;
      probe_timespec_add_ms(&wait_until, PROBE_IMU_FIFO_WATCHDOG_MS);

      do
        {
          wait_status = sem_clockwait(&g_imu_drdy_sem, CLOCK_MONOTONIC,
                                      &wait_until);
        }
      while (wait_status < 0 && errno == EINTR);

      if (wait_status < 0)
        {
          if (errno == ETIMEDOUT)
            {
              timeouts++;
              timeout_status_polls++;
              ret = probe_imu_read(fd, PROBE_IMU_ADDR,
                                   PROBE_IMU_FIFO_STATUS1,
                                   status, sizeof(status));
              if (ret != 0)
                {
                  i2c_errors++;
                  expected_valid = false;
                  continue;
                }

              status_ready = true;
              if ((status[1] & 0x60) == 0 &&
                  ((status[1] & 0x10) != 0 ||
                   probe_imu_fifo_depth(status) == 0))
                {
                  continue;
                }
            }
          else
            {
              printf("openvela_ble_probe: IMU FIFO semaphore wait failed"
                     " errno=%d\n", errno);
              break;
            }
        }
      else
        {
          fifo_wake_count++;
        }

      {
        uint16_t batch_depth = 0;
        uint64_t batch_status_begin_us = 0, batch_status_end_us = 0;
        uint64_t batch_end_us = 0, batch_read_total_us = 0;
        uint64_t batch_read_max_us = 0;
        uint32_t batch_words_read = 0, batch_read_transfers = 0;
        uint32_t batch_enqueue_success = 0, batch_enqueue_failed = 0;
        uint32_t batch_first_sequence = 0, batch_last_sequence = 0;
        uint8_t batch_has_sequence = 0;
        uint8_t batch_status_time_valid = 0;
        int batch_status_ret = 0;
      for (;;)
        {
          uint16_t depth;
          uint16_t pattern;
          uint16_t words_to_read;
          uint64_t drain_now_us;
          uint64_t fifo_status_before_us = 0;
          uint64_t fifo_status_after_us = 0;
          uint16_t batch_pos = 0;
          bool watermark;
          bool empty;
          bool smart_full;
          bool overrun;

          batch_status_time_valid =
            probe_get_monotonic_us(&fifo_status_before_us) == 0;
          if (!status_ready)
            {
              ret = probe_imu_read(fd, PROBE_IMU_ADDR,
                                   PROBE_IMU_FIFO_STATUS1,
                                   status, sizeof(status));
              if (ret != 0)
                {
                  i2c_errors++;
                  expected_valid = false;
                  batch_status_ret = ret;
                  break;
                }
            }

          if (probe_get_monotonic_us(&fifo_status_after_us) != 0)
            batch_status_time_valid = 0;
          else if (fifo_status_after_us < fifo_status_before_us)
            batch_status_time_valid = 0;

          status_ready = false;
          batch_status_ret = 0;

          if (probe_get_monotonic_us(&drain_now_us) < 0)
            {
              printf("openvela_ble_probe: IMU FIFO timestamp failed"
                     " errno=%d\n", errno);
              goto stop_irq;
            }

          depth = probe_imu_fifo_depth(status);
          empty = (status[1] & 0x10) != 0;
          smart_full = (status[1] & 0x20) != 0;
          overrun = (status[1] & 0x40) != 0;
          watermark = (status[1] & 0x80) != 0;
          pattern = (uint16_t)status[2] |
                    ((uint16_t)(status[3] & 0x03) << 8);
          fifo_batch_id++;
          /* Bind every sample and the eventual batch summary to the
           * generation observed at this snapshot boundary.  Recovery may
           * advance the global generation while a batch is being drained. */
          batch_recovery_generation = g_fifo_trace_recovery_generation;
          batch_words_read = 0;
          batch_read_transfers = 0;
          batch_enqueue_success = 0;
          batch_enqueue_failed = 0;
          batch_first_sequence = 0;
          batch_last_sequence = 0;
          batch_has_sequence = 0;
          batch_read_total_us = 0;
          batch_read_max_us = 0;
          batch_depth = depth;
          batch_status_begin_us = fifo_status_before_us;
          batch_status_end_us = fifo_status_after_us;
          g_rank_batch_diag.id = fifo_batch_id;
          g_rank_batch_diag.count = 0;
          g_rank_batch_diag.valid = 0;
          g_rank_batch_diag.drain_before = drain_now_us;
          g_rank_batch_diag.anchor_us = time_est.valid ? time_est.estimate_us : 0;

          if (depth > max_fifo_depth_words)
            {
              max_fifo_depth_words = depth;
              __atomic_store_n(&g_fifo_max_depth_words, depth,
                               __ATOMIC_RELAXED);
            }

          if (watermark && !wake_watermark_seen)
            {
              fifo_watermark_events++;
              wake_watermark_seen = true;
            }

          if (smart_full && !wake_smart_full_seen)
            {
              fifo_smart_full++;
              wake_smart_full_seen = true;
            }

          if (overrun && !wake_overrun_seen)
            {
              fifo_overrun++;
              __atomic_fetch_add(&g_fifo_overrun_total, 1, __ATOMIC_RELAXED);
              wake_overrun_seen = true;
            }

          if (smart_full)
            {
              if (smart_full_streak < UINT8_MAX)
                {
                  smart_full_streak++;
                }
            }
          else
            {
              smart_full_streak = 0;
            }

          if (depth >= PROBE_IMU_FIFO_NEAR_FULL_WORDS)
            {
              if (near_full_streak < UINT8_MAX)
                {
                  near_full_streak++;
                }
            }
          else
            {
              near_full_streak = 0;
            }

          if (overrun)
            {
              recovery_reason = "overrun";
            }
          else if (smart_full_streak >=
                   PROBE_IMU_FIFO_SMART_FULL_LIMIT)
            {
              recovery_reason = "smart-full";
            }
          else if (near_full_streak >= PROBE_IMU_FIFO_NEAR_FULL_LIMIT)
            {
              recovery_reason = "near-full";
            }

          /* Anchor observations to the last complete sample actually
           * produced by this snapshot, never to the next sequence value at
           * snapshot start.  Empty/partial snapshots are excluded. */
          if (batch_complete_samples > 0)
            {
              probe_fifo_time_estimator_observe_end(
                &time_est, batch_last_complete_sequence, drain_now_us,
                batch_complete_samples, PROBE_IMU_RAW_TARGET_HZ);
            }

          if (recovery_reason != NULL)
            {
              recovery_depth = depth;
              break;
            }

          if (empty || depth == 0)
            {
              break;
            }

          words_to_read = depth;
          batch_complete_samples = 0;
          while (words_to_read > 0)
            {
              uint16_t chunk_words = MIN(words_to_read,
                PROBE_IMU_FIFO_READ_CHUNK_BYTES / 2);
              uint16_t chunk_index;
              size_t chunk_bytes = (size_t)chunk_words * 2;
              uint64_t read_begin_us = 0;
              uint64_t read_end_us = 0;

              (void)probe_get_monotonic_us(&read_begin_us);

              ret = probe_imu_read(fd, PROBE_IMU_ADDR,
                                   PROBE_IMU_FIFO_DATA_OUT_L,
                                   fifo_data, chunk_bytes);
              if (ret != 0)
                {
                  i2c_errors++;
                  expected_valid = false;
                  break;
                }
              (void)probe_get_monotonic_us(&read_end_us);

              fifo_data_read_transfers++;
              batch_read_transfers++;
              batch_words_read += chunk_words;
              if (read_end_us >= read_begin_us)
                {
                  uint64_t elapsed = read_end_us - read_begin_us;
                  batch_read_total_us += elapsed;
                  if (elapsed > batch_read_max_us)
                    batch_read_max_us = elapsed;
                }
              fifo_data_read_words += chunk_words;
              if (chunk_bytes > fifo_data_read_max_bytes)
                {
                  fifo_data_read_max_bytes = chunk_bytes;
                }

              fifo_words_read += chunk_words;
              for (chunk_index = 0; chunk_index < chunk_words; chunk_index++)
                {
                                  struct probe_imu_sample sample;
                                  uint64_t complete_us = 0;
                                  uint64_t enqueue_us = 0;
                                  bool enqueued;
                  uint8_t pattern_word = (uint8_t)(pattern %
                    PROBE_IMU_FIFO_PATTERN_WORDS);
                  size_t word_offset = (size_t)chunk_index * 2;
                  int16_t raw;

                  words_to_read--;

                  if (expected_valid && pattern_word != expected_pattern)
                    {
                      unexpected_frames++;
                      if (pattern_error_streak < UINT8_MAX)
                        {
                          pattern_error_streak++;
                        }

                      pattern_valid_streak = 0;
                      gyro_axis_mask = 0;
                      accel_axis_mask = 0;
                      gyro_ready = false;
                    }
                  else
                    {
                      if (pattern_valid_streak <
                          PROBE_IMU_FIFO_PATTERN_WORDS)
                        {
                          pattern_valid_streak++;
                        }

                      if (pattern_valid_streak >=
                          PROBE_IMU_FIFO_PATTERN_WORDS)
                        {
                          pattern_error_streak = 0;
                        }
                    }

                  if (pattern_error_streak >=
                      PROBE_IMU_FIFO_PATTERN_ERR_LIMIT)
                    {
                      recovery_reason = "pattern";
                      recovery_depth = depth;
                      break;
                    }

                  raw = (int16_t)((uint16_t)fifo_data[word_offset] |
                    ((uint16_t)fifo_data[word_offset + 1] << 8));
              if (!pattern_aligned)
                {
                  if (pattern_word != 0)
                    {
                      unexpected_frames++;
                    }
                  else
                    {
                      pattern_aligned = true;
                      discard_remaining = PROBE_IMU_FIFO_PATTERN_WORDS;
                    }
                }

              if (pattern_aligned && discard_remaining > 0)
                {
                  discard_remaining--;
                  startup_discarded_words++;
                }
              else if (pattern_aligned)
                {
                  if (pattern_word < 3)
                    {
                      probe_fifo_pair_word(pattern_word, raw, accel, gyro,
                                           &accel_axis_mask, &gyro_axis_mask);
                      if (pattern_word == 2)
                        {
                          if (gyro_axis_mask == 0x07)
                            {
                              gyro_samples++;
                              gyro_ready = true;
                            }
                          else
                            {
                              unexpected_frames++;
                              gyro_ready = false;
                            }

                          gyro_axis_mask = 0;
                        }
                    }
                  else
                    {
                      probe_fifo_pair_word(pattern_word, raw, accel, gyro,
                                           &accel_axis_mask, &gyro_axis_mask);
                      if (pattern_word == 5)
                        {
                          if (accel_axis_mask == 0x07)
                            {
                              accel_samples++;
                              if (gyro_ready)
                                {
                                  uint32_t complete_cycles_after =
                                    words_to_read /
                                    PROBE_IMU_FIFO_PATTERN_WORDS;

                                  paired_samples++;
                                  memset(&sample, 0, sizeof(sample));
                                  if (!time_est.valid)
                                    {
                                      time_est.estimate_us = probe_fifo_time_estimator_first(&time_est,
                                        drain_now_us, complete_cycles_after,
                                        imu_sequence,
                                        PROBE_IMU_RAW_TARGET_HZ);
                                    }
                                  else
                                    {
                                      time_est.estimate_us = probe_fifo_time_estimator_next(&time_est);
                                    }
                                  sample.monotonic_us = time_est.estimate_us;
                                  sample.sequence = imu_sequence++;
                                  if (!batch_has_sequence)
                                    {
                                      batch_first_sequence = sample.sequence;
                                      batch_has_sequence = 1;
                                    }
                                  batch_last_sequence = sample.sequence;
                                  batch_complete_samples++;
                                  batch_last_complete_sequence = sample.sequence;
                                  memcpy(sample.gyro, gyro,
                                         sizeof(sample.gyro));
                                  memcpy(sample.accel, accel,
                                         sizeof(sample.accel));
                                  sample.flags =
                                    PROBE_IMU_SAMPLE_FLAG_ESTIMATED_TIME;
                                  sample.batch_id = fifo_batch_id;
                                  sample.batch_pos = batch_pos++;
                                  sample.batch_complete_cycles = (uint16_t)complete_cycles_after;
                                  sample.batch_drain_us = drain_now_us;
                                  sample.fifo_status_before_us = fifo_status_before_us;
                                  sample.fifo_status_after_us = fifo_status_after_us;
                                  (void)probe_get_monotonic_us(&complete_us);
                                  sample.pair_complete_us = complete_us;
                                  sample.estimated_sample_us = time_est.estimate_us;
                                  sample.estimator_sequence = sample.sequence;
                                  sample.estimator_period_q16 = time_est.period_q16;
                                  sample.estimator_slew_us = time_est.slew_us;
                                  sample.fifo_depth_words = depth;
                                  sample.remaining_after = complete_cycles_after;
                                  sample.recovery_generation = batch_recovery_generation;
                                  sample.producer_context_valid = 1;
                                  if (g_rank_batch_diag.count == 0) { g_rank_batch_diag.first_seq = sample.sequence; g_rank_batch_diag.first_us = sample.monotonic_us; }
                                  g_rank_batch_diag.last_seq = sample.sequence; g_rank_batch_diag.last_us = sample.monotonic_us; g_rank_batch_diag.count++;
                                  g_rank_batch_diag.valid = 1;
                                  if (g_rank_gen_prev_valid && sample.monotonic_us < g_rank_gen_prev_us)
                                    {
                                      uint32_t fi = g_rank_gen_fault_count;
                                      g_rank_gen_time_faults++;
                                      if (fi < 16)
                                        {
                                          g_rank_gen_faults[fi] = (struct rank_gen_fault){g_rank_gen_prev_seq, sample.sequence, g_rank_gen_prev_batch, fifo_batch_id, g_rank_gen_prev_pos, sample.batch_pos, g_rank_gen_prev_rem, (uint16_t)words_to_read, g_rank_gen_prev_us, sample.monotonic_us, g_rank_gen_prev_drain, drain_now_us, g_rank_gen_prev_cycles, (uint16_t)complete_cycles_after, g_rank_gen_prev_flags, sample.flags};
                                          g_rank_gen_fault_count++;
                                        }
                                      else g_rank_gen_time_dropped++;
                                    }
                                  g_rank_gen_prev_us = sample.monotonic_us;
                                  g_rank_gen_prev_seq = sample.sequence;
                                  g_rank_gen_prev_batch = fifo_batch_id;
                                  g_rank_gen_prev_pos = sample.batch_pos;
                                  g_rank_gen_prev_rem = (uint16_t)words_to_read;
                                  g_rank_gen_prev_cycles = (uint16_t)complete_cycles_after;
                                  g_rank_gen_prev_drain = drain_now_us;
                                  g_rank_gen_prev_flags = sample.flags;
                                  g_rank_gen_prev_valid = true;
                                  if (imu_gap_pending)
                                    {
                                      sample.flags |=
                                        PROBE_IMU_SAMPLE_FLAG_GAP;
                                    }

                                  enqueued = probe_capture_submit(&sample,
                                                                   &enqueue_us);
                                  if (enqueued)
                                    batch_enqueue_success++;
                                  else
                                    batch_enqueue_failed++;
                                  probe_fifo_trace_add(
                                    fifo_batch_id, sample.sequence, depth,
                                    complete_cycles_after, fifo_status_before_us,
                                    fifo_status_after_us,
                                    complete_us, enqueue_us,
                                    sample.monotonic_us,
                                    time_est.valid ? time_est.estimate_us : 0,
                                    sample.sequence, time_est.period_q16,
                                    time_est.slew_us, sample.flags, enqueued,
                                    (ret == 0) ? 1u : 0u, 0);
#if PROBE_RANK_PIPELINE_ENABLED
                                  if (__atomic_load_n(&g_serial_submit_ok,
                                                       __ATOMIC_RELAXED))
                                    {
                                      imu_gap_pending = false;
                                    }
                                  else
                                    {
                                      imu_gap_pending = true;
                                    }
#else
                                  if (probe_game_submit_imu(&sample))
                                    {
                                      imu_gap_pending = false;
                                    }
                                  else
                                    {
                                      imu_gap_pending = true;
                                    }
#endif
                                }
                              else
                                {
                                  unexpected_frames++;
                                }
                            }
                          else
                            {
                              unexpected_frames++;
                            }

                          accel_axis_mask = 0;
                          gyro_ready = false;
                        }
                    }
                }

              expected_pattern = (uint8_t)((pattern_word + 1) %
                                             PROBE_IMU_FIFO_PATTERN_WORDS);
              expected_valid = true;
              pattern++;
            }

          if (recovery_reason != NULL)
            {
              break;
            }
          }

          if (recovery_reason != NULL)
            {
              break;
            }

          if (ret != 0)
            {
              break;
            }

          /* Re-read status and continue draining until FIFO_EMPTY. */
        }

      if (recovery_reason != NULL)
        {
          g_fifo_trace_recovery_generation++;
          bool recovery_complete = false;
          unsigned int attempt;

          for (attempt = 0; attempt < PROBE_IMU_FIFO_RECOVERY_RETRIES;
               attempt++)
            {
              uint8_t recovery_status[4];
              int recovery_ret = 0;

              fifo_recovery_attempts++;
              __atomic_store_n(&g_fifo_recovery_last_depth, recovery_depth,
                               __ATOMIC_RELAXED);
              __atomic_store_n(&g_fifo_recovery_last_reason,
                               recovery_reason == NULL ? 0u :
                               (strcmp(recovery_reason, "overrun") == 0 ? 1u :
                                strcmp(recovery_reason, "smart-full") == 0 ? 2u :
                                strcmp(recovery_reason, "near-full") == 0 ? 3u : 4u),
                               __ATOMIC_RELAXED);
              __atomic_store_n(&g_fifo_recovery_last_status_valid, 0,
                               __ATOMIC_RELAXED);
              __atomic_store_n(&g_fifo_recovery_pre_status_word,
                               (uint32_t)status[0] |
                               ((uint32_t)status[1] << 8) |
                               ((uint32_t)status[2] << 16) |
                               ((uint32_t)status[3] << 24), __ATOMIC_RELAXED);
              __atomic_store_n(&g_fifo_recovery_pre_status_valid,
                               batch_status_time_valid, __ATOMIC_RELAXED);
              __atomic_fetch_add(&g_fifo_recovery_attempts_total, 1,
                                 __ATOMIC_RELAXED);
              __atomic_fetch_add(&g_fifo_recovery_total, 1,
                                 __ATOMIC_RELAXED);
              printf("openvela_ble_probe: IMU FIFO recovery begin"
                     " reason=%s depth=%u attempt=%u\n",
                     recovery_reason, recovery_depth, attempt + 1);

              if (irq_enabled)
                {
                  recovery_ret = board_imu_int1_disable();
                  if (recovery_ret == 0)
                    {
                      irq_enabled = false;
                    }
                }

              probe_imu_drain_semaphore();

              if (recovery_ret == 0)
                {
                  recovery_ret = probe_imu_write_verify(
                    fd, PROBE_IMU_ADDR, PROBE_IMU_INT1_CTRL,
                    int1_fifo_only & (uint8_t)~PROBE_IMU_INT1_FIFO_FTH);
                }

              if (recovery_ret == 0)
                {
                  recovery_ret = probe_imu_write_verify(
                    fd, PROBE_IMU_ADDR, PROBE_IMU_FIFO_CTRL5,
                    PROBE_IMU_FIFO_BYPASS);
                  if (recovery_ret == 0)
                    {
                      fifo_started = false;
                    }
                }

              if (recovery_ret == 0)
                {
                  recovery_ret = probe_imu_read(
                    fd, PROBE_IMU_ADDR, PROBE_IMU_FIFO_STATUS1,
                    recovery_status, sizeof(recovery_status));
                  if (recovery_ret == 0)
                    {
                      __atomic_store_n(&g_fifo_recovery_last_status_word,
                                       (uint32_t)recovery_status[0] |
                                       ((uint32_t)recovery_status[1] << 8) |
                                       ((uint32_t)recovery_status[2] << 16) |
                                       ((uint32_t)recovery_status[3] << 24),
                                       __ATOMIC_RELAXED);
                      __atomic_store_n(&g_fifo_recovery_last_status_valid, 1,
                                       __ATOMIC_RELAXED);
                    }
                  if (recovery_ret == 0 &&
                      (probe_imu_fifo_depth(recovery_status) != 0 ||
                       (recovery_status[1] & 0x10) == 0))
                    {
                      recovery_ret = -EIO;
                    }
                }

              if (recovery_ret == 0)
                {
                  pattern_aligned = false;
                  expected_valid = false;
                  expected_pattern = 0;
                  probe_fifo_runtime_recover(&gyro_axis_mask,
                    &accel_axis_mask, &gyro_ready, gyro, accel,
                    &imu_gap_pending, &time_est,
                    PROBE_IMU_RAW_TARGET_HZ);
                  discard_remaining = PROBE_IMU_FIFO_PATTERN_WORDS;
                  smart_full_streak = 0;
                  near_full_streak = 0;
                  pattern_error_streak = 0;
                  pattern_valid_streak = 0;
                }

              if (recovery_ret == 0)
                {
                  recovery_ret = probe_imu_write_verify(
                    fd, PROBE_IMU_ADDR, PROBE_IMU_FIFO_CTRL1, 0x78);
                }

              if (recovery_ret == 0)
                {
                  recovery_ret = probe_imu_write_verify(
                    fd, PROBE_IMU_ADDR, PROBE_IMU_FIFO_CTRL2, 0x00);
                }

              if (recovery_ret == 0)
                {
                  recovery_ret = probe_imu_write_verify(
                    fd, PROBE_IMU_ADDR, PROBE_IMU_FIFO_CTRL3,
                    PROBE_IMU_FIFO_CTRL3_GY_XL);
                }

              if (recovery_ret == 0)
                {
                  recovery_ret = probe_imu_write_verify(
                    fd, PROBE_IMU_ADDR, PROBE_IMU_FIFO_CTRL4, 0x00);
                }

              if (recovery_ret == 0)
                {
                  recovery_ret = probe_imu_write_verify(
                    fd, PROBE_IMU_ADDR, PROBE_IMU_INT1_CTRL,
                    int1_fifo_only);
                }

              probe_imu_drain_semaphore();
              if (recovery_ret == 0)
                {
                  recovery_ret = board_imu_int1_enable(
                    probe_imu_int1_isr, NULL);
                  if (recovery_ret == 0)
                    {
                      irq_enabled = true;
                    }
                }

              if (recovery_ret == 0)
                {
                  recovery_ret = probe_imu_write_verify(
                    fd, PROBE_IMU_ADDR, PROBE_IMU_FIFO_CTRL5,
                    PROBE_IMU_FIFO_104HZ_CONTINUOUS);
                  if (recovery_ret == 0)
                    {
                      fifo_started = true;
                    }
                }

              __atomic_store_n(&g_fifo_recovery_last_status_ret, recovery_ret,
                               __ATOMIC_RELAXED);
              if (recovery_ret == 0)
                {
                  fifo_recovery_success++;
                  __atomic_fetch_add(&g_fifo_recovery_success_total, 1,
                                     __ATOMIC_RELAXED);
                  recovery_complete = true;
                  printf("openvela_ble_probe: IMU FIFO recovery end"
                         " reason=%s status=0 attempt=%u\n",
                         recovery_reason, attempt + 1);
                  break;
                }

              fifo_recovery_failed++;
              __atomic_store_n(&g_fifo_recovery_last_status_ret, recovery_ret,
                               __ATOMIC_RELAXED);
              __atomic_fetch_add(&g_fifo_recovery_failure_total, 1,
                                 __ATOMIC_RELAXED);
              printf("openvela_ble_probe: IMU FIFO recovery end"
                     " reason=%s status=%d attempt=%u\n",
                     recovery_reason, recovery_ret, attempt + 1);
              if (irq_enabled)
                {
                  board_imu_int1_disable();
                  irq_enabled = false;
                }

              probe_imu_write_reg(fd, PROBE_IMU_ADDR,
                                  PROBE_IMU_FIFO_CTRL5,
                                  PROBE_IMU_FIFO_BYPASS);
              fifo_started = false;
              if (attempt + 1 < PROBE_IMU_FIFO_RECOVERY_RETRIES)
                {
                  usleep(PROBE_IMU_FIFO_RECOVERY_DELAY_US);
                }
            }

          if (!recovery_complete)
            {
              printf("openvela_ble_probe: IMU producer stopped after"
                     " recovery failure\n");
              /* Publish the interrupted batch before leaving the producer.
               * Events already queued from this batch must reach a terminal
               * NOT_FOUND state rather than remain PENDING forever. */
              {
                struct probe_batch_trace_record failed_batch;
                uint64_t failed_end_us = 0;
                (void)probe_get_monotonic_us(&failed_end_us);
                memset(&failed_batch, 0, sizeof(failed_batch));
                failed_batch.session = g_serial_session;
                failed_batch.batch_id = fifo_batch_id;
                failed_batch.recovery_generation = batch_recovery_generation;
                failed_batch.status_begin_us = batch_status_begin_us;
                failed_batch.status_end_us = batch_status_end_us;
                failed_batch.batch_end_us = failed_end_us;
                failed_batch.depth_words = batch_depth;
                failed_batch.words_read = batch_words_read;
                failed_batch.complete_samples = batch_complete_samples;
                failed_batch.first_sequence = batch_first_sequence;
                failed_batch.last_sequence = batch_last_sequence;
                failed_batch.enqueue_success = batch_enqueue_success;
                failed_batch.enqueue_failed = batch_enqueue_failed;
                failed_batch.status_ret = batch_status_ret;
                failed_batch.valid = 0;
                failed_batch.status_valid = batch_status_time_valid;
                failed_batch.has_sequence = batch_has_sequence;
                probe_batch_trace_add(&failed_batch);
                probe_batch_pending_complete(&failed_batch);
              }
              goto stop_irq;
            }
        }

      (void)probe_get_monotonic_us(&batch_end_us);
      {
        struct probe_batch_trace_record batch_record;
        memset(&batch_record, 0, sizeof(batch_record));
        batch_record.session = g_serial_session;
        batch_record.batch_id = fifo_batch_id;
        batch_record.recovery_generation = batch_recovery_generation;
        batch_record.status_begin_us = batch_status_begin_us;
        batch_record.status_end_us = batch_status_end_us;
        batch_record.batch_end_us = batch_end_us;
        batch_record.previous_batch_end_us = previous_batch_end_us;
        batch_record.depth_words = batch_depth;
        batch_record.words_read = batch_words_read;
        batch_record.complete_samples = batch_complete_samples;
        batch_record.first_sequence = batch_first_sequence;
        batch_record.last_sequence = batch_last_sequence;
        batch_record.enqueue_success = batch_enqueue_success;
        batch_record.enqueue_failed = batch_enqueue_failed;
        batch_record.read_transfers = batch_read_transfers;
        batch_record.read_total_us = batch_read_total_us;
        batch_record.read_max_us = batch_read_max_us;
        batch_record.batch_elapsed_us = batch_end_us >= batch_status_begin_us ?
          batch_end_us - batch_status_begin_us : 0;
        batch_record.status_ret = batch_status_ret;
        batch_record.valid = batch_complete_samples > 0 ? 1u : 0u;
        batch_record.status_valid = batch_status_time_valid;
        batch_record.has_sequence = batch_has_sequence;
        probe_batch_trace_add(&batch_record);
        probe_batch_pending_complete(&batch_record);
        /* Events can be emitted by the consumer before this producer
         * snapshot reaches batch_end.  Complete their immutable association
         * here, keyed by the full generation identity rather than by the
         * bounded recent-batch ring. */
        if (batch_complete_samples > 0)
          {
            uint32_t ei;
            pthread_mutex_lock(&g_batch_assoc_lock);
            for (ei = 0; ei < g_latency_event_count; ei++)
              {
                struct probe_latency_event_record *event =
                  &g_latency_events[ei];
                if (event->session == g_serial_session &&
                    event->batch_id == fifo_batch_id &&
                    event->recovery_generation ==
                    batch_recovery_generation)
                  {
                    event->batch_complete_samples =
                      batch_complete_samples;
                    event->batch_first_sequence = batch_first_sequence;
                    event->batch_last_sequence = batch_last_sequence;
                    event->batch_enqueue_success = batch_enqueue_success;
                    event->batch_enqueue_failed = batch_enqueue_failed;
                    event->batch_end_us = batch_end_us;
                    event->batch_complete_valid = 1;
                    event->batch_assoc_reason = PROBE_BATCH_ASSOC_OK;
                  }
              }
            pthread_mutex_unlock(&g_batch_assoc_lock);
          }
        previous_batch_end_us = batch_end_us;
      }
      }
    }

stop_irq:
  if (irq_enabled)
    {
      cleanup_ret = board_imu_int1_disable();
      if (cleanup_ret != 0 && restore_status == 0)
        {
          restore_status = cleanup_ret;
        }
      irq_enabled = false;
    }

  ret = probe_imu_read(fd, PROBE_IMU_ADDR, PROBE_IMU_FIFO_STATUS1,
                       status, sizeof(status));
  if (ret == 0)
    {
      end_depth_words = (uint16_t)status[0] |
                        ((uint16_t)(status[1] & 0x07) << 8);
    }
  else
    {
      i2c_errors++;
    }

  if (fifo_started)
    {
      cleanup_ret = probe_imu_write_reg(fd, PROBE_IMU_ADDR,
                                        PROBE_IMU_FIFO_CTRL5,
                                        PROBE_IMU_FIFO_BYPASS);
      if (cleanup_ret != 0 && restore_status == 0)
        {
          restore_status = cleanup_ret;
        }
    }

restore:
#if PROBE_RANK_PIPELINE_ENABLED
  if (!ready_published)
    {
      printf("@IMU1,FAIL,session=%lu,reason=initialization\n",
             (unsigned long)g_serial_session);
#if BLE_PROBE_JOINT_UI_BLE
      __atomic_store_n(&g_joint_stop_requested, true, __ATOMIC_RELEASE);
#endif
    }
#endif
  /* Restore sensors, FIFO configuration/mode, pulse mode, then INT1. */

  for (i = 7; i < 10; i++)
    {
      if (saved_valid[i])
        {
          cleanup_ret = probe_imu_write_reg(fd, PROBE_IMU_ADDR,
                                            registers[i], saved[i]);
          if (cleanup_ret != 0 && restore_status == 0)
            {
              restore_status = cleanup_ret;
            }
        }
    }

  for (i = 0; i < 5; i++)
    {
      if (saved_valid[i])
        {
          cleanup_ret = probe_imu_write_reg(fd, PROBE_IMU_ADDR,
                                            registers[i], saved[i]);
          if (cleanup_ret != 0 && restore_status == 0)
            {
              restore_status = cleanup_ret;
            }
        }
    }

  for (i = 5; i < 7; i++)
    {
      if (saved_valid[i])
        {
          cleanup_ret = probe_imu_write_reg(fd, PROBE_IMU_ADDR,
                                            registers[i], saved[i]);
          if (cleanup_ret != 0 && restore_status == 0)
            {
              restore_status = cleanup_ret;
            }
        }
    }

  if (semaphore_initialized)
    {
      g_imu_drdy_sem_initialized = false;
      sem_destroy(&g_imu_drdy_sem);
    }

  fifo_irq_count = __atomic_load_n(&g_imu_drdy_irq_count,
                                    __ATOMIC_RELAXED);
  end_us = start_us;
  if (start_us != 0 && probe_get_monotonic_us(&end_us) < 0)
    {
      end_us = start_us;
    }

  probe_game_get_imu_stats(&final_game_imu_stats);

  printf("openvela_ble_probe: IMU FIFO summary duration_ms=%llu"
         " elapsed_ms=%llu"
         " fifo_irq_count=%lu fifo_wake_count=%lu fifo_words_read=%lu"
         " gyro_samples=%lu accel_samples=%lu"
         " paired_six_axis_samples=%lu startup_discarded_words=%lu"
         " fifo_watermark_events=%lu fifo_overrun=%lu fifo_smart_full=%lu"
         " max_fifo_depth_words=%lu unpaired_or_unexpected_frames=%lu"
         " i2c_errors=%lu timeouts=%lu timeout_status_polls=%lu"
         " fifo_recovery_attempts=%lu fifo_recovery_success=%lu"
         " fifo_recovery_failed=%lu imu_ring_full=%lu"
         " imu_gap_samples=%lu fifo_data_read_transfers=%lu"
         " fifo_data_read_words=%lu fifo_data_read_max_bytes=%lu"
         " fifo_data_read_chunk_bytes=%u end_depth_words=%u\n",
         (unsigned long long)(start_us == 0 || end_us < start_us ? 0 :
                              (end_us - start_us) / 1000),
         (unsigned long long)(start_us == 0 || end_us < start_us ? 0 :
                              (end_us - start_us) / 1000),
         (unsigned long)fifo_irq_count, (unsigned long)fifo_wake_count,
         (unsigned long)fifo_words_read, (unsigned long)gyro_samples,
         (unsigned long)accel_samples, (unsigned long)paired_samples,
         (unsigned long)startup_discarded_words,
         (unsigned long)fifo_watermark_events,
         (unsigned long)fifo_overrun, (unsigned long)fifo_smart_full,
         (unsigned long)max_fifo_depth_words,
         (unsigned long)unexpected_frames, (unsigned long)i2c_errors,
         (unsigned long)timeouts, (unsigned long)timeout_status_polls,
         (unsigned long)fifo_recovery_attempts,
         (unsigned long)fifo_recovery_success,
         (unsigned long)fifo_recovery_failed,
         (unsigned long)final_game_imu_stats.ring_full,
         (unsigned long)final_game_imu_stats.gap_samples,
         (unsigned long)fifo_data_read_transfers,
         (unsigned long)fifo_data_read_words,
         (unsigned long)fifo_data_read_max_bytes,
         PROBE_IMU_FIFO_READ_CHUNK_BYTES, end_depth_words);
  printf("openvela_ble_probe: IMU FIFO registers restored status=%d\n",
         restore_status);
  close(fd);
#if BLE_PROBE_JOINT_UI_BLE
  __atomic_fetch_or(&g_joint_stop_exit_done_steps,
                    PROBE_STOP_STEP_PRODUCER, __ATOMIC_RELEASE);
  __atomic_store_n(&g_joint_imu_thread_exited, true, __ATOMIC_RELEASE);
#endif
  return NULL;
}

static void probe_launch_imu_raw_if_pending(void)
{
#if !BLE_PROBE_JOINT_UI_BLE
  pthread_t thread;
#endif
  bool launch;
#if !BLE_PROBE_JOINT_UI_BLE
  int detach_ret;
#endif
  int ret;

  pthread_mutex_lock(&g_state_lock);
  launch = g_imu_raw_launch_pending && !g_imu_raw_launch_attempted;
  if (launch)
    {
      g_imu_raw_launch_pending = false;
      g_imu_raw_launch_attempted = true;
    }
  pthread_mutex_unlock(&g_state_lock);

  if (!launch)
    {
      return;
    }

#if BLE_PROBE_JOINT_UI_BLE
  __atomic_store_n(&g_joint_producer_running, true, __ATOMIC_RELEASE);
  __atomic_store_n(&g_joint_imu_thread_exited, false, __ATOMIC_RELEASE);
  ret = pthread_create(&g_joint_imu_thread, NULL, probe_imu_raw_thread, NULL);
#else
  ret = pthread_create(&thread, NULL, probe_imu_raw_thread, NULL);
#endif
  printf("openvela_ble_probe: IMU raw pthread create ret=%d\n", ret);
  if (ret != 0)
    {
#if BLE_PROBE_JOINT_UI_BLE
      __atomic_store_n(&g_joint_producer_running, false, __ATOMIC_RELEASE);
      __atomic_store_n(&g_joint_imu_thread_exited, true, __ATOMIC_RELEASE);
      __atomic_store_n(&g_joint_stop_requested, true, __ATOMIC_RELEASE);
#endif
      return;
    }

  pthread_mutex_lock(&g_state_lock);
  g_imu_raw_started = true;
#if BLE_PROBE_JOINT_UI_BLE
  g_joint_capture_started = true;
#endif
  pthread_mutex_unlock(&g_state_lock);

#if BLE_PROBE_JOINT_UI_BLE
  g_joint_imu_thread_created = true;
#else
  detach_ret = pthread_detach(thread);
  printf("openvela_ble_probe: IMU raw pthread detach ret=%d\n", detach_ret);
#endif
}

static bool probe_ui_ble_connected(void)
{
  bool connected;

  pthread_mutex_lock(&g_state_lock);
  connected = g_peer_connected;
  pthread_mutex_unlock(&g_state_lock);
  return connected;
}

#if BLE_PROBE_JOINT_UI_BLE
static bool probe_joint_resources_active(void)
{
  return g_instance != NULL || g_service_registered || g_adapter_enabled ||
         g_adapter_callback_cookie != NULL || g_advertiser != NULL ||
         g_adv_start_inflight || g_joint_imu_thread_created ||
         g_serial_thread_created || g_communication_thread_created ||
         g_joint_input_thread_created || g_joint_ble_cleanup_thread_created ||
         g_rank_runtime_started ||
         __atomic_load_n(&g_joint_producer_running, __ATOMIC_ACQUIRE) ||
         __atomic_load_n(&g_serial_running, __ATOMIC_ACQUIRE) ||
         __atomic_load_n(&g_communication_running, __ATOMIC_ACQUIRE) ||
         __atomic_load_n(&g_joint_input_running, __ATOMIC_ACQUIRE);
}

static void probe_sched_result_reset(struct probe_sched_result *result)
{
  result->query_ret = -1;
  result->policy = -1;
  result->priority = -1;
}

/* Reset one run's observability only after the previous run's resources have
 * gone quiescent.  This is deliberately shared by normal startup and the
 * runtime start path so a failed second startup cannot print stale stats. */
static void probe_joint_reset_run_state(void)
{
  hb_diag reset_diag;
  hb_reset();
  /* hb_reset() is the run-initialisation reset.  Keep the process
   * cumulative counter, but start the per-run delta after this reset so
   * failed starts do not report the initialisation itself as a runtime
   * anomaly. */
  hb_get_diag(&reset_diag);
  g_rank_reset_base = reset_diag.resets;
  g_serial_session = (uint32_t)clock_systime_ticks();
  g_serial_head = 0;
  g_serial_tail = 0;
  g_serial_queue_full = 0;
  g_serial_output = 0;
  g_serial_short_writes = 0;
  g_rank_generated = 0;
  g_rank_producer_stats_version = 0;
  memset(&g_rank_last_consistent_counts, 0,
         sizeof(g_rank_last_consistent_counts));
  g_rank_snapshot_fallbacks = 0;
  g_rank_samples_consumed = 0;
  g_rank_events = 0;
  g_latency_event_count = 0;
  g_latency_event_dropped = 0;
  g_latency_dump_requested = false;
  g_latency_dump_full = false;
  g_latency_dump_active = false;
  __atomic_store_n(&g_fifo_trace_count, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_trace_next, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_trace_dropped, 0, __ATOMIC_RELEASE);
  g_fifo_trace_recovery_generation = 0;
  __atomic_store_n(&g_fifo_overrun_total, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_recovery_attempts_total, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_recovery_success_total, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_recovery_failure_total, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_max_depth_words, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_recovery_last_reason, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_recovery_last_depth, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_recovery_last_status_ret, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_recovery_last_status_word, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_recovery_last_status_valid, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_recovery_pre_status_word, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_fifo_recovery_pre_status_valid, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_batch_trace_count, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_batch_trace_dropped, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_batch_trace_next, 0, __ATOMIC_RELEASE);
  g_batch_archive_count = 0;
  g_batch_archive_next = 0;
  memset(g_batch_pending, 0, sizeof(g_batch_pending));
  g_batch_pending_count = 0;
  g_batch_pending_overflow = 0;
  g_rank_event_written = 0;
  g_rank_queue_high_water = 0;
  g_rank_sequence_gap = 0;
  g_rank_gap_flags = 0;
  g_rank_feed_calls = 0;
  g_rank_feed_total_us = 0;
  g_rank_feed_max_us = 0;
  g_rank_queue_wait_calls = 0;
  g_rank_queue_wait_total_us = 0;
  g_rank_queue_wait_max_us = 0;
  g_rank_feed_elapsed_calls = 0;
  g_rank_feed_elapsed_total_us = 0;
  g_rank_feed_elapsed_max_us = 0;
  g_rank_encode_calls = 0;
  g_rank_encode_total_us = 0;
  g_rank_encode_max_us = 0;
  g_rank_write_calls = 0;
  g_rank_write_total_us = 0;
  g_rank_write_max_us = 0;
  g_rank_ui_submit_calls = 0;
  g_rank_ui_submit_total_us = 0;
  g_rank_ui_submit_max_us = 0;
  g_rank_diag_output_calls = 0;
  g_rank_diag_output_total_us = 0;
  g_rank_diag_output_max_us = 0;
  g_rank_fault_count = 0;
  g_rank_fault_dumped = 0;
  g_rank_fault_dropped = 0;
  g_rank_gen_fault_count = 0;
  g_rank_gen_fault_dumped = 0;
  g_rank_gen_time_faults = 0;
  g_rank_gen_time_dropped = 0;
  g_rank_prev_valid = false;
  g_beat_count = 0;
  memset(&g_rank_sched_consumer, 0, sizeof(g_rank_sched_consumer));
  probe_sched_result_reset(&g_rank_sched_consumer);
  probe_sched_result_reset(&g_rank_sched_fifo);
  g_rank_sched_fifo_requested_priority = -1;
  g_rank_sched_fifo_setup_ret = 0;
  probe_sched_result_reset(&g_rank_sched_input);
  probe_sched_result_reset(&g_rank_sched_ui);
  probe_sched_result_reset(&g_rank_sched_communication);
  g_rank_sched_setup_ret = 0;
  g_rank_sched_requested_priority = -1;
  memset(&g_rank_interval_log, 0, sizeof(g_rank_interval_log));
  g_joint_ui_run_valid = false;
  g_joint_capture_started = false;
  g_joint_ready_emitted = false;
}

static int probe_rank_runtime_start(void)
{
  int ret;
  pthread_attr_t attr;
  pthread_attr_t *attrp = NULL;
  struct sched_param param;
  int policy = SCHED_FIFO;
  bool attr_initialized = false;
  struct probe_rank_counts initial_counts = {0};
  uint64_t interval_start_us = 0;

  if (g_rank_runtime_started)
    {
      return 0;
    }

  probe_joint_reset_run_state();
  (void)probe_get_monotonic_us(&interval_start_us);
  memset(&initial_counts, 0, sizeof(initial_counts));
  probe_rank_interval_init(&g_rank_interval_log, interval_start_us,
                           &initial_counts);
  if (sem_init(&g_serial_sem, 0, 0) < 0)
    {
      return -errno;
    }
  if (sem_init(&g_latency_dump_sem, 0, 0) < 0)
    {
      sem_destroy(&g_serial_sem);
      return -errno;
    }
  __atomic_store_n(&g_latency_dump_thread_running, true, __ATOMIC_RELEASE);
  ret = pthread_create(&g_latency_dump_thread, NULL,
                       probe_latency_dump_thread, NULL);
  if (ret != 0)
    {
      __atomic_store_n(&g_latency_dump_thread_running, false, __ATOMIC_RELEASE);
      sem_destroy(&g_latency_dump_sem);
      sem_destroy(&g_serial_sem);
      return -ret;
    }
  g_latency_dump_thread_created = true;

  __atomic_store_n(&g_serial_running, 1, __ATOMIC_RELEASE);
  __atomic_store_n(&g_serial_thread_exited, false, __ATOMIC_RELEASE);
  if (g_rank_sched_mode != PROBE_SCHED_A)
    {
      struct sched_param current = {0};
      ret = pthread_getschedparam(pthread_self(), &policy, &current);
      if (ret == 0) ret = pthread_attr_init(&attr);
      if (ret == 0)
        {
          attr_initialized = true;
          ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        }
      if (ret == 0) ret = pthread_attr_setschedpolicy(&attr, policy);
      param = current;
      if (ret == 0)
        {
          param.sched_priority = current.sched_priority + 1;
          g_rank_sched_requested_priority = param.sched_priority;
        }
      if (ret == 0 &&
          (param.sched_priority >= CONFIG_BLUETOOTH_SERVICE_LOOP_THREAD_PRIORITY ||
           param.sched_priority > sched_get_priority_max(policy)))
        ret = EINVAL;
      if (ret == 0) ret = pthread_attr_setschedparam(&attr, &param);
      g_rank_sched_setup_ret = ret;
      if (ret != 0)
        {
      if (attr_initialized) pthread_attr_destroy(&attr);
      __atomic_store_n(&g_serial_running, 0, __ATOMIC_RELEASE);
      sem_destroy(&g_serial_sem);
      __atomic_store_n(&g_latency_dump_thread_running, false, __ATOMIC_RELEASE);
      sem_post(&g_latency_dump_sem); pthread_join(g_latency_dump_thread, NULL);
      g_latency_dump_thread_created = false; sem_destroy(&g_latency_dump_sem);
      return -ret;
        }
      attrp = &attr;
    }
  ret = pthread_create(&serial_thread, attrp,
                       probe_serial_output_thread, NULL);
  if (attr_initialized) pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      __atomic_store_n(&g_serial_running, 0, __ATOMIC_RELEASE);
      sem_destroy(&g_serial_sem);
      __atomic_store_n(&g_latency_dump_thread_running, false, __ATOMIC_RELEASE);
      sem_post(&g_latency_dump_sem); pthread_join(g_latency_dump_thread, NULL);
      g_latency_dump_thread_created = false; sem_destroy(&g_latency_dump_sem);
      return -ret;
    }

  g_serial_thread_created = true;
  probe_sched_query(serial_thread, &g_rank_sched_consumer);
  if (g_rank_sched_mode != PROBE_SCHED_A &&
      (g_rank_sched_consumer.query_ret != 0 ||
       g_rank_sched_consumer.priority != g_rank_sched_requested_priority ||
       g_rank_sched_consumer.policy != policy))
    {
      __atomic_store_n(&g_serial_running, 0, __ATOMIC_RELEASE);
      sem_post(&g_serial_sem);
      pthread_join(serial_thread, NULL);
      g_serial_thread_created = false;
      sem_destroy(&g_serial_sem);
      return -EIO;
    }
  g_rank_runtime_started = true;
  printf("openvela_ble_probe: onboard rank1 consumer started "
         "build_id=%s stride=%u sched_mode=%s policy=%d priority=%d "
         "requested_priority=%d setup_ret=%d query_ret=%d\n",
         PROBE_EXPERIMENT_BUILD_ID, (unsigned)HB_EVAL_STRIDE,
         probe_sched_mode_name(), g_rank_sched_consumer.policy,
         g_rank_sched_consumer.priority, g_rank_sched_requested_priority,
         g_rank_sched_setup_ret, g_rank_sched_consumer.query_ret);
  return 0;
}
#endif

static void probe_launch_visible_game_if_pending(void)
{
  bool launch;
  bool game_started;
  bool imu_skipped = false;
  int ret;

  pthread_mutex_lock(&g_state_lock);
  launch = g_visible_game_launch_pending &&
           !g_visible_game_launch_attempted;
  if (launch)
    {
      g_visible_game_launch_pending = false;
      g_visible_game_launch_attempted = true;
    }
  pthread_mutex_unlock(&g_state_lock);

  if (!launch)
    {
      return;
    }

#if BLE_PROBE_JOINT_UI_BLE
  ret = probe_rank_runtime_start();
  printf("openvela_ble_probe: rank1 consumer create status=%d\n", ret);
  game_started = ret == 0;
  if (!game_started)
    {
      __atomic_store_n(&g_joint_stop_requested, true, __ATOMIC_RELEASE);
    }
  if (game_started)
    {
      ret = probe_ui_start_beat(probe_ui_ble_connected, g_imu_detected,
                                g_joint_ui_refresh_mode);
      printf("openvela_ble_probe: beat UI thread create status=%d\n", ret);
      g_joint_ui_run_valid = ret == 0;
      if (ret != 0)
        {
          __atomic_store_n(&g_joint_stop_requested, true,
                           __ATOMIC_RELEASE);
        }
    }
#else
  ret = probe_game_start();
  printf("openvela_ble_probe: game thread create status=%d\n", ret);
  game_started = ret == 0;
  ret = probe_ui_start(probe_ui_ble_connected, g_imu_detected);
  printf("openvela_ble_probe: UI thread create status=%d\n", ret);
#endif

  pthread_mutex_lock(&g_state_lock);
  if (game_started && g_imu_detected && !g_imu_raw_launch_attempted &&
      !g_imu_raw_started)
    {
      g_imu_raw_launch_pending = true;
    }
  else if (!g_imu_detected)
    {
      imu_skipped = true;
    }
  pthread_mutex_unlock(&g_state_lock);

  if (imu_skipped)
    {
      printf("openvela_ble_probe: IMU FIFO producer skipped:"
             " IMU unavailable\n");
    }
}

static int probe_monotonic_ms(uint64_t *now_ms)
{
  uint64_t now_us;

  if (probe_get_monotonic_us(&now_us) < 0)
    {
      return -1;
    }

  *now_ms = now_us / 1000ULL;
  return 0;
}

static uint32_t probe_get_le32(const uint8_t *value)
{
  return (uint32_t)value[0] |
         ((uint32_t)value[1] << 8) |
         ((uint32_t)value[2] << 16) |
         ((uint32_t)value[3] << 24);
}

static void probe_put_le32(uint8_t *value, uint32_t number)
{
  value[0] = (uint8_t)number;
  value[1] = (uint8_t)(number >> 8);
  value[2] = (uint8_t)(number >> 16);
  value[3] = (uint8_t)(number >> 24);
}

static uint16_t probe_get_le16(const uint8_t *value)
{
  return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static void probe_put_le16(uint8_t *value, uint16_t number)
{
  value[0] = (uint8_t)number;
  value[1] = (uint8_t)(number >> 8);
}

static uint64_t probe_get_le64(const uint8_t *value)
{
  return (uint64_t)value[0] |
         ((uint64_t)value[1] << 8) |
         ((uint64_t)value[2] << 16) |
         ((uint64_t)value[3] << 24) |
         ((uint64_t)value[4] << 32) |
         ((uint64_t)value[5] << 40) |
         ((uint64_t)value[6] << 48) |
         ((uint64_t)value[7] << 56);
}

#if !BLE_PROBE_JOINT_UI_BLE
static void probe_put_le64(uint8_t *value, uint64_t number)
{
  value[0] = (uint8_t)number;
  value[1] = (uint8_t)(number >> 8);
  value[2] = (uint8_t)(number >> 16);
  value[3] = (uint8_t)(number >> 24);
  value[4] = (uint8_t)(number >> 32);
  value[5] = (uint8_t)(number >> 40);
  value[6] = (uint8_t)(number >> 48);
  value[7] = (uint8_t)(number >> 56);
}
#endif

static uint16_t probe_crc16_ccitt_false(const uint8_t *value, size_t length)
{
  uint16_t crc = 0xffff;
  size_t i;
  uint8_t bit;

  for (i = 0; i < length; i++)
    {
      crc ^= (uint16_t)value[i] << 8;
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc & 0x8000) != 0 ?
                (uint16_t)((crc << 1) ^ 0x1021) :
                (uint16_t)(crc << 1);
        }
    }

  return crc;
}

static bt_status_t probe_notify(gatts_handle_t srv_handle,
                                uint8_t *value, uint16_t length)
{
  bt_address_t peer_addr;
  bool ready;

  pthread_mutex_lock(&g_state_lock);
  ready = g_peer_connected && g_notify_enabled;
  memcpy(&peer_addr, &g_peer_addr, sizeof(peer_addr));
  pthread_mutex_unlock(&g_state_lock);

  if (!ready)
    {
      printf("openvela_ble_probe: TX notification not subscribed\n");
      return BT_STATUS_NOT_READY;
    }

  return bt_gatts_notify(srv_handle, &peer_addr, PROBE_TX_HANDLE,
                         value, length);
}

#if !BLE_PROBE_CAPTURE_ACK_ONLY
static bool probe_capture_take(struct probe_capture_sample_s *sample)
{
  uint32_t tail = __atomic_load_n(&g_capture_tail, __ATOMIC_RELAXED);
  uint32_t head = __atomic_load_n(&g_capture_head, __ATOMIC_ACQUIRE);

  if (tail == head)
    {
      return false;
    }

  memcpy(sample, &g_capture_queue[tail % PROBE_CAPTURE_QUEUE_DEPTH],
         sizeof(*sample));
  __atomic_store_n(&g_capture_tail, tail + 1, __ATOMIC_RELEASE);
  return true;
}
#endif

static void probe_capture_drain(uint32_t generation, uint32_t limit)
{
#if BLE_PROBE_CAPTURE_ACK_ONLY
  (void)generation;
  (void)limit;
#else
  struct probe_capture_sample_s sample;
  uint8_t frame[PROBE_CAPTURE_SAMPLE_SIZE];
  uint32_t count = 0;
  bt_status_t status;

  while (count < limit && probe_capture_take(&sample))
    {
      count++;
      if (sample.connection_generation != generation)
        {
          continue;
        }

      frame[0] = PROBE_CAPTURE_SAMPLE_TYPE;
      frame[1] = PROBE_CAPTURE_PROTOCOL_VERSION;
      frame[2] = sample.flags;
      probe_put_le32(&frame[3], sample.sequence);
      probe_put_le32(&frame[7], sample.uptime_ms);
      probe_put_le16(&frame[11], (uint16_t)sample.accel[0]);
      probe_put_le16(&frame[13], (uint16_t)sample.accel[1]);
      probe_put_le16(&frame[15], (uint16_t)sample.accel[2]);
      status = probe_notify(g_service_handle, frame, sizeof(frame));
      if (status == BT_STATUS_SUCCESS)
        {
          __atomic_fetch_add(&g_capture_notify_success, 1,
                             __ATOMIC_RELAXED);
        }
      else
        {
          __atomic_fetch_add(&g_capture_notify_failure, 1,
                             __ATOMIC_RELAXED);
        }
    }
#endif
}

static bt_status_t probe_capture_control(uint8_t subtype,
                                         uint8_t status_code,
                                         uint32_t session_id,
                                         const uint32_t *values,
                                         size_t count)
{
  uint8_t frame[PROBE_CAPTURE_CONTROL_MAX_SIZE];
  size_t i;
  size_t length = 8 + count * sizeof(uint32_t);

  if (length > sizeof(frame))
    {
      return BT_STATUS_PARM_INVALID;
    }

  frame[0] = PROBE_CAPTURE_CONTROL_TYPE;
  frame[1] = PROBE_CAPTURE_PROTOCOL_VERSION;
  frame[2] = subtype;
  frame[3] = status_code;
  probe_put_le32(&frame[4], session_id);
  for (i = 0; i < count; i++)
    {
      probe_put_le32(&frame[8 + i * sizeof(uint32_t)], values[i]);
    }

  return probe_notify(g_service_handle, frame, (uint16_t)length);
}

static bt_status_t probe_capture_start_ack(uint32_t session_id,
                                           uint32_t command_generation,
                                           uint8_t status_code)
{
  uint32_t current_generation;
  bool connected;
  bool notify_enabled;
  bt_status_t status;

  pthread_mutex_lock(&g_state_lock);
  current_generation =
    __atomic_load_n(&g_connection_generation, __ATOMIC_ACQUIRE);
  connected = g_peer_connected;
  notify_enabled = g_notify_enabled;
  pthread_mutex_unlock(&g_state_lock);

  printf("openvela_ble_probe: capture_start_ack notify_call_begin "
         "token=%lu command_generation=%lu current_generation=%lu "
         "connected=%d notify_enabled=%d ack_length=8 ack_status=%u\n",
         (unsigned long)session_id, (unsigned long)command_generation,
         (unsigned long)current_generation, connected, notify_enabled,
         status_code);
  status = probe_capture_control(1, status_code, session_id, NULL, 0);
  printf("openvela_ble_probe: capture_start_ack notify_submit_return "
         "token=%lu command_generation=%lu current_generation=%lu "
         "status=%d notify_submit_success=%d host_ack_received=unknown\n",
         (unsigned long)session_id, (unsigned long)command_generation,
         (unsigned long)current_generation, status,
         status == BT_STATUS_SUCCESS);
  return status;
}

static void probe_capture_start(uint32_t session_id,
                                uint32_t connection_generation)
{
  uint32_t head;
  uint32_t current_generation;
  bt_status_t ack_status;
  bool connected;
  bool notify_enabled;
  bool ready;

  if (__atomic_load_n(&g_capture_active, __ATOMIC_ACQUIRE))
    {
      uint32_t log_count;

      probe_capture_start_ack(session_id, connection_generation, 3);
      log_count = __atomic_add_fetch(&g_capture_active_reject_logs, 1,
                                     __ATOMIC_RELAXED);
      if (log_count <= PROBE_CAPTURE_LOG_LIMIT)
        {
          printf("openvela_ble_probe: capture start active_reject token=%lu "
                 "active_token=%lu command_generation=%lu log=%lu/%u\n",
                 (unsigned long)session_id,
                 (unsigned long)g_capture_session_id,
                 (unsigned long)connection_generation,
                 (unsigned long)log_count, PROBE_CAPTURE_LOG_LIMIT);
        }
      return;
    }

  pthread_mutex_lock(&g_state_lock);
  current_generation =
    __atomic_load_n(&g_connection_generation, __ATOMIC_ACQUIRE);
  connected = g_peer_connected;
  notify_enabled = g_notify_enabled;
  ready = connected && notify_enabled &&
          connection_generation == current_generation;
  pthread_mutex_unlock(&g_state_lock);
  if (!ready)
    {
      uint32_t log_count;

      probe_capture_start_ack(session_id, connection_generation, 1);
      log_count = __atomic_add_fetch(&g_capture_ready_reject_logs, 1,
                                     __ATOMIC_RELAXED);
      if (log_count <= PROBE_CAPTURE_LOG_LIMIT)
        {
          printf("openvela_ble_probe: capture start readiness_reject "
                 "token=%lu command_generation=%lu current_generation=%lu "
                 "connected=%d notify_enabled=%d log=%lu/%u\n",
                 (unsigned long)session_id,
                 (unsigned long)connection_generation,
                 (unsigned long)current_generation, connected,
                 notify_enabled, (unsigned long)log_count,
                 PROBE_CAPTURE_LOG_LIMIT);
        }
      return;
    }

  __atomic_store_n(&g_capture_active, false, __ATOMIC_RELEASE);
  head = __atomic_load_n(&g_capture_head, __ATOMIC_ACQUIRE);
  __atomic_store_n(&g_capture_tail, head, __ATOMIC_RELEASE);
  __atomic_store_n(&g_capture_generated, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_capture_enqueued, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_capture_queue_full, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_capture_notify_success, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_capture_notify_failure, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_capture_last_sequence, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_capture_gap_samples, 0, __ATOMIC_RELAXED);
  g_capture_session_id = session_id;
  g_capture_fifo_recovery_start =
    __atomic_load_n(&g_fifo_recovery_total, __ATOMIC_RELAXED);
  __atomic_store_n(&g_capture_generation, connection_generation,
                   __ATOMIC_RELAXED);
  ack_status = probe_capture_start_ack(session_id, connection_generation, 0);
  if (ack_status != BT_STATUS_SUCCESS)
    {
      printf("openvela_ble_probe: capture start submit_reject token=%lu "
             "generation=%lu status=%d session_not_started=1\n",
             (unsigned long)session_id,
             (unsigned long)connection_generation, ack_status);
      return;
    }

  pthread_mutex_lock(&g_state_lock);
  current_generation =
    __atomic_load_n(&g_connection_generation, __ATOMIC_ACQUIRE);
  ready = g_peer_connected && g_notify_enabled &&
          connection_generation == current_generation;
  pthread_mutex_unlock(&g_state_lock);
  if (!ready)
    {
      printf("openvela_ble_probe: capture start post_submit_cancel "
             "token=%lu command_generation=%lu current_generation=%lu\n",
             (unsigned long)session_id,
             (unsigned long)connection_generation,
             (unsigned long)current_generation);
      return;
    }

  __atomic_store_n(&g_capture_active, true, __ATOMIC_RELEASE);
  printf("openvela_ble_probe: capture started session=%lu generation=%lu\n",
         (unsigned long)session_id,
         (unsigned long)connection_generation);
#if BLE_PROBE_CAPTURE_ACK_ONLY
  printf("openvela_ble_probe: capture ACK-only diagnostic mode active; "
         "sample production and transmission disabled; not capture pass\n");
#endif
}

static void probe_capture_print_stats(const char *reason)
{
  uint32_t recovery =
    __atomic_load_n(&g_fifo_recovery_total, __ATOMIC_RELAXED) -
    g_capture_fifo_recovery_start;

  printf("openvela_ble_probe: capture %s session=%lu generated=%lu "
         "enqueued=%lu queue_full=%lu notify_success=%lu "
         "notify_failure=%lu last_sequence=%lu gap=%lu "
         "fifo_recovery=%lu\n",
         reason, (unsigned long)g_capture_session_id,
         (unsigned long)__atomic_load_n(&g_capture_generated,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_capture_enqueued,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_capture_queue_full,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_capture_notify_success,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_capture_notify_failure,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_capture_last_sequence,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_capture_gap_samples,
                                         __ATOMIC_RELAXED),
         (unsigned long)recovery);
}

static void probe_capture_stop(uint32_t session_id,
                               uint32_t connection_generation)
{
  uint32_t values[3];
  uint32_t recovery;

  if (!__atomic_load_n(&g_capture_active, __ATOMIC_ACQUIRE) ||
      session_id != g_capture_session_id ||
      connection_generation !=
        __atomic_load_n(&g_capture_generation, __ATOMIC_RELAXED))
    {
      probe_capture_control(5, 2, session_id, NULL, 0);
      return;
    }

  __atomic_store_n(&g_capture_active, false, __ATOMIC_RELEASE);
  probe_capture_drain(connection_generation, UINT32_MAX);
  recovery =
    __atomic_load_n(&g_fifo_recovery_total, __ATOMIC_RELAXED) -
    g_capture_fifo_recovery_start;

  values[0] = __atomic_load_n(&g_capture_generated, __ATOMIC_RELAXED);
  values[1] = __atomic_load_n(&g_capture_enqueued, __ATOMIC_RELAXED);
  values[2] = __atomic_load_n(&g_capture_queue_full, __ATOMIC_RELAXED);
  probe_capture_control(2, 0, session_id, values, 3);
  values[0] = __atomic_load_n(&g_capture_notify_success, __ATOMIC_RELAXED);
  values[1] = __atomic_load_n(&g_capture_notify_failure, __ATOMIC_RELAXED);
  values[2] = __atomic_load_n(&g_capture_gap_samples, __ATOMIC_RELAXED);
  probe_capture_control(3, 0, session_id, values, 3);
  values[0] = __atomic_load_n(&g_capture_last_sequence, __ATOMIC_RELAXED);
  values[1] = recovery;
  probe_capture_control(4, 0, session_id, values, 2);
  probe_capture_control(5, 0, session_id, NULL, 0);
  probe_capture_print_stats("stopped");
}

static uint32_t probe_elapsed_ms(uint64_t started_us)
{
  uint64_t ended_us;
  uint64_t milliseconds;

  if (probe_get_monotonic_us(&ended_us) < 0 || ended_us < started_us)
    {
      return 0;
    }

  milliseconds = (ended_us - started_us) / 1000ULL;
  return milliseconds > UINT32_MAX ? UINT32_MAX : (uint32_t)milliseconds;
}

static void probe_print_rx_queue_stats(void)
{
  printf("openvela_ble_probe: rx queue enqueued=%lu lock_busy=%lu "
         "full=%lu high_water=%lu stale=%lu invalid_length=%lu\n",
         (unsigned long)__atomic_load_n(&g_rx_enqueued,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_rx_drop_lock_busy,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_rx_drop_full,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_rx_high_water,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_rx_stale,
                                         __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_rx_invalid_length,
                                         __ATOMIC_RELAXED));
}

static void probe_print_reliable39_stats(void)
{
  printf("openvela_ble_probe: reliable39_rx_ok=%lu "
         "reliable39_bad_length=%lu reliable39_bad_crc=%lu "
         "reliable39_duplicates=%lu reliable39_tx_attempted=%lu "
         "reliable39_tx_status_nonzero=%lu\n",
         (unsigned long)__atomic_load_n(&g_reliable39_rx_ok,
                                        __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_reliable39_bad_length,
                                        __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_reliable39_bad_crc,
                                        __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_reliable39_duplicates,
                                        __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_reliable39_tx_attempted,
                                        __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&g_reliable39_tx_status_nonzero,
                                        __ATOMIC_RELAXED));
}

static void probe_print_gesture_stats(void)
{
  uint32_t enqueued;
  uint32_t full;
  uint32_t acked;
  uint32_t retransmitted;
  uint32_t dropped;
  uint32_t tx_status_nonzero;

  pthread_mutex_lock(&g_state_lock);
  enqueued = g_gesture_enqueued;
  full = g_gesture_queue_full;
  acked = g_gesture_acked;
  retransmitted = g_gesture_retransmitted;
  dropped = g_gesture_dropped;
  tx_status_nonzero = g_gesture_tx_status_nonzero;
  pthread_mutex_unlock(&g_state_lock);

  printf("openvela_ble_probe: gesture enqueued=%lu full=%lu acked=%lu "
         "retransmitted=%lu dropped=%lu tx_status_nonzero=%lu\n",
         (unsigned long)enqueued,
         (unsigned long)full,
         (unsigned long)acked,
         (unsigned long)retransmitted,
         (unsigned long)dropped,
         (unsigned long)tx_status_nonzero);
}

/* Future gesture producers must use this bounded publisher rather than
 * calling a GATT API.  It is task-context only, never ISR-context.
 */

#if !BLE_PROBE_JOINT_UI_BLE
static int probe_publish_gesture(uint8_t gesture, uint8_t confidence,
                                 uint8_t flags, uint8_t battery,
                                 uint64_t event_monotonic_us)
{
  struct probe_gesture_record_s *record;
  uint64_t now_monotonic_us;
  uint64_t timestamp_ms;
  uint32_t generation;
  uint32_t used;
  bool synced;

  if (probe_get_monotonic_us(&now_monotonic_us) < 0)
    {
      return -1;
    }

  if (event_monotonic_us == 0)
    {
      event_monotonic_us = now_monotonic_us;
    }

  pthread_mutex_lock(&g_state_lock);
  generation = __atomic_load_n(&g_connection_generation, __ATOMIC_ACQUIRE);
  if (!g_peer_connected)
    {
      pthread_mutex_unlock(&g_state_lock);
      return -1;
    }

  used = g_gesture_rx_head - g_gesture_rx_tail;
  if (used >= PROBE_GESTURE_QUEUE_DEPTH)
    {
      g_gesture_queue_full++;
      pthread_mutex_unlock(&g_state_lock);
      return -1;
    }

  synced = g_gesture_time_synced;
  if (synced && event_monotonic_us < g_gesture_sync_monotonic_us)
    {
      event_monotonic_us = now_monotonic_us;
    }

  timestamp_ms = synced ?
    g_gesture_sync_unix_ms +
      (event_monotonic_us - g_gesture_sync_monotonic_us) / 1000ULL :
    event_monotonic_us / 1000ULL;
  if (synced)
    {
      flags |= 0x01;
    }
  else
    {
      flags &= (uint8_t)~0x01;
    }

  record = &g_gesture_queue[g_gesture_rx_head %
                            PROBE_GESTURE_QUEUE_DEPTH];
  memset(record, 0, sizeof(*record));
  record->connection_generation = generation;
  record->packet_id = g_gesture_next_packet_id++;
  record->event[0] = 1;
  record->event[1] = 0x01;
  probe_put_le16(&record->event[2], record->packet_id);
  probe_put_le64(&record->event[4], timestamp_ms);
  record->event[12] = gesture;
  record->event[13] = confidence;
  record->event[14] = flags;
  record->event[15] = battery;
  g_gesture_rx_head++;
  g_gesture_enqueued++;
  pthread_mutex_unlock(&g_state_lock);

  sem_post(&g_rx_sem);
  return 0;
}
#endif

static void probe_gesture_time_sync(const uint8_t *value)
{
  uint64_t monotonic_us;
  uint64_t unix_ms;

  if (probe_get_monotonic_us(&monotonic_us) < 0)
    {
      printf("openvela_ble_probe: gesture time sync clock failed\n");
      return;
    }

  pthread_mutex_lock(&g_state_lock);
  unix_ms = probe_get_le64(value);
  g_gesture_sync_unix_ms = unix_ms;
  g_gesture_sync_monotonic_us = monotonic_us;
  g_gesture_time_synced = true;
  pthread_mutex_unlock(&g_state_lock);
  printf("openvela_ble_probe: gesture time synchronized unix_ms=%" PRIu64
         " monotonic_us=%" PRIu64 "\n", unix_ms, monotonic_us);
}

static void probe_gesture_ack(uint16_t packet_id, uint32_t generation)
{
  struct probe_gesture_record_s *record;
  bool matched = false;

  pthread_mutex_lock(&g_state_lock);
  if (g_gesture_rx_tail != g_gesture_rx_head)
    {
      record = &g_gesture_queue[g_gesture_rx_tail %
                                PROBE_GESTURE_QUEUE_DEPTH];
      if (record->connection_generation == generation &&
          record->packet_id == packet_id && record->send_count != 0)
        {
          memset(record, 0, sizeof(*record));
          g_gesture_rx_tail++;
          g_gesture_acked++;
          matched = true;
        }
    }
  pthread_mutex_unlock(&g_state_lock);

  if (matched)
    {
      printf("openvela_ble_probe: gesture ACK packet_id=%u\n", packet_id);
    }
}

static void probe_process_gesture_tx(void)
{
  static const uint16_t retry_delays_ms[PROBE_GESTURE_MAX_SENDS] =
    {
      500, 1000, 2000
    };
  struct probe_gesture_record_s *record;
  uint8_t event[PROBE_GESTURE_EVENT_SIZE];
  uint64_t now_ms;
  uint32_t generation;
  uint16_t packet_id = 0;
  uint8_t send_count = 0;
  bool send = false;
  bool dropped = false;
  bt_status_t status;

  if (probe_monotonic_ms(&now_ms) < 0)
    {
      return;
    }

  pthread_mutex_lock(&g_state_lock);
  generation = __atomic_load_n(&g_connection_generation, __ATOMIC_ACQUIRE);
  if (g_gesture_rx_tail != g_gesture_rx_head)
    {
      record = &g_gesture_queue[g_gesture_rx_tail %
                                PROBE_GESTURE_QUEUE_DEPTH];
      if (!g_peer_connected || !g_notify_enabled ||
          record->connection_generation != generation)
        {
          if (record->connection_generation != generation)
            {
              g_gesture_rx_tail = g_gesture_rx_head;
            }
        }
      else if (record->send_count == 0 || now_ms >= record->next_retry_ms)
        {
          if (record->send_count >= PROBE_GESTURE_MAX_SENDS)
            {
              packet_id = record->packet_id;
              memset(record, 0, sizeof(*record));
              g_gesture_rx_tail++;
              g_gesture_dropped++;
              dropped = true;
            }
          else
            {
              packet_id = record->packet_id;
              memcpy(event, record->event, sizeof(event));
              record->send_count++;
              send_count = record->send_count;
              record->next_retry_ms =
                now_ms + retry_delays_ms[send_count - 1];
              if (send_count > 1)
                {
                  g_gesture_retransmitted++;
                }
              send = true;
            }
        }
    }
  pthread_mutex_unlock(&g_state_lock);

  if (dropped)
    {
      printf("openvela_ble_probe: gesture dropped packet_id=%u\n",
             packet_id);
      return;
    }

  if (!send)
    {
      return;
    }

  status = probe_notify(g_service_handle, event, sizeof(event));
  if (status != BT_STATUS_SUCCESS)
    {
      pthread_mutex_lock(&g_state_lock);
      g_gesture_tx_status_nonzero++;
      pthread_mutex_unlock(&g_state_lock);
    }

  if (send_count > 1)
    {
      printf("openvela_ble_probe: gesture retransmit packet_id=%u "
             "attempt=%u status=%d\n",
             packet_id, send_count, status);
    }
}

static uint16_t probe_tx_cccd_written(gatts_handle_t srv_handle,
                                      bt_address_t *addr,
                                      uint16_t attr_handle,
                                      const uint8_t *value,
                                      uint16_t length,
                                      uint16_t offset)
{
  bool capture_was_active = false;
  bool notify_enabled;
  uint16_t cccd;

  (void)srv_handle;
  (void)attr_handle;

  if (offset != 0 || length < 2)
    {
      printf("openvela_ble_probe: invalid CCCD write\n");
      return 0;
    }

  cccd = (uint16_t)value[0] | ((uint16_t)value[1] << 8);
  pthread_mutex_lock(&g_state_lock);
  memcpy(&g_peer_addr, addr, sizeof(g_peer_addr));
  g_notify_enabled = (cccd & GATT_CCC_NOTIFY) != 0;
  notify_enabled = g_notify_enabled;
  if (!notify_enabled)
    {
      capture_was_active = __atomic_exchange_n(&g_capture_active, false,
                                                __ATOMIC_ACQ_REL);
    }
  /* The visible-game build starts its long-lived FIFO producer after the
   * first successful advertising start, not from this GATT callback.
   */
  pthread_mutex_unlock(&g_state_lock);

  printf("openvela_ble_probe: TX notify=%d\n", notify_enabled);
  if (capture_was_active)
    {
      probe_capture_print_stats("notifications_disabled");
      sem_post(&g_rx_sem);
    }

  return length;
}

static void probe_process_frame(gatts_handle_t srv_handle,
                                const uint8_t *value, uint16_t length,
                                uint32_t connection_generation)
{
  uint8_t response[21];
  uint8_t *reply;
  uint16_t reply_len;
  uint32_t elapsed_ms;
  uint32_t id_or_seq;
  uint32_t seq;
  bt_status_t status;

  if (length == 4 && memcmp(value, "PING", 4) == 0)
    {
      reply = g_pong;
      reply_len = sizeof(g_pong);
      printf("openvela_ble_probe: RX PING\n");
    }
  else if (length >= 1 && value[0] == 0x01)
    {
      if (length < 5)
        {
          printf("openvela_ble_probe: short binary PING length=%u\n",
                 length);
          return;
        }

      id_or_seq = probe_get_le32(&value[1]);
      response[0] = 0x81;
      probe_put_le32(&response[1], id_or_seq);
      printf("openvela_ble_probe: RX binary PING seq=%lu\n",
             (unsigned long)id_or_seq);
      status = probe_notify(srv_handle, response, 5);
      printf("openvela_ble_probe: TX binary PONG status=%d\n", status);
      return;
    }
  else if (length >= 1 && value[0] == PROBE_RELIABLE39_REQUEST_TYPE)
    {
      uint16_t expected_crc;
      uint16_t received_crc;

      if (length != PROBE_RELIABLE39_FRAME_SIZE)
        {
          __atomic_fetch_add(&g_reliable39_bad_length, 1,
                             __ATOMIC_RELAXED);
          return;
        }

      received_crc = probe_get_le16(&value[PROBE_RELIABLE39_CRC_OFFSET]);
      expected_crc = probe_crc16_ccitt_false(
        value, PROBE_RELIABLE39_CRC_OFFSET);
      if (received_crc != expected_crc)
        {
          __atomic_fetch_add(&g_reliable39_bad_crc, 1, __ATOMIC_RELAXED);
          return;
        }

      id_or_seq = probe_get_le32(&value[1]);
      if (g_reliable39.valid &&
          g_reliable39.connection_generation == connection_generation &&
          g_reliable39.sequence == id_or_seq)
        {
          __atomic_fetch_add(&g_reliable39_duplicates, 1,
                             __ATOMIC_RELAXED);
        }
      else
        {
          g_reliable39.response[0] = PROBE_RELIABLE39_RESPONSE_TYPE;
          probe_put_le32(&g_reliable39.response[1], id_or_seq);
          memcpy(&g_reliable39.response[5], &value[5],
                 PROBE_RELIABLE39_PAYLOAD_SIZE);
          probe_put_le16(&g_reliable39.response[PROBE_RELIABLE39_CRC_OFFSET],
                         probe_crc16_ccitt_false(
                           g_reliable39.response,
                           PROBE_RELIABLE39_CRC_OFFSET));
          g_reliable39.connection_generation = connection_generation;
          g_reliable39.sequence = id_or_seq;
          g_reliable39.valid = true;
          __atomic_fetch_add(&g_reliable39_rx_ok, 1, __ATOMIC_RELAXED);
        }

      __atomic_fetch_add(&g_reliable39_tx_attempted, 1, __ATOMIC_RELAXED);
      status = probe_notify(srv_handle, g_reliable39.response,
                            sizeof(g_reliable39.response));
      if (status != BT_STATUS_SUCCESS)
        {
          __atomic_fetch_add(&g_reliable39_tx_status_nonzero, 1,
                             __ATOMIC_RELAXED);
        }

      return;
    }
  else if (length >= 1 && value[0] == PROBE_GESTURE_TIME_SYNC_TYPE)
    {
      if (length != 9)
        {
          printf("openvela_ble_probe: invalid gesture time sync length=%u\n",
                 length);
          return;
        }

      probe_gesture_time_sync(&value[1]);
      return;
    }
  else if (length >= 1 && value[0] == PROBE_GESTURE_ACK_TYPE)
    {
      if (length != 3)
        {
          printf("openvela_ble_probe: invalid gesture ACK length=%u\n",
                 length);
          return;
        }

      probe_gesture_ack(probe_get_le16(&value[1]),
                        connection_generation);
      return;
    }
  else if (length >= 1 && value[0] == PROBE_GESTURE_INJECT_TYPE)
    {
      if (length != 5)
        {
          printf("openvela_ble_probe: invalid gesture inject length=%u\n",
                 length);
          return;
        }

#if BLE_PROBE_GESTURE_TEST_INJECT && !BLE_PROBE_JOINT_UI_BLE
      {
        struct probe_game_command command;

        command.connection_generation = connection_generation;
        command.gesture = value[1];
        command.confidence = value[2];
        command.flags = value[3];
        command.battery = value[4];
        printf("openvela_ble_probe: gesture test inject game_queue=%d\n",
               probe_game_submit_command(&command));
      }
#else
      printf("openvela_ble_probe: gesture test inject disabled in this "
             "runtime mode\n");
#endif
      return;
    }
  else if (length >= 1 && value[0] == PROBE_CAPTURE_START_TYPE)
    {
      uint32_t log_count =
        __atomic_add_fetch(&g_capture_start_rx_logs, 1, __ATOMIC_RELAXED);

      if (log_count <= PROBE_CAPTURE_LOG_LIMIT)
        {
          printf("openvela_ble_probe: capture start parse_enter "
                 "length=%u command_generation=%lu log=%lu/%u\n",
                 length, (unsigned long)connection_generation,
                 (unsigned long)log_count, PROBE_CAPTURE_LOG_LIMIT);
        }

      if (length != 5)
        {
          printf("openvela_ble_probe: invalid capture start length=%u\n",
                 length);
          return;
        }

      probe_capture_start(probe_get_le32(&value[1]),
                          connection_generation);
      return;
    }
  else if (length >= 1 && value[0] == PROBE_CAPTURE_STOP_TYPE)
    {
      if (length != 5)
        {
          printf("openvela_ble_probe: invalid capture stop length=%u\n",
                 length);
          return;
        }

      probe_capture_stop(probe_get_le32(&value[1]),
                         connection_generation);
      return;
    }
  else if (length >= 1 && value[0] == 0x10)
    {
      if (length < 5)
        {
          printf("openvela_ble_probe: short START length=%u\n", length);
          return;
        }

      id_or_seq = probe_get_le32(&value[1]);
      memset(&g_throughput, 0, sizeof(g_throughput));
      g_throughput.active = true;
      g_throughput.test_id = id_or_seq;

      response[0] = 0x90;
      probe_put_le32(&response[1], id_or_seq);
      printf("openvela_ble_probe: throughput START test_id=%lu\n",
             (unsigned long)id_or_seq);
      status = probe_notify(srv_handle, response, 5);
      printf("openvela_ble_probe: throughput START_ACK status=%d\n", status);
      return;
    }
  else if (length >= 1 && value[0] == 0x11)
    {
      if (length < 5)
        {
          printf("openvela_ble_probe: short DATA length=%u\n", length);
          return;
        }

      if (!g_throughput.active)
        {
          printf("openvela_ble_probe: DATA without active test\n");
          return;
        }

      seq = probe_get_le32(&value[1]);
      if (!g_throughput.timing)
        {
          if (probe_get_monotonic_us(&g_throughput.started_us) == 0)
            {
              g_throughput.timing = true;
            }
          else
            {
              printf("openvela_ble_probe: monotonic clock failed\n");
            }
        }

      g_throughput.frames++;
      g_throughput.payload_bytes += length - 5;

      if (!g_throughput.have_expected_seq)
        {
          g_throughput.expected_seq = seq + 1;
          g_throughput.have_expected_seq = true;
        }
      else if (seq == g_throughput.expected_seq)
        {
          g_throughput.expected_seq++;
        }
      else if (seq > g_throughput.expected_seq)
        {
          g_throughput.missing_frames += seq - g_throughput.expected_seq;
          g_throughput.expected_seq = seq + 1;
        }
      else
        {
          g_throughput.missing_frames++;
        }

      return;
    }
  else if (length >= 1 && value[0] == 0x12)
    {
      if (length < 5)
        {
          printf("openvela_ble_probe: short END length=%u\n", length);
          return;
        }

      id_or_seq = probe_get_le32(&value[1]);
      if (!g_throughput.active || id_or_seq != g_throughput.test_id)
        {
          printf("openvela_ble_probe: unmatched END test_id=%lu\n",
                 (unsigned long)id_or_seq);
          return;
        }

      elapsed_ms = g_throughput.timing ?
                   probe_elapsed_ms(g_throughput.started_us) : 0;
      response[0] = 0x91;
      probe_put_le32(&response[1], g_throughput.test_id);
      probe_put_le32(&response[5], g_throughput.frames);
      probe_put_le32(&response[9], g_throughput.payload_bytes);
      probe_put_le32(&response[13], elapsed_ms);
      probe_put_le32(&response[17], g_throughput.missing_frames);

      printf("openvela_ble_probe: throughput END test_id=%lu frames=%lu "
             "payload=%lu elapsed_ms=%lu missing=%lu\n",
             (unsigned long)g_throughput.test_id,
             (unsigned long)g_throughput.frames,
             (unsigned long)g_throughput.payload_bytes,
             (unsigned long)elapsed_ms,
             (unsigned long)g_throughput.missing_frames);
      status = probe_notify(srv_handle, response, sizeof(response));
      printf("openvela_ble_probe: throughput REPORT status=%d\n", status);
      probe_print_rx_queue_stats();
      memset(&g_throughput, 0, sizeof(g_throughput));
      return;
    }
  else
    {
      reply = g_error;
      reply_len = sizeof(g_error);
      printf("openvela_ble_probe: RX unknown command\n");
    }

  status = probe_notify(srv_handle, reply, reply_len);
  printf("openvela_ble_probe: TX notify status=%d\n", status);
}

static uint16_t probe_rx_written(gatts_handle_t srv_handle,
                                 bt_address_t *addr,
                                 uint16_t attr_handle,
                                 const uint8_t *value,
                                 uint16_t length,
                                 uint16_t offset)
{
  struct probe_rx_slot_s *slot;
  uint32_t generation;
  uint32_t head;
  uint32_t tail;
  uint32_t used;

  (void)srv_handle;
  (void)attr_handle;

  if (offset != 0 || length > PROBE_RX_FRAME_MAX)
    {
      __atomic_fetch_add(&g_rx_invalid_length, 1, __ATOMIC_RELAXED);
      return 0;
    }

  generation = __atomic_load_n(&g_connection_generation, __ATOMIC_ACQUIRE);
  head = __atomic_load_n(&g_rx_head, __ATOMIC_RELAXED);
  tail = __atomic_load_n(&g_rx_tail, __ATOMIC_ACQUIRE);
  used = head - tail;
  if (used >= PROBE_RX_QUEUE_DEPTH)
    {
      uint32_t log_count;

      __atomic_fetch_add(&g_rx_drop_full, 1, __ATOMIC_RELAXED);
      log_count = __atomic_add_fetch(&g_capture_rx_full_logs, 1,
                                     __ATOMIC_RELAXED);
      if (log_count <= PROBE_CAPTURE_LOG_LIMIT)
        {
          printf("openvela_ble_probe: RX full_drop generation=%lu "
                 "length=%u first_type=0x%02x used=%lu log=%lu/%u\n",
                 (unsigned long)generation, length,
                 length > 0 ? value[0] : 0,
                 (unsigned long)used, (unsigned long)log_count,
                 PROBE_CAPTURE_LOG_LIMIT);
        }
      return 0;
    }

  slot = &g_rx_queue[head % PROBE_RX_QUEUE_DEPTH];
  memcpy(&slot->addr, addr, sizeof(slot->addr));
  slot->connection_generation = generation;
  slot->length = length;
  slot->offset = offset;
  memcpy(slot->value, value, length);
  used++;
  if (used >
      __atomic_load_n(&g_rx_high_water, __ATOMIC_RELAXED))
    {
      __atomic_store_n(&g_rx_high_water, used, __ATOMIC_RELAXED);
    }

  /* Publish only after the complete slot is visible to the consumer. */

  __atomic_store_n(&g_rx_head, head + 1, __ATOMIC_RELEASE);

  __atomic_fetch_add(&g_rx_enqueued, 1, __ATOMIC_RELAXED);
  sem_post(&g_rx_sem);
  return length;
}

static void *probe_communication_thread(void *arg)
{
#if !BLE_PROBE_JOINT_UI_BLE
  struct probe_game_result game_result;
#endif
  struct probe_rx_slot_s slot;
  bt_address_t peer_addr;
  uint32_t generation;
  uint32_t active_generation = 0;
  uint32_t head;
  uint32_t tail;
  bool connected;
  bool have_slot;
  int wait_status;
  struct timespec wait_until;

  (void)arg;
  __atomic_store_n(&g_communication_exited, false, __ATOMIC_RELEASE);

  while (__atomic_load_n(&g_communication_running, __ATOMIC_ACQUIRE))
    {
      if (clock_gettime(CLOCK_REALTIME, &wait_until) < 0)
        {
          continue;
        }

      wait_until.tv_nsec += PROBE_GESTURE_WORKER_POLL_MS * 1000000L;
      if (wait_until.tv_nsec >= 1000000000L)
        {
          wait_until.tv_sec++;
          wait_until.tv_nsec -= 1000000000L;
        }

      do
        {
          wait_status = sem_timedwait(&g_rx_sem, &wait_until);
        }
      while (wait_status < 0 && errno == EINTR);

      if (wait_status < 0 && errno != ETIMEDOUT)
        {
          continue;
        }

      tail = __atomic_load_n(&g_rx_tail, __ATOMIC_RELAXED);
      head = __atomic_load_n(&g_rx_head, __ATOMIC_ACQUIRE);
      have_slot = tail != head;
      if (have_slot)
        {
          memcpy(&slot, &g_rx_queue[tail % PROBE_RX_QUEUE_DEPTH],
                 sizeof(slot));
          __atomic_store_n(&g_rx_tail, tail + 1, __ATOMIC_RELEASE);
        }

      pthread_mutex_lock(&g_state_lock);
      generation = __atomic_load_n(&g_connection_generation,
                                   __ATOMIC_ACQUIRE);
      connected = g_peer_connected;
      memcpy(&peer_addr, &g_peer_addr, sizeof(peer_addr));
      pthread_mutex_unlock(&g_state_lock);

      if (generation != active_generation)
        {
          if (__atomic_exchange_n(&g_capture_active, false,
                                  __ATOMIC_ACQ_REL))
            {
              probe_capture_print_stats("generation_changed");
            }

          __atomic_store_n(&g_capture_tail,
                           __atomic_load_n(&g_capture_head,
                                           __ATOMIC_ACQUIRE),
                           __ATOMIC_RELEASE);
          memset(&g_throughput, 0, sizeof(g_throughput));
          memset(&g_reliable39, 0, sizeof(g_reliable39));
          active_generation = generation;
        }

      if (have_slot &&
          (!connected || slot.connection_generation != generation ||
           memcmp(&slot.addr, &peer_addr, sizeof(peer_addr)) != 0))
        {
          uint32_t log_count;

          __atomic_fetch_add(&g_rx_stale, 1, __ATOMIC_RELAXED);
          log_count = __atomic_add_fetch(&g_capture_rx_stale_logs, 1,
                                         __ATOMIC_RELAXED);
          if (log_count <= PROBE_CAPTURE_LOG_LIMIT)
            {
              printf("openvela_ble_probe: RX stale_drop "
                     "slot_generation=%lu current_generation=%lu "
                     "connected=%d length=%u first_type=0x%02x "
                     "log=%lu/%u\n",
                     (unsigned long)slot.connection_generation,
                     (unsigned long)generation, connected, slot.length,
                     slot.length > 0 ? slot.value[0] : 0,
                     (unsigned long)log_count, PROBE_CAPTURE_LOG_LIMIT);
            }
          have_slot = false;
        }

      if (have_slot)
        {
          probe_process_frame(g_service_handle, slot.value, slot.length,
                              generation);
        }

      probe_capture_drain(generation, 16);

#if !BLE_PROBE_JOINT_UI_BLE
      while (probe_game_take_ble_result(&game_result))
        {
          int publish_status;

          if (!connected || game_result.connection_generation != generation)
            {
              printf("openvela_ble_probe: stale game result dropped"
                     " generation=%lu current=%lu\n",
                     (unsigned long)game_result.connection_generation,
                     (unsigned long)generation);
              continue;
            }

          publish_status = probe_publish_gesture(game_result.gesture,
                                                 game_result.confidence,
                                                 game_result.flags,
                                                 game_result.battery,
                                                 game_result.monotonic_us);

          printf("openvela_ble_probe: game result publish gesture=%u"
                 " session=%lu combo=%lu status=%d\n",
                 game_result.gesture,
                 (unsigned long)game_result.game_session,
                   (unsigned long)game_result.combo, publish_status);
        }
#endif

      probe_process_gesture_tx();
    }

#if BLE_PROBE_JOINT_UI_BLE
  if (!__atomic_load_n(&g_communication_running, __ATOMIC_ACQUIRE))
    {
      __atomic_fetch_or(&g_joint_stop_seen_steps,
                        PROBE_STOP_STEP_COMMUNICATION, __ATOMIC_RELAXED);
      __atomic_fetch_or(&g_joint_stop_exit_begin_steps,
                        PROBE_STOP_STEP_COMMUNICATION, __ATOMIC_RELAXED);
    }
  __atomic_fetch_or(&g_joint_stop_exit_done_steps,
                    PROBE_STOP_STEP_COMMUNICATION, __ATOMIC_RELEASE);
#endif
  __atomic_store_n(&g_communication_exited, true, __ATOMIC_RELEASE);
  return NULL;
}

static gatt_attr_db_t g_attr_db[] =
{
  GATT_H_PRIMARY_SERVICE(BT_UUID_DECLARE_16(PROBE_SERVICE_UUID),
                         PROBE_SERVICE_HANDLE),

  GATT_H_CHARACTERISTIC_USER_RSP(
    BT_UUID_DECLARE_16(PROBE_RX_UUID),
    GATT_PROP_WRITE | GATT_PROP_WRITE_NR,
    GATT_PERM_WRITE,
    NULL, probe_rx_written, PROBE_RX_HANDLE),

  GATT_H_CHARACTERISTIC_AUTO_RSP(
    BT_UUID_DECLARE_16(PROBE_TX_UUID),
    GATT_PROP_NOTIFY, 0, NULL, 0, PROBE_TX_HANDLE),

  GATT_H_CCCD(GATT_PERM_READ | GATT_PERM_WRITE,
              probe_tx_cccd_written, PROBE_TX_CCCD_HANDLE),
};

static gatt_srv_db_t g_service_db =
{
  .attr_num = sizeof(g_attr_db) / sizeof(g_attr_db[0]),
  .attr_db = g_attr_db,
};

static void on_gatts_connected(gatts_handle_t srv_handle, bt_address_t *addr)
{
  (void)srv_handle;

  pthread_mutex_lock(&g_state_lock);
  __atomic_add_fetch(&g_connection_generation, 1, __ATOMIC_RELEASE);
  __atomic_store_n(&g_rx_enqueued, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_rx_invalid_length, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_rx_drop_lock_busy, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_rx_drop_full, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_rx_high_water, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_rx_stale, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_reliable39_rx_ok, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_reliable39_bad_length, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_reliable39_bad_crc, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_reliable39_duplicates, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_reliable39_tx_attempted, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_reliable39_tx_status_nonzero, 0, __ATOMIC_RELAXED);
  memset(g_gesture_queue, 0, sizeof(g_gesture_queue));
  g_gesture_rx_head = 0;
  g_gesture_rx_tail = 0;
  g_gesture_next_packet_id = 1;
  g_gesture_time_synced = false;
  g_gesture_sync_unix_ms = 0;
  g_gesture_sync_monotonic_us = 0;
  g_gesture_enqueued = 0;
  g_gesture_queue_full = 0;
  g_gesture_acked = 0;
  g_gesture_retransmitted = 0;
  g_gesture_dropped = 0;
  g_gesture_tx_status_nonzero = 0;
  memcpy(&g_peer_addr, addr, sizeof(g_peer_addr));
  g_notify_enabled = false;
  g_phy_read_requested = false;
  g_phy_update_requested = false;
  g_phy_update_pending = false;
  g_peer_connected = true;
  g_adv_restart_pending = false;
  g_adv_restart_delay_armed = false;
  g_adv_restart_attempts = 0;
#if BLE_PROBE_ENABLE_AUTO_2M_PHY
  g_phy_read_pending = true;
#else
  g_phy_read_pending = false;
#endif
  pthread_mutex_unlock(&g_state_lock);
  printf("openvela_ble_probe: GATT client connected\n");
#if !BLE_PROBE_ENABLE_AUTO_2M_PHY
  printf("openvela_ble_probe: automatic 2M PHY request disabled\n");
#endif
}

static void on_gatts_disconnected(gatts_handle_t srv_handle,
                                  bt_address_t *addr)
{
  bool capture_was_active;

  (void)srv_handle;
  (void)addr;

  capture_was_active = __atomic_exchange_n(&g_capture_active, false,
                                            __ATOMIC_ACQ_REL);
  pthread_mutex_lock(&g_state_lock);
  __atomic_add_fetch(&g_connection_generation, 1, __ATOMIC_RELEASE);
  g_peer_connected = false;
  g_notify_enabled = false;
  g_phy_read_pending = false;
  g_phy_update_pending = false;
  g_phy_read_requested = false;
  g_phy_update_requested = false;
#if BLE_PROBE_IMU_DRDY_START_ON_TX_SUBSCRIBE
  if (!g_imu_raw_launch_attempted && !g_imu_raw_started)
    {
      g_imu_raw_launch_pending = false;
    }
#endif
  memset(g_gesture_queue, 0, sizeof(g_gesture_queue));
  g_gesture_rx_head = 0;
  g_gesture_rx_tail = 0;
  g_gesture_time_synced = false;
  g_gesture_sync_unix_ms = 0;
  g_gesture_sync_monotonic_us = 0;
  memset(&g_peer_addr, 0, sizeof(g_peer_addr));
  g_adv_restart_pending =
#if BLE_PROBE_JOINT_UI_BLE
    !__atomic_load_n(&g_joint_stop_in_progress, __ATOMIC_ACQUIRE);
#else
    true;
#endif
  g_adv_restart_delay_armed = false;
  g_adv_restart_attempts = 0;
  pthread_mutex_unlock(&g_state_lock);
  sem_post(&g_rx_sem);
  printf("openvela_ble_probe: GATT client disconnected\n");
  if (capture_was_active)
    {
      probe_capture_print_stats("disconnected");
    }

  probe_print_rx_queue_stats();
  probe_print_reliable39_stats();
  probe_print_gesture_stats();
  sf32lb52_bt_diag_dump_acl_tx();
}

static void on_attr_table_added(gatts_handle_t srv_handle,
                                gatt_status_t status,
                                uint16_t attr_handle)
{
  (void)srv_handle;

  printf("openvela_ble_probe: GATT table status=%d handle=%u\n",
         status, attr_handle);
}

static void on_notify_complete(gatts_handle_t srv_handle, bt_address_t *addr,
                               gatt_status_t status, uint16_t attr_handle)
{
  (void)srv_handle;
  (void)addr;

  printf("openvela_ble_probe: notify status=%d handle=%u\n",
         status, attr_handle);
}

static void on_mtu_changed(gatts_handle_t srv_handle, bt_address_t *addr,
                           uint32_t mtu)
{
  (void)srv_handle;
  (void)addr;

  printf("openvela_ble_probe: MTU=%lu\n", (unsigned long)mtu);
}

static void on_phy_read(gatts_handle_t srv_handle, bt_address_t *addr,
                        ble_phy_type_t tx_phy, ble_phy_type_t rx_phy)
{
  (void)srv_handle;

  printf("openvela_ble_probe: PHY current tx=%d rx=%d\n",
         tx_phy, rx_phy);

#if BLE_PROBE_ENABLE_AUTO_2M_PHY
  pthread_mutex_lock(&g_state_lock);
  if (g_peer_connected && g_phy_read_requested &&
      !g_phy_update_requested &&
      memcmp(&g_peer_addr, addr, sizeof(g_peer_addr)) == 0)
    {
      g_phy_update_pending = true;
    }
  pthread_mutex_unlock(&g_state_lock);
#else
  (void)addr;
#endif
}

static void on_phy_updated(gatts_handle_t srv_handle, bt_address_t *addr,
                           gatt_status_t status, ble_phy_type_t tx_phy,
                           ble_phy_type_t rx_phy)
{
  bool requested;

  (void)srv_handle;
  (void)addr;

  pthread_mutex_lock(&g_state_lock);
  requested = g_phy_update_requested;
  pthread_mutex_unlock(&g_state_lock);

  printf("openvela_ble_probe: PHY updated status=%d tx=%d rx=%d "
         "requested=%d\n",
         status, tx_phy, rx_phy, requested);
}

static void on_conn_param_changed(gatts_handle_t srv_handle,
                                  bt_address_t *addr,
                                  uint16_t connection_interval,
                                  uint16_t peripheral_latency,
                                  uint16_t supervision_timeout)
{
  (void)srv_handle;
  (void)addr;

  printf("openvela_ble_probe: connection params interval=%u latency=%u "
         "timeout=%u\n",
         connection_interval, peripheral_latency, supervision_timeout);
}

static const gatts_callbacks_t g_gatts_callbacks =
{
  .size = sizeof(g_gatts_callbacks),
  .on_connected = on_gatts_connected,
  .on_disconnected = on_gatts_disconnected,
  .on_attr_table_added = on_attr_table_added,
  .on_notify_complete = on_notify_complete,
  .on_mtu_changed = on_mtu_changed,
  .on_phy_read = on_phy_read,
  .on_phy_updated = on_phy_updated,
  .on_conn_param_changed = on_conn_param_changed,
};

static int init_gatt_service(void)
{
  bt_status_t status;

  status = bt_gatts_register_service(g_instance, &g_service_handle,
                                     (gatts_callbacks_t *)&g_gatts_callbacks);
#if BLE_PROBE_JOINT_UI_BLE
  probe_ble_lifecycle_note(PROBE_BLE_LC_SERVICE_REGISTER, status,
                           bt_adapter_get_state(g_instance));
#endif
  if (status != BT_STATUS_SUCCESS || g_service_handle == NULL)
    {
      printf("openvela_ble_probe: GATT register failed:%d\n", status);
      return -1;
    }

  status = bt_gatts_add_attr_table(g_service_handle, &g_service_db);
  if (status != BT_STATUS_SUCCESS)
    {
      printf("openvela_ble_probe: GATT table add failed:%d\n", status);
      return -1;
    }

  printf("openvela_ble_probe: GATT service FFF0, RX FFF1, TX FFF2\n");
  return 0;
}

static void on_adapter_state_changed(void *cookie, bt_adapter_state_t state)
{
  (void)cookie;

#if BLE_PROBE_JOINT_UI_BLE
  probe_ble_lifecycle_note(PROBE_BLE_LC_ADAPTER_CALLBACK, 0, state);
#endif
  printf("openvela_ble_probe: adapter state=%d\n", state);

  if (!g_ble_on &&
      (state == BT_ADAPTER_STATE_BLE_ON ||
       state == BT_ADAPTER_STATE_ON))
    {
      g_ble_on = true;
      sem_post(&g_ble_on_sem);
    }
#if BLE_PROBE_JOINT_UI_BLE
  if (state == BT_ADAPTER_STATE_OFF)
    {
      g_adapter_enabled = false;
    }
#endif
}

static const adapter_callbacks_t g_adapter_callbacks =
{
  .on_adapter_state_changed = on_adapter_state_changed,
};

static void on_advertising_start(bt_advertiser_t *adv, uint8_t id,
                                 uint8_t status)
{
  bool retries_exhausted = false;
  bool log_imu_launch_check = false;
  bool imu_detected = false;
  bool imu_started = false;

#if BLE_PROBE_JOINT_UI_BLE
  probe_ble_lifecycle_note(PROBE_BLE_LC_ADV_START, status,
                           g_instance != NULL ?
                           bt_adapter_get_state(g_instance) : -1);
#endif

  pthread_mutex_lock(&g_state_lock);
  g_adv_start_inflight = false;
  g_adv_start_callback_seen = true;
  g_adv_start_callback_status = status;
  if (status == 0)
    {
      g_adv_restart_pending = false;
      g_adv_restart_delay_armed = false;
      g_adv_restart_attempts = 0;
      if (!g_imu_raw_launch_check_logged)
        {
          g_imu_raw_launch_check_logged = true;
          log_imu_launch_check = true;
          imu_detected = g_imu_detected;
          imu_started = g_imu_raw_started;
        }

      if (id == 1 && !g_visible_game_launch_attempted
#if BLE_PROBE_JOINT_UI_BLE
          && !__atomic_load_n(&g_joint_stop_in_progress, __ATOMIC_ACQUIRE)
#endif
         )
        {
          g_visible_game_launch_pending = true;
        }

#if !BLE_PROBE_IMU_DRDY_START_ON_TX_SUBSCRIBE
      if (id == 1 && g_imu_detected && !g_imu_raw_launch_attempted &&
          !g_imu_raw_started)
        {
          g_imu_raw_launch_pending = true;
        }
#endif
    }
  else
    {
      if (adv == g_advertiser)
        {
          g_advertiser = NULL;
        }

      if (g_adv_restart_pending)
        {
          g_adv_restart_delay_armed = false;
          if (g_adv_restart_attempts >= PROBE_ADV_RESTART_MAX_ATTEMPTS)
            {
              g_adv_restart_pending = false;
              retries_exhausted = true;
            }
        }
    }

  pthread_mutex_unlock(&g_state_lock);
  syslog(LOG_INFO, "ov_ble_adv_diag: probe adv: callback id=%u status=%u\n",
         id, status);
  printf("openvela_ble_probe: advertising start id=%u status=%u\n",
         id, status);
  if (log_imu_launch_check)
    {
      printf("openvela_ble_probe: IMU raw launch check detected=%d"
             " started=%d adv_id=%u status=%u\n",
             imu_detected, imu_started, id, status);
    }
  if (retries_exhausted)
    {
      printf("openvela_ble_probe: advertising restart failed after %u "
             "attempts\n", PROBE_ADV_RESTART_MAX_ATTEMPTS);
    }
}

static void on_advertising_stopped(bt_advertiser_t *adv, uint8_t id)
{
#if BLE_PROBE_JOINT_UI_BLE
  probe_ble_lifecycle_note(PROBE_BLE_LC_ADV_STOP, 0,
                           g_instance != NULL ?
                           bt_adapter_get_state(g_instance) : -1);
#endif
  printf("openvela_ble_probe: advertising stopped id=%u\n", id);

  pthread_mutex_lock(&g_state_lock);
  if (adv == g_advertiser)
    {
      g_advertiser = NULL;
    }
  pthread_mutex_unlock(&g_state_lock);
}

static advertiser_callback_t g_advertiser_callbacks =
{
  .size = sizeof(g_advertiser_callbacks),
  .on_advertising_start = on_advertising_start,
  .on_advertising_stopped = on_advertising_stopped,
};

static int start_advertising(void)
{
  ble_adv_params_t params;
  bt_advertiser_t *advertiser;
  bool callback_failed;

  pthread_mutex_lock(&g_state_lock);
#if BLE_PROBE_JOINT_UI_BLE
  if (__atomic_load_n(&g_joint_stop_in_progress, __ATOMIC_ACQUIRE))
    {
      pthread_mutex_unlock(&g_state_lock);
      return -ESHUTDOWN;
    }
#endif
  if (g_advertiser != NULL || g_adv_start_inflight || g_peer_connected)
    {
      pthread_mutex_unlock(&g_state_lock);
      return 0;
    }

  g_adv_start_inflight = true;
  g_adv_start_callback_seen = false;
  pthread_mutex_unlock(&g_state_lock);

  memset(&params, 0, sizeof(params));
  params.adv_type = BT_LE_LEGACY_ADV_IND;
  params.interval = 320;
  params.duration = 0;
  params.channel_map = BT_LE_ADV_CHANNEL_DEFAULT;
  params.filter_policy = BT_LE_ADV_FILTER_WHITE_LIST_FOR_NONE;

  if (g_adv_data == NULL)
    {
      g_adv_data = advertiser_data_new();
      if (g_adv_data != NULL)
        {
          g_adv_data_owned = true;
        }
    }
  if (g_scan_rsp_data == NULL)
    {
      g_scan_rsp_data = advertiser_data_new();
      if (g_scan_rsp_data != NULL)
        {
          g_scan_rsp_data_owned = true;
        }
    }
  if (g_adv_data == NULL || g_scan_rsp_data == NULL)
    {
      printf("openvela_ble_probe: advertiser_data_new failed\n");
      pthread_mutex_lock(&g_state_lock);
      g_adv_start_inflight = false;
      pthread_mutex_unlock(&g_state_lock);
      return -1;
    }

  if (g_adv_payload == NULL || g_scan_rsp_payload == NULL)
    {
      advertiser_data_set_flags(g_adv_data,
                                BT_AD_FLAG_GENERAL_DISCOVERABLE |
                                BT_AD_FLAG_BREDR_NOT_SUPPORT);
      advertiser_data_set_name(g_scan_rsp_data, "ov_ble_probe");

      g_adv_payload = advertiser_data_build(g_adv_data,
                                            &g_adv_payload_len);
      g_scan_rsp_payload = advertiser_data_build(g_scan_rsp_data,
                                                 &g_scan_rsp_payload_len);
      if (g_adv_payload == NULL || g_scan_rsp_payload == NULL)
        {
          printf("openvela_ble_probe: advertiser_data_build failed\n");
          pthread_mutex_lock(&g_state_lock);
          g_adv_start_inflight = false;
          pthread_mutex_unlock(&g_state_lock);
          return -1;
        }
    }

  syslog(LOG_INFO, "ov_ble_adv_diag: probe adv: request begin\n");
  advertiser = bt_le_start_advertising(g_instance, &params,
                                       g_adv_payload, g_adv_payload_len,
                                       g_scan_rsp_payload,
                                       g_scan_rsp_payload_len,
                                       &g_advertiser_callbacks);
  syslog(LOG_INFO, "ov_ble_adv_diag: probe adv: request returned status=%d\n",
         advertiser != NULL ? 0 : -1);

  pthread_mutex_lock(&g_state_lock);
  callback_failed = g_adv_start_callback_seen &&
                    g_adv_start_callback_status != 0;
  if (advertiser != NULL && !callback_failed)
    {
      g_advertiser = advertiser;
    }
  else
    {
      g_adv_start_inflight = false;
    }
  pthread_mutex_unlock(&g_state_lock);

  if (advertiser == NULL)
    {
      printf("openvela_ble_probe: bt_le_start_advertising failed\n");
      return -1;
    }

  printf("openvela_ble_probe: advertising as ov_ble_probe\n");
  return 0;
}

static void process_advertising_restart(void)
{
  static const uint16_t retry_delays_ms[PROBE_ADV_RESTART_MAX_ATTEMPTS] =
    {
      200, 500, 1000
    };
  uint64_t now_ms;
  uint16_t delay_ms = 0;
  uint8_t attempt = 0;
  bool start_now = false;
  bool log_scheduled = false;
  bool retries_exhausted = false;
  int ret;

  if (probe_monotonic_ms(&now_ms) < 0)
    {
      return;
    }

  pthread_mutex_lock(&g_state_lock);
#if BLE_PROBE_JOINT_UI_BLE
  if (__atomic_load_n(&g_joint_stop_in_progress, __ATOMIC_ACQUIRE))
    {
      g_adv_restart_pending = false;
      g_adv_restart_delay_armed = false;
      pthread_mutex_unlock(&g_state_lock);
      return;
    }
#endif
  if (!g_adv_restart_pending || g_peer_connected ||
      g_advertiser != NULL || g_adv_start_inflight)
    {
      pthread_mutex_unlock(&g_state_lock);
      return;
    }

  if (g_adv_restart_attempts >= PROBE_ADV_RESTART_MAX_ATTEMPTS)
    {
      g_adv_restart_pending = false;
      retries_exhausted = true;
    }
  else if (!g_adv_restart_delay_armed)
    {
      delay_ms = retry_delays_ms[g_adv_restart_attempts];
      g_adv_restart_due_ms = now_ms + delay_ms;
      g_adv_restart_delay_armed = true;
      log_scheduled = true;
    }
  else if (now_ms >= g_adv_restart_due_ms)
    {
      g_adv_restart_delay_armed = false;
      g_adv_restart_attempts++;
      attempt = g_adv_restart_attempts;
      start_now = true;
    }

  pthread_mutex_unlock(&g_state_lock);

  if (log_scheduled)
    {
      printf("openvela_ble_probe: advertising restart scheduled in %u ms\n",
             delay_ms);
    }

  if (retries_exhausted)
    {
      printf("openvela_ble_probe: advertising restart failed after %u "
             "attempts\n", PROBE_ADV_RESTART_MAX_ATTEMPTS);
      return;
    }

  if (!start_now)
    {
      return;
    }

  printf("openvela_ble_probe: restart advertising after disconnect "
         "attempt=%u\n", attempt);
  ret = start_advertising();
  if (ret < 0)
    {
      pthread_mutex_lock(&g_state_lock);
      if (g_adv_restart_pending)
        {
          if (g_adv_restart_attempts >= PROBE_ADV_RESTART_MAX_ATTEMPTS)
            {
              g_adv_restart_pending = false;
              retries_exhausted = true;
            }
          else
            {
              g_adv_restart_delay_armed = false;
            }
        }
      pthread_mutex_unlock(&g_state_lock);

      if (retries_exhausted)
        {
          printf("openvela_ble_probe: advertising restart failed after %u "
                 "attempts\n", PROBE_ADV_RESTART_MAX_ATTEMPTS);
        }
    }
}

#if BLE_PROBE_JOINT_UI_BLE
static void probe_joint_restore_terminal(void)
{
  if (g_joint_terminal_saved)
    {
      (void)tcsetattr(STDIN_FILENO, TCSANOW, &g_joint_terminal);
      g_joint_terminal_saved = false;
    }
}

static void *probe_joint_input_thread(void *arg)
{
  struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
  struct probe_app_input input;

  (void)arg;
  probe_app_input_init(&input);
  __atomic_store_n(&g_joint_input_exited, false, __ATOMIC_RELEASE);
  while (__atomic_load_n(&g_joint_input_running, __ATOMIC_ACQUIRE))
    {
      char ch;
      ssize_t n;
      int poll_status = poll(&pfd, 1, 100);

      if (poll_status < 0 && errno != EINTR)
        {
          break;
        }

      if (poll_status <= 0 || (pfd.revents & POLLIN) == 0)
        {
          continue;
        }

      n = read(STDIN_FILENO, &ch, 1);
      if (n != 1)
        {
          continue;
        }

      switch (probe_app_input_feed(&input, ch))
        {
          case PROBE_APP_INPUT_STOP:
#if BLE_PROBE_JOINT_UI_BLE
            __atomic_fetch_or(&g_joint_stop_seen_steps,
                              PROBE_STOP_STEP_INPUT, __ATOMIC_RELAXED);
#endif
            __atomic_fetch_add(&g_joint_stop_requests, 1,
                               __ATOMIC_RELAXED);
            __atomic_store_n(&g_joint_stop_requested, true,
                             __ATOMIC_RELEASE);
            break;

          case PROBE_APP_INPUT_LATENCY:
            /* Capture is serviced by the dedicated diagnostic writer; the
             * rank1 consumer is never asked to format or emit this dump. */
            g_latency_dump_requested = true;
            if (sem_post(&g_latency_dump_sem) < 0)
              g_latency_dump_requested = false;
            break;

          case PROBE_APP_INPUT_LATENCY_FULL:
            g_latency_dump_full = true;
            g_latency_dump_requested = true;
            if (sem_post(&g_latency_dump_sem) < 0)
              g_latency_dump_requested = false;
            break;

          case PROBE_APP_INPUT_UNKNOWN:
            __atomic_fetch_add(&g_joint_stop_unknown_lines, 1,
                               __ATOMIC_RELAXED);
            break;

          case PROBE_APP_INPUT_OVERLONG:
            __atomic_fetch_add(&g_joint_stop_overlong_lines, 1,
                               __ATOMIC_RELAXED);
            break;

          case PROBE_APP_INPUT_NONE:
          default:
            break;
        }
    }

#if BLE_PROBE_JOINT_UI_BLE
  __atomic_fetch_or(&g_joint_stop_exit_begin_steps,
                    PROBE_STOP_STEP_INPUT, __ATOMIC_RELAXED);
  __atomic_fetch_or(&g_joint_stop_exit_done_steps,
                    PROBE_STOP_STEP_INPUT, __ATOMIC_RELEASE);
#endif
  __atomic_store_n(&g_joint_input_exited, true, __ATOMIC_RELEASE);
  return NULL;
}

static int probe_joint_start_input(void)
{
  struct termios terminal;
  int ret;

  if (tcgetattr(STDIN_FILENO, &g_joint_terminal) == 0)
    {
      terminal = g_joint_terminal;
      terminal.c_lflag &= (tcflag_t)~ECHO;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal) == 0)
        {
          g_joint_terminal_saved = true;
        }
      else
        {
          return -errno;
        }
    }
  else
    {
      return -errno;
    }

  __atomic_store_n(&g_joint_input_running, true, __ATOMIC_RELEASE);
  __atomic_store_n(&g_joint_input_exited, false, __ATOMIC_RELEASE);
  ret = pthread_create(&g_joint_input_thread, NULL,
                       probe_joint_input_thread, NULL);
  if (ret != 0)
    {
      __atomic_store_n(&g_joint_input_running, false, __ATOMIC_RELEASE);
      probe_joint_restore_terminal();
      return -ret;
    }

  g_joint_input_thread_created = true;
  return 0;
}

static bool probe_joint_wait_flag(volatile bool *flag, uint64_t deadline_us)
{
  for (;;)
    {
      uint64_t now_us = 0;

      if (__atomic_load_n(flag, __ATOMIC_ACQUIRE))
        {
          return true;
        }

      if (probe_get_monotonic_us(&now_us) < 0 || now_us >= deadline_us)
        {
          return false;
        }

      usleep(10000);
    }
}

static bool probe_joint_before_deadline(void)
{
  uint64_t now_us = 0;

  return probe_get_monotonic_us(&now_us) == 0 &&
         now_us < g_joint_stop_control.deadline_us;
}

/*
 * All normal UART producers have exited before this helper is used.  Make the
 * final evidence write nonblocking and poll only up to the shared stop
 * deadline so a wedged console cannot silently extend cooperative shutdown.
 */
static bool probe_joint_write_before_deadline(const char *buffer, size_t length)
{
  struct pollfd pfd;
  size_t offset = 0;
  int saved_flags;
  bool changed_flags = false;
  bool complete = false;

  saved_flags = fcntl(STDOUT_FILENO, F_GETFL);
  if (saved_flags < 0)
    {
      return false;
    }

  if ((saved_flags & O_NONBLOCK) == 0)
    {
      if (fcntl(STDOUT_FILENO, F_SETFL, saved_flags | O_NONBLOCK) < 0)
        {
          return false;
        }

      changed_flags = true;
    }

  pfd.fd = STDOUT_FILENO;
  pfd.events = POLLOUT;
  pfd.revents = 0;
  while (offset < length)
    {
      uint64_t now_us;
      uint64_t remaining_us;
      int timeout_ms;
      int poll_status;
      ssize_t written;

      if (probe_get_monotonic_us(&now_us) < 0 ||
          now_us >= g_joint_stop_control.deadline_us)
        {
          break;
        }

      remaining_us = g_joint_stop_control.deadline_us - now_us;
      timeout_ms = (int)((remaining_us + 999u) / 1000u);
      if (timeout_ms > 100)
        {
          timeout_ms = 100;
        }

      pfd.revents = 0;
      poll_status = poll(&pfd, 1, timeout_ms);
      if (poll_status < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          break;
        }

      if (poll_status == 0)
        {
          continue;
        }

      if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        {
          break;
        }

      if ((pfd.revents & POLLOUT) == 0)
        {
          continue;
        }

      written = write(STDOUT_FILENO, buffer + offset, length - offset);
      if (written > 0)
        {
          if ((size_t)written < length - offset)
            {
              __atomic_fetch_add(&g_serial_short_writes, 1,
                                 __ATOMIC_RELAXED);
            }

          offset += (size_t)written;
          continue;
        }

      if (written < 0 && (errno == EINTR || errno == EAGAIN))
        {
          continue;
        }

      break;
    }

  complete = offset == length;
  if (changed_flags && fcntl(STDOUT_FILENO, F_SETFL, saved_flags) < 0)
    {
      complete = false;
    }

  return complete;
}

static bool probe_ble_lifecycle_dump(void)
{
  uint32_t count;
  uint32_t dumped;

  count = __atomic_load_n(&g_ble_lifecycle_count, __ATOMIC_ACQUIRE);
  dumped = __atomic_load_n(&g_ble_lifecycle_dumped, __ATOMIC_ACQUIRE);
  if (count > PROBE_BLE_LIFECYCLE_LOG_DEPTH &&
      dumped < count - PROBE_BLE_LIFECYCLE_LOG_DEPTH)
    {
      dumped = count - PROBE_BLE_LIFECYCLE_LOG_DEPTH;
    }

  while (dumped < count)
    {
      struct probe_ble_lifecycle_record record =
        g_ble_lifecycle_log[dumped % PROBE_BLE_LIFECYCLE_LOG_DEPTH];
      int n;

      n = snprintf(g_joint_stop_report, sizeof(g_joint_stop_report),
                   "BTE1 BLE_LIFECYCLE run=%lu session=%lu stage=%s "
                   "status=%d adapter_state=%d at_us=%llu\n",
                   (unsigned long)record.run,
                   (unsigned long)record.session,
                   probe_ble_lifecycle_stage_name(record.stage),
                   (int)record.status, (int)record.adapter_state,
                   (unsigned long long)record.at_us);
      if (n < 0 || (size_t)n >= sizeof(g_joint_stop_report) ||
          !probe_joint_write_before_deadline(g_joint_stop_report,
                                             (size_t)n))
        {
          return false;
        }
      dumped++;
      __atomic_store_n(&g_ble_lifecycle_dumped, dumped, __ATOMIC_RELEASE);
      if (!probe_joint_before_deadline())
        {
          return false;
        }
    }

  {
    struct service_trace_record records[64];
    uint32_t dropped = 0;
    uint32_t n = service_trace_copy(records, 64, &dropped);
    int i;
    n = n > 64 ? 64 : n;
    snprintf(g_joint_stop_report, sizeof(g_joint_stop_report),
             "BTE1 BLE_INT_TRACE records=%lu dropped=%lu exported=%lu\n",
             (unsigned long)n, (unsigned long)dropped,
             (unsigned long)n);
    if (!probe_joint_write_before_deadline(g_joint_stop_report,
                                           strlen(g_joint_stop_report)))
      return false;
    for (i = 0; i < (int)n; i++)
      {
        int m = snprintf(g_joint_stop_report, sizeof(g_joint_stop_report),
                         "BTE1 BLE_INT run=%lu stage=%u status=%d state=%ld event=%lu tid=%lu at_us=%llu\n",
                         (unsigned long)g_serial_session,
                         (unsigned)records[i].stage, (int)records[i].status,
                         (long)records[i].state, (unsigned long)records[i].event,
                         (unsigned long)records[i].thread_id,
                         (unsigned long long)records[i].at_us);
        if (m < 0 || (size_t)m >= sizeof(g_joint_stop_report) ||
            !probe_joint_write_before_deadline(g_joint_stop_report, (size_t)m))
          return false;
      }
  }
  return true;
}

/* Deadline expiry leaves no time to poll.  Make one nonblocking best-effort
 * write for STOP_TIMEOUT so even the failure report cannot wait forever. */
static void probe_joint_try_write_nonblocking(const char *buffer, size_t length)
{
  int saved_flags = fcntl(STDOUT_FILENO, F_GETFL);
  bool changed_flags = false;
  ssize_t written;

  if (saved_flags < 0)
    {
      return;
    }

  if ((saved_flags & O_NONBLOCK) == 0)
    {
      if (fcntl(STDOUT_FILENO, F_SETFL, saved_flags | O_NONBLOCK) < 0)
        {
          return;
        }

      changed_flags = true;
    }

  written = write(STDOUT_FILENO, buffer, length);
  if (written >= 0 && (size_t)written < length)
    {
      __atomic_fetch_add(&g_serial_short_writes, 1, __ATOMIC_RELAXED);
    }

  if (changed_flags)
    {
      (void)fcntl(STDOUT_FILENO, F_SETFL, saved_flags);
    }
}

static void *probe_joint_ble_cleanup_thread(void *arg)
{
  bt_address_t peer;
  bt_advertiser_t *advertiser = NULL;
  bool connected = false;
  bt_status_t status;

  (void)arg;
  __atomic_store_n(&g_joint_ble_cleanup_exited, false, __ATOMIC_RELEASE);
  __atomic_fetch_or(&g_joint_stop_seen_steps, PROBE_STOP_STEP_BLE,
                    __ATOMIC_RELAXED);
  __atomic_fetch_or(&g_joint_stop_exit_begin_steps, PROBE_STOP_STEP_BLE,
                    __ATOMIC_RELAXED);
  g_joint_stop_ble_status = 0;

  if (g_state_lock_initialized)
    {
      pthread_mutex_lock(&g_state_lock);
      g_adv_restart_pending = false;
      g_adv_restart_delay_armed = false;
      connected = g_peer_connected;
      memcpy(&peer, &g_peer_addr, sizeof(peer));
      advertiser = g_advertiser;
      pthread_mutex_unlock(&g_state_lock);
    }

  if (connected && g_service_handle != NULL && probe_joint_before_deadline())
    {
      status = bt_gatts_disconnect(g_service_handle, &peer);
      if (status != BT_STATUS_SUCCESS && status != BT_STATUS_DONE)
        {
          g_joint_stop_ble_status |= 1u << 0;
        }

      while (probe_joint_before_deadline())
        {
          bool still_connected;

          pthread_mutex_lock(&g_state_lock);
          still_connected = g_peer_connected;
          pthread_mutex_unlock(&g_state_lock);
          if (!still_connected)
            {
              break;
            }

          usleep(10000);
        }
    }

  if (advertiser != NULL && g_instance != NULL &&
      probe_joint_before_deadline())
    {
#if BLE_PROBE_JOINT_UI_BLE
      probe_ble_lifecycle_note(PROBE_BLE_LC_ADV_STOP, 0,
                               bt_adapter_get_state(g_instance));
#endif
      bt_le_stop_advertising(g_instance, advertiser);
      while (probe_joint_before_deadline())
        {
          bool stopped;

          pthread_mutex_lock(&g_state_lock);
          stopped = g_advertiser == NULL;
          pthread_mutex_unlock(&g_state_lock);
          if (stopped)
            {
              break;
            }

          usleep(10000);
        }
    }
  else if (g_instance != NULL && g_adv_start_inflight &&
           probe_joint_before_deadline())
    {
#if BLE_PROBE_JOINT_UI_BLE
      probe_ble_lifecycle_note(PROBE_BLE_LC_ADV_STOP, 0,
                               bt_adapter_get_state(g_instance));
#endif
      bt_le_stop_advertising_id(g_instance, 1);
      while (probe_joint_before_deadline())
        {
          bool stopped;

          pthread_mutex_lock(&g_state_lock);
          stopped = !g_adv_start_inflight && g_advertiser == NULL;
          pthread_mutex_unlock(&g_state_lock);
          if (stopped)
            break;
          usleep(10000);
        }
    }

  if (g_service_registered && g_service_handle != NULL &&
      probe_joint_before_deadline())
    {
      status = bt_gatts_unregister_service(g_service_handle);
      if (status != BT_STATUS_SUCCESS && status != BT_STATUS_DONE)
        {
          g_joint_stop_ble_status |= 1u << 1;
        }
      else
        {
          g_service_registered = false;
          g_service_handle = NULL;
        }
    }

  if (g_adapter_enabled && g_instance != NULL &&
      probe_joint_before_deadline())
    {
      status = bt_adapter_disable_le(g_instance);
#if BLE_PROBE_JOINT_UI_BLE
      probe_ble_lifecycle_note(PROBE_BLE_LC_DISABLE_REQUEST, status,
                               bt_adapter_get_state(g_instance));
#endif
      if (status != BT_STATUS_SUCCESS && status != BT_STATUS_DONE)
        {
          g_joint_stop_ble_status |= 1u << 2;
        }

      while (probe_joint_before_deadline() &&
             bt_adapter_get_state(g_instance) != BT_ADAPTER_STATE_OFF)
        {
          usleep(10000);
        }

      if (bt_adapter_get_state(g_instance) == BT_ADAPTER_STATE_OFF)
        {
          g_adapter_enabled = false;
        }
    }

  if (probe_joint_before_deadline() && g_instance != NULL &&
      g_adapter_callback_cookie != NULL)
    {
      if (!bt_adapter_unregister_callback(g_instance,
                                          g_adapter_callback_cookie))
        {
          g_joint_stop_ble_status |= 1u << 3;
#if BLE_PROBE_JOINT_UI_BLE
          probe_ble_lifecycle_note(PROBE_BLE_LC_CALLBACK_UNREGISTER, 0,
                                   bt_adapter_get_state(g_instance));
#endif
        }
      else
        {
#if BLE_PROBE_JOINT_UI_BLE
          probe_ble_lifecycle_note(PROBE_BLE_LC_CALLBACK_UNREGISTER, 1,
                                   bt_adapter_get_state(g_instance));
#endif
          g_adapter_callback_cookie = NULL;
        }
    }

  if (probe_joint_before_deadline() && g_instance != NULL &&
      g_joint_stop_ble_status == 0 && !g_service_registered &&
      !g_adapter_enabled && g_adapter_callback_cookie == NULL)
    {
      bluetooth_delete_instance(g_instance);
#if BLE_PROBE_JOINT_UI_BLE
      probe_ble_lifecycle_note(PROBE_BLE_LC_INSTANCE_DELETE, 0,
                               BT_ADAPTER_STATE_OFF);
#endif
      g_instance = NULL;
    }

  if (g_instance == NULL)
    {
      probe_advertiser_data_release();
    }

  __atomic_fetch_or(&g_joint_stop_exit_done_steps, PROBE_STOP_STEP_BLE,
                    __ATOMIC_RELEASE);
  __atomic_store_n(&g_joint_ble_cleanup_exited, true, __ATOMIC_RELEASE);
  return NULL;
}

static uint32_t probe_joint_required_stop_steps(void)
{
  uint32_t required = PROBE_STOP_STEP_PRE_BLE_STATS;

  if (g_joint_imu_thread_created)
    required |= PROBE_STOP_STEP_PRODUCER;
  if (g_serial_thread_created)
    required |= PROBE_STOP_STEP_CONSUMER;
  if (!probe_ui_has_exited())
    required |= PROBE_STOP_STEP_UI;
  if (g_communication_thread_created)
    required |= PROBE_STOP_STEP_COMMUNICATION;
  if (g_joint_input_thread_created)
    required |= PROBE_STOP_STEP_INPUT;
  if (g_instance != NULL || g_service_registered || g_adapter_enabled)
    required |= PROBE_STOP_STEP_BLE;
  return required;
}

static bool probe_joint_print_final_stats(const char *state,
                                          const char *phase,
                                          bool cleanup_complete,
                                          uint64_t snapshot_us)
{
  hb_diag rank_diag;
  struct probe_ui_stats ui_stats;
  struct probe_rank_counts final_counts;
  uint32_t head = __atomic_load_n(&g_serial_head, __ATOMIC_ACQUIRE);
  uint32_t tail = __atomic_load_n(&g_serial_tail, __ATOMIC_ACQUIRE);
  uint64_t runtime_us = snapshot_us >= g_joint_runtime_started_us ?
                        snapshot_us - g_joint_runtime_started_us : 0;
  int stats_status;
  int timing_status;
  uint32_t interval_i;

  for (interval_i = 0; interval_i < g_rank_interval_log.count; interval_i++)
    {
      const struct probe_rank_interval_record *r =
        &g_rank_interval_log.records[interval_i];
      int interval_status = snprintf(g_joint_stop_report,
        sizeof(g_joint_stop_report),
        "BTE1 STOP_INTERVAL index=%lu start_us=%llu end_us=%llu duration_us=%llu generated=%lu enqueued=%lu consumed=%lu queue_full=%lu GAP=%lu sequence_gap=%lu high_water=%lu hb_calls=%lu hb_total_us=%llu hb_max_us=%lu queue_wait_total_us=%llu queue_wait_max_us=%lu events=%lu sched_mode=%s\n",
        (unsigned long)interval_i, (unsigned long long)r->start_us,
        (unsigned long long)r->end_us,
        (unsigned long long)(r->end_us - r->start_us),
        (unsigned long)r->delta.generated,
        (unsigned long)r->delta.enqueued,
        (unsigned long)r->delta.consumed,
        (unsigned long)r->delta.queue_full,
        (unsigned long)r->delta.gap,
        (unsigned long)r->delta.sequence_gap,
        (unsigned long)r->queue_high_water,
        (unsigned long)r->delta.hb_calls,
        (unsigned long long)r->delta.hb_total_us,
        (unsigned long)r->hb_max_us,
        (unsigned long long)r->delta.wait_total_us,
        (unsigned long)r->wait_max_us,
        (unsigned long)r->delta.events, probe_sched_mode_name());
      if (interval_status < 0 ||
          (size_t)interval_status >= sizeof(g_joint_stop_report) ||
          !probe_joint_write_before_deadline(g_joint_stop_report,
                                              (size_t)interval_status))
        return false;
    }

  probe_ui_get_stats(&ui_stats);
  if (!g_joint_ui_run_valid)
    {
      memset(&ui_stats, 0, sizeof(ui_stats));
      ui_stats.initialized = false;
      ui_stats.exited = true;
    }
  hb_get_diag(&rank_diag);
  probe_rank_counts_snapshot(&final_counts);
  head = final_counts.enqueued;
  tail = __atomic_load_n(&g_serial_tail, __ATOMIC_ACQUIRE);
  stats_status = snprintf(g_joint_stop_report, sizeof(g_joint_stop_report),
         "BTE1 STOP_STATS state=%s phase=%s cleanup_complete=%u "
         "build_id=%s session=%lu ui_refresh=%s diag=%u sched_mode=%s "
         "capture_started=%u ready_emitted=%u ui_stats_valid=%u runtime_us=%llu "
         "generated=%lu enqueued=%lu consumed=%lu "
         "queue_full=%lu depth=%lu pending_samples=%lu high_water=%lu "
         "stop_seen=0x%lx stop_exit_begin=0x%lx stop_exit_done=0x%lx "
         "drain_complete=%u production_balance_ok=%u consumption_balance_ok=%u "
         "sequence_gap=%lu GAP_flag_count=%lu reset_count=%lu "
         "reset_count_run=%lu "
         "reset_gap=%lu reset_sequence=%lu reset_session=%lu reset_time=%lu "
         "event_generated=%lu event_written=%lu "
         "hb_elapsed_calls=%lu hb_elapsed_total_us=%llu "
         "hb_elapsed_max_us=%lu diag_output_calls=%lu "
         "diag_output_total_us=%llu diag_output_max_us=%lu "
         "stop_sample_drops=%lu "
         "stop_event_drops=%lu stop_ui_drops=%lu short_write=%lu "
         "ui_submitted=%lu ui_consumed=%lu ui_dropped=%lu "
         "ui_pending=%lu ui_logical_beats=%lu stop_requests=%lu "
         "duplicate_stop=%lu "
         "unknown_input=%lu overlong_input=%lu ble_status=0x%lx "
         "producer_stage_us=%llu consumer_stage_us=%llu ui_stage_us=%llu "
         "communication_stage_us=%llu input_stage_us=%llu "
         "ble_stage_us=%llu interval_records=%lu interval_dropped=%lu "
         "snapshot_fallbacks=%lu "
         "consumer_policy=%d consumer_priority=%d consumer_requested=%d "
         "sched_setup_ret=%d sched_query_ret=%d fifo_policy=%d "
         "fifo_priority=%d fifo_query_ret=%d\n",
         state, phase, cleanup_complete ? 1u : 0u,
         PROBE_EXPERIMENT_BUILD_ID, (unsigned long)g_serial_session,
         probe_ui_refresh_mode_name(g_joint_ui_refresh_mode),
         (unsigned)g_rank_diag_enabled, probe_sched_mode_name(),
         g_joint_capture_started ? 1u : 0u,
         g_joint_ready_emitted ? 1u : 0u,
         g_joint_ui_run_valid ? 1u : 0u,
         (unsigned long long)runtime_us,
         (unsigned long)final_counts.generated, (unsigned long)final_counts.enqueued,
         (unsigned long)final_counts.consumed,
         (unsigned long)final_counts.queue_full, (unsigned long)(head - tail),
         (unsigned long)(head - tail),
         (unsigned long)g_rank_queue_high_water,
         (unsigned long)__atomic_load_n(&g_joint_stop_seen_steps,
                                        __ATOMIC_ACQUIRE),
         (unsigned long)__atomic_load_n(&g_joint_stop_exit_begin_steps,
                                        __ATOMIC_ACQUIRE),
         (unsigned long)__atomic_load_n(&g_joint_stop_exit_done_steps,
                                        __ATOMIC_ACQUIRE),
         head == tail ? 1u : 0u,
         final_counts.generated == head + final_counts.queue_full ? 1u : 0u,
         head == final_counts.consumed + (head - tail) ? 1u : 0u,
         (unsigned long)g_rank_sequence_gap,
         (unsigned long)g_rank_gap_flags,
         (unsigned long)rank_diag.resets,
         (unsigned long)(rank_diag.resets - g_rank_reset_base),
         (unsigned long)rank_diag.reset_gap,
         (unsigned long)rank_diag.reset_sequence,
         (unsigned long)rank_diag.reset_session,
         (unsigned long)rank_diag.reset_time,
         (unsigned long)g_rank_events,
         (unsigned long)g_rank_event_written,
         (unsigned long)g_rank_feed_elapsed_calls,
         (unsigned long long)g_rank_feed_elapsed_total_us,
         (unsigned long)g_rank_feed_elapsed_max_us,
         (unsigned long)g_rank_diag_output_calls,
         (unsigned long long)g_rank_diag_output_total_us,
         (unsigned long)g_rank_diag_output_max_us,
         (unsigned long)g_joint_stop_sample_drops,
         (unsigned long)g_joint_stop_event_drops,
         (unsigned long)ui_stats.stop_drops,
         (unsigned long)g_serial_short_writes,
         (unsigned long)ui_stats.submitted,
         (unsigned long)ui_stats.consumed,
         (unsigned long)ui_stats.dropped,
         (unsigned long)(ui_stats.submitted - ui_stats.consumed),
         (unsigned long)ui_stats.logical_beats,
         (unsigned long)g_joint_stop_requests,
         (unsigned long)(g_joint_stop_requests > 0 ?
                           g_joint_stop_requests - 1 : 0),
         (unsigned long)g_joint_stop_unknown_lines,
         (unsigned long)g_joint_stop_overlong_lines,
         (unsigned long)g_joint_stop_ble_status,
         (unsigned long long)g_joint_stop_producer_us,
         (unsigned long long)g_joint_stop_consumer_us,
         (unsigned long long)g_joint_stop_ui_us,
         (unsigned long long)g_joint_stop_communication_us,
         (unsigned long long)g_joint_stop_input_us,
         (unsigned long long)g_joint_stop_ble_us,
         (unsigned long)g_rank_interval_log.count,
         (unsigned long)g_rank_interval_log.dropped,
         (unsigned long)g_rank_snapshot_fallbacks,
         g_rank_sched_consumer.policy, g_rank_sched_consumer.priority,
         g_rank_sched_requested_priority, g_rank_sched_setup_ret,
         g_rank_sched_consumer.query_ret, g_rank_sched_fifo.policy,
         g_rank_sched_fifo.priority, g_rank_sched_fifo.query_ret);
  if (stats_status < 0 ||
      (size_t)stats_status >= sizeof(g_joint_stop_report) ||
      !probe_joint_write_before_deadline(g_joint_stop_report,
                                         (size_t)stats_status))
    {
      return false;
    }

  timing_status = snprintf(g_joint_stop_report, sizeof(g_joint_stop_report),
         "BTE1 STOP_TIMING state=%s phase=%s cleanup_complete=%u "
         "build_id=%s session=%lu ui_refresh=%s diag=%u runtime_us=%llu "
         "queue_wait_calls=%lu queue_wait_total_us=%llu "
         "queue_wait_max_us=%lu encode_calls=%lu encode_total_us=%llu "
         "encode_max_us=%lu write_calls=%lu write_total_us=%llu "
         "write_max_us=%lu ui_submit_calls=%lu ui_submit_total_us=%llu "
         "ui_submit_max_us=%lu ui_init_refresh_calls=%lu "
         "ui_init_refresh_total_us=%llu ui_init_refresh_max_us=%lu "
         "ui_runtime_refresh_calls=%lu ui_runtime_refresh_total_us=%llu "
         "ui_runtime_refresh_max_us=%lu ui_runtime_flush_calls=%lu "
         "ui_runtime_flush_total_us=%llu ui_runtime_flush_max_us=%lu "
         "ui_maintenance_calls=%lu ui_maintenance_total_us=%llu "
         "ui_maintenance_max_us=%lu ui_refresh_deadline_misses=%lu\n",
         state, phase, cleanup_complete ? 1u : 0u,
         PROBE_EXPERIMENT_BUILD_ID, (unsigned long)g_serial_session,
         probe_ui_refresh_mode_name(g_joint_ui_refresh_mode),
         (unsigned)g_rank_diag_enabled, (unsigned long long)runtime_us,
         (unsigned long)g_rank_queue_wait_calls,
         (unsigned long long)g_rank_queue_wait_total_us,
         (unsigned long)g_rank_queue_wait_max_us,
         (unsigned long)g_rank_encode_calls,
         (unsigned long long)g_rank_encode_total_us,
         (unsigned long)g_rank_encode_max_us,
         (unsigned long)g_rank_write_calls,
         (unsigned long long)g_rank_write_total_us,
         (unsigned long)g_rank_write_max_us,
         (unsigned long)g_rank_ui_submit_calls,
         (unsigned long long)g_rank_ui_submit_total_us,
         (unsigned long)g_rank_ui_submit_max_us,
         (unsigned long)ui_stats.init_refresh_calls,
         (unsigned long long)ui_stats.init_refresh_total_us,
         (unsigned long)ui_stats.init_refresh_max_us,
         (unsigned long)ui_stats.runtime_refresh_calls,
         (unsigned long long)ui_stats.runtime_refresh_total_us,
         (unsigned long)ui_stats.runtime_refresh_max_us,
         (unsigned long)ui_stats.runtime_flush_calls,
         (unsigned long long)ui_stats.runtime_flush_total_us,
         (unsigned long)ui_stats.runtime_flush_max_us,
         (unsigned long)ui_stats.maintenance_calls,
         (unsigned long long)ui_stats.maintenance_total_us,
         (unsigned long)ui_stats.maintenance_max_us,
         (unsigned long)ui_stats.refresh_deadline_misses);
  return timing_status >= 0 &&
         (size_t)timing_status < sizeof(g_joint_stop_report) &&
         probe_joint_write_before_deadline(g_joint_stop_report,
                                           (size_t)timing_status);
}

static int probe_joint_cooperative_stop(void)
{
  uint64_t now_us = 0;
  uint64_t stage_started_us;
  uint32_t required;
  bool pre_ble_stats_emitted = false;
  int ret;

  if (probe_get_monotonic_us(&now_us) < 0)
    {
      return -EIO;
    }

  required = probe_joint_required_stop_steps();
  probe_stop_control_request(&g_joint_stop_control, now_us,
                             g_joint_stop_timeout_ms, required);
  __atomic_store_n(&g_joint_stop_in_progress, true, __ATOMIC_RELEASE);
  ret = snprintf(g_joint_stop_report, sizeof(g_joint_stop_report),
                 "BTE1 STOPPING timeout_ms=%lu required=0x%lx\n",
                 (unsigned long)g_joint_stop_timeout_ms,
                 (unsigned long)required);
  if (ret < 0 || (size_t)ret >= sizeof(g_joint_stop_report) ||
      !probe_joint_write_before_deadline(g_joint_stop_report, (size_t)ret))
    {
      goto timeout;
    }

  if ((required & PROBE_STOP_STEP_PRODUCER) != 0)
    {
      stage_started_us = now_us;
      (void)probe_get_monotonic_us(&stage_started_us);
      __atomic_store_n(&g_joint_producer_running, false, __ATOMIC_RELEASE);
      if (g_imu_drdy_sem_initialized)
        sem_post(&g_imu_drdy_sem);
      /* Stop the higher-priority consumer at the same boundary.  Waiting
       * for a lower-priority producer while the consumer remains runnable
       * can otherwise starve the producer under sched-b.  The consumer still
       * drains samples already published because its loop includes the queue
       * non-empty condition. */
      if (g_serial_thread_created)
        {
          __atomic_store_n(&g_serial_running, 0, __ATOMIC_RELEASE);
          sem_post(&g_serial_sem);
        }
      if (!probe_joint_wait_flag(&g_joint_imu_thread_exited,
                                 g_joint_stop_control.deadline_us))
        goto timeout;
      ret = pthread_join(g_joint_imu_thread, NULL);
      if (ret != 0)
        goto timeout;
      g_joint_imu_thread_created = false;
      probe_stop_control_complete(&g_joint_stop_control,
                                  PROBE_STOP_STEP_PRODUCER);
      (void)probe_get_monotonic_us(&now_us);
      g_joint_stop_producer_us = now_us >= stage_started_us ?
                                 now_us - stage_started_us : 0;
    }

  if ((required & PROBE_STOP_STEP_CONSUMER) != 0)
    {
      stage_started_us = now_us;
      (void)probe_get_monotonic_us(&stage_started_us);
      __atomic_store_n(&g_serial_running, 0, __ATOMIC_RELEASE);
      sem_post(&g_serial_sem);
      if (!probe_joint_wait_flag(&g_serial_thread_exited,
                                 g_joint_stop_control.deadline_us))
        goto timeout;
      ret = pthread_join(serial_thread, NULL);
      if (ret != 0)
        goto timeout;
      g_serial_thread_created = false;
      if (g_latency_dump_thread_created)
        {
          __atomic_store_n(&g_latency_dump_thread_running, false,
                           __ATOMIC_RELEASE);
          sem_post(&g_latency_dump_sem);
          ret = pthread_join(g_latency_dump_thread, NULL);
          if (ret != 0) goto timeout;
          g_latency_dump_thread_created = false;
          sem_destroy(&g_latency_dump_sem);
        }
      probe_stop_control_complete(&g_joint_stop_control,
                                  PROBE_STOP_STEP_CONSUMER);
      (void)probe_get_monotonic_us(&now_us);
      g_joint_stop_consumer_us = now_us >= stage_started_us ?
                                 now_us - stage_started_us : 0;
    }

  if ((required & PROBE_STOP_STEP_UI) != 0)
    {
      stage_started_us = now_us;
      (void)probe_get_monotonic_us(&stage_started_us);
      __atomic_fetch_or(&g_joint_stop_seen_steps,
                        PROBE_STOP_STEP_UI, __ATOMIC_RELAXED);
      __atomic_fetch_or(&g_joint_stop_exit_begin_steps,
                        PROBE_STOP_STEP_UI, __ATOMIC_RELAXED);
      probe_ui_request_stop();
      while (!probe_ui_has_exited())
        {
          if (!probe_joint_before_deadline())
            goto timeout;
          usleep(10000);
        }
      if (probe_ui_join() < 0)
        goto timeout;
      __atomic_fetch_or(&g_joint_stop_exit_done_steps,
                        PROBE_STOP_STEP_UI, __ATOMIC_RELEASE);
      probe_stop_control_complete(&g_joint_stop_control, PROBE_STOP_STEP_UI);
      (void)probe_get_monotonic_us(&now_us);
      g_joint_stop_ui_us = now_us >= stage_started_us ?
                           now_us - stage_started_us : 0;
    }

  if ((required & PROBE_STOP_STEP_COMMUNICATION) != 0)
    {
      stage_started_us = now_us;
      (void)probe_get_monotonic_us(&stage_started_us);
      __atomic_store_n(&g_communication_running, false, __ATOMIC_RELEASE);
      sem_post(&g_rx_sem);
      if (!probe_joint_wait_flag(&g_communication_exited,
                                 g_joint_stop_control.deadline_us))
        goto timeout;
      if (pthread_join(g_communication_thread, NULL) != 0)
        goto timeout;
      g_communication_thread_created = false;
      probe_stop_control_complete(&g_joint_stop_control,
                                  PROBE_STOP_STEP_COMMUNICATION);
      (void)probe_get_monotonic_us(&now_us);
      g_joint_stop_communication_us = now_us >= stage_started_us ?
                                      now_us - stage_started_us : 0;
    }

  if ((required & PROBE_STOP_STEP_INPUT) != 0)
    {
      stage_started_us = now_us;
      (void)probe_get_monotonic_us(&stage_started_us);
      __atomic_store_n(&g_joint_input_running, false, __ATOMIC_RELEASE);
      if (!probe_joint_wait_flag(&g_joint_input_exited,
                                 g_joint_stop_control.deadline_us))
        goto timeout;
      if (pthread_join(g_joint_input_thread, NULL) != 0)
        goto timeout;
      g_joint_input_thread_created = false;
      probe_stop_control_complete(&g_joint_stop_control,
                                  PROBE_STOP_STEP_INPUT);
      (void)probe_get_monotonic_us(&now_us);
      g_joint_stop_input_us = now_us >= stage_started_us ?
                              now_us - stage_started_us : 0;
    }

  /*
   * Everything that can still publish samples, events, or UI work has now
   * stopped.  Export the stable session evidence before GATT teardown: a
   * target-side allocator failure in BLE cleanup must not erase diag-off (or
   * diag-on) run statistics.  This is evidence only, not STOP_COMPLETE.
   */
  if (probe_get_monotonic_us(&now_us) < 0 ||
      now_us >= g_joint_stop_control.deadline_us)
    {
      goto timeout;
    }
  if (!probe_joint_print_final_stats("stopping", "pre_ble_cleanup", false,
                                     now_us) ||
      !probe_joint_before_deadline())
    {
      goto timeout;
    }
  probe_stop_control_complete(&g_joint_stop_control,
                              PROBE_STOP_STEP_PRE_BLE_STATS);
  pre_ble_stats_emitted = true;

  if ((required & PROBE_STOP_STEP_BLE) != 0)
    {
      stage_started_us = now_us;
      (void)probe_get_monotonic_us(&stage_started_us);
      __atomic_store_n(&g_joint_ble_cleanup_exited, false,
                       __ATOMIC_RELEASE);
      ret = pthread_create(&g_joint_ble_cleanup_thread, NULL,
                           probe_joint_ble_cleanup_thread, NULL);
      if (ret != 0)
        goto timeout;
      g_joint_ble_cleanup_thread_created = true;
      if (!probe_joint_wait_flag(&g_joint_ble_cleanup_exited,
                                 g_joint_stop_control.deadline_us))
        goto timeout;
      if (pthread_join(g_joint_ble_cleanup_thread, NULL) != 0)
        goto timeout;
      g_joint_ble_cleanup_thread_created = false;
      if (g_instance != NULL || bt_sal_le_host_initialized() ||
          bt_sal_le_cleanup_result() != 0)
        goto timeout;
      probe_stop_control_complete(&g_joint_stop_control, PROBE_STOP_STEP_BLE);
      (void)probe_get_monotonic_us(&now_us);
      g_joint_stop_ble_us = now_us >= stage_started_us ?
                            now_us - stage_started_us : 0;
    }

  if (!probe_stop_control_is_complete(&g_joint_stop_control))
    goto timeout;

  if (!probe_ble_lifecycle_dump())
    goto timeout;

  ret = snprintf(g_joint_stop_report, sizeof(g_joint_stop_report),
                 "BTE1 STOP_COMPLETE cleanup_complete=1 elapsed_us=%llu\n",
                 (unsigned long long)probe_stop_control_elapsed_us(
                   &g_joint_stop_control, now_us));
  if (ret < 0 || (size_t)ret >= sizeof(g_joint_stop_report) ||
      !probe_joint_write_before_deadline(g_joint_stop_report, (size_t)ret))
    {
      goto timeout;
    }
  probe_joint_restore_terminal();
  return 0;

timeout:
  (void)probe_get_monotonic_us(&now_us);
  g_joint_stop_pending_threads =
    g_joint_stop_control.required_steps &
    ~g_joint_stop_control.completed_steps;
  (void)probe_joint_print_final_stats("timeout",
                                      pre_ble_stats_emitted ?
                                        "ble_cleanup" : "pre_ble_cleanup",
                                      false, now_us);
  (void)probe_ble_lifecycle_dump();
  ret = snprintf(g_joint_stop_report, sizeof(g_joint_stop_report),
                 "BTE1 STOP_TIMEOUT elapsed_us=%llu pending_steps=0x%lx "
                 "producer=%u consumer=%u ui=%u communication=%u input=%u "
                 "ble=%u stop_seen=0x%lx stop_exit_begin=0x%lx "
                 "stop_exit_done=0x%lx action=reset_required\n",
                 (unsigned long long)probe_stop_control_elapsed_us(
                   &g_joint_stop_control, now_us),
                 (unsigned long)g_joint_stop_pending_threads,
                 g_joint_imu_thread_created && !g_joint_imu_thread_exited,
                 g_serial_thread_created && !g_serial_thread_exited,
                 !probe_ui_has_exited(),
                 g_communication_thread_created && !g_communication_exited,
                 g_joint_input_thread_created && !g_joint_input_exited,
                 g_joint_ble_cleanup_thread_created &&
                   !g_joint_ble_cleanup_exited,
                 (unsigned long)__atomic_load_n(&g_joint_stop_seen_steps,
                                                __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_joint_stop_exit_begin_steps,
                                                __ATOMIC_ACQUIRE),
                 (unsigned long)__atomic_load_n(&g_joint_stop_exit_done_steps,
                                                __ATOMIC_ACQUIRE));
  if (ret > 0 && (size_t)ret < sizeof(g_joint_stop_report))
    {
      probe_joint_try_write_nonblocking(g_joint_stop_report, (size_t)ret);
    }
  return -ETIMEDOUT;
}
#endif

int main(int argc, char *argv[])
{
#if BLE_PROBE_ENABLE_AUTO_2M_PHY
  bt_address_t peer_addr;
#endif
  bt_status_t status;
  uint64_t startup_target_ms;
  uint64_t adapter_wait_start_us = 0;
#if BLE_PROBE_ENABLE_AUTO_2M_PHY
  uint32_t generation;
  bool do_phy_read;
  bool do_phy_update;
#endif
  int ret;

#if BLE_PROBE_JOINT_UI_BLE
  g_joint_ui_refresh_mode = PROBE_UI_REFRESH_FULL;
  g_joint_stop_timeout_ms = PROBE_STOP_DEFAULT_TIMEOUT_MS;
  g_rank_diag_enabled = true;
  g_rank_sched_mode = PROBE_SCHED_A;
#endif

  if (argc == 2 && strcmp(argv[1], "--rank1-benchmark") == 0)
    {
      return probe_rank1_benchmark_run();
    }

  if (probe_parse_startup_uptime(argc, argv, &startup_target_ms) < 0)
    {
      return 1;
    }

#if BLE_PROBE_JOINT_UI_BLE
  /* A second invocation is allowed only after every resource-owning thread
   * and callback from the previous run has quiesced.  In particular, do not
   * clear statistics while an old worker can still reference them. */
  if (probe_joint_resources_active())
    {
      printf("openvela_ble_probe: startup refused: previous resources "
             "still active; reset required\n");
      return 1;
    }
  g_joint_run_generation++;
  probe_ble_lifecycle_reset();
  probe_joint_reset_run_state();
#endif

#if BLE_PROBE_SERIAL_ONLY
  (void)startup_target_ms;
  printf("openvela_ble_probe: serial-only IMU mode; BLE/game/UI disabled\n");
#if BLE_PROBE_ONBOARD_BEAT
  hb_reset();
#endif
  g_serial_session = (uint32_t)clock_systime_ticks();
#if !BLE_PROBE_ONBOARD_BEAT
  printf("openvela_ble_probe: serial beat link enabled prefix=@BEAT1"
         " build_id=%s\n", PROBE_EXPERIMENT_BUILD_ID);
  probe_serial_parse_selftest();
#else
  printf("openvela_ble_probe: onboard rank1 enabled prefix=@BTE1 build_id=%s model=%s\n",
         PROBE_EXPERIMENT_BUILD_ID, HB_VERSION);
#endif
  if (tcgetattr(STDIN_FILENO, &g_serial_termios) == 0)
    {
      struct termios t = g_serial_termios;
      t.c_lflag &= (tcflag_t)~ECHO;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0)
        g_serial_termios_saved = true;
    }
  if (sem_init(&g_serial_sem, 0, 0) < 0)
    {
      probe_serial_restore_terminal();
      printf("IMU1 FAIL stage=queue errno=%d\n", errno);
      return 1;
    }
  __atomic_store_n(&g_serial_running, 1, __ATOMIC_RELEASE);
#if !BLE_PROBE_ONBOARD_BEAT
  __atomic_store_n(&g_beat_running, 1, __ATOMIC_RELEASE);
#endif
  ret = pthread_create(&serial_thread, NULL,
                       probe_serial_output_thread, NULL);
  if (ret != 0)
    {
      probe_serial_restore_terminal();
      printf("IMU1 FAIL stage=output_thread errno=%d\n", ret);
      return 1;
    }
#if !BLE_PROBE_ONBOARD_BEAT
  ret = pthread_create(&beat_thread, NULL, probe_serial_beat_thread, NULL);
  if (ret != 0)
    {
      __atomic_store_n(&g_beat_running, 0, __ATOMIC_RELEASE);
      __atomic_store_n(&g_serial_running, 0, __ATOMIC_RELEASE);
      sem_post(&g_serial_sem);
      pthread_join(serial_thread, NULL);
      probe_serial_restore_terminal();
      return 1;
    }
#endif
  ret = pthread_create(&imu_thread, NULL,
                       probe_imu_raw_thread, NULL);
  if (ret != 0)
    {
      printf("IMU1 FAIL stage=imu_thread errno=%d\n", ret);
      __atomic_store_n(&g_serial_running, 0, __ATOMIC_RELEASE);
      sem_post(&g_serial_sem);
      probe_serial_restore_terminal();
      return 1;
    }
  pthread_join(imu_thread, NULL);
  __atomic_store_n(&g_beat_running, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_serial_running, 0, __ATOMIC_RELEASE);
  sem_post(&g_serial_sem);
#if !BLE_PROBE_ONBOARD_BEAT
  pthread_join(beat_thread, NULL);
#endif
  pthread_join(serial_thread, NULL);
  probe_serial_restore_terminal();
  return 0;
#endif

#if BLE_PROBE_JOINT_UI_BLE
  memset(&g_joint_stop_control, 0, sizeof(g_joint_stop_control));
  __atomic_store_n(&g_joint_stop_requested, false, __ATOMIC_RELEASE);
  __atomic_store_n(&g_joint_stop_in_progress, false, __ATOMIC_RELEASE);
  __atomic_store_n(&g_joint_producer_running, false, __ATOMIC_RELEASE);
  g_joint_stop_requests = 0;
  g_joint_stop_unknown_lines = 0;
  g_joint_stop_overlong_lines = 0;
  g_joint_stop_sample_drops = 0;
  g_joint_stop_event_drops = 0;
  g_joint_stop_ui_drops = 0;
  g_joint_stop_producer_us = 0;
  g_joint_stop_consumer_us = 0;
  g_joint_stop_ui_us = 0;
  g_joint_stop_communication_us = 0;
  g_joint_stop_input_us = 0;
  g_joint_stop_ble_us = 0;
  g_joint_stop_ble_status = 0;
  g_joint_stop_seen_steps = 0;
  g_joint_stop_exit_begin_steps = 0;
  g_joint_stop_exit_done_steps = 0;
  g_joint_runtime_started_us = 0;
  (void)probe_get_monotonic_us(&g_joint_runtime_started_us);
  g_visible_game_launch_pending = false;
  g_visible_game_launch_attempted = false;
  g_imu_raw_launch_pending = false;
  g_imu_raw_launch_attempted = false;
  g_imu_raw_started = false;
  g_imu_raw_launch_check_logged = false;
  g_ble_on = false;
  printf("openvela_ble_probe: joint onboard rank1 + UI + BLE mode "
         "build_id=%s ui_refresh=%s ui_refresh_limit_hz=%u diag=%u sched_mode=%s "
         "stop_timeout_ms=%lu; BTE1 remains UART-only\n",
         PROBE_EXPERIMENT_BUILD_ID,
         probe_ui_refresh_mode_name(g_joint_ui_refresh_mode),
         g_joint_ui_refresh_mode == PROBE_UI_REFRESH_24HZ ?
           PROBE_UI_REFRESH_LIMIT_HZ : 0,
         (unsigned)g_rank_diag_enabled,
         probe_sched_mode_name(),
         (unsigned long)g_joint_stop_timeout_ms);

  ret = pthread_mutex_init(&g_state_lock, NULL);
  if (ret != 0)
    {
      printf("openvela_ble_probe: state mutex init failed:%d\n", ret);
      return 1;
    }
  g_state_lock_initialized = true;

  ret = probe_joint_start_input();
  if (ret < 0)
    {
      printf("openvela_ble_probe: stop input start failed:%d\n", ret);
      pthread_mutex_destroy(&g_state_lock);
      g_state_lock_initialized = false;
      return 1;
    }
  printf("openvela_ble_probe: application input owner active "
         "command=stop echo=off canonical=preserved\n");
#endif

#if BLE_PROBE_CAPTURE_ACK_ONLY
  printf("openvela_ble_probe: ACK-only diagnostic build; capture sample "
         "production, wakeups, and transmission disabled; not capture pass\n");
#else
  printf("openvela_ble_probe: normal capture sample transmission build\n");
#endif

  if (probe_wait_for_ble_startup(startup_target_ms) < 0)
    {
#if BLE_PROBE_JOINT_UI_BLE
      goto joint_start_failed;
#else
      return 1;
#endif
    }

#if BLE_PROBE_JOINT_UI_BLE
  if (__atomic_load_n(&g_joint_stop_requested, __ATOMIC_ACQUIRE))
    goto joint_stop_requested;
#endif

  g_imu_detected = probe_detect_imu();

  if (sem_init(&g_ble_on_sem, 0, 0) < 0)
    {
      printf("openvela_ble_probe: BLE semaphore init failed errno=%d\n",
             errno);
#if BLE_PROBE_JOINT_UI_BLE
      goto joint_start_failed;
#else
      return 1;
#endif
    }
#if BLE_PROBE_JOINT_UI_BLE
  g_ble_on_sem_initialized = true;
#endif

  g_instance = bluetooth_create_instance();
#if BLE_PROBE_JOINT_UI_BLE
  probe_ble_lifecycle_note(PROBE_BLE_LC_INSTANCE_CREATE,
                           g_instance != NULL ? 0 : -1,
                           g_instance != NULL ?
                           bt_adapter_get_state(g_instance) : -1);
#endif
  if (g_instance == NULL)
    {
      printf("openvela_ble_probe: bluetooth_create_instance failed\n");
#if BLE_PROBE_JOINT_UI_BLE
      goto joint_start_failed;
#else
      return 1;
#endif
    }

  g_adapter_callback_cookie =
    bt_adapter_register_callback(g_instance, &g_adapter_callbacks);
#if BLE_PROBE_JOINT_UI_BLE
  probe_ble_lifecycle_note(PROBE_BLE_LC_CALLBACK_REGISTER,
                           g_adapter_callback_cookie != NULL ? 0 : -1,
                           bt_adapter_get_state(g_instance));
#endif
  if (g_adapter_callback_cookie == NULL)
    {
      printf("openvela_ble_probe: callback registration failed\n");
#if BLE_PROBE_JOINT_UI_BLE
      goto joint_start_failed;
#else
      return 1;
#endif
    }

  status = bt_adapter_enable_le(g_instance);
#if BLE_PROBE_JOINT_UI_BLE
  probe_ble_lifecycle_note(PROBE_BLE_LC_ENABLE_REQUEST, status,
                           bt_adapter_get_state(g_instance));
#endif
  if (status != BT_STATUS_SUCCESS && status != BT_STATUS_DONE)
    {
      printf("openvela_ble_probe: bt_adapter_enable_le failed:%d\n",
             status);
#if BLE_PROBE_JOINT_UI_BLE
      goto joint_start_failed;
#else
      return 1;
#endif
    }
#if BLE_PROBE_JOINT_UI_BLE
  g_adapter_enabled = true;
#endif

  if (bt_adapter_get_state(g_instance) != BT_ADAPTER_STATE_BLE_ON &&
      bt_adapter_get_state(g_instance) != BT_ADAPTER_STATE_ON)
    {
      printf("openvela_ble_probe: waiting for BLE adapter\n");
      (void)probe_get_monotonic_us(&adapter_wait_start_us);
      for (;;)
        {
          struct timespec adapter_wait;
          uint64_t adapter_now_us;

#if BLE_PROBE_JOINT_UI_BLE
          if (__atomic_load_n(&g_joint_stop_requested, __ATOMIC_ACQUIRE))
            goto joint_stop_requested;
#endif
          if (clock_gettime(CLOCK_REALTIME, &adapter_wait) < 0)
            continue;
          probe_timespec_add_ms(&adapter_wait, 100);
          ret = sem_timedwait(&g_ble_on_sem, &adapter_wait);
          if (ret == 0 || bt_adapter_get_state(g_instance) ==
                          BT_ADAPTER_STATE_BLE_ON ||
              bt_adapter_get_state(g_instance) == BT_ADAPTER_STATE_ON)
            break;
          if (probe_get_monotonic_us(&adapter_now_us) == 0 &&
              adapter_now_us >= adapter_wait_start_us &&
              adapter_now_us - adapter_wait_start_us >=
                (uint64_t)PROBE_BLE_ADAPTER_WAIT_TIMEOUT_MS * 1000ULL)
            {
#if BLE_PROBE_JOINT_UI_BLE
              probe_ble_lifecycle_note(PROBE_BLE_LC_STARTUP_TIMEOUT,
                                       -ETIMEDOUT,
                                       bt_adapter_get_state(g_instance));
#endif
              printf("openvela_ble_probe: BLE adapter wait timeout "
                     "elapsed_ms=%llu state=%d\n",
                     (unsigned long long)((adapter_now_us -
                                           adapter_wait_start_us) / 1000ULL),
                     bt_adapter_get_state(g_instance));
#if BLE_PROBE_JOINT_UI_BLE
              goto joint_start_failed;
#else
              return 1;
#endif
            }
          if (errno != ETIMEDOUT && errno != EINTR)
            {
#if BLE_PROBE_JOINT_UI_BLE
              goto joint_start_failed;
#else
              return 1;
#endif
            }
        }
    }

#if !BLE_PROBE_JOINT_UI_BLE
  ret = pthread_mutex_init(&g_state_lock, NULL);
  if (ret != 0)
    {
      printf("openvela_ble_probe: state mutex init failed:%d\n", ret);
      return 1;
    }
#endif

  if (sem_init(&g_rx_sem, 0, 0) < 0)
    {
      printf("openvela_ble_probe: RX semaphore init failed\n");
#if BLE_PROBE_JOINT_UI_BLE
      goto joint_start_failed;
#else
      return 1;
#endif
    }
#if BLE_PROBE_JOINT_UI_BLE
  g_rx_sem_initialized = true;
#endif

  if (init_gatt_service() < 0)
    {
#if BLE_PROBE_JOINT_UI_BLE
      goto joint_start_failed;
#else
      return 1;
#endif
    }
#if BLE_PROBE_JOINT_UI_BLE
  g_service_registered = true;
#endif

  __atomic_store_n(&g_communication_running, true, __ATOMIC_RELEASE);
  __atomic_store_n(&g_communication_exited, false, __ATOMIC_RELEASE);
  ret = pthread_create(&g_communication_thread, NULL,
                       probe_communication_thread, NULL);
  if (ret != 0)
    {
      __atomic_store_n(&g_communication_running, false, __ATOMIC_RELEASE);
      printf("openvela_ble_probe: communication thread create failed:%d\n",
             ret);
#if BLE_PROBE_JOINT_UI_BLE
      goto joint_start_failed;
#else
      return 1;
#endif
    }
#if BLE_PROBE_JOINT_UI_BLE
  g_communication_thread_created = true;
#endif

  printf("openvela_ble_probe: delaying advertising 100 ms after GATT "
         "table setup\n");
  usleep(100000);

  if (start_advertising() < 0)
    {
#if BLE_PROBE_JOINT_UI_BLE
      goto joint_start_failed;
#else
      return 1;
#endif
    }

  for (;;)
    {
      usleep(100000);

#if BLE_PROBE_JOINT_UI_BLE
      if (__atomic_load_n(&g_joint_stop_requested, __ATOMIC_ACQUIRE))
        {
          goto joint_stop_requested;
        }
#endif

      probe_launch_visible_game_if_pending();
      probe_launch_imu_raw_if_pending();
      process_advertising_restart();

#if BLE_PROBE_ENABLE_AUTO_2M_PHY
      do_phy_read = false;
      do_phy_update = false;

      pthread_mutex_lock(&g_state_lock);
      generation = __atomic_load_n(&g_connection_generation,
                                   __ATOMIC_ACQUIRE);
      if (g_peer_connected && g_phy_read_pending &&
          !g_phy_read_requested)
        {
          g_phy_read_pending = false;
          g_phy_read_requested = true;
          memcpy(&peer_addr, &g_peer_addr, sizeof(peer_addr));
          do_phy_read = true;
        }
      else if (g_peer_connected && g_phy_update_pending &&
               !g_phy_update_requested)
        {
          g_phy_update_pending = false;
          g_phy_update_requested = true;
          memcpy(&peer_addr, &g_peer_addr, sizeof(peer_addr));
          do_phy_update = true;
        }
      pthread_mutex_unlock(&g_state_lock);

      if (do_phy_read)
        {
          status = bt_gatts_read_phy(g_service_handle, &peer_addr);
          printf("openvela_ble_probe: PHY read request status=%d\n",
                 status);

          if (status == BT_STATUS_SUCCESS)
            {
              pthread_mutex_lock(&g_state_lock);
              if (g_peer_connected &&
                  generation ==
                  __atomic_load_n(&g_connection_generation,
                                  __ATOMIC_ACQUIRE))
                {
                  g_phy_update_pending = true;
                }
              pthread_mutex_unlock(&g_state_lock);
            }

          continue;
        }

      if (do_phy_update)
        {
          status = bt_gatts_update_phy(g_service_handle, &peer_addr,
                                       BT_LE_2M_PHY, BT_LE_2M_PHY);
          printf("openvela_ble_probe: PHY 2M request status=%d\n",
                 status);
        }
#endif
    }

#if BLE_PROBE_JOINT_UI_BLE
joint_start_failed:
  printf("openvela_ble_probe: startup failure entering cooperative cleanup\n");
  __atomic_store_n(&g_joint_stop_requested, true, __ATOMIC_RELEASE);

joint_stop_requested:
  ret = probe_joint_cooperative_stop();
  if (ret < 0)
    {
      for (;;)
        {
          usleep(1000000);
        }
    }

  if (g_rank_runtime_started)
    {
      sem_destroy(&g_serial_sem);
      g_rank_runtime_started = false;
    }
  (void)probe_ui_join();
  if (g_rx_sem_initialized)
    {
      sem_destroy(&g_rx_sem);
      g_rx_sem_initialized = false;
    }
  if (g_ble_on_sem_initialized)
    {
      sem_destroy(&g_ble_on_sem);
      g_ble_on_sem_initialized = false;
    }
  if (g_state_lock_initialized)
    {
      pthread_mutex_destroy(&g_state_lock);
      g_state_lock_initialized = false;
    }
  return 0;
#endif
}
