/*
 * RT1060-EVKB compile vehicle of the integrated management composition. Compile and link only: this
 * image is never flashed or run. The first account is the declared test step of the native images,
 * and the static address is the M5 image's. See docs/rt1060-management-image-feasibility.md.
 */
#include "crux_native_management.h"
#include "crux_native_management_services.h"
#include "ssh_target.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/printk.h>

/* As tests/networking_physical/main.c: 192.168.77.2/24, set before any listener starts. */
static bool static_address_up(void)
{
    struct net_if *iface = net_if_get_default();
    struct net_in_addr address, mask;

    if (iface == NULL || net_addr_pton(NET_AF_INET, "192.168.77.2", &address) != 0 ||
        net_addr_pton(NET_AF_INET, "255.255.255.0", &mask) != 0 ||
        net_if_ipv4_addr_add(iface, &address, NET_ADDR_MANUAL, 0) == NULL ||
        !net_if_ipv4_set_netmask_by_addr(iface, &address, &mask)) {
        return false;
    }
    int status = net_if_up(iface);
    return status == 0 || status == -EALREADY;
}

int main(void)
{
    if (!static_address_up()) {
        printk("MGMT_RT1060 address failed\n");
        return 1;
    }
    if (crux_native_management_start() != CRUX_NATIVE_MANAGEMENT_STARTED) {
        printk("MGMT_RT1060 composition failed\n");
        return 1;
    }
    /* The declared test step: a first account, only on an empty store. */
    if (crux_native_management_store_state_at_start() == CRUX_CREDENTIAL_STATE_EMPTY &&
        crux_native_management_provision_test_account() != CRUX_ACCESS_OK) {
        printk("MGMT_RT1060 first account failed\n");
        return 1;
    }
    if (crux_native_management_services_start() != CRUX_NATIVE_MANAGEMENT_SERVICES_STARTED) {
        printk("MGMT_RT1060 services failed\n");
        return 1;
    }
    if (!crux_mgmt_rt1060_ssh_start()) {
        printk("MGMT_RT1060 ssh failed\n");
        return 1;
    }
    printk("MGMT_RT1060 ready\n");
    return 0;
}
