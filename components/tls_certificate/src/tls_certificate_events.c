/* The event port of the TLS certificate module, as a thin translation onto the public emit. It
 * holds no state and takes no lock of its own. The subject is a fixed word for the reason, never
 * bytes from a certificate or a client.
 */
#include "tls_certificate_port.h"

#include <crux/security_event.h>

#include <string.h>

static const char *subject_of(crux_tls_certificate_reason_t reason)
{
    /* No default case: the compiler refuses a reason that this table does not name. */
    switch (reason) {
    case CRUX_TLS_CERTIFICATE_REASON_GENERATED:
    case CRUX_TLS_CERTIFICATE_REASON_REGENERATED:
        return "self_signed";
    case CRUX_TLS_CERTIFICATE_REASON_INSTALLED:
        return "installed";
    case CRUX_TLS_CERTIFICATE_REASON_SAVE_FAILED:
        return "save_failed";
    case CRUX_TLS_CERTIFICATE_REASON_MALFORMED:
        return "malformed";
    case CRUX_TLS_CERTIFICATE_REASON_INVALID:
        return "invalid";
    case CRUX_TLS_CERTIFICATE_REASON_KEY_MISMATCH:
        return "key_mismatch";
    case CRUX_TLS_CERTIFICATE_REASON_TOO_LARGE:
        return "too_large";
    }
    return NULL;
}

void crux_tls_certificate_port_event(const crux_tls_certificate_event_t *event)
{
    crux_security_event_t record;
    memset(&record, 0, sizeof(record));
    switch (event->type) {
    case CRUX_TLS_CERTIFICATE_EVENT_REPLACED:
        record.id = CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REPLACED;
        record.result = event->failed ? CRUX_SECURITY_EVENT_FAILED : CRUX_SECURITY_EVENT_OK;
        break;
    case CRUX_TLS_CERTIFICATE_EVENT_REJECTED:
        record.id = CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REJECTED;
        record.result = CRUX_SECURITY_EVENT_INVALID;
        break;
    case CRUX_TLS_CERTIFICATE_EVENT_UNUSABLE:
        record.id = CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_UNUSABLE;
        record.result = CRUX_SECURITY_EVENT_FAILED;
        break;
    }
    record.channel = event->channel;
    record.source_length = event->source_length;
    memcpy(record.source, event->source, sizeof(record.source));
    record.user = event->user[0] != '\0' ? event->user : NULL;
    record.subject = subject_of(event->reason);
    crux_security_event_emit(&record);
}
