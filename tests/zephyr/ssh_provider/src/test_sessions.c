/*
 * Stand-ins for the CLI of #193 and the SFTP adapter of #194, so that OpenSSH can exercise the
 * channel functions, the exit status and the close. TEST ONLY: none of these commands exists in
 * Crux.
 */
#include "crux_ssh_attachment.h"
#include "crux_ssh_wolf_provider.h"
#include "test_support.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_heap.h>

#include <stdlib.h>
#include <string.h>

struct crux_ssh_file_handle {
    int id;
};

/* In Zephyr's malloc.c, with no header of its own at this revision. */
int malloc_runtime_stats_get(struct sys_memory_stats *stats);

static K_SEM_DEFINE(stop_requested, 0, 1);
static struct crux_ssh_file_handle file_handles[CRUX_SSH_WOLF_CONNECTIONS_MAX];
static atomic_t next_handle;

static crux_status_t write_all(crux_ssh_channel_t *channel, const void *data, size_t length)
{
    const crux_ssh_provider_t *provider = crux_ssh_wolf_provider();
    const char *at = data;

    while (length > 0u) {
        size_t written = 0;
        crux_status_t status = provider->write(channel, at, length, &written);

        if (status == CRUX_ERR_TIMEOUT) {
            continue;
        }
        if (status != CRUX_OK) {
            return status;
        }
        at += written;
        length -= written;
    }
    return CRUX_OK;
}

static crux_status_t write_text(crux_ssh_channel_t *channel, const char *text)
{
    return write_all(channel, text, strlen(text));
}

static int echo_until_end(crux_ssh_channel_t *channel)
{
    const crux_ssh_provider_t *provider = crux_ssh_wolf_provider();
    char buffer[256];

    while (true) {
        size_t count = 0;
        crux_status_t status = provider->read(channel, buffer, sizeof(buffer), &count);

        if (status == CRUX_ERR_TIMEOUT) {
            continue;
        }
        if (status != CRUX_OK) {
            return status == CRUX_ERR_NO_DATA ? 0 : 1;
        }
        if (write_all(channel, buffer, count) != CRUX_OK) {
            return 1;
        }
    }
}

static int send_pattern(crux_ssh_channel_t *channel, size_t length)
{
    char block[512];

    for (size_t i = 0; i < sizeof(block); ++i) {
        block[i] = (char)('a' + i % 26u);
    }
    while (length > 0u) {
        size_t piece = MIN(length, sizeof(block));

        if (write_all(channel, block, piece) != CRUX_OK) {
            return 1;
        }
        length -= piece;
    }
    return 0;
}

static int print_user(crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel)
{
    crux_access_session_t session = crux_ssh_attachment_session(attachment);
    char user[CRUX_ACCESS_NAME_SIZE];
    char role[CRUX_ACCESS_NAME_SIZE];

    if (crux_access_session_user(session, user, sizeof(user)) != CRUX_ACCESS_OK ||
        crux_access_session_role(session, role, sizeof(role)) != CRUX_ACCESS_OK) {
        return 1;
    }
    (void)write_text(channel, user);
    (void)write_text(channel, " ");
    (void)write_text(channel, role);
    (void)write_text(channel, "\n");
    return 0;
}

void test_print_measurements(void)
{
    struct sys_memory_stats stats = {0};
    size_t worker = 0;
    size_t listener = 0;

    (void)malloc_runtime_stats_get(&stats);
    crux_ssh_wolf_test_stack_used(&worker, &listener);
    printk("MEASURE heap_allocated=%zu heap_peak=%zu heap_free=%zu worker_stack_used=%zu "
           "listener_stack_used=%zu\n",
           stats.allocated_bytes, stats.max_allocated_bytes, stats.free_bytes, worker, listener);
}

static int test_exec(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                     const char *command, size_t command_length)
{
    ARG_UNUSED(context);
    ARG_UNUSED(command_length);
    if (strcmp(command, "whoami") == 0) {
        return print_user(attachment, channel);
    }
    if (strncmp(command, "exit ", 5) == 0) {
        return atoi(command + 5);
    }
    if (strncmp(command, "output ", 7) == 0) {
        return send_pattern(channel, (size_t)atoi(command + 7));
    }
    if (strcmp(command, "cat") == 0) {
        return echo_until_end(channel);
    }
    if (strncmp(command, "sleep ", 6) == 0) {
        k_msleep(atoi(command + 6));
        return 0;
    }
    if (strncmp(command, "derive-delay ", 13) == 0) {
        test_access_derive_delay_ms(atoi(command + 13));
        return 0;
    }
    if (strcmp(command, "measure") == 0) {
        test_print_measurements();
        return 0;
    }
    if (strcmp(command, "stop") == 0) {
        k_sem_give(&stop_requested);
        return 0;
    }
    (void)write_text(channel, "unknown test command\n");
    return 127;
}

static int test_shell(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                      bool terminal)
{
    ARG_UNUSED(context);
    ARG_UNUSED(attachment);
    (void)write_text(channel, terminal ? "terminal=1\r\n" : "terminal=0\n");
    return echo_until_end(channel);
}

static crux_status_t test_sftp_open(void *context, crux_ssh_attachment_t *attachment,
                                    crux_ssh_file_handle_t **handle)
{
    ARG_UNUSED(context);
    ARG_UNUSED(attachment);
    /* Every operation of the refusing port in components/ssh/sftp/port fails behind this handle. */
    *handle = &file_handles[(size_t)atomic_inc(&next_handle) % ARRAY_SIZE(file_handles)];
    printk("SFTP open\n");
    return CRUX_OK;
}

static void test_sftp_close(void *context, crux_ssh_attachment_t *attachment,
                            crux_ssh_file_handle_t *handle)
{
    ARG_UNUSED(context);
    ARG_UNUSED(attachment);
    ARG_UNUSED(handle);
    printk("SFTP close\n");
}

void test_sessions_fill(crux_ssh_callbacks_t *callbacks)
{
    callbacks->shell = test_shell;
    callbacks->exec = test_exec;
    callbacks->sftp_open = test_sftp_open;
    callbacks->sftp_close = test_sftp_close;
}

void test_sessions_wait_for_stop(void)
{
    (void)k_sem_take(&stop_requested, K_FOREVER);
}
