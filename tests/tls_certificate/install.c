/*
 * The acceptance criteria of #190 on installation: invalid, mismatched and malformed uploads are
 * refused and logged, the previous certificate stays active, and each result states what was
 * checked.
 */
#include "service_test.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length;
    crux_tls_certificate_info_t info;
    size_t writes;
    size_t events;
    size_t store_length;
    uint8_t store[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX];
} before_t;

static void remember(before_t *before)
{
    before->length = active_der(before->der);
    CHECK(crux_tls_certificate_get(s_admin, &before->info) == CRUX_TLS_CERTIFICATE_OK);
    before->writes = fake_store_write_count();
    before->events = fake_events_count();
    const uint8_t *stored = fake_store_certificate(&before->store_length);
    memcpy(before->store, stored, before->store_length);
}

/* Nothing changed but one rejection event, from this user, for this reason. */
static void expect_refused(const before_t *before, crux_tls_certificate_reason_t reason,
                           const char *user)
{
    uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = active_der(der);
    CHECK(length == before->length && memcmp(der, before->der, length) == 0);
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_admin, &info) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(memcmp(info.fingerprint, before->info.fingerprint, 32) == 0);
    CHECK(fake_store_write_count() == before->writes);
    size_t stored_length = 0;
    const uint8_t *stored = fake_store_certificate(&stored_length);
    CHECK(stored_length == before->store_length &&
          memcmp(stored, before->store, stored_length) == 0);
    CHECK(fake_events_count() == before->events + 1u);
    const crux_tls_certificate_event_t *event = fake_event_last();
    CHECK(event != NULL && event->type == CRUX_TLS_CERTIFICATE_EVENT_REJECTED);
    CHECK(event != NULL && event->reason == reason);
    CHECK(event != NULL && strcmp(event->user, user) == 0);
    CHECK(event != NULL && event->channel == CRUX_ACCESS_CHANNEL_WEB);
    CHECK(event != NULL && event->source_length == 4u && event->source[3] == 1u);
}

static bool bytes_equal(const void *a, size_t length, const void *b)
{
    const uint8_t *x = a;
    const uint8_t *y = b;
    for (size_t i = 0; i < length; ++i) {
        if (x[i] != y[i]) {
            return false;
        }
    }
    return true;
}

static void refused(const uint8_t *der, size_t length, crux_tls_certificate_result_t expected,
                    crux_tls_certificate_reason_t reason)
{
    before_t before;
    remember(&before);
    crux_tls_certificate_info_t out;
    memset(&out, 0x5a, sizeof(out));
    crux_tls_certificate_info_t untouched = out;
    crux_tls_certificate_result_t result =
        crux_tls_certificate_install(s_operator, der, length, &out);
    if (result != expected) {
        printf("install of %zu bytes: %d, expected %d\n", length, (int)result, (int)expected);
    }
    CHECK(result == expected);
    CHECK(bytes_equal(&out, sizeof(out), &untouched));
    expect_refused(&before, reason, "operator");
}

static void test_mismatched_key(void)
{
    fixture_start();
    refused(fixture_other, sizeof(fixture_other), CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH,
            CRUX_TLS_CERTIFICATE_REASON_KEY_MISMATCH);
    /* The device's own point with one bit changed is another key. */
    uint8_t changed[sizeof(fixture_ec_self)];
    memcpy(changed, fixture_ec_self, sizeof(changed));
    for (size_t i = 0; i + 65u <= sizeof(changed); ++i) {
        if (memcmp(changed + i, fixture_device_key, 65) == 0) {
            changed[i + 64u] ^= 0x01u;
            break;
        }
    }
    refused(changed, sizeof(changed), CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH,
            CRUX_TLS_CERTIFICATE_REASON_KEY_MISMATCH);
}

