/* Fake sources of the status service, with fault injection for each. They exist only when Crux
 * builds its own tests, and an adapter's tests can link them too. */
#ifndef MGMT_STATUS_FAKE_SOURCES_H
#define MGMT_STATUS_FAKE_SOURCES_H

#include "mgmt_status_port.h"

/* Every source succeeds, the clock reads 1000 ms, and the store and events are linked. */
void status_fake_reset(void);

void status_fake_uptime(crux_status_t result, uint64_t uptime_ms);
void status_fake_advance_ms(uint64_t delta_ms);
void status_fake_clock(crux_status_t result, crux_mgmt_status_clock_t value);
void status_fake_system(crux_status_t result, crux_status_t watchdog_status,
                        crux_watchdog_state_t state);
void status_fake_net(crux_status_t result, crux_mgmt_status_net_t value);
void status_fake_credential(bool linked, crux_status_t result, crux_mgmt_status_credential_t value);
void status_fake_events(bool linked, crux_status_t result, crux_mgmt_status_event_counts_t value);

unsigned status_fake_clock_reads(void);
unsigned status_fake_system_reads(void);
unsigned status_fake_net_reads(void);
unsigned status_fake_credential_reads(void);
unsigned status_fake_events_reads(void);

/* Runs once inside the next system read, which is inside a refresh. */
void status_fake_system_hook(void (*hook)(void *context), void *context);

#endif
