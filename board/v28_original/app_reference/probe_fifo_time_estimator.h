#ifndef PROBE_FIFO_TIME_ESTIMATOR_H
#define PROBE_FIFO_TIME_ESTIMATOR_H
#include <stdbool.h>
#include <stdint.h>
struct probe_fifo_time_estimator
{
  uint64_t estimate_us;
  uint32_t estimate_frac;
  uint64_t period_q16;
  uint64_t last_drain_us;
  uint32_t last_seq;
  bool valid;
  bool anchor_valid;
  uint64_t window_drain_us;
  uint32_t window_seq;
  uint32_t window_samples;
  int64_t slew_us;
  int64_t phase_offset_us;
  uint32_t estimated_sequence;
  bool estimated_sequence_valid;
  uint32_t increment_floor_hits;
  uint32_t period_rejects;
  uint32_t phase_saturations;
};
void probe_fifo_time_estimator_init(struct probe_fifo_time_estimator *e,
                                    uint32_t hz);
void probe_fifo_time_estimator_reset(struct probe_fifo_time_estimator *e,
                                     uint32_t hz);
void probe_fifo_time_estimator_set_phase_offset(struct probe_fifo_time_estimator *e,
                                                int64_t offset_us);
void probe_fifo_time_estimator_observe(struct probe_fifo_time_estimator *e,
                                       uint32_t next_seq,
                                       uint64_t drain_us,
                                       uint32_t hz);
/* Observe the end of one FIFO snapshot.  seq is the last complete sample
 * represented by this snapshot; empty/partial snapshots are ignored. */
void probe_fifo_time_estimator_observe_end(struct probe_fifo_time_estimator *e,
                                           uint32_t last_seq,
                                           uint64_t drain_us,
                                           uint32_t complete_samples,
                                           uint32_t hz);
uint64_t probe_fifo_time_estimator_first(struct probe_fifo_time_estimator *e,
                                         uint64_t drain_us,
                                         uint32_t remaining_after,
                                         uint32_t sample_sequence,
                                         uint32_t hz);
uint64_t probe_fifo_time_estimator_next(struct probe_fifo_time_estimator *e);
#endif
