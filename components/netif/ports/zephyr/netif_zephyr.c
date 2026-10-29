#include "netif_port.h"
#include <string.h>
#include <zephyr/net/net_if.h>

crux_status_t crux_netif_observe(crux_netif_status_t *out)
{
    struct net_if *primary = net_if_get_default();
    if (primary == NULL) {
        return CRUX_ERR_NOT_READY;
    }

    net_if_lock(primary);
    out->administrative_up = net_if_is_admin_up(primary);
    out->link = net_if_is_carrier_ok(primary) ? CRUX_NETIF_LINK_UP : CRUX_NETIF_LINK_DOWN;

    const struct net_if_ipv4 *configuration = primary->config.ip.ipv4;
    if (configuration != NULL) {
        for (unsigned int index = 0; index < NET_IF_MAX_IPV4_ADDR; ++index) {
            const struct net_if_addr_ipv4 *candidate = &configuration->unicast[index];
            const uint8_t *address = (const uint8_t *)&candidate->ipv4.address.in_addr;
            if (!candidate->ipv4.is_used || candidate->ipv4.addr_state != NET_ADDR_PREFERRED ||
                !crux_netif_ipv4_eligible(address, net_ntohl(candidate->netmask.s_addr))) {
                continue;
            }
            if (!out->ipv4_available || memcmp(address, out->ipv4_address, 4) < 0) {
                out->ipv4_available = true;
                memcpy(out->ipv4_address, address, 4);
            }
        }
    }

    net_if_unlock(primary);
    return CRUX_OK;
}
