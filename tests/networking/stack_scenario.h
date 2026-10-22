#ifndef NETWORKING_STACK_SCENARIO_H
#define NETWORKING_STACK_SCENARIO_H
#include <stdbool.h>
void networking_peer_start(void (*entry)(void *), void *context);
void networking_resources_active(void);
int networking_peer_join(void);
void networking_fixture_link(bool up);
int networking_stack_link_loss(void);
int networking_stack_scenarios(void);
int runtime_port_exchange(const char *address, unsigned short port);
#endif
