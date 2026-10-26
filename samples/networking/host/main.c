/* Host executable composition owns this process-wide SIGPIPE policy. */
#include "client.h"
#include <crux/netif.h>
#include <signal.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "usage: crux-networking-host [numeric-ipv4]\n");
        return 2;
    }
    struct sigaction policy = {0};
    policy.sa_handler = SIG_IGN;
    sigemptyset(&policy.sa_mask);
    if (sigaction(SIGPIPE, &policy, NULL) != 0)
        return 2;
    crux_netif_status_t status;
    if (crux_netif_get_status(&status) != CRUX_OK)
        return 2;
    printf("NETIF admin=%d link=%d address=%d\n", status.administrative_up, (int)status.link,
           status.ipv4_available);
    const char *peer = argc == 2 ? argv[1] : "127.0.0.1";
    int tcp = networking_exchange(peer, 42420, false, 2000);
    int udp = networking_exchange(peer, 42421, true, 2000);
    printf("NETWORK tcp=%d udp=%d\n", tcp, udp);
    return tcp != 0 || udp != 0;
}
