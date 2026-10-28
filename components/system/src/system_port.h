#ifndef CRUX_SYSTEM_PORT_H
#define CRUX_SYSTEM_PORT_H
#include <crux/system.h>
typedef struct {
    bool running;
    bool expired;
} system_watchdog_probe_t;
bool crux_system_context_allowed(void);
crux_status_t system_port_lock(void);
void system_port_unlock(void);
bool crux_watchdog_is_emulated(void);
/* Pure profile validation; no device effects. */
crux_status_t crux_watchdog_validate_timeout(uint32_t timeout_ms);
crux_status_t crux_watchdog_probe(system_watchdog_probe_t *out);
/* Called once after ownership admission; failure may have irreversible effects. */
crux_status_t crux_watchdog_impl_start(uint32_t timeout_ms);
crux_status_t crux_watchdog_impl_feed(void);
crux_status_t crux_system_reset_status(void);
/* Outside metadata lock. Never returns OK; native success is nonreturning. */
crux_status_t crux_system_impl_reset(void);
/* Pure service selection: an absent service is distinct from invalid lifecycle. */
bool crux_reset_cause_supported(void);
/* Port normalizes native bits and validates raw supported/reported consistency. */
crux_status_t crux_reset_cause_get(crux_reset_cause_t *out);
crux_status_t crux_reset_cause_clear(void);
#endif
