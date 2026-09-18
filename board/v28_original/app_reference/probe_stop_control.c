#include "probe_stop_control.h"

bool probe_stop_control_request(struct probe_stop_control *control,
                                uint64_t now_us, uint32_t timeout_ms,
                                uint32_t required_steps)
{
  if (control->requested)
    {
      control->duplicate_requests++;
      return false;
    }

  control->requested = true;
  control->requested_us = now_us;
  control->deadline_us = now_us + (uint64_t)timeout_ms * UINT64_C(1000);
  control->required_steps = required_steps;
  control->completed_steps = 0;
  control->duplicate_requests = 0;
  return true;
}

void probe_stop_control_complete(struct probe_stop_control *control,
                                 uint32_t step)
{
  control->completed_steps |= step & control->required_steps;
}

bool probe_stop_control_is_complete(const struct probe_stop_control *control)
{
  return control->requested &&
         control->completed_steps == control->required_steps;
}

bool probe_stop_control_is_expired(const struct probe_stop_control *control,
                                   uint64_t now_us)
{
  return control->requested && now_us >= control->deadline_us &&
         !probe_stop_control_is_complete(control);
}

uint64_t probe_stop_control_elapsed_us(const struct probe_stop_control *control,
                                       uint64_t now_us)
{
  return control->requested && now_us >= control->requested_us ?
         now_us - control->requested_us : 0;
}
