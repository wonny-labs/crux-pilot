#include "fake_sources.h"

#include <stdatomic.h>
#include <stddef.h>

static crux_status_t uptime_result, clock_result, system_result, watchdog_status, net_result,
    credential_result, events_result;
static _Atomic uint64_t uptime_ms;
static crux_mgmt_status_clock_t clock_value;
static crux_watchdog_state_t watchdog_state;
static crux_mgmt_status_net_t net_value;
static crux_mgmt_status_credential_t credential_value;
static crux_mgmt_status_event_counts_t events_value;
static bool credential_linked, events_linked;
static atomic_uint clock_reads, system_reads, net_reads, credential_reads, events_reads;
static void (*system_hook)(void *context);
static void *system_hook_context;

void status_fake_reset(void)
{
    uptime_result = clock_result = system_result = watchdog_status = net_result = CRUX_OK;
    credential_result = events_result = CRUX_OK;
    atomic_store(&uptime_ms, 1000u);
    clock_value = CRUX_MGMT_STATUS_CLOCK_RTC;
    watchdog_state = CRUX_WATCHDOG_ACTIVE;
    net_value = (crux_mgmt_status_net_t){true, true, true, true, {192, 0, 2, 10}};
    credential_value =
        (crux_mgmt_status_credential_t){"valid", "device_id_key", "software", false, true, false};
    events_value = (crux_mgmt_status_event_counts_t){11, 22, 33, 44};
    credential_linked = events_linked = true;
    atomic_store(&clock_reads, 0u);
    atomic_store(&system_reads, 0u);
    atomic_store(&net_reads, 0u);
    atomic_store(&credential_reads, 0u);
    atomic_store(&events_reads, 0u);
    system_hook = NULL;
    system_hook_context = NULL;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test control
void status_fake_uptime(crux_status_t result, uint64_t value)
{
    uptime_result = result;
    atomic_store(&uptime_ms, value);
}

void status_fake_advance_ms(uint64_t delta_ms)
{
    atomic_fetch_add(&uptime_ms, delta_ms);
}

void status_fake_clock(crux_status_t result, crux_mgmt_status_clock_t value)
{
    clock_result = result;
    clock_value = value;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test control
void status_fake_system(crux_status_t result, crux_status_t status, crux_watchdog_state_t state)
{
    system_result = result;
    watchdog_status = status;
    watchdog_state = state;
}

void status_fake_net(crux_status_t result, crux_mgmt_status_net_t value)
{
    net_result = result;
    net_value = value;
}

void status_fake_credential(bool linked, crux_status_t result, crux_mgmt_status_credential_t value)
{
    credential_linked = linked;
    credential_result = result;
    credential_value = value;
}

void status_fake_events(bool linked, crux_status_t result, crux_mgmt_status_event_counts_t value)
{
    events_linked = linked;
    events_result = result;
    events_value = value;
}

unsigned status_fake_clock_reads(void)
{
    return atomic_load(&clock_reads);
}

unsigned status_fake_system_reads(void)
{
    return atomic_load(&system_reads);
}

unsigned status_fake_net_reads(void)
{
    return atomic_load(&net_reads);
}

unsigned status_fake_credential_reads(void)
{
    return atomic_load(&credential_reads);
}

unsigned status_fake_events_reads(void)
{
    return atomic_load(&events_reads);
}

void status_fake_system_hook(void (*hook)(void *context), void *context)
{
    system_hook = hook;
    system_hook_context = context;
}

crux_status_t crux_mgmt_status_port_uptime_ms(uint64_t *out)
{
    if (uptime_result != CRUX_OK) {
        return uptime_result;
    }
    *out = atomic_load(&uptime_ms);
    return CRUX_OK;
}

crux_status_t crux_mgmt_status_port_clock(crux_mgmt_status_clock_t *out)
{
    atomic_fetch_add(&clock_reads, 1u);
    if (clock_result != CRUX_OK) {
        return clock_result;
    }
    *out = clock_value;
    return CRUX_OK;
}

crux_status_t crux_mgmt_status_port_system(crux_system_status_t *out)
{
    atomic_fetch_add(&system_reads, 1u);
    void (*hook)(void *context) = system_hook;
    if (hook != NULL) {
        system_hook = NULL;
        hook(system_hook_context);
    }
    if (system_result != CRUX_OK) {
        return system_result;
    }
    *out = (crux_system_status_t){.watchdog_status = watchdog_status,
                                  .watchdog_state = watchdog_state};
    return CRUX_OK;
}

crux_status_t crux_mgmt_status_port_net(crux_mgmt_status_net_t *out)
{
    atomic_fetch_add(&net_reads, 1u);
    if (net_result != CRUX_OK) {
        return net_result;
    }
    *out = net_value;
    return CRUX_OK;
}

bool crux_mgmt_status_port_has_credential(void)
{
    return credential_linked;
}

crux_status_t crux_mgmt_status_port_credential(crux_mgmt_status_credential_t *out)
{
    atomic_fetch_add(&credential_reads, 1u);
    if (credential_result != CRUX_OK) {
        return credential_result;
    }
    *out = credential_value;
    return CRUX_OK;
}

bool crux_mgmt_status_port_has_security_events(void)
{
    return events_linked;
}

crux_status_t crux_mgmt_status_port_security_events(crux_mgmt_status_event_counts_t *out)
{
    atomic_fetch_add(&events_reads, 1u);
    if (events_result != CRUX_OK) {
        return events_result;
    }
    *out = events_value;
    return CRUX_OK;
}
