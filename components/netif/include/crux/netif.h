/**
 * @file
 * @brief Read-only status of the composition-selected primary network interface.
 */
#ifndef CRUX_NETIF_H
#define CRUX_NETIF_H

#include <crux/status.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CRUX_NETIF_LINK_UNKNOWN = 0,
    CRUX_NETIF_LINK_DOWN,
    CRUX_NETIF_LINK_UP
} crux_netif_link_t;

/** Best-effort interface observation; not a guarantee of remote reachability. */
typedef struct {
    bool administrative_up;
    crux_netif_link_t link;
    bool ipv4_available;
    uint8_t ipv4_address[4];
} crux_netif_status_t;

/**
 * Observe the primary interface in thread context.
 * @param[out] out Updated only on success.
 * @return CRUX_OK or a crux_status_t error.
 */
crux_status_t crux_netif_get_status(crux_netif_status_t *out);

#ifdef __cplusplus
}
#endif

#endif
