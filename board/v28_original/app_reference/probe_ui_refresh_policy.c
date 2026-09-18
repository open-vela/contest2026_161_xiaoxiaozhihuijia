#include "probe_ui_refresh_policy.h"

#include <stddef.h>

#define PROBE_UI_SECOND_NS UINT64_C(1000000000)

static uint64_t probe_ui_refresh_next_delta(
  struct probe_ui_refresh_policy *policy)
{
  uint64_t delta = PROBE_UI_SECOND_NS / PROBE_UI_LIMIT_HZ;

  policy->remainder += (uint32_t)(PROBE_UI_SECOND_NS % PROBE_UI_LIMIT_HZ);
  if (policy->remainder >= PROBE_UI_LIMIT_HZ)
    {
      policy->remainder -= PROBE_UI_LIMIT_HZ;
      delta++;
    }

  return delta;
}

void probe_ui_refresh_policy_start(struct probe_ui_refresh_policy *policy,
                                   uint64_t now_ns)
{
  policy->remainder = 0;
  policy->next_ns = now_ns + probe_ui_refresh_next_delta(policy);
  policy->valid = true;
}

bool probe_ui_refresh_policy_due(struct probe_ui_refresh_policy *policy,
                                 uint64_t now_ns,
                                 uint32_t *missed_deadlines)
{
  uint64_t late_ns;
  uint64_t missed;

  if (missed_deadlines != NULL)
    {
      *missed_deadlines = 0;
    }

  if (!policy->valid)
    {
      probe_ui_refresh_policy_start(policy, now_ns);
      return false;
    }

  if (now_ns < policy->next_ns)
    {
      return false;
    }

  late_ns = now_ns - policy->next_ns;
  missed = late_ns * PROBE_UI_LIMIT_HZ / PROBE_UI_SECOND_NS;
  if (missed > UINT32_MAX)
    {
      missed = UINT32_MAX;
    }

  if (missed_deadlines != NULL)
    {
      *missed_deadlines = (uint32_t)missed;
    }

  /* Anchor the next deadline after the actual refresh opportunity.  This
   * deliberately drops late frames instead of issuing catch-up refreshes.
   * The remainder retains the exact 1/24 second fractional cadence.
   */

  policy->next_ns = now_ns + probe_ui_refresh_next_delta(policy);
  return true;
}
