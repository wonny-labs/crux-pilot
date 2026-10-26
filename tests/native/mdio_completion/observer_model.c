#include "network_mdio_trace.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>

static struct device devices[2];
static bool retained, copying;
static unsigned int prints, reads;
const struct device *observer_device_get(int node)
{
    assert(node == 1 || node == 2);
    return &devices[node - 1];
}
bool device_is_ready(const struct device *dev)
{
    assert(retained && !copying && dev != NULL);
    return true;
}
bool crux_m3_mdio_failure_get(const struct device *dev, struct crux_m3_mdio_failure *out)
{
    assert(dev == &devices[1]);
    reads++;
    copying = true;
    if (retained) {
        memset(out, 0, sizeof(*out));
        out->valid = 1;
        out->result = -116;
    }
    copying = false;
    return retained;
}
int printk(const char *format, ...)
{
    assert(retained && !copying);
    char line[1024];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    assert(length > 0 && (size_t)length < sizeof(line));
    assert(strncmp(line, "M3_MDIO_FIRST_FAILURE ", 22) == 0);
    assert(line[length - 1] == '\n' && strchr(line, '\n') == line + length - 1);
    assert(strstr(line, "result=-116 ") && strstr(line, "valid=1 "));
    prints++;
    return length;
}
int main(void)
{
    crux_m3_mdio_trace_observe();
    crux_m3_mdio_trace_observe();
    assert(prints == 0 && reads == 2);
    retained = true;
    crux_m3_mdio_trace_observe();
    assert(prints == 1 && reads == 3);
    crux_m3_mdio_trace_observe();
    assert(prints == 1 && reads == 3);
    puts("ACTUAL_OBSERVER_CONTROLS_PASS; quiet until retained failure, one whole line after copy");
}
