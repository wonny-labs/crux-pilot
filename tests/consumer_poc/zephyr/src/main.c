/* Console lines are this application's own contract with its functional oracle. */
#include "app_run.h"

#include <zephyr/sys/printk.h>

static void print_cycle(void *context, int cycle, uint64_t skipped)
{
    (void)context;
    printk("APP cycle=%d skipped=%llu\n", cycle, (unsigned long long)skipped);
}

int main(void)
{
    printk("APP ready\n");
    int result = app_run(print_cycle, NULL);
    printk("APP done result=%d\n", result);
    return 0;
}
