#ifndef NETWORKING_CLIENT_H
#define NETWORKING_CLIENT_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Sample-owned 32-byte echo protocol; returns 0 or negative symbolic errno.
 * timeout_ms is finite and positive, shared by connect/send/receive. */
int networking_exchange(const char *ipv4, uint16_t port, bool datagram, int timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
