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

typedef struct {
    bool administrative_up;
    crux_netif_link_t link;
    bool ipv4_available;
    uint8_t ipv4_address[4];
} crux_netif_status_t;

/* Thread-context, best-effort observation of the composition-selected primary
 * interface. No route, socket binding or remote reachability guarantee.
 * On failure *out is unchanged. See docs/networking-contract.md. */
crux_status_t crux_netif_get_status(crux_netif_status_t *out);

#ifdef __cplusplus
}
#endif
#endif
