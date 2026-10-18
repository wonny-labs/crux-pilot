#ifndef CONTROLLER_NETWORK_CLIENT_H
#define CONTROLLER_NETWORK_CLIENT_H

#include <stdint.h>

/* Sample-owned native client; one caller owns setup, rounds and observations. */
int controller_network_init(void);
int controller_network_round(unsigned int trial);
/* Both protocols must succeed and return native resources within the supplied budget. */
int controller_network_pair(unsigned int trial, unsigned int sequence, int64_t deadline_ms);
void controller_network_report(void);

#endif
