/* Private test controls. They exist only in a build with CRUX_SECURITY_EVENT_TESTING, which only
 * Crux's own tests define. */
#ifndef CRUX_SECURITY_EVENT_TEST_H
#define CRUX_SECURITY_EVENT_TEST_H

#include "security_event_internal.h"

/* Returns to the state before init, as a reset does. Stop the drain first. */
void crux_security_event_test_reset(void);
/* Writes up to one whole burst with no thread and no pause. Returns the lines it wrote. */
unsigned crux_security_event_test_drain_step(void);
/* Asks a started drain to end, and joins it. */
crux_status_t crux_security_event_test_stop_drain(void);
void crux_security_event_test_drain_clear(void);
crux_mutex_t crux_security_event_test_ring_lock(void);
void crux_security_event_test_set_next_sequence(uint32_t sequence);
size_t crux_security_event_test_pending(void);
/* Reads with no permission check, which the product build has no way to do. */
crux_security_event_read_result_t crux_security_event_test_read(uint32_t *cursor,
                                                                crux_security_event_record_t *out);

#endif
