/* Starts the adapter on plain HTTP over the native socket offload. There is no asset code here: the
 * folder named in CMakeLists.txt is all that this image says about its interface. test_driver.py is
 * the client. */
#include "crux_web_adapter.h"

#include <zephyr/kernel.h>

int main(void)
{
    static const crux_web_config_t config = {
        .authenticate = NULL,
        .request_deadline_ms = CONFIG_CRUX_WEB_REQUEST_DEADLINE_MS,
        .handler_limit_ms = 100u,
    };

    if (crux_web_start(&config) != CRUX_WEB_OK) {
        printk("CRUX_WEB_START_FAILED\n");
        return 1;
    }
    printk("CRUX_WEB_READY %d\n", CONFIG_CRUX_WEB_PORT);
    return 0;
}
