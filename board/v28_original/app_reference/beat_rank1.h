#ifndef BEAT_RANK1_H
#define BEAT_RANK1_H
#include <stddef.h>
#include <stdint.h>
#ifndef HB_EVAL_STRIDE
#define HB_EVAL_STRIDE 2
#endif
#define HB_VERSION "rank1_t014_r012_restart_20260912_c1"
/* One singleton, one task-context caller; not reentrant and not ISR safe. */
typedef struct {
    uint32_t session, sequence;
    uint64_t monotonic_us; /* sample time, including existing FIFO estimate */
    int16_t accel[3], gyro[3]; /* +/-4g and +/-1000 dps, not converted */
    uint16_t flags; /* bit 0 ESTIMATED_TIME; bit 1 GAP */
} hb_sample;
typedef struct {
    uint8_t event; /* always 1 */
    uint8_t flags; /* bit 0: beat timestamp estimated */
    uint32_t source_sequence;
    uint64_t beat_time_us, confirmation_sample_us;
    double nominal_center_s, nominal_confirmation_s, score;
} hb_event;
void hb_reset(void);
/* 1=event; 0=no event; -1=invalid pointer. Feed every ordered full sample. */
int hb_feed(const hb_sample *sample, hb_event *event);
size_t hb_state_bytes(void);
size_t hb_model_bytes(void);
typedef struct { uint32_t resets, reset_gap, reset_sequence, reset_session, reset_time, candidates, evaluations; } hb_diag;
void hb_get_diag(hb_diag *d);
typedef struct { uint32_t filter_calls, feature_calls, match_calls, gate_calls; uint64_t filter_total_us, feature_total_us, match_total_us, gate_total_us; uint32_t filter_max_us, feature_max_us, match_max_us, gate_max_us; } hb_timing;
void hb_get_timing(hb_timing *t);
/* Proposed NEW UART event envelope. Does not write UART or allocate.
 * detected_us is the caller's monotonic clock read after hb_feed returns 1.
 * Returns encoded byte count (excluding NUL), or 0 if capacity insufficient. */
size_t hb_encode_event(const hb_event *event, uint32_t session,
                      uint32_t event_id, uint64_t detected_us,
                      char *line, size_t capacity);
#endif
