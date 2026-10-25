#ifndef CRUX_SYSTEM_PORT_H
#define CRUX_SYSTEM_PORT_H
#include <crux/system.h>
typedef struct {
    bool running;
    bool expired;
} system_watchdog_probe_t;
bool system_port_context_allowed(void);
crux_status_t system_port_lock(void);
void system_port_unlock(void);
bool system_port_watchdog_emulated(void);
/* Pure profile validation; no device effects. */
crux_status_t system_port_watchdog_validate(uint32_t timeout_ms);
crux_status_t system_port_watchdog_probe(system_watchdog_probe_t *out);
/* Called once after ownership admission; failure may have irreversible effects. */
crux_status_t system_port_watchdog_start(uint32_t timeout_ms);
crux_status_t system_port_watchdog_feed(void);
crux_status_t system_port_reset_status(void);
/* Outside metadata lock. Never returns OK; native success is nonreturning. */
crux_status_t system_port_reset(void);
/* Pure service selection: an absent service is distinct from invalid lifecycle. */
bool system_port_reset_cause_supported(void);
/* Port normalizes native bits and validates raw supported/reported consistency. */
crux_status_t system_port_reset_cause_get(crux_reset_cause_t *out);
crux_status_t system_port_reset_cause_clear(void);
#endif
