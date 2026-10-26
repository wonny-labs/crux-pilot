#include "time_port.h"

#include <stdbool.h>
#include <stddef.h>

static bool g_backend_initialised;
static bool g_has_time;
static crux_realtime_t g_time;

#ifdef CRUX_TIME_TESTING
static bool g_fail_next_init;
static bool g_next_read_pending;
static crux_status_t g_next_read_status;
static bool g_fail_next_write;
static unsigned int g_init_calls;
static unsigned int g_read_calls;
static unsigned int g_write_calls;
#endif

crux_status_t crux_time_port_init(void)
{
#ifdef CRUX_TIME_TESTING
    ++g_init_calls;
    if (g_fail_next_init) {
        g_fail_next_init = false;
        return CRUX_ERR_IO;
    }
#endif
    g_backend_initialised = true;
    return CRUX_OK;
}

crux_status_t crux_time_port_read(crux_realtime_t *out)
{
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!g_backend_initialised) {
        return CRUX_ERR_NOT_READY;
    }
#ifdef CRUX_TIME_TESTING
    ++g_read_calls;
    if (g_next_read_pending) {
        crux_status_t status = g_next_read_status;
        g_next_read_pending = false;
        if (status == CRUX_ERR_NO_DATA) {
            g_has_time = false;
        }
        if (status != CRUX_OK) {
            return status;
        }
    }
#endif
    if (!g_has_time) {
        return CRUX_ERR_NO_DATA;
    }

    *out = g_time;
    return CRUX_OK;
}

crux_status_t crux_time_port_write(const crux_realtime_t *value)
{
    if (!g_backend_initialised) {
        return CRUX_ERR_NOT_READY;
    }
#ifdef CRUX_TIME_TESTING
    ++g_write_calls;
    if (g_fail_next_write) {
        g_fail_next_write = false;
        return CRUX_ERR_IO;
    }
#endif
    g_time = *value;
    g_has_time = true;
    return CRUX_OK;
}

#ifdef CRUX_TIME_TESTING
void crux_time_host_test_reset(void)
{
    g_backend_initialised = false;
    g_has_time = false;
    g_time = (crux_realtime_t){CRUX_TIME_MIN_UNIX_SECONDS, 0};
    g_fail_next_init = false;
    g_next_read_pending = false;
    g_next_read_status = CRUX_OK;
    g_fail_next_write = false;
    g_init_calls = 0;
    g_read_calls = 0;
    g_write_calls = 0;
}

void crux_time_host_test_fail_next_init(void)
{
    g_fail_next_init = true;
}

void crux_time_host_test_next_read_status(crux_status_t status)
{
    g_next_read_pending = true;
    g_next_read_status = status;
}

void crux_time_host_test_fail_next_write(void)
{
    g_fail_next_write = true;
}

crux_status_t crux_time_host_test_advance(int64_t seconds)
{
    int64_t advanced;

    if (!g_backend_initialised) {
        return CRUX_ERR_NOT_READY;
    }
    if (!g_has_time) {
        return CRUX_ERR_NO_DATA;
    }
    if ((seconds > 0 && g_time.seconds > INT64_MAX - seconds) ||
        (seconds < 0 && g_time.seconds < INT64_MIN - seconds)) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    advanced = g_time.seconds + seconds;
    if (advanced < CRUX_TIME_MIN_UNIX_SECONDS || advanced >= CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    g_time.seconds = advanced;
    return CRUX_OK;
}

void crux_time_host_test_override_time(int64_t value)
{
    g_time = (crux_realtime_t){value, 0};
    g_has_time = true;
}

unsigned int crux_time_host_test_init_calls(void)
{
    return g_init_calls;
}

unsigned int crux_time_host_test_read_calls(void)
{
    return g_read_calls;
}

unsigned int crux_time_host_test_write_calls(void)
{
    return g_write_calls;
}
#endif
