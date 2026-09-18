#ifndef PROBE_APP_INPUT_H
#define PROBE_APP_INPUT_H

#include <stdbool.h>
#include <stddef.h>

#define PROBE_APP_INPUT_LINE_MAX 128u

enum probe_app_input_result
{
  PROBE_APP_INPUT_NONE = 0,
  PROBE_APP_INPUT_STOP,
  PROBE_APP_INPUT_LATENCY,
  PROBE_APP_INPUT_LATENCY_FULL,
  PROBE_APP_INPUT_UNKNOWN,
  PROBE_APP_INPUT_OVERLONG
};

struct probe_app_input
{
  char line[PROBE_APP_INPUT_LINE_MAX];
  size_t used;
  bool discarding;
};

void probe_app_input_init(struct probe_app_input *input);
enum probe_app_input_result probe_app_input_feed(struct probe_app_input *input,
                                                  char ch);

#endif
