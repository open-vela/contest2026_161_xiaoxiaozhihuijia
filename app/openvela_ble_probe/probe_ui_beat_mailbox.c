#include "probe_ui_beat_mailbox.h"

#include <string.h>

void probe_ui_beat_mailbox_init(struct probe_ui_beat_mailbox *mailbox)
{
  memset(mailbox, 0, sizeof(*mailbox));
}

void probe_ui_beat_mailbox_publish(struct probe_ui_beat_mailbox *mailbox,
                                   const struct probe_ui_beat_event *event)
{
  uint32_t version = __atomic_load_n(&mailbox->version, __ATOMIC_RELAXED);
  uint32_t submitted = __atomic_load_n(&mailbox->submitted,
                                        __ATOMIC_RELAXED);

  __atomic_store_n(&mailbox->version, version + 1, __ATOMIC_RELEASE);
  memcpy(&mailbox->latest, event, sizeof(*event));
  __atomic_store_n(&mailbox->submitted, submitted + 1, __ATOMIC_RELAXED);
  __atomic_store_n(&mailbox->version, version + 2, __ATOMIC_RELEASE);
}

bool probe_ui_beat_mailbox_take_latest(struct probe_ui_beat_mailbox *mailbox,
                                       struct probe_ui_beat_event *event,
                                       uint32_t *submitted)
{
  uint32_t before;
  uint32_t after;
  uint32_t available;

  do
    {
      before = __atomic_load_n(&mailbox->version, __ATOMIC_ACQUIRE);
      if ((before & 1u) != 0)
        {
          continue;
        }

      available = __atomic_load_n(&mailbox->submitted, __ATOMIC_RELAXED);
      if (available == __atomic_load_n(&mailbox->consumed,
                                        __ATOMIC_RELAXED))
        {
          return false;
        }

      memcpy(event, &mailbox->latest, sizeof(*event));
      after = __atomic_load_n(&mailbox->version, __ATOMIC_ACQUIRE);
    }
  while (before != after || (after & 1u) != 0);

  *submitted = available;
  return true;
}

void probe_ui_beat_mailbox_consume(struct probe_ui_beat_mailbox *mailbox,
                                   uint32_t submitted,
                                   uint32_t logical_beats)
{
  __atomic_store_n(&mailbox->logical_beats, logical_beats,
                   __ATOMIC_RELAXED);
  __atomic_store_n(&mailbox->consumed, submitted, __ATOMIC_RELEASE);
}

bool probe_ui_beat_mailbox_empty(const struct probe_ui_beat_mailbox *mailbox)
{
  return __atomic_load_n(&mailbox->consumed, __ATOMIC_RELAXED) ==
         __atomic_load_n(&mailbox->submitted, __ATOMIC_ACQUIRE);
}

void probe_ui_beat_mailbox_get(const struct probe_ui_beat_mailbox *mailbox,
                               uint32_t *submitted, uint32_t *consumed,
                               uint32_t *logical_beats)
{
  *submitted = __atomic_load_n(&mailbox->submitted, __ATOMIC_ACQUIRE);
  *consumed = __atomic_load_n(&mailbox->consumed, __ATOMIC_ACQUIRE);
  *logical_beats = __atomic_load_n(&mailbox->logical_beats,
                                    __ATOMIC_RELAXED);
}
