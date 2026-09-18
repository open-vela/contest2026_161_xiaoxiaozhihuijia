#include <string.h>
#include "probe_rank_interval.h"

static uint32_t sub32(uint32_t a, uint32_t b) { return a - b; }
static uint64_t sub64(uint64_t a, uint64_t b) { return a - b; }

void probe_rank_interval_init(struct probe_rank_interval_log *log,
                              uint64_t now_us,
                              const struct probe_rank_counts *counts)
{
  memset(log, 0, sizeof(*log));
  log->base = *counts;
  log->start_us = now_us;
  log->next_us = now_us + PROBE_RANK_INTERVAL_US;
}

void probe_rank_interval_note_depth(struct probe_rank_interval_log *log,
                                    uint32_t depth)
{
  uint32_t old = __atomic_load_n(&log->interval_high_water,
                                 __ATOMIC_RELAXED);
  while (depth > old &&
         !__atomic_compare_exchange_n(&log->interval_high_water, &old, depth,
                                      false, __ATOMIC_RELAXED,
                                      __ATOMIC_RELAXED))
    {
    }
}

void probe_rank_interval_note_hb(struct probe_rank_interval_log *log,
                                 uint32_t elapsed_us)
{
  if (elapsed_us > log->hb_max_us) log->hb_max_us = elapsed_us;
}

void probe_rank_interval_note_wait(struct probe_rank_interval_log *log,
                                   uint32_t elapsed_us)
{
  if (elapsed_us > log->wait_max_us) log->wait_max_us = elapsed_us;
}

void probe_rank_interval_maybe_close(struct probe_rank_interval_log *log,
                                     uint64_t now_us,
                                     const struct probe_rank_counts *counts,
                                     bool force)
{
  struct probe_rank_interval_record *r;
  if ((!force && now_us < log->next_us) || now_us <= log->start_us) return;
  if (log->count >= PROBE_RANK_INTERVAL_CAPACITY)
    {
      log->dropped++;
    }
  else
    {
      r = &log->records[log->count++];
      r->start_us = log->start_us; r->end_us = now_us;
      r->delta.generated = sub32(counts->generated, log->base.generated);
      r->delta.enqueued = sub32(counts->enqueued, log->base.enqueued);
      r->delta.consumed = sub32(counts->consumed, log->base.consumed);
      r->delta.queue_full = sub32(counts->queue_full, log->base.queue_full);
      r->delta.gap = sub32(counts->gap, log->base.gap);
      r->delta.sequence_gap = sub32(counts->sequence_gap,
                                    log->base.sequence_gap);
      r->delta.hb_calls = sub32(counts->hb_calls, log->base.hb_calls);
      r->delta.events = sub32(counts->events, log->base.events);
      r->delta.hb_total_us = sub64(counts->hb_total_us,
                                   log->base.hb_total_us);
      r->delta.wait_total_us = sub64(counts->wait_total_us,
                                     log->base.wait_total_us);
      r->queue_high_water = __atomic_exchange_n(&log->interval_high_water, 0,
                                                __ATOMIC_RELAXED);
      r->hb_max_us = log->hb_max_us; r->wait_max_us = log->wait_max_us;
    }
  log->base = *counts; log->start_us = now_us;
  log->next_us = now_us + PROBE_RANK_INTERVAL_US;
  log->hb_max_us = 0; log->wait_max_us = 0;
}
