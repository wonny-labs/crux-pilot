#include "network_client.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

#include <errno.h>

static atomic_t requested;

static int request_round(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(shell);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (!atomic_cas(&requested, 0, -1)) {
        printk("M3_NET_REJECTED busy\n");
        return -EBUSY;
    }
    printk("M3_NET_REQUESTED\n");
    atomic_set(&requested, 1);
    return 0;
}

SHELL_CMD_REGISTER(m3net, NULL, "Request one bounded isolated network round", request_round);

static void report_stack(void) {
    size_t unused = 0;
    int result = k_thread_stack_space_get(k_current_get(), &unused);
    printk("M3_NET_STACK status=%d usable=%zu unused=%zu\n", result,
           k_current_get()->stack_info.size, unused);
}

int main(void) {
    printk("M3_NET_BOOT source=%s profile=network-probe exposure=isolated-only\n", CRUX_M3_SOURCE);
    int result = controller_network_init();
    printk("M3_NET_INIT result=%d board=192.168.77.2 peer=192.168.77.1\n", result);
    if (result != 0) {
        return 1;
    }
    int64_t next_report = 0;
    for (unsigned int round = 1; round <= 7;) {
        if (atomic_cas(&requested, 1, 2)) {
            result = controller_network_round(round);
            printk("M3_NET_ROUND trial=%u result=%d monotonic_ms=%lld\n", round, result,
                   (long long)k_uptime_get());
            report_stack();
            round++;
            atomic_set(&requested, 0);
        }
        if (k_uptime_get() >= next_report) {
            controller_network_report();
            report_stack();
            next_report = k_uptime_get() + 1000;
        }
        k_sleep(K_MSEC(100));
    }
    atomic_set(&requested, 2);
    printk("M3_NET_STOP round_limit=7\n");
    return 0;
}
