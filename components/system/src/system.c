#include "system_port.h"
#include <stddef.h>

static crux_watchdog_state_t lifecycle = CRUX_WATCHDOG_IDLE;
static bool attempted, issued, observed_cause;
static uint32_t successful_timeout;

static crux_status_t service_status(crux_status_t value)
{
    switch (value) {
    case CRUX_OK:
    case CRUX_ERR_NOT_SUPPORTED:
    case CRUX_ERR_NOT_READY:
    case CRUX_ERR_IO:
        return value;
    default:
        return CRUX_ERR_IO;
    }
}

static crux_status_t enter(void)
{
    return system_port_lock() == CRUX_OK ? CRUX_OK : CRUX_ERR_IO;
}

static crux_status_t cause_read(crux_reset_cause_t *out)
{
    if (!system_port_reset_cause_supported())
        return CRUX_ERR_NOT_SUPPORTED;
    crux_reset_cause_t value = {0};
    crux_status_t result = service_status(system_port_reset_cause_get(&value));
    const uint32_t known = (uint32_t)CRUX_RESET_CAUSE_OTHER * 2u - 1u;
    if (result != CRUX_OK)
        return result;
    if (((value.supported | value.causes) & ~known) != 0 || (value.causes & ~value.supported) != 0)
        return CRUX_ERR_IO;
    *out = value;
    return CRUX_OK;
}

static crux_status_t fault(void)
{
    lifecycle = CRUX_WATCHDOG_FAULTED;
    return CRUX_ERR_IO;
}

static crux_status_t refresh(void)
{
    if (lifecycle == CRUX_WATCHDOG_FAULTED)
        return CRUX_ERR_IO;
    if (lifecycle == CRUX_WATCHDOG_EXPIRED)
        return CRUX_ERR_TIMEOUT;
    system_watchdog_probe_t value = {0};
    crux_status_t result = service_status(system_port_watchdog_probe(&value));
    if (result != CRUX_OK) {
        if (issued || result == CRUX_ERR_IO)
            return fault();
        lifecycle = CRUX_WATCHDOG_UNAVAILABLE;
        return result;
    }
    if (issued) {
        if (value.expired && system_port_watchdog_emulated()) {
            lifecycle = CRUX_WATCHDOG_EXPIRED;
            return CRUX_ERR_TIMEOUT;
        }
        if (!value.running || value.expired)
            return fault();
        lifecycle = CRUX_WATCHDOG_ACTIVE;
    } else {
        if (value.running || value.expired)
            return fault();
        lifecycle = CRUX_WATCHDOG_IDLE;
    }
    return CRUX_OK;
}

crux_status_t crux_system_status_get(crux_system_status_t *out)
{
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (!system_port_context_allowed())
        return CRUX_ERR_NOT_SUPPORTED;
    if (enter() != CRUX_OK)
        return CRUX_ERR_IO;
    crux_system_status_t value = {0};
    value.watchdog_status = refresh();
    value.watchdog_state = lifecycle;
    value.watchdog_emulated = system_port_watchdog_emulated();
    value.watchdog_timeout_ms = successful_timeout;
    value.reset_status = service_status(system_port_reset_status());
    crux_reset_cause_t unused;
    value.reset_cause_status = cause_read(&unused);
    *out = value;
    system_port_unlock();
    return CRUX_OK;
}

crux_status_t crux_system_reset_cause_get(crux_reset_cause_t *out)
{
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (!system_port_context_allowed())
        return CRUX_ERR_NOT_SUPPORTED;
    if (enter() != CRUX_OK)
        return CRUX_ERR_IO;
    crux_reset_cause_t value;
    crux_status_t result = cause_read(&value);
    if (result == CRUX_OK) {
        *out = value;
        observed_cause = true;
    }
    system_port_unlock();
    return result;
}

crux_status_t crux_system_reset_cause_clear(void)
{
    if (!system_port_context_allowed())
        return CRUX_ERR_NOT_SUPPORTED;
    if (enter() != CRUX_OK)
        return CRUX_ERR_IO;
    crux_status_t result = CRUX_ERR_INVALID_STATE;
    if (!system_port_reset_cause_supported()) {
        system_port_unlock();
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (observed_cause) {
        observed_cause = false;
        result = service_status(system_port_reset_cause_clear());
        if (result == CRUX_OK) {
            crux_reset_cause_t remaining;
            if (cause_read(&remaining) != CRUX_OK || remaining.causes != 0)
                result = CRUX_ERR_IO;
        }
    }
    system_port_unlock();
    return result;
}

crux_status_t crux_system_reset(void)
{
    if (!system_port_context_allowed())
        return CRUX_ERR_NOT_SUPPORTED;
    if (enter() != CRUX_OK)
        return CRUX_ERR_IO;
    crux_status_t result = service_status(system_port_reset_status());
    system_port_unlock();
    if (result != CRUX_OK)
        return result;
    result = service_status(system_port_reset());
    return result == CRUX_OK ? CRUX_ERR_IO : result;
}

crux_status_t crux_watchdog_start(uint32_t timeout_ms, crux_watchdog_t *out)
{
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    *out = (crux_watchdog_t){0};
    if (timeout_ms == 0)
        return CRUX_ERR_OUT_OF_RANGE;
    if (!system_port_context_allowed())
        return CRUX_ERR_NOT_SUPPORTED;
    crux_status_t result = system_port_watchdog_validate(timeout_ms);
    if (result != CRUX_OK)
        return result;
    if (enter() != CRUX_OK)
        return CRUX_ERR_IO;
    if (attempted || lifecycle == CRUX_WATCHDOG_FAULTED) {
        system_port_unlock();
        return CRUX_ERR_INVALID_STATE;
    }
    result = refresh();
    if (result == CRUX_OK) {
        attempted = true;
        result = system_port_watchdog_start(timeout_ms);
        if (result == CRUX_OK) {
            system_watchdog_probe_t value = {0};
            result = system_port_watchdog_probe(&value);
            if (result == CRUX_OK && value.running && !value.expired) {
                lifecycle = CRUX_WATCHDOG_ACTIVE;
                successful_timeout = timeout_ms;
                issued = true;
                *out = (crux_watchdog_t){1};
            } else
                result = fault();
        } else
            result = fault();
    }
    system_port_unlock();
    return result;
}

crux_status_t crux_watchdog_feed(crux_watchdog_t watchdog)
{
    if (!system_port_context_allowed())
        return CRUX_ERR_NOT_SUPPORTED;
    if (watchdog.reserved == 0)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (enter() != CRUX_OK)
        return CRUX_ERR_IO;
    crux_status_t result;
    if (!issued || watchdog.reserved != 1)
        result = CRUX_ERR_INVALID_ARGUMENT;
    else {
        result = refresh();
        if (result == CRUX_OK) {
            result = system_port_watchdog_feed();
            if (result == CRUX_ERR_TIMEOUT && system_port_watchdog_emulated()) {
                lifecycle = CRUX_WATCHDOG_EXPIRED;
            } else if (result != CRUX_OK)
                result = fault();
        }
    }
    system_port_unlock();
    return result;
}
