#include "probe_app_input.h"

#include <string.h>

void probe_app_input_init(struct probe_app_input *input)
{
  memset(input, 0, sizeof(*input));
}

enum probe_app_input_result probe_app_input_feed(struct probe_app_input *input,
                                                  char ch)
{
  if (ch == '\n' || ch == '\r')
    {
      if (input->discarding)
        {
          input->discarding = false;
          input->used = 0;
          return PROBE_APP_INPUT_NONE;
        }

      if (input->used == 0)
        {
          return PROBE_APP_INPUT_NONE;
        }

      input->line[input->used] = '\0';
      input->used = 0;
      if (strcmp(input->line, "stop") == 0)
        return PROBE_APP_INPUT_STOP;
      if (strcmp(input->line, "latency") == 0)
        return PROBE_APP_INPUT_LATENCY;
      if (strcmp(input->line, "latency full") == 0)
        return PROBE_APP_INPUT_LATENCY_FULL;
      return PROBE_APP_INPUT_UNKNOWN;
    }

  if (input->used + 1 < sizeof(input->line))
    {
      input->line[input->used++] = ch;
      return PROBE_APP_INPUT_NONE;
    }

  input->discarding = true;
  input->used = 0;
  return PROBE_APP_INPUT_OVERLONG;
}
