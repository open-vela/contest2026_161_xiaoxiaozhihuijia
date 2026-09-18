#ifndef PROBE_RANK_INTERVAL_H
#define PROBE_RANK_INTERVAL_H

#include <stdbool.h>
#include <stdint.h>

#define PROBE_RANK_INTERVAL_CAPACITY 48
#define PROBE_RANK_INTERVAL_US 5000000ULL

struct probe_rank_counts
{
  uint32_t generated, enqueued, consumed, queue_full;
  uint32_t gap, sequence_gap, hb_calls, events;
  uint64_t hb_total_us, wait_total_us;
};

struct probe_rank_interval_record
{
  uint64_t start_us, end_us;
  struct probe_rank_counts delta;
  uint32_t queue_high_water, hb_max_us, wait_max_us;
};

struct probe_rank_interval_log
{
  struct probe_rank_interval_record records[PROBE_RANK_INTERVAL_CAPACITY];
  struct probe_rank_counts base;
  uint64_t start_us, next_us;
  volatile uint32_t interval_high_water;
  uint32_t hb_max_us, wait_max_us, count, dropped;
};

void probe_rank_interval_init(struct probe_rank_interval_log *log,
                              uint64_t now_us,
                              const struct probe_rank_counts *counts);
void probe_rank_interval_note_depth(struct probe_rank_interval_log *log,
                                    uint32_t depth);
void probe_rank_interval_note_hb(struct probe_rank_interval_log *log,
                                 uint32_t elapsed_us);
void probe_rank_interval_note_wait(struct probe_rank_interval_log *log,
                                   uint32_t elapsed_us);
void probe_rank_interval_maybe_close(struct probe_rank_interval_log *log,
                                     uint64_t now_us,
                                     const struct probe_rank_counts *counts,
                                     bool force);

#endif
