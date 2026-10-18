#ifndef CONTROLLER_NETWORK_CLIENT_H
#define CONTROLLER_NETWORK_CLIENT_H

/* Sample-owned native client; one caller owns setup, rounds and observations. */
int controller_network_init(void);
int controller_network_round(unsigned int trial);
void controller_network_report(void);

#endif
