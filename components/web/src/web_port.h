/* What the portable web core needs from its platform. */
#ifndef CRUX_WEB_PORT_H
#define CRUX_WEB_PORT_H

#include <stdint.h>

/* A monotonic clock in milliseconds, read on the server thread. */
uint64_t crux_web_port_now_ms(void);

#ifdef CRUX_WEB_TESTING
/* The Host clock, for a handler that simulates taking time. */
#define crux_web_port_now_ms_for_tests() crux_web_port_now_ms()
#endif

#endif /* CRUX_WEB_PORT_H */
