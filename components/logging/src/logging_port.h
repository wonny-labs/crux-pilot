#ifndef CRUX_LOGGING_PORT_H
#define CRUX_LOGGING_PORT_H
#include <crux/logging.h>
#include <stdbool.h>
bool logging_port_context_allowed(void);
/* Inputs validated, borrowed until return. Native async path copies writable
 * strings before returning, including when it cannot report per-record loss. */
crux_status_t logging_port_write(crux_log_level_t level, const char *tag, const char *text);
#endif
