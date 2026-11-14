/*
 * The image for tests/ssh/cli/openssh/openssh_test.py: the real entry points, the real CLI, the
 * real access core and the real wolfSSH provider, with #192's test Access ports. TEST ONLY: the
 * commands below exist in no product, and it is never flashed or run on a board.
 */
#include "cli_port.h"
#include "crux_access_adapter.h"
#include "crux_ssh_callbacks.h"
#include "crux_ssh_cli.h"
#include "crux_ssh_wolf_provider.h"
#include "test_support.h"

#include <crux/cli.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_heap.h>

#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_ARCH_POSIX
#include "cmdline.h"
#include "nsi_main.h"
#endif

#define ADMINISTRATOR "operator"
#define VIEWER "viewer"
#define PASSWORD "correct horse battery"
#define BOTH (CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH)

/* In Zephyr's malloc.c, with no header of its own at this revision. */
int malloc_runtime_stats_get(struct sys_memory_stats *stats);

static K_SEM_DEFINE(stop_requested, 0, 1);

static const char *const administrator_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE,
                                                        "probe.read"};
static const char *const viewer_permissions[] = {"probe.read"};
static const crux_access_role_t roles[] = {
    {"administrator", administrator_permissions, 2},
    {"viewer", viewer_permissions, 1},
};
static crux_access_policy_t policy = {roles, 2, 15u * 60u, 8u * 3600u, NULL};

static struct {
    bool forced_change;
    crux_ssh_config_t config;
} options = {
    .config = {.port = 2222,
               .max_connections = 2,
               .max_auth_attempts = 3,
               .auth_timeout_ms = 3000,
               .io_timeout_ms = 200},
};

static test_key_t host_key;

/* ---- What the image's commands do ---- */

static crux_cli_result_t run_probe(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                   void *context)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    ARG_UNUSED(context);
    return crux_cli_write(call, "ran\n", 4);
}

static crux_cli_result_t run_fail(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                  void *context)
{
    ARG_UNUSED(call);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    ARG_UNUSED(context);
    return CRUX_CLI_ERR_FAILED;
}

static crux_cli_result_t run_flood(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                   void *context)
{
    static const char block[512] = "flood";
    size_t remaining = (size_t)atoi(argv[0]);

    ARG_UNUSED(argc);
    ARG_UNUSED(context);
    while (remaining > 0u) {
        size_t piece = MIN(remaining, sizeof(block));
        crux_cli_result_t result = crux_cli_write(call, block, piece);

        if (result != CRUX_CLI_OK) {
            return result;
        }
        remaining -= piece;
    }
    return CRUX_CLI_OK;
}

static crux_cli_result_t run_measure(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                     void *context)
{
    struct sys_memory_stats stats = {0};
    size_t worker = 0;
    size_t listener = 0;

    ARG_UNUSED(call);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    ARG_UNUSED(context);
    (void)malloc_runtime_stats_get(&stats);
    crux_ssh_wolf_test_stack_used(&worker, &listener);
    printk("MEASURE heap_peak=%zu worker_stack_used=%zu listener_stack_used=%zu\n",
           stats.max_allocated_bytes, worker, listener);
    return CRUX_CLI_OK;
}

static crux_cli_result_t run_stop(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                  void *context)
{
    ARG_UNUSED(call);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    ARG_UNUSED(context);
    k_sem_give(&stop_requested);
    return CRUX_CLI_OK;
}

static const crux_cli_command_t commands[] = {
    CRUX_CLI_COMMAND("probe-read", "viewers and above", CRUX_CLI_PERMISSION("probe.read"), 0, 0,
                     BOTH, run_probe, NULL),
    CRUX_CLI_COMMAND("probe-admin", "administrators",
                     CRUX_CLI_PERMISSION(CRUX_ACCESS_PERMISSION_MANAGE), 0, 0, BOTH, run_probe,
                     NULL),
    CRUX_CLI_COMMAND("probe-fail", "always fails", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, run_fail,
                     NULL),
    CRUX_CLI_COMMAND("any", "any session", CRUX_CLI_SESSION_ONLY, 0, 1, BOTH, run_probe, NULL),
    CRUX_CLI_COMMAND("serial-only", "not on SSH", CRUX_CLI_SESSION_ONLY, 0, 0,
                     CRUX_CLI_CHANNEL_SERIAL, run_probe, NULL),
    CRUX_CLI_COMMAND("flood", "much output", CRUX_CLI_SESSION_ONLY, 1, 1, BOTH, run_flood, NULL),
    CRUX_CLI_COMMAND("measure", "prints what the run has used", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH,
                     run_measure, NULL),
    CRUX_CLI_COMMAND("stop", "ends the image", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, run_stop, NULL),
};

