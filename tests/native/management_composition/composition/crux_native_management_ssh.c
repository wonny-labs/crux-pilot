#include "crux_native_management_ssh.h"
#include "ssh_test_host_key_guard.h"

#include "crux_ssh_access.h"
#include "crux_ssh_callbacks.h"
#include "crux_ssh_cli.h"
#include "crux_ssh_wolf_provider.h"

#include "cmdline.h"

#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_heap.h>

#define KEY_BLOB_MAX 192u

/* The key the provider borrows until stop(). TEST FORM: held in a static buffer for the life of the
 * process and never wiped, because no product handling of the host key is defined. The guard header
 * refuses this file in any build that is not a declared native_sim test image. */
static crux_ssh_host_key_t held_host_key;

/* In Zephyr's malloc.c, with no header of its own at this revision. */
int malloc_runtime_stats_get(struct sys_memory_stats *stats);

static int hex_value(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    return digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : -1;
}

static size_t decode_hex(const char *text, uint8_t *out, size_t capacity)
{
    size_t length = strlen(text) / 2u;

    if (length == 0u || length > capacity) {
        return 0u;
    }
    for (size_t i = 0; i < length; ++i) {
        int high = hex_value(text[2u * i]);
        int low = hex_value(text[2u * i + 1u]);

        if (high < 0 || low < 0) {
            return 0u;
        }
        out[i] = (uint8_t)(high * 16 + low);
    }
    return length;
}

static const char *test_argument(const char *name)
{
    int argc = 0;
    char **argv = NULL;
    size_t length = strlen(name);

    native_get_test_cmd_line_args(&argc, &argv);
    for (int i = 0; i < argc; ++i) {
        if (strncmp(argv[i], name, length) == 0 && argv[i][length] == '=') {
            return &argv[i][length + 1u];
        }
    }
    return NULL;
}

bool crux_native_management_ssh_add_keys(crux_access_session_t administrator,
                                         const char *const *users, size_t user_count)
{
    static uint8_t blob[KEY_BLOB_MAX];
    const char *text = test_argument("key");
    size_t length;

    if (text == NULL) {
        return true;
    }
    length = decode_hex(text, blob, sizeof(blob));
    if (length == 0u) {
        return false;
    }
    for (size_t i = 0; i < user_count; ++i) {
        crux_access_result_t result =
            crux_access_account_add_key(administrator, users[i], blob, length);

        /* A later boot on the same disk finds the key that the first one added. */
        if (result != CRUX_ACCESS_OK && result != CRUX_ACCESS_ERR_ALREADY_EXISTS) {
            return false;
        }
    }
    return true;
}

crux_native_management_ssh_start_t crux_native_management_ssh_start(void)
{
    static crux_ssh_config_t config;
    char fingerprint[CRUX_SSH_WOLF_FINGERPRINT_SIZE];

    /* The store generates the key at the first boot, and the same key loads at every later boot. */
    if (crux_ssh_host_key_load(CRUX_SSH_KEY_ED25519, &held_host_key) != CRUX_OK) {
        printk("SSH-HOST-KEY unavailable\n");
        return CRUX_NATIVE_MANAGEMENT_SSH_FAILED_HOST_KEY;
    }
    if (crux_ssh_wolf_host_key_fingerprint(held_host_key.type, held_host_key.key,
                                           held_host_key.length, fingerprint,
                                           sizeof(fingerprint)) != CRUX_OK) {
        printk("SSH-HOST-KEY unavailable\n");
        return CRUX_NATIVE_MANAGEMENT_SSH_FAILED_HOST_KEY;
    }
    /* The fingerprint is public. The key itself is never printed. */
    printk("SSH-HOST-KEY %s\n", fingerprint);
    if (crux_ssh_cli_set_provider(crux_ssh_wolf_provider()) != CRUX_OK) {
        return CRUX_NATIVE_MANAGEMENT_SSH_FAILED_CLI;
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
    if (crux_ssh_wolf_provider()->start(&config, crux_ssh_callback_table()) != CRUX_OK) {
        return CRUX_NATIVE_MANAGEMENT_SSH_FAILED_PROVIDER;
    }
    return CRUX_NATIVE_MANAGEMENT_SSH_STARTED;
}

void crux_native_management_ssh_report(void)
{
    struct sys_memory_stats stats = {0};
    size_t worker = 0;
    size_t listener = 0;

    static size_t last[4];
    size_t now[4];

    (void)malloc_runtime_stats_get(&stats);
    crux_ssh_wolf_test_stack_used(&worker, &listener);
    now[0] = stats.max_allocated_bytes;
    now[1] = stats.allocated_bytes;
    now[2] = worker;
    now[3] = listener;
    if (memcmp(now, last, sizeof(now)) == 0) {
        return;
    }
    memcpy(last, now, sizeof(now));
    printk("SSH-MEASURE heap_peak=%zu heap_now=%zu worker_stack_used=%zu listener_stack_used=%zu\n",
           stats.max_allocated_bytes, stats.allocated_bytes, worker, listener);
}
