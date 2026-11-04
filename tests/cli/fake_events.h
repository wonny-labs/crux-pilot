/* A recording provider of the CLI's event port. A test composition: a product provider connects the
 * port to Issue #182. */
#ifndef CRUX_CLI_FAKE_EVENTS_H
#define CRUX_CLI_FAKE_EVENTS_H

#include "cli_port.h"

#include <stddef.h>

void cli_fake_events_clear(void);
size_t cli_fake_event_count(void);
size_t cli_fake_event_count_of(crux_cli_event_type_t type);
const crux_cli_event_t *cli_fake_event(size_t index);

#endif