/* ---- The CLI's event port, which a test on the host reads ---- */

void crux_cli_port_event(const crux_cli_event_t *event)
{
    printk("CLI event type=%d outcome=%d channel=%u user=%s\n", (int)event->type,
           (int)event->outcome, event->channel, event->user);
}

/* ---- A counting provider, so that a test on the host knows that a write has really waited ---- */

static const crux_ssh_provider_t *real_provider(void)
{
    return crux_ssh_wolf_provider();
}

static crux_status_t counted_write(crux_ssh_channel_t *channel, const void *buffer, size_t length,
                                   size_t *transferred)
{
    crux_status_t status = real_provider()->write(channel, buffer, length, transferred);

    if (status == CRUX_ERR_TIMEOUT) {
        printk("CLI write timeout\n");
    }
    return status;
}

static crux_status_t counted_close(crux_ssh_channel_t *channel)
{
    printk("CLI channel close\n");
    return real_provider()->close(channel);
}

static crux_status_t forwarded_read(crux_ssh_channel_t *channel, void *buffer, size_t length,
                                    size_t *transferred)
{
    return real_provider()->read(channel, buffer, length, transferred);
}

static const crux_ssh_provider_t counted_provider = {
    .read = forwarded_read,
    .write = counted_write,
    .close = counted_close,
};

/* ---- Starting ---- */

#ifdef CONFIG_ARCH_POSIX
static void parse_arguments(void)
{
    int argc = 0;
    char **argv = NULL;

    native_get_test_cmd_line_args(&argc, &argv);
    for (int i = 0; i < argc; ++i) {
        const char *value = strchr(argv[i], '=');

        value = value != NULL ? value + 1 : "";
        if (strncmp(argv[i], "idle=", 5) == 0) {
            policy.session_idle_limit_s = (uint32_t)atoi(value);
        } else if (strncmp(argv[i], "connections=", 12) == 0) {
            options.config.max_connections = (uint8_t)atoi(value);
        } else if (strcmp(argv[i], "forced-change") == 0) {
            options.forced_change = true;
        }
    }
}
#else
static void parse_arguments(void)
{
}
#endif

static bool set_up_accounts(void)
{
    static const crux_access_origin_t serial = {.channel = CRUX_ACCESS_CHANNEL_SERIAL};
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    const size_t length = sizeof(PASSWORD) - 1u;

    if (crux_access_init(&policy) != CRUX_ACCESS_OK ||
        crux_access_provision_first_account(ADMINISTRATOR, PASSWORD, length, "administrator",
                                            &serial) != CRUX_ACCESS_OK ||
        crux_access_login_password(ADMINISTRATOR, PASSWORD, length, &serial, &session,
                                   &retry_after_ms) != CRUX_ACCESS_OK ||
        crux_access_account_create(session, VIEWER, "viewer", PASSWORD, length) != CRUX_ACCESS_OK) {
        return false;
    }
    if (options.forced_change &&
        crux_access_account_reset_password(session, VIEWER, PASSWORD, length) != CRUX_ACCESS_OK) {
        return false;
    }
    crux_access_logout(&session);
    return true;
}

static int serve(void)
{
    static const crux_cli_command_t *const tables[] = {commands};
    static const size_t counts[] = {ARRAY_SIZE(commands)};
    crux_status_t status;

    if (crux_cli_init(tables, counts, 1) != CRUX_CLI_OK || !set_up_accounts() ||
        !test_key_generate(CRUX_SSH_KEY_ED25519, &host_key) ||
        crux_ssh_cli_set_provider(&counted_provider) != CRUX_OK) {
        printk("SETUP failed\n");
        return 2;
    }
    options.config.host_key_type = host_key.type;
    options.config.host_key = host_key.host_key;
    options.config.host_key_length = host_key.host_key_length;
    status = real_provider()->start(&options.config, crux_ssh_callback_table());
    if (status != CRUX_OK) {
        printk("START failed %d\n", (int)status);
        return 3;
    }
    printk("READY port=%u\n", options.config.port);
    (void)k_sem_take(&stop_requested, K_FOREVER);
    status = real_provider()->stop();
    printk("STOPPED %d\n", (int)status);
    return status == CRUX_OK ? 0 : 4;
}

int main(void)
{
    int status;

    parse_arguments();
    status = serve();
    printk("EXIT %d\n", status);
#ifdef CONFIG_ARCH_POSIX
    nsi_exit(status);
#endif
    return 0;
}
