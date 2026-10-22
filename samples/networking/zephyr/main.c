/* Crux-owned RT1060 composition: native startup stays outside client source. */
#include "client.h"
#include <crux/netif.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
int main(void) {
    struct net_if *iface = net_if_get_default();
    struct net_in_addr address, mask;
    if (iface == NULL || net_addr_pton(NET_AF_INET, "192.168.77.2", &address) != 0 ||
        net_addr_pton(NET_AF_INET, "255.255.255.0", &mask) != 0 ||
        net_if_ipv4_addr_add(iface, &address, NET_ADDR_MANUAL, 0) == NULL ||
        !net_if_ipv4_set_netmask_by_addr(iface, &address, &mask))
        return 1;
    int result = net_if_up(iface);
    if (result != 0 && result != -EALREADY)
        return 1;
    int64_t end = k_uptime_get() + 10000;
    crux_netif_status_t status;
    do {
        if (crux_netif_get_status(&status) != CRUX_OK)
            return 1;
        if (status.link == CRUX_NETIF_LINK_UP)
            break;
        k_sleep(K_MSEC(10));
    } while (k_uptime_get() < end);
    printk("NETIF admin=%d link=%d address=%d\n", status.administrative_up, (int)status.link,
           status.ipv4_available);
    int tcp = networking_exchange("192.168.77.1", 42420, false, 2000);
    int udp = networking_exchange("192.168.77.1", 42421, true, 2000);
    printk("NETWORK tcp=%d udp=%d\n", tcp, udp);
    return tcp != 0 || udp != 0;
}
