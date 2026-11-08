/*
 * SSH start for the RT1060 compile vehicle. It repeats
 * tests/native/management_composition/composition/ crux_native_management_ssh.c without its
 * native_sim parts (the test arguments, the heap report and the stack probe), and it declares
 * itself compile-only. The host key is in test form: the store generates it, and the provider
 * borrows it from a static buffer that is never wiped. A board run needs a decision on how the host
 * key is held and wiped: docs/rt1060-management-image-feasibility.md.
 */
#include "ssh_target.h"

#ifndef CRUX_MGMT_RT1060_COMPILE_ONLY
#error "The test host key is allowed only in the declared RT1060 compile-only image"
#endif
#if defined(CONFIG_TEST_RANDOM_GENERATOR) || defined(CONFIG_FAKE_ENTROPY_NATIVE_SIM)
#error "The SSH composition draws random values from its PSA provider over the TRNG driver"
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
    };
    return crux_ssh_wolf_provider()->start(&config, crux_ssh_callback_table()) == CRUX_OK;
}
