/* Host composition owns threads, process signal policy and resource inspection. */
#include "stack_scenario.h"
#include <crux/netif.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
static pthread_t worker;
static void (*peer_entry)(void *);
static void *peer_context;
static void *run_peer(void *unused) {
    (void)unused;
    peer_entry(peer_context);
    return NULL;
}
void networking_peer_start(void (*entry)(void *), void *context) {
    peer_entry = entry;
    peer_context = context;
    if (pthread_create(&worker, NULL, run_peer, NULL) != 0)
        abort();
}
void networking_fixture_link(bool up) {
    (void)up;
    abort();
}
void networking_resources_active(void) {
}
int networking_peer_join(void) {
    return pthread_join(worker, NULL);
}
static unsigned int descriptors(void) {
    DIR *directory = opendir("/proc/self/fd");
    if (directory == NULL)
        abort();
    unsigned int count = 0;
    while (readdir(directory) != NULL)
        ++count;
    closedir(directory);
    return count;
}
int main(void) {
    struct sigaction policy = {0};
    policy.sa_handler = SIG_IGN;
    sigemptyset(&policy.sa_mask);
    if (sigaction(SIGPIPE, &policy, NULL) != 0)
        return 1;
    crux_netif_status_t status;
    if (crux_netif_get_status(&status) != CRUX_OK || !status.ipv4_available ||
        status.ipv4_address[0] != 127 || status.link != CRUX_NETIF_LINK_UP)
        return 2;
    unsigned int before = descriptors();
    int result = networking_stack_scenarios();
    unsigned int after = descriptors();
    printf("STACK_RESOURCES descriptors_before=%u after=%u\n", before, after);
    return result != 0 || before != after;
}
