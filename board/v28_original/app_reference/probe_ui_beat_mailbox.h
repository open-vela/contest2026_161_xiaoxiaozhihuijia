#ifndef __APPS_EXAMPLES_OPENVELA_BLE_PROBE_UI_BEAT_MAILBOX_H
#define __APPS_EXAMPLES_OPENVELA_BLE_PROBE_UI_BEAT_MAILBOX_H

#include <stdbool.h>
#include <stdint.h>

#include "probe_ui.h"

struct probe_ui_beat_mailbox
{
  struct probe_ui_beat_event latest;
  volatile uint32_t version;
  volatile uint32_t submitted;
  volatile uint32_t consumed;
  volatile uint32_t logical_beats;
};

void probe_ui_beat_mailbox_init(struct probe_ui_beat_mailbox *mailbox);
void probe_ui_beat_mailbox_publish(struct probe_ui_beat_mailbox *mailbox,
                                   const struct probe_ui_beat_event *event);
bool probe_ui_beat_mailbox_take_latest(struct probe_ui_beat_mailbox *mailbox,
                                       struct probe_ui_beat_event *event,
                                       uint32_t *submitted);
void probe_ui_beat_mailbox_consume(struct probe_ui_beat_mailbox *mailbox,
                                   uint32_t submitted,
                                   uint32_t logical_beats);
bool probe_ui_beat_mailbox_empty(const struct probe_ui_beat_mailbox *mailbox);
void probe_ui_beat_mailbox_get(const struct probe_ui_beat_mailbox *mailbox,
                               uint32_t *submitted, uint32_t *consumed,
                               uint32_t *logical_beats);

#endif
