/* Starts the adapter on plain HTTP over the native socket offload. test_driver.py is the client. */
#include "web_probe.h"

#include <zephyr/kernel.h>

int main(void)
{
    static const crux_web_config_t config = {
        .authenticate = web_probe_authenticate,
        .request_deadline_ms = CONFIG_CRUX_WEB_REQUEST_DEADLINE_MS,
        .handler_limit_ms = 100u,
    };

    web_probe_fill_large_body();
    if (crux_web_start(&config) != CRUX_WEB_OK) {
        printk("CRUX_WEB_START_FAILED\n");
        return 1;
    }
    printk("CRUX_WEB_READY %d\n", CONFIG_CRUX_WEB_PORT);
    return 0;
}
