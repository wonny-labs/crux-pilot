/* The event port of the command line, as a thin translation onto the public emit (Issue #242). It
 * holds no state and takes no lock of its own. The record names the session's user and channel
 * that the CLI gives, and never the line: the port carries none. */
#include "cli_port.h"

#include <crux/security_event.h>

void crux_cli_port_event(const crux_cli_event_t *event)
{
    crux_security_event_t record = {0};

    if (event == NULL) {
        return;
    }
    /* No default case: the compiler refuses a port value that these tables do not map. */
    switch (event->type) {
    case CRUX_CLI_EVENT_LINE_REJECTED:
        record.id = CRUX_SECURITY_EVENT_ID_CLI_LINE_REJECTED;
        break;
    case CRUX_CLI_EVENT_CONSOLE_ERROR:
        record.id = CRUX_SECURITY_EVENT_ID_CLI_CONSOLE_ERROR;
        break;
    }
    switch (event->outcome) {
    case CRUX_CLI_EVENT_INVALID:
        record.result = CRUX_SECURITY_EVENT_INVALID;
        break;
    case CRUX_CLI_EVENT_LIMIT:
        record.result = CRUX_SECURITY_EVENT_LIMIT;
        break;
    case CRUX_CLI_EVENT_DENIED:
        record.result = CRUX_SECURITY_EVENT_DENIED;
        break;
    case CRUX_CLI_EVENT_FAILED:
        record.result = CRUX_SECURITY_EVENT_FAILED;
        break;
    }
    record.channel = event->channel;
    /* An empty name is no user, which the contract reads as NULL. */
    record.user = event->user[0] != '\0' ? event->user : NULL;
    crux_security_event_emit(&record);
}