static void test_invalid(void)
{
    fixture_start();
    refused(fixture_p384, sizeof(fixture_p384), CRUX_TLS_CERTIFICATE_ERR_INVALID,
            CRUX_TLS_CERTIFICATE_REASON_INVALID);
    refused(fixture_rsa_self, sizeof(fixture_rsa_self), CRUX_TLS_CERTIFICATE_ERR_INVALID,
            CRUX_TLS_CERTIFICATE_REASON_INVALID);
    /* fixture_ec_self with its outer signature algorithm made SHA-384: the two differ. */
    uint8_t differing[sizeof(fixture_ec_self)];
    memcpy(differing, fixture_ec_self, sizeof(differing));
    static const uint8_t ecdsa_sha256[] = {0x06, 0x08, 0x2a, 0x86, 0x48,
                                           0xce, 0x3d, 0x04, 0x03, 0x02};
    size_t last = 0;
    for (size_t i = 0; i + sizeof(ecdsa_sha256) <= sizeof(differing); ++i) {
        if (memcmp(differing + i, ecdsa_sha256, sizeof(ecdsa_sha256)) == 0) {
            last = i;
        }
    }
    CHECK(last > 0u);
    differing[last + sizeof(ecdsa_sha256) - 1u] = 0x03;
    refused(differing, sizeof(differing), CRUX_TLS_CERTIFICATE_ERR_INVALID,
            CRUX_TLS_CERTIFICATE_REASON_INVALID);
}

static void test_malformed(void)
{
    fixture_start();
    refused(fixture_ec_self_pem, sizeof(fixture_ec_self_pem), CRUX_TLS_CERTIFICATE_ERR_MALFORMED,
            CRUX_TLS_CERTIFICATE_REASON_MALFORMED);
    refused(fixture_ec_self, sizeof(fixture_ec_self) - 1u, CRUX_TLS_CERTIFICATE_ERR_MALFORMED,
            CRUX_TLS_CERTIFICATE_REASON_MALFORMED);
    uint8_t trailing[sizeof(fixture_ec_self) + 1];
    memcpy(trailing, fixture_ec_self, sizeof(fixture_ec_self));
    trailing[sizeof(fixture_ec_self)] = 0;
    refused(trailing, sizeof(trailing), CRUX_TLS_CERTIFICATE_ERR_MALFORMED,
            CRUX_TLS_CERTIFICATE_REASON_MALFORMED);
    static const uint8_t garbage[] = {'h', 'e', 'l', 'l', 'o'};
    refused(garbage, sizeof(garbage), CRUX_TLS_CERTIFICATE_ERR_MALFORMED,
            CRUX_TLS_CERTIFICATE_REASON_MALFORMED);
    static const uint8_t one[] = {0x30};
    refused(one, sizeof(one), CRUX_TLS_CERTIFICATE_ERR_MALFORMED,
            CRUX_TLS_CERTIFICATE_REASON_MALFORMED);
    /* A certificate request is not a certificate. */
    uint8_t request[CRUX_TLS_CERTIFICATE_CSR_MAX];
    size_t request_length = 0;
    CHECK(crux_tls_certificate_request(s_operator, NULL, request, sizeof(request),
                                       &request_length) == CRUX_TLS_CERTIFICATE_OK);
    refused(request, request_length, CRUX_TLS_CERTIFICATE_ERR_MALFORMED,
            CRUX_TLS_CERTIFICATE_REASON_MALFORMED);
}

static void test_too_large(void)
{
    fixture_start();
    static uint8_t big[CRUX_TLS_CERTIFICATE_DER_MAX + 1u];
    memset(big, 0x30, sizeof(big));
    refused(big, sizeof(big), CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE,
            CRUX_TLS_CERTIFICATE_REASON_TOO_LARGE);
}

static void test_every_truncation_and_flip_keeps_the_previous(void)
{
    fixture_start();
    before_t before;
    remember(&before);
    unsigned installed = 0;
    for (size_t cut = 1; cut < sizeof(fixture_other); ++cut) {
        CHECK(crux_tls_certificate_install(s_operator, fixture_other, cut, NULL) ==
              CRUX_TLS_CERTIFICATE_ERR_MALFORMED);
    }
    for (size_t at = 0; at < sizeof(fixture_other); ++at) {
        uint8_t copy[sizeof(fixture_other)];
        memcpy(copy, fixture_other, sizeof(copy));
        copy[at] ^= 0x01u;
        crux_tls_certificate_result_t result =
            crux_tls_certificate_install(s_operator, copy, sizeof(copy), NULL);
        CHECK(result == CRUX_TLS_CERTIFICATE_ERR_MALFORMED ||
              result == CRUX_TLS_CERTIFICATE_ERR_INVALID ||
              result == CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH);
        installed += result == CRUX_TLS_CERTIFICATE_OK;
    }
    CHECK(installed == 0u);
    uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = active_der(der);
    CHECK(length == before.length && memcmp(der, before.der, length) == 0);
    CHECK(fake_store_write_count() == before.writes);
    /* One event for each refusal. */
    CHECK(fake_events_count() == before.events + sizeof(fixture_other) * 2u - 1u);
}

