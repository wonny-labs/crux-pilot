#ifndef M5_FRAME_H
#define M5_FRAME_H
#include <stdbool.h>
#include <stdint.h>
void m5_frame(uint8_t out[32], uint32_t token, uint16_t phase, uint32_t transaction, bool udp);
struct m5_metrics {
    int64_t entered_ms, functional_ms, returned_ms;
    int functional_result, close_error, peak_owned, owned_after;
};
int m5_exchange(const char *ipv4, uint16_t port, bool datagram, const uint8_t frame[32],
                int timeout_ms, struct m5_metrics *metrics);
#endif
