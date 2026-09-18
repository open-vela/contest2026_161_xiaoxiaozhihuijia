#include "probe_fifo_pair.h"
void probe_fifo_pair_word(uint8_t p,int16_t raw,int16_t a[3],int16_t g[3],uint8_t *am,uint8_t *gm){if(p<3){g[p]=raw;*gm|=(uint8_t)(1u<<p);}else if(p<6){a[p-3]=raw;*am|=(uint8_t)(1u<<(p-3));}}
