#include <crux/status.h>
#include <crux/time.h>

#include <zephyr/sys/printk.h>

int main(void)
{
    crux_monotonic_time_t now;
    const crux_status_t status = crux_monotonic_now(&now);

    if (status != CRUX_OK) {
        printk("product-x: clock failed, status=%d\n", (int)status);
        return -1;
    }

    printk("product-x: ready\n");
    return 0;
}
