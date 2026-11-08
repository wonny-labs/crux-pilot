/*
 * SSH start for the RT1060-EVKB bench image. It repeats
 * tests/native/management_composition/composition/ crux_native_management_ssh.c without its
 * native_sim parts (the test arguments, the heap report and the stack probe). The host key is in
 * test form (D5): the store generates it, and the provider borrows it from a static buffer that is
 * never wiped. That is allowed only under the bench declaration, and the listener is bound to the
 * bench address only (D10). See docs/rt1060-management-image-feasibility.md.
 */
#include "ssh_target.h"

#include "bench_guard.h"

#ifndef CONFIG_CRUX_BENCH_TEST_HOST_KEY
#error                                                                                             \
    "The test host key form is declared only with CONFIG_CRUX_BENCH_TEST_HOST_KEY of the bench image"
#endif

#include "crux_ssh_access.h"
#include "crux_ssh_callbacks.h"
#include "crux_ssh_cli.h"
#include "crux_ssh_wolf_provider.h"

#include <zephyr/sys/printk.h>

static crux_ssh_host_key_t held_host_key;

bool crux_mgmt_rt1060_ssh_start(void)
{
    static crux_ssh_config_t config;
    char fingerprint[CRUX_SSH_WOLF_FINGERPRINT_SIZE];

    if (crux_ssh_host_key_load(CRUX_SSH_KEY_ED25519, &held_host_key) != CRUX_OK ||
        crux_ssh_wolf_host_key_fingerprint(held_host_key.type, held_host_key.key,
                                           held_host_key.length, fingerprint,
                                           sizeof(fingerprint)) != CRUX_OK) {
        printk("SSH-HOST-KEY unavailable\n");
        return false;
    }
    printk("SSH-HOST-KEY %s\n", fingerprint);
    if (crux_ssh_cli_set_provider(crux_ssh_wolf_provider()) != CRUX_OK) {
        return false;
    }
    config = (crux_ssh_config_t){
        .port = CONFIG_CRUX_SSH_PORT,
        .max_connections = CONFIG_CRUX_SSH_MAX_CONNECTIONS,
        .max_auth_attempts = CONFIG_CRUX_SSH_MAX_AUTH_ATTEMPTS,
        .auth_timeout_ms = CONFIG_CRUX_SSH_AUTH_TIMEOUT_MS,
        .io_timeout_ms = CONFIG_CRUX_SSH_IO_TIMEOUT_MS,
        .host_key_type = held_host_key.type,
        .host_key = held_host_key.key,
        .host_key_length = held_host_key.length,
        .bind_address = CONFIG_CRUX_SSH_BIND_ADDRESS,
    };
    return crux_ssh_wolf_provider()->start(&config, crux_ssh_callback_table()) == CRUX_OK;
}
