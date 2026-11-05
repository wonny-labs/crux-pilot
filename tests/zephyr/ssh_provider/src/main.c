/*
 * The test image. With no arguments it serves SSH for scripts/test-ssh-provider.sh, with the access
 * adapter of #192 on the test ports, and stand-ins for the CLI and SFTP. With "conformance" it runs
 * the cases of tests/ssh on the provider with a scripted client. TEST ONLY. It prints what a test
 * on the host reads, and it is never flashed or run on a board.
 */
#include "crux_access_adapter.h"
#include "crux_ssh_callbacks.h"
#include "crux_ssh_wolf_provider.h"
#include "test_support.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_ARCH_POSIX
#include "cmdline.h"
#include "nsi_main.h"
#endif

#define KEYS_MAX 4
#define KEY_BLOB_MAX 192u

static const char *const administrator_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE};
static const crux_access_role_t roles[] = {{"administrator", administrator_permissions, 1}};
static const crux_access_policy_t policy = {roles, 1, 15u * 60u, 8u * 3600u, NULL};

static struct {
    const char *mode;
    const char *only_case;
    const char *user;
    const char *password;
    const char *keys[KEYS_MAX];
    size_t key_count;
    crux_ssh_key_type_t host_key_type;
    bool repository_stand_ins;
    crux_ssh_config_t config;
} options = {
    .mode = "serve",
    .user = "operator",
    .password = "correct horse battery",
    .host_key_type = CRUX_SSH_KEY_ED25519,
    .config = {.port = 2222,
               .max_connections = 2,
               .max_auth_attempts = 3,
               .auth_timeout_ms = 3000,
               .io_timeout_ms = 200},
};

static test_key_t host_key;

static void finish(int status)
{
    printk("EXIT %d\n", status);
#ifdef CONFIG_ARCH_POSIX
    nsi_exit(status);
#endif
}

#ifdef CONFIG_ARCH_POSIX
static void parse_arguments(void)
{
    int argc = 0;
    char **argv = NULL;

    native_get_test_cmd_line_args(&argc, &argv);
    for (int i = 0; i < argc; ++i) {
        const char *value = strchr(argv[i], '=');

        value = value != NULL ? value + 1 : "";
        if (strcmp(argv[i], "conformance") == 0) {
            options.mode = "conformance";
        } else if (strncmp(argv[i], "only=", 5) == 0) {
            options.only_case = value;
        } else if (strncmp(argv[i], "key=", 4) == 0 && options.key_count < KEYS_MAX) {
            options.keys[options.key_count++] = value;
        } else if (strcmp(argv[i], "host=ecdsa") == 0) {
            options.host_key_type = CRUX_SSH_KEY_ECDSA_P256;
        } else if (strcmp(argv[i], "stand-ins=repository") == 0) {
            options.repository_stand_ins = true;
        } else if (strncmp(argv[i], "connections=", 12) == 0) {
            options.config.max_connections = (uint8_t)atoi(value);
        } else if (strncmp(argv[i], "attempts=", 9) == 0) {
            options.config.max_auth_attempts = (uint8_t)atoi(value);
        } else if (strncmp(argv[i], "send-block=", 11) == 0) {
            crux_ssh_wolf_test_block_sends((unsigned)atoi(value));
        } else if (strncmp(argv[i], "auth-ms=", 8) == 0) {
            options.config.auth_timeout_ms = (uint32_t)atoi(value);
        }
    }
}
#else
static void parse_arguments(void)
{
}
#endif

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

    if (length > capacity) {
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

static bool set_up_accounts(void)
{
    static const crux_access_origin_t serial = {.channel = CRUX_ACCESS_CHANNEL_SERIAL};
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    size_t password_length = strlen(options.password);

    if (crux_access_init(&policy) != CRUX_ACCESS_OK ||
        crux_access_provision_first_account(options.user, options.password, password_length,
                                            "administrator", &serial) != CRUX_ACCESS_OK ||
        crux_access_login_password(options.user, options.password, password_length, &serial,
                                   &session, &retry_after_ms) != CRUX_ACCESS_OK) {
        return false;
    }
    for (size_t i = 0; i < options.key_count; ++i) {
        uint8_t blob[KEY_BLOB_MAX];
        size_t length = decode_hex(options.keys[i], blob, sizeof(blob));

        if (length == 0u ||
            crux_access_account_add_key(session, options.user, blob, length) != CRUX_ACCESS_OK) {
            return false;
        }
    }
    crux_access_logout(&session);
    return true;
}

static int serve(void)
{
    crux_ssh_callbacks_t callbacks = *crux_ssh_callback_table();
    crux_status_t status;

    if (!set_up_accounts() || !test_key_generate(options.host_key_type, &host_key)) {
        printk("SETUP failed\n");
        return 2;
    }
    if (!options.repository_stand_ins) {
        test_sessions_fill(&callbacks);
    }
    options.config.host_key_type = host_key.type;
    options.config.host_key = host_key.host_key;
    options.config.host_key_length = host_key.host_key_length;
    test_key_print_fingerprint(&host_key);
    status = crux_ssh_wolf_provider()->start(&options.config, &callbacks);
    if (status != CRUX_OK) {
        printk("START failed %d\n", (int)status);
        return 3;
    }
    printk("READY port=%u\n", options.config.port);
    test_sessions_wait_for_stop();
    status = crux_ssh_wolf_provider()->stop();
    printk("STOPPED %d\n", (int)status);
    test_print_measurements();
    return status == CRUX_OK ? 0 : 4;
}

int main(void)
{
    parse_arguments();
#ifdef CONFIG_ARCH_POSIX
    if (strcmp(options.mode, "conformance") == 0) {
        finish(test_conformance_run(options.only_case));
        return 0;
    }
#endif
    finish(serve());
    return 0;
}
