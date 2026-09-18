#include "probe_fifo_time_estimator.h"

void probe_fifo_time_estimator_init(struct probe_fifo_time_estimator *e,
                                    uint32_t hz)
{
  e->estimate_us = 0; e->estimate_frac = 0;
  e->period_q16 = ((uint64_t)1000000 << 16) / hz;
  e->last_drain_us = 0; e->last_seq = 0;
  e->valid = false; e->anchor_valid = false;
  e->window_drain_us = 0; e->window_seq = 0;
  e->window_samples = 0; e->slew_us = 0;
  e->increment_floor_hits = 0; e->period_rejects = 0;
  e->phase_saturations = 0;
  e->phase_offset_us = 0;
  e->estimated_sequence = 0; e->estimated_sequence_valid = false;
}

void probe_fifo_time_estimator_set_phase_offset(struct probe_fifo_time_estimator *e,
                                                int64_t offset_us)
{ e->phase_offset_us = offset_us; }

void probe_fifo_time_estimator_reset(struct probe_fifo_time_estimator *e,
                                     uint32_t hz)
{ probe_fifo_time_estimator_init(e, hz); }

void probe_fifo_time_estimator_observe(struct probe_fifo_time_estimator *e,
                                       uint32_t next_seq,
                                       uint64_t drain_us, uint32_t hz)
{
  if (e->anchor_valid && next_seq > e->last_seq && drain_us > e->last_drain_us)
    {
      if (e->window_samples == 0) { e->window_seq = e->last_seq; e->window_drain_us = e->last_drain_us; }
      e->window_samples += next_seq - e->last_seq;
      if (e->window_samples >= 256)
        {
          uint64_t dn = (uint64_t)(next_seq - e->window_seq);
          uint64_t obs = ((drain_us - e->window_drain_us) << 16) / dn;
          uint64_t nominal = ((uint64_t)1000000 << 16) / hz;
          if (obs > nominal / 2 && obs < nominal * 2)
            {
              /* Absolute phase at the same末 sample endpoint.  The
               * observation timestamp is only a delayed boundary, so this
               * remains an estimate, but it is not merely interval growth. */
              int64_t predicted = (int64_t)e->estimate_us;
              if (!e->estimated_sequence_valid || e->estimated_sequence != next_seq)
                predicted += (int64_t)((dn * e->period_q16) >> 16);
              int64_t phase = (int64_t)drain_us - predicted;
              e->period_q16 = (e->period_q16 * 7 + obs) / 8;
              e->slew_us = phase / 8;
            }
          else e->period_rejects++;
          e->window_samples = 0;
        }
    }
  e->last_drain_us = drain_us;
  e->last_seq = next_seq;
  e->anchor_valid = true;
  /* Do not relabel an already-published estimate. */
}

void probe_fifo_time_estimator_observe_end(struct probe_fifo_time_estimator *e,
                                           uint32_t last_seq,
                                           uint64_t drain_us,
                                           uint32_t complete_samples,
                                           uint32_t hz)
{
  if (complete_samples == 0) return;
  probe_fifo_time_estimator_observe(e, last_seq, drain_us, hz);
}

uint64_t probe_fifo_time_estimator_first(struct probe_fifo_time_estimator *e,
                                         uint64_t drain_us,
                                         uint32_t remaining_after,
                                         uint32_t sample_sequence, uint32_t hz)
{
  e->estimate_us = (uint64_t)((int64_t)drain_us -
    (int64_t)((uint64_t)remaining_after * 1000000ULL / hz) + e->phase_offset_us);
  e->estimate_frac = 0; e->valid = true;
  e->estimated_sequence = sample_sequence;
  e->estimated_sequence_valid = true;
  return e->estimate_us;
}

uint64_t probe_fifo_time_estimator_next(struct probe_fifo_time_estimator *e)
{
  uint64_t step = e->period_q16 + e->estimate_frac;
  int64_t inc = (int64_t)(step >> 16);
  if (e->slew_us != 0)
    {
      int64_t adj = e->slew_us > 0 ? 500 : -500;
      if ((adj > 0 && adj > e->slew_us) || (adj < 0 && adj < e->slew_us)) adj = e->slew_us;
      if (inc + adj < 1) { adj = 1 - inc; e->increment_floor_hits++; }
      if (adj == 500 || adj == -500) e->phase_saturations++;
      inc += adj; e->slew_us -= adj;
    }
  e->estimate_us += (uint64_t)inc;
  if (e->estimated_sequence_valid) e->estimated_sequence++;
  e->estimate_frac = (uint32_t)(step & 0xffffu);
  return e->estimate_us;
}
