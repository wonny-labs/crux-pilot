#include "frame.h"
#include <string.h>
static void put32(uint8_t *out, uint32_t value)
{
    for (unsigned int i = 0; i < 4; ++i)
        out[i] = (uint8_t)(value >> (24 - i * 8));
}
void m5_frame(uint8_t out[32], uint32_t token, uint16_t phase, uint32_t transaction, bool udp)
{
    memcpy(out, "M5N1", 4);
    put32(out + 4, token);
    out[8] = (uint8_t)(phase >> 8);
    out[9] = (uint8_t)phase;
    out[10] = 0;
    out[11] = udp ? 2 : 1;
    put32(out + 12, transaction);
    memcpy(out + 16, "crux-m5-echo-v1", 16);
}
