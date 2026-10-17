#include <crux/status.h>
#include <crux/time.h>

#include <zephyr/sys/printk.h>

int main(void) {
    const crux_status_t status = crux_time_init();

    if (status != CRUX_OK) {
        printk("CRUX_TIME_INIT_FAILED status=%d\n", (int)status);
        return -1;
    }

    printk("Crux Pilot Time Zephyr sample. Use `time help` for commands.\n");
    printk("CRUX_TIME_READY composition=zephyr\n");
    return 0;
}
