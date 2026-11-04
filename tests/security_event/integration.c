/* The real arrangement a composition uses: product cores with no test hooks, the provider as
 * objects, and real Logging, Thread, Sync and Time authority and monotonic clock. Time's realtime
 * provider is its controlled memory one, and Access's crypto and storage ports are fakes. Lines
 * reach the Host Logging provider's stderr, captured here. */
#include "fake_ports.h"
#include "realtime_port.h"

#include <crux/logging.h>
#include <crux/security_event.h>
#include <crux/thread.h>
#include <crux/time_authority.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks, failures;

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

#define PASSWORD "correct horse battery"
#define WAIT_MS 20000u

static const char *const reader[] = {CRUX_SECURITY_EVENT_PERMISSION_READ, "status.read",
                                     CRUX_ACCESS_PERMISSION_MANAGE};
static const crux_access_role_t roles[] = {{"administrator", reader, 3}};
static const crux_access_policy_t policy = {roles, 1, 900, 28800, NULL};
static const crux_access_origin_t serial = {CRUX_ACCESS_CHANNEL_SERIAL, {0}, 0};
static const crux_access_origin_t web = {CRUX_ACCESS_CHANNEL_WEB, {192, 0, 2, 7}, 4};

static int captured_fd;
static int saved_stderr;

/* Appends, so that the drain's writes and this test's reads share no file position. */
static void capture_stderr(void)
{
    FILE *captured = tmpfile();
    CHECK(captured != NULL);
    captured_fd = fileno(captured);
    CHECK(fcntl(captured_fd, F_SETFL, O_APPEND) == 0);
    saved_stderr = dup(STDERR_FILENO);
    CHECK(saved_stderr >= 0);
    CHECK(dup2(captured_fd, STDERR_FILENO) == STDERR_FILENO);
}

/* Reads everything written so far into a terminated buffer. */
static void read_captured(char *buffer, size_t size)
{
    ssize_t length = pread(captured_fd, buffer, size - 1u, 0);
    CHECK(length >= 0);
    buffer[length < 0 ? 0 : length] = '\0';
}

static crux_security_event_stats_t stats(void)
{
    crux_security_event_stats_t out;
    CHECK(crux_security_event_get_stats(&out) == CRUX_OK);
    return out;
}

static void wait_for_delivered(uint32_t delivered)
{
    for (unsigned attempt = 0; attempt < WAIT_MS / 10u; ++attempt) {
        if (stats().delivered >= delivered) {
            return;
        }
        CHECK(crux_thread_sleep_for(crux_duration_from_ms(10)) == CRUX_OK);
    }
    CHECK(stats().delivered >= delivered);
}

static void emit_while_authority_is_held(void *argument)
{
    crux_access_session_t session = *(crux_access_session_t *)argument;
    /* Through access control, as a real caller does. This session may not use the permission. */
    CHECK(crux_access_check(session, "firmware.upgrade") == CRUX_ACCESS_ERR_DENIED);
}

static crux_monotonic_time_t now(void)
{
    crux_monotonic_time_t value = {0};
    CHECK(crux_monotonic_now(&value) == CRUX_OK);
    return value;
}

