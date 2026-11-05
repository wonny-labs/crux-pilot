/* The event provider: each port event to its web.* record, on the recording fake of the contract.
 */
#include "security_event_fake.h"
#include "tls_certificate_port.h"
#include "tls_test.h"

#include <string.h>

static crux_tls_certificate_event_t event_of(crux_tls_certificate_event_type_t type,
                                             crux_tls_certificate_reason_t reason, bool failed)
{
    crux_tls_certificate_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.reason = reason;
    event.failed = failed;
    return event;
}

static void test_replaced(void)
{
    security_event_fake_reset();
    security_event_fake_boot();
    crux_tls_certificate_event_t event =
        event_of(CRUX_TLS_CERTIFICATE_EVENT_REPLACED, CRUX_TLS_CERTIFICATE_REASON_GENERATED, false);
    crux_tls_certificate_port_event(&event);
    const crux_security_event_record_t *record =
        security_event_fake_last(CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REPLACED);
    CHECK(record != NULL);
    CHECK(record != NULL && record->result == CRUX_SECURITY_EVENT_OK);
    CHECK(record != NULL && strcmp(record->subject, "self_signed") == 0);
    CHECK(record != NULL && record->user[0] == '\0' && record->channel == 0u);

    event =
        event_of(CRUX_TLS_CERTIFICATE_EVENT_REPLACED, CRUX_TLS_CERTIFICATE_REASON_INSTALLED, false);
    event.channel = 2;
    event.source_length = 4;
    event.source[0] = 192;
    event.source[3] = 7;
    memcpy(event.user, "operator", sizeof("operator"));
    crux_tls_certificate_port_event(&event);
    record = security_event_fake_last(CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REPLACED);
    CHECK(strcmp(record->subject, "installed") == 0);
    CHECK(strcmp(record->user, "operator") == 0);
    CHECK(record->channel == 2u && record->source_length == 4u && record->source[3] == 7u);

    event = event_of(CRUX_TLS_CERTIFICATE_EVENT_REPLACED, CRUX_TLS_CERTIFICATE_REASON_SAVE_FAILED,
                     true);
    crux_tls_certificate_port_event(&event);
    record = security_event_fake_last(CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REPLACED);
    CHECK(record->result == CRUX_SECURITY_EVENT_FAILED);
    CHECK(strcmp(record->subject, "save_failed") == 0);
    CHECK(security_event_fake_count_of(CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REPLACED) == 3u);
}

static void test_rejected(void)
{
    security_event_fake_reset();
    security_event_fake_boot();
    struct {
        crux_tls_certificate_reason_t reason;
        const char *subject;
    } cases[] = {
        {CRUX_TLS_CERTIFICATE_REASON_MALFORMED, "malformed"},
        {CRUX_TLS_CERTIFICATE_REASON_INVALID, "invalid"},
        {CRUX_TLS_CERTIFICATE_REASON_KEY_MISMATCH, "key_mismatch"},
        {CRUX_TLS_CERTIFICATE_REASON_TOO_LARGE, "too_large"},
        {CRUX_TLS_CERTIFICATE_REASON_STORED, "stored"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        crux_tls_certificate_event_t event =
            event_of(CRUX_TLS_CERTIFICATE_EVENT_REJECTED, cases[i].reason, false);
        memcpy(event.user, "admin", sizeof("admin"));
        crux_tls_certificate_port_event(&event);
        const crux_security_event_record_t *record =
            security_event_fake_last(CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REJECTED);
        CHECK(record != NULL && record->result == CRUX_SECURITY_EVENT_INVALID);
        CHECK(record != NULL && strcmp(record->subject, cases[i].subject) == 0);
        CHECK(record != NULL && strcmp(record->user, "admin") == 0);
    }
    CHECK(security_event_fake_count_of(CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REJECTED) == 5u);
    CHECK(strcmp(crux_security_event_name(CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REJECTED),
                 "web.certificate_rejected") == 0);
}

int main(void)
{
    tls_test_case("replaced", test_replaced);
    tls_test_case("rejected", test_rejected);
    return tls_test_finish("tls_certificate_events");
}