static void test_accepted(void)
{
    fixture_start();
    size_t events = fake_events_count();
    crux_tls_certificate_info_t out;
    CHECK(crux_tls_certificate_install(s_operator, fixture_ec_self, sizeof(fixture_ec_self),
                                       &out) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(out.source == CRUX_TLS_CERTIFICATE_SOURCE_INSTALLED);
    CHECK(strcmp(out.subject, "device.example") == 0);
    CHECK(out.not_before_s == FIXTURE_EC_SELF_NOT_BEFORE);
    CHECK(out.not_after_s == FIXTURE_EC_SELF_NOT_AFTER);
    CHECK(!out.no_expiry);
    CHECK(out.length == sizeof(fixture_ec_self));
    uint8_t digest[32];
    tls_test_sha256(fixture_ec_self, sizeof(fixture_ec_self), digest);
    CHECK(memcmp(out.fingerprint, digest, 32) == 0);
    CHECK(out.checks != NULL && strcmp(out.checks, CRUX_TLS_CERTIFICATE_CHECKS) == 0);

    CHECK(fake_events_count() == events + 1u);
    const crux_tls_certificate_event_t *event = fake_event_last();
    CHECK(event->type == CRUX_TLS_CERTIFICATE_EVENT_REPLACED && !event->failed);
    CHECK(event->reason == CRUX_TLS_CERTIFICATE_REASON_INSTALLED);
    CHECK(strcmp(event->user, "operator") == 0);
    CHECK(event->channel == CRUX_ACCESS_CHANNEL_WEB);

    /* Stored in this module's form, and active. */
    size_t stored_length = 0;
    const uint8_t *stored = fake_store_certificate(&stored_length);
    uint8_t expected[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX];
    size_t expected_length = record_of(CRUX_TLS_CERTIFICATE_SOURCE_INSTALLED, fixture_ec_self,
                                       sizeof(fixture_ec_self), expected);
    CHECK(stored_length == expected_length && memcmp(stored, expected, expected_length) == 0);
    uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX];
    CHECK(active_der(der) == sizeof(fixture_ec_self));
    CHECK(memcmp(der, fixture_ec_self, sizeof(fixture_ec_self)) == 0);
    char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    CHECK(crux_tls_certificate_fingerprint(text) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(strcmp(text, out.fingerprint_text) == 0);

    /* Signed by an RSA CA, which is not checked, and a date past 2049. */
    CHECK(crux_tls_certificate_install(s_admin, fixture_ec_by_rsa, sizeof(fixture_ec_by_rsa),
                                       &out) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(strcmp(out.issuer, "Example Test CA") == 0);
    CHECK(crux_tls_certificate_install(s_admin, fixture_far, sizeof(fixture_far), &out) ==
          CRUX_TLS_CERTIFICATE_OK);
    CHECK(out.not_after_s == FIXTURE_FAR_NOT_AFTER);

    /* It survives a power cycle as installed. */
    crux_tls_certificate_start_t how = CRUX_TLS_CERTIFICATE_GENERATED;
    CHECK(fixture_restart(&how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(how == CRUX_TLS_CERTIFICATE_STARTED);
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_admin, &info) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(info.source == CRUX_TLS_CERTIFICATE_SOURCE_INSTALLED);
    CHECK(info.not_after_s == FIXTURE_FAR_NOT_AFTER);
}

static void test_expired_is_installed_and_reported(void)
{
    fixture_start();
    crux_tls_certificate_info_t out;
    CHECK(crux_tls_certificate_install(s_operator, fixture_ec_self, sizeof(fixture_ec_self),
                                       &out) == CRUX_TLS_CERTIFICATE_OK);
    /* Dates are not checked on upload, and are reported against a clock when there is one. */
    CHECK(crux_tls_certificate_dates_at(&out, true, FIXTURE_EC_SELF_NOT_AFTER + 1) ==
          CRUX_TLS_CERTIFICATE_DATES_EXPIRED);
    CHECK(crux_tls_certificate_dates_at(&out, false, FIXTURE_EC_SELF_NOT_AFTER + 1) ==
          CRUX_TLS_CERTIFICATE_DATES_UNKNOWN);
}

static void test_save_failure_keeps_the_previous(void)
{
    fixture_start();
    before_t before;
    remember(&before);
    fake_store_write_result(CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_tls_certificate_install(s_operator, fixture_ec_self, sizeof(fixture_ec_self),
                                       NULL) == CRUX_TLS_CERTIFICATE_ERR_STORE);
    uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = active_der(der);
    CHECK(length == before.length && memcmp(der, before.der, length) == 0);
    const crux_tls_certificate_event_t *event = fake_event_last();
    CHECK(event->type == CRUX_TLS_CERTIFICATE_EVENT_REPLACED && event->failed);
    CHECK(event->reason == CRUX_TLS_CERTIFICATE_REASON_SAVE_FAILED);
    CHECK(strcmp(event->user, "operator") == 0);
    fake_store_write_result(CRUX_CREDENTIAL_OK);
    CHECK(crux_tls_certificate_install(s_operator, fixture_ec_self, sizeof(fixture_ec_self),
                                       NULL) == CRUX_TLS_CERTIFICATE_OK);
}

static void test_arguments_and_permission_log_nothing(void)
{
    fixture_start();
    size_t events = fake_events_count();
    CHECK(crux_tls_certificate_install(s_operator, NULL, 10, NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    CHECK(crux_tls_certificate_install(s_operator, fixture_ec_self, 0, NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    /* A session without the permission learns nothing about its upload, and Access logs it. */
    CHECK(crux_tls_certificate_install(s_viewer, fixture_ec_self_pem, sizeof(fixture_ec_self_pem),
                                       NULL) == CRUX_TLS_CERTIFICATE_ERR_DENIED);
    CHECK(crux_tls_certificate_install(s_viewer, fixture_ec_self, sizeof(fixture_ec_self), NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_DENIED);
    crux_access_session_t never = {0};
    CHECK(crux_tls_certificate_install(never, fixture_ec_self, sizeof(fixture_ec_self), NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_SESSION_INVALID);
    CHECK(fake_events_count() == events);
    CHECK(fake_store_write_count() == 1u);
}

static void test_checks_statement(void)
{
    /* The statement the issue asks every result to carry. */
    CHECK(strstr(CRUX_TLS_CERTIFICATE_CHECKS, "format") != NULL);
    CHECK(strstr(CRUX_TLS_CERTIFICATE_CHECKS, "device key only") != NULL);
    CHECK(strstr(CRUX_TLS_CERTIFICATE_CHECKS, "chain") != NULL);
    CHECK(strstr(CRUX_TLS_CERTIFICATE_CHECKS, "revocation") != NULL);
    fixture_start();
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_admin, &info) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(info.checks != NULL && strcmp(info.checks, CRUX_TLS_CERTIFICATE_CHECKS) == 0);
}

int main(void)
{
    tls_test_case("mismatched key", test_mismatched_key);
    tls_test_case("invalid", test_invalid);
    tls_test_case("malformed", test_malformed);
    tls_test_case("too large", test_too_large);
    tls_test_case("every truncation and flip keeps the previous",
                  test_every_truncation_and_flip_keeps_the_previous);
    tls_test_case("accepted", test_accepted);
    tls_test_case("expired is installed and reported", test_expired_is_installed_and_reported);
    tls_test_case("save failure keeps the previous", test_save_failure_keeps_the_previous);
    tls_test_case("arguments and permission log nothing",
                  test_arguments_and_permission_log_nothing);
    tls_test_case("checks statement", test_checks_statement);
    return tls_test_finish("tls_certificate_install");
}
