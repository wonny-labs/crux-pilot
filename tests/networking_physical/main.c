/* Test-only RT1060 composition: no application traffic until an explicit armed command. */
#include "fixture.h"
#include "source.h"
#include <errno.h>
#include <stdio.h>
#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
static bool step(const struct fixture_command *command)
{
    static const char actions[] = "AIPPPPPLPI";
    if (command->action == 'A')
        return true;
    unsigned int seq = command->sequence;
    return seq <= 9 && command->action == actions[seq] &&
           command->count == (seq == 2              ? 10U
                              : actions[seq] == 'I' ? 0U
                                                    : 1U);
}
int main(void)
{
    struct net_if *iface = net_if_get_default();
    struct net_in_addr address, mask;
    if (iface == NULL || net_addr_pton(NET_AF_INET, "192.168.77.2", &address) != 0 ||
        net_addr_pton(NET_AF_INET, "255.255.255.0", &mask) != 0 ||
        net_if_ipv4_addr_add(iface, &address, NET_ADDR_MANUAL, 0) == NULL ||
        !net_if_ipv4_set_netmask_by_addr(iface, &address, &mask))
        return 1;
    int status = net_if_up(iface);
    if ((status != 0 && status != -EALREADY) || console_init() != 0 || m5_fixture_init() != 0)
        return 1;
    m5_fixture_record("M5_BOOT source=%s\n", M5_SOURCE);
    struct fixture_session session = {0};
    char line[80];
    unsigned int used = 0;
    int64_t input_deadline = k_uptime_get() + 90000;
    while (session.next_sequence <= 9) {
        int64_t remaining = input_deadline - k_uptime_get();
        if (remaining <= 0)
            break;
        console_set_rx_timeout(K_MSEC(remaining));
        int ch = console_getchar();
        if (ch < 0)
            break;
        if (ch != '\n') {
            if (ch < 32 || ch > 126 || used >= sizeof(line) - 1)
                break;
            line[used++] = (char)ch;
            continue;
        }
        line[used] = '\0';
        used = 0;
        struct fixture_command command;
        if (fixture_control(line, &session, &command) || !step(&command))
            break;
        if (m5_fixture_execute(&session, &command) != 0)
            break;
        console_set_rx_timeout(K_NO_WAIT);
        if (console_getchar() >= 0)
            break;
        session.ticket = sys_rand32_get();
        if (!session.ticket)
            session.ticket = 1;
        m5_fixture_record("M5 READY token=%u seq=%u ticket=%u at=%lld\n", session.token,
                          session.next_sequence, session.ticket, (long long)k_uptime_get());
        input_deadline = k_uptime_get() + 90000;
        if (command.sequence == 9)
            return 0;
    }
    m5_fixture_record("M5_FAIL stopped=1 no_more_traffic=1\n");
    return 1;
}