int main(void)
{
    capture_stderr();
    CHECK(crux_realtime_init() == CRUX_OK);
    const crux_realtime_t set_time = {1793750400, 0};
    CHECK(crux_realtime_set(&set_time, CRUX_REALTIME_SOURCE_MANUAL, false) == CRUX_OK);
    CHECK(crux_security_event_init(NULL, 0) == CRUX_OK);
    access_fake_reset();
    CHECK(crux_access_init(&policy) == CRUX_ACCESS_OK);

    /* Records wait in the ring until the drain starts. */
    CHECK(crux_access_provision_first_account("admin", PASSWORD, strlen(PASSWORD), "administrator",
                                              &serial) == CRUX_ACCESS_OK);
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    CHECK(crux_access_login_password("nobody", PASSWORD, strlen(PASSWORD), &web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_login_password("admin", PASSWORD, strlen(PASSWORD), &web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_OK);
    CHECK(stats().emitted == 3);
    CHECK(stats().delivered == 0);

    CHECK(crux_security_event_start() == CRUX_OK);
    wait_for_delivered(3);

    static char text[65536];
    read_captured(text, sizeof(text));
    CHECK(strstr(text, "INFO crux.security: evt=access.provisioned seq=1 ") != NULL);
    CHECK(strstr(text, " wall=") != NULL);
    CHECK(strstr(text, " clock=man chan=serial user=admin res=ok\n") != NULL);
    CHECK(strstr(text, "WARN crux.security: evt=access.login_failed seq=2 ") != NULL);
    CHECK(strstr(text, " clock=man chan=web src=192.0.2.7 res=denied\n") != NULL);
    CHECK(strstr(text, "INFO crux.security: evt=access.login_ok seq=3 ") != NULL);
    CHECK(strstr(text, "nobody") == NULL);
    CHECK(strstr(text, PASSWORD) == NULL);

    /* The public read, with a real session. */
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_ACCESS_PROVISIONED);
    CHECK(record.clock == CRUX_SECURITY_EVENT_CLOCK_MANUAL);
    /* The controlled clock stands still, so a record's wall time is before it by the record's age.
     */
    CHECK(record.wall_s <= 1793750400 && record.wall_s > 1793750400 - 600);
    CHECK(strcmp(crux_security_event_name(record.id), "access.provisioned") == 0);

    /* With the Time authority lock held on this thread, an emit on another still returns. */
    CHECK(crux_realtime_impl_lock() == CRUX_OK);
    const crux_thread_options_t options = {0, CRUX_THREAD_PRIORITY_NORMAL};
    crux_thread_t worker;
    CHECK(crux_thread_create(&options, emit_while_authority_is_held, &session, &worker) == CRUX_OK);
    crux_status_t joined = crux_thread_join(&worker, WAIT_MS);
    CHECK(joined == CRUX_OK);
    crux_realtime_impl_unlock();
    wait_for_delivered(4);

    /* A burst and a pause: twelve lines take at least two pauses of the default 100 ms. Holding the
     * authority keeps the drain from writing until all twelve wait, so that the bursts are full. */
    uint32_t delivered = stats().delivered;
    CHECK(crux_realtime_impl_lock() == CRUX_OK);
    for (unsigned index = 0; index < 12; ++index) {
        crux_security_event_t event = {.id = CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED,
                                       .result = CRUX_SECURITY_EVENT_OK,
                                       .user = "admin",
                                       .subject = "net.hostname"};
        crux_security_event_emit(&event);
    }
    crux_monotonic_time_t before = now();
    crux_realtime_impl_unlock();
    wait_for_delivered(delivered + 12u);
    crux_duration_t took = crux_monotonic_diff(now(), before);
    CHECK(crux_duration_to_ms_floor(took) >= 200);
    CHECK(stats().dropped == 0);

    /* Batches smaller than a burst get the same pacing, because the count carries across wakes.
     * Thirty-six lines hold eight pauses of 100 ms. A wake after a long stall of this thread would
     * restart the count, so this asks for half of them. */
    before = now();
    for (unsigned batch = 0; batch < 12; ++batch) {
        delivered = stats().delivered;
        for (unsigned index = 0; index < 3; ++index) {
            crux_security_event_t event = {.id = CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED,
                                           .result = CRUX_SECURITY_EVENT_OK,
                                           .user = "admin",
                                           .subject = "net.hostname"};
            crux_security_event_emit(&event);
        }
        wait_for_delivered(delivered + 3u);
    }
    took = crux_monotonic_diff(now(), before);
    CHECK(crux_duration_to_ms_floor(took) >= 400);
    CHECK(stats().dropped == 0);

    /* The contract's limit under E6: a threshold below INFO hides OK lines from the log, Logging
     * still returns OK, delivered overstates, and the record stays readable. */
    CHECK(crux_log_set_level(CRUX_LOG_WARN) == CRUX_OK);
    delivered = stats().delivered;
    crux_security_event_t hidden = {.id = CRUX_SECURITY_EVENT_ID_CONFIG_SAVED,
                                    .result = CRUX_SECURITY_EVENT_OK,
                                    .subject = "hidden.by.threshold"};
    crux_security_event_emit(&hidden);
    wait_for_delivered(delivered + 1u);
    read_captured(text, sizeof(text));
    CHECK(strstr(text, "hidden.by.threshold") == NULL);
    cursor = 0;
    bool readable = false;
    while (crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK) {
        readable = readable || strcmp(record.subject, "hidden.by.threshold") == 0;
    }
    CHECK(readable);
    CHECK(crux_log_set_level(CRUX_LOG_INFO) == CRUX_OK);

    /* Each line is within the Logging rule, and the whole run wrote no error. */
    read_captured(text, sizeof(text));
    size_t lines = 0;
    for (char *line = strtok(text, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        CHECK(strncmp(line, "INFO crux.security: evt=", 24) == 0 ||
              strncmp(line, "WARN crux.security: evt=", 24) == 0);
        lines += 1;
    }
    CHECK(lines >= 52u);
    CHECK(stats().log_errors == 0);

    /* Nothing stops the drain's thread in a product. It simply ends with the process. */
    fflush(stderr);
    CHECK(dup2(saved_stderr, STDERR_FILENO) == STDERR_FILENO);
    printf("Security event integration: %u checks, %u failures\n", checks, failures);
    fflush(stdout);
    _exit(failures == 0 ? 0 : 1);
}
