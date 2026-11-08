/*
 * RT1060-EVKB bench image of the integrated management composition (HTTPS web, the command line
 * over SSH and SFTP), under the bench declaration of bench_declaration.json. The test forms in it
 * (the SSH host key as a static buffer, no time source) are accepted only under that declaration.
 * The static address is the M5 image's, and the listeners answer on it only. See
 * docs/rt1060-management-image-feasibility.md.
 *
 * THIS IMAGE REFUSES TO START. Rows C2 to C9 are not done, the first account (D3) has no
 * implementation, the file backend is still the microSD provider, and the board stage has no
 * approval. Nothing here is a statement that the image may run on a board.
 */
#include "bench_first_account.h"
#include "bench_guard.h"
#include "bench_report.h"
#include "crux_native_management.h"
#include "crux_native_management_services.h"
#include "ssh_target.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/printk.h>

#ifndef CRUX_BENCH_RUN_REFUSED
#error "CMake sets CRUX_BENCH_RUN_REFUSED: 1 while the image refuses to start"
#endif

/* The bench address and netmask of the declaration, set before any listener starts, as
 * tests/networking_physical/main.c does for the M5 image. */
static bool static_address_up(void)
{
    struct net_if *iface = net_if_get_default();
    struct net_in_addr address, mask;

    if (iface == NULL || net_addr_pton(NET_AF_INET, CRUX_BENCH_ADDRESS, &address) != 0 ||
        net_addr_pton(NET_AF_INET, CRUX_BENCH_NETMASK, &mask) != 0 ||
        net_if_ipv4_addr_add(iface, &address, NET_ADDR_MANUAL, 0) == NULL ||
        !net_if_ipv4_set_netmask_by_addr(iface, &address, &mask)) {
        return false;
    }
    int status = net_if_up(iface);
    return status == 0 || status == -EALREADY;
}

/* D10 at run time: the interface has the bench address and no other IPv4 address, so that a
 * listener cannot answer on one that the declaration does not name. */
static bool only_bench_address(void)
{
    struct net_if *iface = net_if_get_default();
    struct net_in_addr address;
    struct net_if_ipv4 *ipv4;
    size_t used = 0;

    if (iface == NULL || net_addr_pton(NET_AF_INET, CRUX_BENCH_ADDRESS, &address) != 0 ||
        net_if_config_ipv4_get(iface, &ipv4) != 0 || ipv4 == NULL) {
        return false;
    }
    for (size_t i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
        if (!ipv4->unicast[i].ipv4.is_used) {
            continue;
        }
        used++;
        if (!net_ipv4_addr_cmp(&ipv4->unicast[i].ipv4.address.in_addr, &address)) {
            return false;
        }
    }
    return used == 1u;
}

/* The refusal: the image is bootable, because a QSPI image needs its boot header, and it stops
 * here. The volatile read keeps all of the composition linked, so that the sizes in the report stay
 * those of the composition. The switch is CRUX_BENCH_RUN_REFUSED in ../CMakeLists.txt, and removing
 * it is a reviewed change that follows the approval of the board stage. */
static volatile const bool run_refused = CRUX_BENCH_RUN_REFUSED;

static void banner(void)
{
    printk("BENCH image=%s declaration=CRUX_MANAGEMENT_BENCH_IMAGE board=%s address=%s/%d\n",
           CRUX_BENCH_IMAGE_ID, CONFIG_BOARD_TARGET, CRUX_BENCH_ADDRESS, CRUX_BENCH_PREFIX_LENGTH);
    printk("BENCH test forms: host_key=%d no_time_source=%d ram_disk=%d first_account_file=%d\n",
           IS_ENABLED(CONFIG_CRUX_BENCH_TEST_HOST_KEY),
           IS_ENABLED(CONFIG_CRUX_BENCH_NO_TIME_SOURCE),
           IS_ENABLED(CONFIG_CRUX_BENCH_FILE_RAM_DISK),
           IS_ENABLED(CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE));
}

static int bench_main(void)
{
    banner();
    if (run_refused) {
        printk("BENCH refusing to start: this image is not approved to run\n");
        return 1;
    }
    if (!static_address_up() || !only_bench_address()) {
        printk("BENCH address failed\n");
        return 1;
    }
    if (crux_native_management_start() != CRUX_NATIVE_MANAGEMENT_STARTED) {
        printk("BENCH composition failed\n");
        return 1;
    }
    /* The first account, only on an empty store. */
    if (crux_native_management_store_state_at_start() == CRUX_CREDENTIAL_STATE_EMPTY &&
        !crux_bench_first_account()) {
        printk("BENCH first account failed\n");
        return 1;
    }
    if (crux_native_management_services_start() != CRUX_NATIVE_MANAGEMENT_SERVICES_STARTED) {
        printk("BENCH services failed\n");
        return 1;
    }
    /* D4: the fingerprint of the certificate the listener serves, once it exists. */
    if (!crux_bench_report_certificate(crux_native_management_services_certificate_generated())) {
        printk("BENCH certificate fingerprint failed\n");
        return 1;
    }
    if (!crux_mgmt_rt1060_ssh_start()) {
        printk("BENCH ssh failed\n");
        return 1;
    }
    /* C2: the stack and heap lines at boot, after each client and at idle. */
    if (!crux_bench_report_start()) {
        printk("BENCH report failed\n");
        return 1;
    }
    printk("BENCH ready\n");
    return 0;
}

/* The main thread ends when main() returns, and the kernel's thread list then no longer shows it,
 * so its stack figure is taken here, on every way out. */
int main(void)
{
    int result = bench_main();

    crux_bench_report_main_exit();
    return result;
}
