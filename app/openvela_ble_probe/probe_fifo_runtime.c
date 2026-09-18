#include <string.h>
#include "probe_fifo_runtime.h"
void probe_fifo_runtime_recover(uint8_t *gm,uint8_t *am,bool *ready,
                                int16_t g[3],int16_t a[3],bool *gap,
                                struct probe_fifo_time_estimator *te,uint32_t hz)
{
  *gm=0; *am=0; *ready=false;
  memset(g,0,3*sizeof(*g)); memset(a,0,3*sizeof(*a));
  *gap=true; probe_fifo_time_estimator_reset(te,hz);
}
