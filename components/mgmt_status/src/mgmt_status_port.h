/*
 * Private ports of the status service, one for each built-in source. A composition selects a
 * provider for each, and the sources in this directory are thin ones over the public contracts. A
 * source reports its answer in these narrow types, so that the service needs no native or provider
 * type and no header of a component that a composition may not link.
 */
#ifndef CRUX_MGMT_STATUS_PORT_H
#define CRUX_MGMT_STATUS_PORT_H

#include <crux/management_status.h>

/* The most often the snapshot's sources are read: decision C7. */
#ifndef CRUX_MGMT_STATUS_REFRESH_MS
#define CRUX_MGMT_STATUS_REFRESH_MS 1000u
#endif

typedef enum {
    CRUX_MGMT_STATUS_CLOCK_UNSET,
    CRUX_MGMT_STATUS_CLOCK_UNKNOWN,
    CRUX_MGMT_STATUS_CLOCK_RTC,
    CRUX_MGMT_STATUS_CLOCK_MANUAL,
    CRUX_MGMT_STATUS_CLOCK_EXTERNAL
} crux_mgmt_status_clock_t;

typedef struct {
    bool admin_up;
    bool link_known;
    bool link_up;
    bool ipv4_available;
    uint8_t ipv4[4];
} crux_mgmt_status_net_t;

/* What the credential store declares, in words a client can show. Every field travels together, so
 * a state never appears without its basis. The strings live for the program's life. */
typedef struct {
    const char *state;
    const char *basis;
    const char *evidence;
    bool confidential;
    bool forgery_detected;
    bool rollback_detected;
} crux_mgmt_status_credential_t;

typedef struct {
    uint32_t emitted;
    uint32_t dropped;
    uint32_t delivered;
    uint32_t log_errors;
} crux_mgmt_status_event_counts_t;

/* Milliseconds since boot. It also dates the snapshot, so it must not wait on a lock. */
crux_status_t crux_mgmt_status_port_uptime_ms(uint64_t *out);

/* The snapshot's sources: each is read at most once per refresh period. */
crux_status_t crux_mgmt_status_port_clock(crux_mgmt_status_clock_t *out);
crux_status_t crux_mgmt_status_port_system(crux_system_status_t *out);

crux_status_t crux_mgmt_status_port_net(crux_mgmt_status_net_t *out);

/* Whether the composition links a source, which is asked once at start. If it does not, its items
 * are omitted, and the matching read below is never called. */
bool crux_mgmt_status_port_has_credential(void);
crux_status_t crux_mgmt_status_port_credential(crux_mgmt_status_credential_t *out);
bool crux_mgmt_status_port_has_security_events(void);
crux_status_t crux_mgmt_status_port_security_events(crux_mgmt_status_event_counts_t *out);

#endif
