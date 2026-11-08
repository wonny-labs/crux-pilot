/* The snapshot's lock: it is only ever polled, so that a caller never waits behind a refresh, and a
 * lock that cannot be taken means no read. This suite uses a double for access, because the double
 * takes no lock: every lock seen here is the service's, reached through --wrap=crux_mutex_lock. */
#include "fake_sources.h"

#include <crux/sync.h>

#include <stdio.h>
#include <string.h>

static unsigned failures;
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #expression);                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_status_t __real_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
crux_status_t __real_crux_mutex_create(crux_mutex_t *out);
crux_status_t __wrap_crux_mutex_create(crux_mutex_t *out);

static crux_status_t lock_result = CRUX_OK;
static crux_status_t create_result = CRUX_OK;
static unsigned locks;
static unsigned waits;

crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms)
{
    ++locks;
    if (timeout_ms != 0) {
        ++waits;
    }
    return lock_result == CRUX_OK ? __real_crux_mutex_lock(mutex, timeout_ms) : lock_result;
}

crux_status_t __wrap_crux_mutex_create(crux_mutex_t *out)
{
    return create_result == CRUX_OK ? __real_crux_mutex_create(out) : create_result;
}
// NOLINTEND(bugprone-reserved-identifier)

static const crux_access_session_t session = {1};

static crux_mgmt_status_entry_t watchdog_entry(void)
{
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry = {.name = ""};
    while (crux_mgmt_status_next(session, &cursor, &entry) == CRUX_OK) {
        if (strcmp(entry.name, "system.watchdog_state") == 0) {
            break;
        }
    }
    return entry;
}

int main(void)
{
    status_fake_reset();
    const crux_mgmt_status_config_t config = {.platform_version = "1", .project_version = "2"};

    /* A lock that cannot be created is reported as it is, leaves the service not ready, and start
     * can be retried. */
    create_result = CRUX_ERR_NO_RESOURCES;
    CHECK(crux_mgmt_status_start(&config) == CRUX_ERR_NO_RESOURCES);
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    CHECK(crux_mgmt_status_next(session, &cursor, &entry) == CRUX_ERR_NOT_READY);
    create_result = CRUX_OK;
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);

    /* Whatever the poll reports before the first refresh, nothing is read and the entry is busy. */
    const crux_status_t results[] = {CRUX_ERR_BUSY, CRUX_ERR_TIMEOUT, CRUX_ERR_IO,
                                     CRUX_ERR_INVALID_STATE, CRUX_ERR_NOT_SUPPORTED};
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
        lock_result = results[i];
        CHECK(watchdog_entry().status == CRUX_ERR_BUSY);
        CHECK(status_fake_system_reads() == 0);
    }

    /* The first refresh, then a period later a poll that fails: the last answer stands. */
    lock_result = CRUX_OK;
    CHECK(watchdog_entry().status == CRUX_OK);
    CHECK(status_fake_system_reads() == 1);
    status_fake_system(CRUX_OK, CRUX_OK, CRUX_WATCHDOG_FAULTED);
    status_fake_advance_ms(CRUX_MGMT_STATUS_REFRESH_MS);
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
        lock_result = results[i];
        entry = watchdog_entry();
        CHECK(entry.status == CRUX_OK);
        CHECK(strcmp(entry.value.data.text, "active") == 0);
        CHECK(status_fake_system_reads() == 1);
    }
    lock_result = CRUX_OK;
    entry = watchdog_entry();
    CHECK(strcmp(entry.value.data.text, "faulted") == 0);

    /* The rule itself: every take of the lock in all of the above was a poll. */
    CHECK(locks > 0);
    CHECK(waits == 0);

    printf("MgmtStatus.lock_faults: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
