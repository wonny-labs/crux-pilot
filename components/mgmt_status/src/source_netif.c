/* A thin provider of the network source. Best effort, as the Netif contract says, and IPv4 only. */
#include "mgmt_status_port.h"

#include <crux/netif.h>

#include <string.h>

crux_status_t crux_mgmt_status_port_net(crux_mgmt_status_net_t *out)
{
    crux_netif_status_t status;
    crux_status_t result = crux_netif_get_status(&status);
    if (result != CRUX_OK) {
        return result;
    }
    out->admin_up = status.administrative_up;
    out->link_known = status.link != CRUX_NETIF_LINK_UNKNOWN;
    out->link_up = status.link == CRUX_NETIF_LINK_UP;
    out->ipv4_available = status.ipv4_available;
    memcpy(out->ipv4, status.ipv4_address, sizeof(out->ipv4));
    return CRUX_OK;
}
