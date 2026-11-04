/* The thin providers, each over a stand-in for the public contract it calls, so that every mapping
 * is held to a controlled answer. The real contracts run in real_sources.c and on native_sim. */
#include "mgmt_status_port.h"

#include <crux/netif.h>
#include <crux/time.h>

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

static crux_status_t monotonic_result;
static int64_t monotonic_ns;
static crux_status_t snapshot_result;
static crux_realtime_snapshot_t snapshot;
static crux_status_t netif_result;
static crux_netif_status_t netif;
static crux_status_t system_result;
static crux_system_status_t system_status;

crux_status_t crux_monotonic_now(crux_monotonic_time_t *out)
{
    if (monotonic_result == CRUX_OK) {
        out->private_ns = monotonic_ns;
    }
    return monotonic_result;
}

crux_status_t crux_realtime_get_snapshot(crux_realtime_snapshot_t *out)
{
    if (snapshot_result == CRUX_OK) {
        *out = snapshot;
    }
    return snapshot_result;
}

crux_status_t crux_netif_get_status(crux_netif_status_t *out)
{
    if (netif_result == CRUX_OK) {
        *out = netif;
    }
    return netif_result;
}

crux_status_t crux_system_status_get(crux_system_status_t *out)
{
    if (system_result == CRUX_OK) {
        *out = system_status;
    }
    return system_result;
}

static void uptime_cases(void)
{
    static const struct {
        int64_t ns;
        uint64_t ms;
    } rows[] = {{0, 0},
                {999999, 0},
                {1000000, 1},
                {1999999, 1},
                {2000000, 2},
                {INT64_C(86400000000000), UINT64_C(86400000)},
                {INT64_MAX, UINT64_C(9223372036854)}};
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        monotonic_result = CRUX_OK;
        monotonic_ns = rows[i].ns;
        uint64_t out = 99;
        CHECK(crux_mgmt_status_port_uptime_ms(&out) == CRUX_OK);
        CHECK(out == rows[i].ms);
    }
    monotonic_result = CRUX_ERR_IO;
    uint64_t out = 99;
    CHECK(crux_mgmt_status_port_uptime_ms(&out) == CRUX_ERR_IO);
    CHECK(out == 99);
}

static void clock_cases(void)
{
    static const struct {
        crux_realtime_source_t source;
        crux_mgmt_status_clock_t clock;
    } rows[] = {{CRUX_REALTIME_SOURCE_RTC, CRUX_MGMT_STATUS_CLOCK_RTC},
                {CRUX_REALTIME_SOURCE_MANUAL, CRUX_MGMT_STATUS_CLOCK_MANUAL},
                {CRUX_REALTIME_SOURCE_EXTERNAL, CRUX_MGMT_STATUS_CLOCK_EXTERNAL},
                {CRUX_REALTIME_SOURCE_UNKNOWN, CRUX_MGMT_STATUS_CLOCK_UNKNOWN},
                {CRUX_REALTIME_SOURCE_NONE, CRUX_MGMT_STATUS_CLOCK_UNKNOWN}};
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        snapshot_result = CRUX_OK;
        snapshot = (crux_realtime_snapshot_t){.last_source = rows[i].source, .is_valid = true};
        crux_mgmt_status_clock_t out = CRUX_MGMT_STATUS_CLOCK_UNSET;
        CHECK(crux_mgmt_status_port_clock(&out) == CRUX_OK);
        CHECK(out == rows[i].clock);

        /* Time that is not valid is unset, whatever source it last had. */
        snapshot.is_valid = false;
        out = CRUX_MGMT_STATUS_CLOCK_RTC;
        CHECK(crux_mgmt_status_port_clock(&out) == CRUX_OK);
        CHECK(out == CRUX_MGMT_STATUS_CLOCK_UNSET);
    }
    snapshot_result = CRUX_ERR_NOT_READY;
    crux_mgmt_status_clock_t out = CRUX_MGMT_STATUS_CLOCK_MANUAL;
    CHECK(crux_mgmt_status_port_clock(&out) == CRUX_ERR_NOT_READY);
    CHECK(out == CRUX_MGMT_STATUS_CLOCK_MANUAL);
}

static void net_cases(void)
{
    static const struct {
        crux_netif_link_t link;
        bool known;
        bool up;
    } rows[] = {{CRUX_NETIF_LINK_UNKNOWN, false, false},
                {CRUX_NETIF_LINK_DOWN, true, false},
                {CRUX_NETIF_LINK_UP, true, true}};
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        netif_result = CRUX_OK;
        netif = (crux_netif_status_t){true, rows[i].link, true, {192, 0, 2, 10}};
        crux_mgmt_status_net_t out = {0};
        CHECK(crux_mgmt_status_port_net(&out) == CRUX_OK);
        CHECK(out.link_known == rows[i].known);
        CHECK(out.link_up == rows[i].up);
        CHECK(out.admin_up);
        CHECK(out.ipv4_available);
        CHECK(memcmp(out.ipv4, netif.ipv4_address, sizeof(out.ipv4)) == 0);

        netif = (crux_netif_status_t){false, rows[i].link, false, {0, 0, 0, 0}};
        CHECK(crux_mgmt_status_port_net(&out) == CRUX_OK);
        CHECK(out.link_known == rows[i].known);
        CHECK(out.link_up == rows[i].up);
        CHECK(!out.admin_up);
        CHECK(!out.ipv4_available);
    }
    netif_result = CRUX_ERR_NOT_READY;
    crux_mgmt_status_net_t out = {.admin_up = true};
    CHECK(crux_mgmt_status_port_net(&out) == CRUX_ERR_NOT_READY);
    CHECK(out.admin_up);
}

static void system_cases(void)
{
    system_result = CRUX_OK;
    system_status = (crux_system_status_t){.watchdog_status = CRUX_ERR_TIMEOUT,
                                           .watchdog_state = CRUX_WATCHDOG_EXPIRED};
    crux_system_status_t out = {0};
    CHECK(crux_mgmt_status_port_system(&out) == CRUX_OK);
    CHECK(out.watchdog_status == CRUX_ERR_TIMEOUT);
    CHECK(out.watchdog_state == CRUX_WATCHDOG_EXPIRED);
    system_result = CRUX_ERR_IO;
    CHECK(crux_mgmt_status_port_system(&out) == CRUX_ERR_IO);
}

static void no_security_cases(void)
{
    CHECK(!crux_mgmt_status_port_has_credential());
    CHECK(!crux_mgmt_status_port_has_security_events());
    crux_mgmt_status_credential_t credential = {0};
    crux_mgmt_status_event_counts_t counts = {0};
    CHECK(crux_mgmt_status_port_credential(&credential) == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_mgmt_status_port_security_events(&counts) == CRUX_ERR_NOT_SUPPORTED);
}

int main(void)
{
    uptime_cases();
    clock_cases();
    net_cases();
    system_cases();
    no_security_cases();
    printf("MgmtStatus.source_mapping: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
