#include "netif_port.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>

crux_status_t crux_netif_observe(crux_netif_status_t *out)
{
    struct ifaddrs *list;
    if (getifaddrs(&list) != 0) {
        return CRUX_ERR_IO;
    }

    bool found = false;
    for (const struct ifaddrs *item = list; item != NULL; item = item->ifa_next) {
        if (strcmp(item->ifa_name, CRUX_NETIF_HOST_INTERFACE) != 0) {
            continue;
        }

        found = true;
        out->administrative_up = (item->ifa_flags & IFF_UP) != 0;
        out->link = (item->ifa_flags & IFF_LOOPBACK) != 0
                        ? (out->administrative_up ? CRUX_NETIF_LINK_UP : CRUX_NETIF_LINK_DOWN)
                        : CRUX_NETIF_LINK_UNKNOWN;
        if (item->ifa_addr == NULL || item->ifa_netmask == NULL ||
            item->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        const struct sockaddr_in *ip = (const struct sockaddr_in *)item->ifa_addr;
        const struct sockaddr_in *mask = (const struct sockaddr_in *)item->ifa_netmask;
        const uint8_t *bytes = (const uint8_t *)&ip->sin_addr;
        if (crux_netif_ipv4_eligible(bytes, ntohl(mask->sin_addr.s_addr)) &&
            (!out->ipv4_available || memcmp(bytes, out->ipv4_address, 4) < 0)) {
            memcpy(out->ipv4_address, bytes, 4);
            out->ipv4_available = true;
        }
    }

    freeifaddrs(list);
    return found ? CRUX_OK : CRUX_ERR_NOT_READY;
}
