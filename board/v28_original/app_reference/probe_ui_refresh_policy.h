#ifndef __APPS_EXAMPLES_OPENVELA_BLE_PROBE_UI_REFRESH_POLICY_H
#define __APPS_EXAMPLES_OPENVELA_BLE_PROBE_UI_REFRESH_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define PROBE_UI_LIMIT_HZ 24u

struct probe_ui_refresh_policy
{
  uint64_t next_ns;
  uint32_t remainder;
  bool valid;
};

void probe_ui_refresh_policy_start(struct probe_ui_refresh_policy *policy,
                                   uint64_t now_ns);
bool probe_ui_refresh_policy_due(struct probe_ui_refresh_policy *policy,
                                 uint64_t now_ns,
                                 uint32_t *missed_deadlines);

#endif
