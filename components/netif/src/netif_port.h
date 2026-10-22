#ifndef CRUX_NETIF_PORT_H
#define CRUX_NETIF_PORT_H
#include <crux/netif.h>
crux_status_t crux_netif_observe(crux_netif_status_t *out);
bool crux_netif_ipv4_eligible(const uint8_t address[4], uint32_t netmask);
#endif
