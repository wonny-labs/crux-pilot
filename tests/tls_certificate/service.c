/*
 * Start, first boot and later boots, failing closed, permissions, the request, regeneration, the
 * material for a TLS stack, contention, and that no output holds the private key.
 */
#include "service_test.h"
#include "tls_x509.h"

#include <string.h>

static void test_first_boot(void)
{
    fixture_start();
    CHECK(fake_store_ensure_count() == 1u);
    CHECK(fake_store_write_count() == 1u);
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_operator, &info) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(info.source == CRUX_TLS_CERTIFICATE_SOURCE_SELF_SIGNED);
    CHECK(strcmp(info.subject, "Crux device") == 0);
    CHECK(strcmp(info.issuer, "Crux device") == 0);
    CHECK(info.not_before_s == CRUX_TLS_CERTIFICATE_DEFAULT_NOT_BEFORE_S);
    CHECK(info.no_expiry);
    CHECK(info.not_after_s == CRUX_TLS_CERTIFICATE_TIME_MAX_S);
    CHECK(info.checks != NULL && strcmp(info.checks, CRUX_TLS_CERTIFICATE_CHECKS) == 0);

    uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = active_der(der);
    CHECK(length == info.length);
    uint8_t digest[32];
    tls_test_sha256(der, length, digest);
    CHECK(memcmp(info.fingerprint, digest, 32) == 0);
    char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    tls_x509_fingerprint_text(digest, text);
    CHECK(strcmp(info.fingerprint_text, text) == 0);
    char boot_text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    CHECK(crux_tls_certificate_fingerprint(boot_text) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(strcmp(boot_text, text) == 0);

    /* The device key, signed with the device scalar, stored in this module's form. */
    tls_x509_view_t view;
    CHECK(tls_x509_parse(der, length, &view) == TLS_X509_OK);
    CHECK(memcmp(view.public_key, fixture_device_key, 65) == 0);
    CHECK(memcmp(fake_ports_last_sign_key(), fake_device_scalar, 32) == 0);
    size_t stored_length = 0;
    const uint8_t *stored = fake_store_certificate(&stored_length);
    uint8_t expected[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX];
    size_t expected_length =
        record_of(CRUX_TLS_CERTIFICATE_SOURCE_SELF_SIGNED, der, length, expected);
    CHECK(stored_length == expected_length && memcmp(stored, expected, expected_length) == 0);

    /* One event, from no one. */
    CHECK(fake_events_count() == 1u);
    const crux_tls_certificate_event_t *event = fake_event(0);
    CHECK(event->type == CRUX_TLS_CERTIFICATE_EVENT_REPLACED && !event->failed);
    CHECK(event->reason == CRUX_TLS_CERTIFICATE_REASON_GENERATED);
    CHECK(event->user[0] == '\0' && event->channel == 0u && event->source_length == 0u);
}

static void test_later_boot_loads(void)
{
    fixture_start();
    crux_tls_certificate_info_t first;
    CHECK(crux_tls_certificate_get(s_admin, &first) == CRUX_TLS_CERTIFICATE_OK);
    crux_tls_certificate_start_t how = CRUX_TLS_CERTIFICATE_GENERATED;
    CHECK(fixture_restart(&how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(how == CRUX_TLS_CERTIFICATE_STARTED);
    crux_tls_certificate_info_t second;
    CHECK(crux_tls_certificate_get(s_admin, &second) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(memcmp(first.fingerprint, second.fingerprint, 32) == 0);
    CHECK(fake_store_write_count() == 1u);
    CHECK(fake_ports_sign_count() == 0u);
    CHECK(fake_events_count() == 0u);
}

static void test_start_arguments(void)
{
    fixture_boot();
    crux_tls_certificate_start_t how;
    crux_tls_certificate_options_t bad = fixture_options;
    CHECK(crux_tls_certificate_start(NULL, &how) == CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    CHECK(crux_tls_certificate_start(&fixture_options, NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    bad.names.common_name = "";
    CHECK(crux_tls_certificate_start(&bad, &how) == CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    bad = fixture_options;
    bad.not_before_s = -1;
    CHECK(crux_tls_certificate_start(&bad, &how) == CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    bad.not_before_s = CRUX_TLS_CERTIFICATE_TIME_MAX_S;
    bad.validity_days = 1;
    CHECK(crux_tls_certificate_start(&bad, &how) == CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    CHECK(fake_store_ensure_count() == 0u);
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_ERR_BUSY);
}

static void test_dated_options(void)
{
    fixture_boot();
    static const crux_tls_certificate_options_t dated = {
        {"Crux device", NULL, 0, NULL, 0, NULL, 0}, 1800000000, 825};
    crux_tls_certificate_start_t how;
    CHECK(crux_tls_certificate_start(&dated, &how) == CRUX_TLS_CERTIFICATE_OK);
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_admin, &info) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(info.not_before_s == 1800000000);
    CHECK(info.not_after_s == 1800000000 + 825 * 86400);
    CHECK(!info.no_expiry);
}

static void test_key_failures(void)
{
    crux_tls_certificate_start_t how;
    struct {
        crux_credential_result_t store;
        crux_tls_certificate_result_t expected;
    } cases[] = {
        {CRUX_CREDENTIAL_ERR_ENTROPY, CRUX_TLS_CERTIFICATE_ERR_CRYPTO},
        {CRUX_CREDENTIAL_ERR_UNUSABLE, CRUX_TLS_CERTIFICATE_ERR_NOT_READY},
        {CRUX_CREDENTIAL_ERR_NOT_READY, CRUX_TLS_CERTIFICATE_ERR_NOT_READY},
        {CRUX_CREDENTIAL_ERR_STORE, CRUX_TLS_CERTIFICATE_ERR_STORE},
        {CRUX_CREDENTIAL_ERR_READ, CRUX_TLS_CERTIFICATE_ERR_STORE},
    };
    fixture_boot();
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        fake_store_ensure_result(cases[i].store);
        CHECK(crux_tls_certificate_start(&fixture_options, &how) == cases[i].expected);
        CHECK(fake_store_write_count() == 0u);
        crux_tls_certificate_material_t material;
        CHECK(crux_tls_certificate_material_take(&material) != CRUX_TLS_CERTIFICATE_OK);
    }
    /* A failed start can be retried. */
    fake_store_ensure_result(CRUX_CREDENTIAL_OK);
    fake_store_read_result(CRUX_CREDENTIAL_RECORD_TLS_KEY, CRUX_CREDENTIAL_ERR_READ);
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_ERR_STORE);
    fake_store_read_result(CRUX_CREDENTIAL_RECORD_TLS_KEY, CRUX_CREDENTIAL_OK);
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(how == CRUX_TLS_CERTIFICATE_GENERATED);
}

static void test_not_a_p256_key(void)
{
    fixture_boot();
    fake_store_key_present(true, CRUX_CREDENTIAL_KEY_ED25519);
    crux_tls_certificate_start_t how;
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_ERR_INVALID);
    CHECK(fake_store_write_count() == 0u);
}

static void test_first_save_fails(void)
{
    fixture_boot();
    fake_store_write_result(CRUX_CREDENTIAL_ERR_STORE);
    crux_tls_certificate_start_t how;
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_ERR_STORE);
    crux_tls_certificate_material_t material;
    CHECK(crux_tls_certificate_material_take(&material) == CRUX_TLS_CERTIFICATE_ERR_NOT_READY);
    CHECK(fake_events_count() == 0u);
    fake_store_write_result(CRUX_CREDENTIAL_OK);
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_OK);
    fake_ports_reset();
    fake_ports_sign_fail(true);
    crux_tls_certificate_test_reset();
    fake_store_reset();
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_ERR_CRYPTO);
    fake_ports_sign_fail(false);
    fake_ports_random_fail(true);
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_ERR_CRYPTO);
    CHECK(fake_store_write_count() == 0u);
}

/* A stored record that cannot be served: refused, logged, never replaced, and recoverable. */
static void stored_refused(crux_tls_certificate_result_t expected, const uint8_t *record,
                           size_t length)
{
    fixture_boot();
    fake_store_set_certificate(record, length);
    crux_tls_certificate_start_t how;
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == expected);
    CHECK(fake_store_write_count() == 0u);
    size_t stored_length = 0;
    const uint8_t *stored = fake_store_certificate(&stored_length);
    CHECK(stored_length == length && memcmp(stored, record, length) == 0);
    CHECK(fake_events_count() == 1u);
    const crux_tls_certificate_event_t *event = fake_event(0);
    CHECK(event->type == CRUX_TLS_CERTIFICATE_EVENT_REJECTED);
    CHECK(event->reason == CRUX_TLS_CERTIFICATE_REASON_STORED);

    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_admin, &info) == CRUX_TLS_CERTIFICATE_ERR_NOT_READY);
    crux_tls_certificate_material_t material;
    CHECK(crux_tls_certificate_material_take(&material) == CRUX_TLS_CERTIFICATE_ERR_NOT_READY);
    char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    CHECK(crux_tls_certificate_fingerprint(text) == CRUX_TLS_CERTIFICATE_ERR_NOT_READY);
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_ERR_BUSY);
    /* A request still works: it needs the key alone. */
    uint8_t request[CRUX_TLS_CERTIFICATE_CSR_MAX];
    size_t request_length = 0;
    CHECK(crux_tls_certificate_request(s_admin, NULL, request, sizeof(request), &request_length) ==
          CRUX_TLS_CERTIFICATE_OK);
}

static void test_stored_other_key(void)
{
    uint8_t record[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX];
    size_t length = record_of(CRUX_TLS_CERTIFICATE_SOURCE_INSTALLED, fixture_other,
                              sizeof(fixture_other), record);
    stored_refused(CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH, record, length);
    /* An operator installs a matching one. */
    CHECK(crux_tls_certificate_install(s_admin, fixture_ec_self, sizeof(fixture_ec_self), NULL) ==
          CRUX_TLS_CERTIFICATE_OK);
    crux_tls_certificate_material_t material;
    CHECK(crux_tls_certificate_material_take(&material) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(material.certificate_length == sizeof(fixture_ec_self));
}

static void test_stored_malformed(void)
{
    uint8_t record[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX];
    size_t length = record_of(CRUX_TLS_CERTIFICATE_SOURCE_INSTALLED, fixture_ec_self,
                              sizeof(fixture_ec_self) - 3u, record);
    stored_refused(CRUX_TLS_CERTIFICATE_ERR_MALFORMED, record, length);
    /* An operator regenerates a self-signed one. */
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_regenerate(s_admin, NULL, &info) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(info.source == CRUX_TLS_CERTIFICATE_SOURCE_SELF_SIGNED);
}

static uint8_t header_record[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX];
static size_t header_record_length;

static void stored_header_case(void)
{
    stored_refused(CRUX_TLS_CERTIFICATE_ERR_INVALID, header_record, header_record_length);
}

static void run_header_cases(void)
{
    header_record_length = record_of(CRUX_TLS_CERTIFICATE_SOURCE_INSTALLED, fixture_ec_self,
                                     sizeof(fixture_ec_self), header_record);
    const size_t positions[] = {0, 3, 4, 5, 6, 7};
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        header_record[positions[i]] ^= 0x40u;
        tls_test_case("stored record with a bad header byte", stored_header_case);
        header_record[positions[i]] ^= 0x40u;
    }
    header_record_length = 8;
    tls_test_case("stored record of a header alone", stored_header_case);
}

static void test_store_read_failure_at_start(void)
{
    fixture_start();
    fake_store_read_result(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, CRUX_CREDENTIAL_ERR_READ);
    crux_tls_certificate_start_t how;
    CHECK(fixture_restart(&how) == CRUX_TLS_CERTIFICATE_ERR_STORE);
    /* Absence is unproven, so nothing is generated or written. */
    CHECK(fake_store_write_count() == 1u);
    CHECK(fake_events_count() == 0u);
    fake_store_read_result(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, CRUX_CREDENTIAL_OK);
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(how == CRUX_TLS_CERTIFICATE_STARTED);
}

static void test_permissions(void)
{
    fixture_start();
    crux_tls_certificate_info_t info;
    uint8_t buffer[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = 0;
    crux_access_session_t sessions[] = {s_viewer, {0}};
    crux_tls_certificate_result_t expected[] = {CRUX_TLS_CERTIFICATE_ERR_DENIED,
                                                CRUX_TLS_CERTIFICATE_ERR_SESSION_INVALID};
    for (size_t i = 0; i < 2u; ++i) {
        CHECK(crux_tls_certificate_get(sessions[i], &info) == expected[i]);
        CHECK(crux_tls_certificate_copy(sessions[i], buffer, sizeof(buffer), &length) ==
              expected[i]);
        CHECK(crux_tls_certificate_request(sessions[i], NULL, buffer, sizeof(buffer), &length) ==
              expected[i]);
        CHECK(crux_tls_certificate_install(sessions[i], fixture_ec_self, sizeof(fixture_ec_self),
                                           NULL) == expected[i]);
        CHECK(crux_tls_certificate_regenerate(sessions[i], NULL, NULL) == expected[i]);
    }
    CHECK(fake_events_count() == 1u);
    CHECK(fake_store_write_count() == 1u);
    CHECK(fake_ports_sign_count() == 1u);
    crux_access_session_t ended = s_operator;
    crux_access_logout(&ended);
    CHECK(crux_tls_certificate_get(s_operator, &info) == CRUX_TLS_CERTIFICATE_ERR_SESSION_INVALID);
}

static void test_not_started(void)
{
    fixture_boot();
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_viewer, &info) == CRUX_TLS_CERTIFICATE_ERR_DENIED);
    CHECK(crux_tls_certificate_get(s_admin, &info) == CRUX_TLS_CERTIFICATE_ERR_NOT_READY);
    CHECK(crux_tls_certificate_install(s_admin, fixture_ec_self, sizeof(fixture_ec_self), NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_NOT_READY);
    crux_tls_certificate_material_t material;
    CHECK(crux_tls_certificate_material_take(&material) == CRUX_TLS_CERTIFICATE_ERR_NOT_READY);
    char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    CHECK(crux_tls_certificate_fingerprint(text) == CRUX_TLS_CERTIFICATE_ERR_NOT_READY);
    CHECK(crux_tls_certificate_get(s_admin, NULL) == CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
}

static void test_copy(void)
{
    fixture_start();
    size_t length = 0;
    CHECK(crux_tls_certificate_copy(s_admin, NULL, 0, &length) ==
          CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE);
    CHECK(length > 0u);
    uint8_t small[16];
    size_t reported = 0;
    CHECK(crux_tls_certificate_copy(s_admin, small, sizeof(small), &reported) ==
          CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE);
    CHECK(reported == length);
    CHECK(crux_tls_certificate_copy(s_admin, NULL, 1, &reported) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    CHECK(crux_tls_certificate_copy(s_admin, small, sizeof(small), NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
}

static void test_request(void)
{
    fixture_start();
    size_t events = fake_events_count();
    crux_tls_certificate_info_t before;
    CHECK(crux_tls_certificate_get(s_admin, &before) == CRUX_TLS_CERTIFICATE_OK);
    uint8_t request[CRUX_TLS_CERTIFICATE_CSR_MAX];
    size_t length = 0;
    CHECK(crux_tls_certificate_request(s_operator, NULL, request, sizeof(request), &length) ==
          CRUX_TLS_CERTIFICATE_OK);
    CHECK(tls_test_contains(request, length, fixture_device_key, 65));
    static const uint8_t start_dns[] = {0x82, 10, 'c', 'r', 'u', 'x', '.', 'l', 'o', 'c', 'a', 'l'};
    CHECK(tls_test_contains(request, length, start_dns, sizeof(start_dns)));
    CHECK(memcmp(fake_ports_last_sign_key(), fake_device_scalar, 32) == 0);
    /* Exactly the size, as a caller may retry with it. */
    uint8_t exact[CRUX_TLS_CERTIFICATE_CSR_MAX];
    size_t exact_length = 0;
    CHECK(crux_tls_certificate_request(s_operator, NULL, exact, length, &exact_length) ==
          CRUX_TLS_CERTIFICATE_OK);
    CHECK(exact_length == length);
    CHECK(crux_tls_certificate_request(s_operator, NULL, exact, length - 1u, &exact_length) ==
          CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE);
    CHECK(exact_length == 0u);

    const char *const names[] = {"device.example"};
    crux_tls_certificate_names_t other = {"device.example", names, 1, NULL, 0, NULL, 0};
    CHECK(crux_tls_certificate_request(s_operator, &other, request, sizeof(request), &length) ==
          CRUX_TLS_CERTIFICATE_OK);
    CHECK(!tls_test_contains(request, length, start_dns, sizeof(start_dns)));
    other.common_name = "bad/name";
    CHECK(crux_tls_certificate_request(s_operator, &other, request, sizeof(request), &length) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    fake_ports_sign_fail(true);
    CHECK(crux_tls_certificate_request(s_operator, NULL, request, sizeof(request), &length) ==
          CRUX_TLS_CERTIFICATE_ERR_CRYPTO);
    fake_ports_sign_fail(false);
    fake_ports_hash_fail(true);
    CHECK(crux_tls_certificate_request(s_operator, NULL, request, sizeof(request), &length) ==
          CRUX_TLS_CERTIFICATE_ERR_CRYPTO);
    fake_ports_hash_fail(false);
    CHECK(crux_tls_certificate_request(s_operator, NULL, NULL, sizeof(request), &length) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);

    /* A request changes nothing and logs nothing. */
    crux_tls_certificate_info_t after;
    CHECK(crux_tls_certificate_get(s_admin, &after) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(memcmp(before.fingerprint, after.fingerprint, 32) == 0);
    CHECK(fake_events_count() == events);
    CHECK(fake_store_write_count() == 1u);
}

static void test_regenerate(void)
{
    fixture_start();
    crux_tls_certificate_info_t first, second;
    CHECK(crux_tls_certificate_get(s_admin, &first) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(crux_tls_certificate_install(s_operator, fixture_ec_self, sizeof(fixture_ec_self),
                                       NULL) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(crux_tls_certificate_regenerate(s_operator, NULL, &second) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(second.source == CRUX_TLS_CERTIFICATE_SOURCE_SELF_SIGNED);
    CHECK(memcmp(first.fingerprint, second.fingerprint, 32) != 0); /* A new serial. */
    const crux_tls_certificate_event_t *event = fake_event_last();
    CHECK(event->type == CRUX_TLS_CERTIFICATE_EVENT_REPLACED);
    CHECK(event->reason == CRUX_TLS_CERTIFICATE_REASON_REGENERATED);
    CHECK(strcmp(event->user, "operator") == 0);

    /* After an address change, with new names. */
    static const uint8_t new_address[][4] = {{198, 51, 100, 7}};
    crux_tls_certificate_names_t moved = {"Crux device", NULL, 0, new_address, 1, NULL, 0};
    CHECK(crux_tls_certificate_regenerate(s_operator, &moved, &second) == CRUX_TLS_CERTIFICATE_OK);
    uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = active_der(der);
    static const uint8_t san_ip[] = {0x87, 0x04, 198, 51, 100, 7};
    CHECK(tls_test_contains(der, length, san_ip, sizeof(san_ip)));

    size_t writes = fake_store_write_count();
    moved.common_name = "";
    CHECK(crux_tls_certificate_regenerate(s_operator, &moved, NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    fake_ports_random_fail(true);
    CHECK(crux_tls_certificate_regenerate(s_operator, NULL, NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_CRYPTO);
    fake_ports_random_fail(false);
    fake_ports_sign_fail(true);
    CHECK(crux_tls_certificate_regenerate(s_operator, NULL, NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_CRYPTO);
    fake_ports_sign_fail(false);
    CHECK(fake_store_write_count() == writes);
    uint8_t still[CRUX_TLS_CERTIFICATE_DER_MAX];
    CHECK(active_der(still) == length && memcmp(still, der, length) == 0);
    fake_store_write_result(CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_tls_certificate_regenerate(s_operator, NULL, NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_STORE);
    CHECK(fake_event_last()->failed);
    CHECK(active_der(still) == length && memcmp(still, der, length) == 0);
}

static unsigned changes;
static crux_tls_certificate_material_t held;

static void on_change(void *context)
{
    CHECK(context == &changes);
    changes += 1u;
    /* A TLS provider re-registers: take the new, then give back the old. */
    crux_tls_certificate_material_t fresh;
    CHECK(crux_tls_certificate_material_take(&fresh) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(fresh.generation != held.generation);
    CHECK(crux_tls_certificate_material_release(&held) == CRUX_TLS_CERTIFICATE_OK);
    held = fresh;
}

static void test_material(void)
{
    fixture_boot();
    crux_tls_certificate_on_change(on_change, &changes);
    crux_tls_certificate_start_t how;
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(changes == 0u); /* Start does not call it: the provider takes material after start. */
    CHECK(crux_tls_certificate_material_take(&held) == CRUX_TLS_CERTIFICATE_OK);
    uint8_t expected_key[CRUX_TLS_CERTIFICATE_KEY_DER_BYTES];
    size_t key_length = 0;
    CHECK(tls_x509_build_pkcs8(fake_device_scalar, fixture_device_key, expected_key,
                               sizeof(expected_key), &key_length) == TLS_X509_BUILT);
    CHECK(held.private_key_length == key_length);
    CHECK(memcmp(held.private_key, expected_key, key_length) == 0);
    uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = active_der(der);
    CHECK(held.certificate_length == length && memcmp(held.certificate, der, length) == 0);

    CHECK(crux_tls_certificate_install(s_operator, fixture_ec_self, sizeof(fixture_ec_self),
                                       NULL) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(changes == 1u);
    CHECK(held.certificate_length == sizeof(fixture_ec_self));
    CHECK(memcmp(held.certificate, fixture_ec_self, sizeof(fixture_ec_self)) == 0);
    /* A refusal is not a change. */
    CHECK(crux_tls_certificate_install(s_operator, fixture_other, sizeof(fixture_other), NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH);
    CHECK(changes == 1u);
    CHECK(crux_tls_certificate_regenerate(s_operator, NULL, NULL) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(changes == 2u);
}

static void test_material_held_blocks_overwrite(void)
{
    fixture_start();
    crux_tls_certificate_material_t old_material, new_material;
    CHECK(crux_tls_certificate_material_take(&old_material) == CRUX_TLS_CERTIFICATE_OK);
    uint8_t old_copy[CRUX_TLS_CERTIFICATE_DER_MAX];
    memcpy(old_copy, old_material.certificate, old_material.certificate_length);

    CHECK(crux_tls_certificate_install(s_operator, fixture_ec_self, sizeof(fixture_ec_self),
                                       NULL) == CRUX_TLS_CERTIFICATE_OK);
    /* The old material is unchanged while held. */
    CHECK(memcmp(old_material.certificate, old_copy, old_material.certificate_length) == 0);
    CHECK(crux_tls_certificate_material_take(&new_material) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(new_material.certificate != old_material.certificate);
    /* Both buffers are held, so another change would overwrite one in use. */
    size_t events = fake_events_count();
    CHECK(crux_tls_certificate_install(s_operator, fixture_far, sizeof(fixture_far), NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_BUSY);
    CHECK(crux_tls_certificate_regenerate(s_operator, NULL, NULL) == CRUX_TLS_CERTIFICATE_ERR_BUSY);
    CHECK(fake_events_count() == events);
    CHECK(memcmp(old_material.certificate, old_copy, old_material.certificate_length) == 0);
    CHECK(crux_tls_certificate_material_release(&old_material) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(crux_tls_certificate_material_release(&old_material) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    CHECK(crux_tls_certificate_install(s_operator, fixture_far, sizeof(fixture_far), NULL) ==
          CRUX_TLS_CERTIFICATE_OK);
    /* The newer material, still held, is unchanged too. */
    CHECK(memcmp(new_material.certificate, fixture_ec_self, sizeof(fixture_ec_self)) == 0);
    crux_tls_certificate_material_t stranger = new_material;
    stranger.certificate = old_copy;
    CHECK(crux_tls_certificate_material_release(&stranger) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    CHECK(crux_tls_certificate_material_release(NULL) == CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
    CHECK(crux_tls_certificate_material_take(NULL) == CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT);
}

static void during_save(void)
{
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_admin, &info) == CRUX_TLS_CERTIFICATE_ERR_BUSY);
    char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    CHECK(crux_tls_certificate_fingerprint(text) == CRUX_TLS_CERTIFICATE_ERR_BUSY);
    crux_tls_certificate_material_t material;
    CHECK(crux_tls_certificate_material_take(&material) == CRUX_TLS_CERTIFICATE_ERR_BUSY);
    CHECK(crux_tls_certificate_install(s_admin, fixture_ec_self, sizeof(fixture_ec_self), NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_BUSY);
}

static unsigned hook_events;

static void during_event(const crux_tls_certificate_event_t *event)
{
    (void)event;
    hook_events += 1u;
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_admin, &info) == CRUX_TLS_CERTIFICATE_ERR_BUSY);
}

static void test_contention(void)
{
    fixture_start();
    fake_store_write_hook(during_save);
    CHECK(crux_tls_certificate_install(s_operator, fixture_ec_self, sizeof(fixture_ec_self),
                                       NULL) == CRUX_TLS_CERTIFICATE_OK);
    fake_store_write_hook(NULL);
    fake_events_hook(during_event);
    CHECK(crux_tls_certificate_install(s_operator, fixture_other, sizeof(fixture_other), NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH);
    CHECK(hook_events == 1u);
    fake_events_hook(NULL);
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_admin, &info) == CRUX_TLS_CERTIFICATE_OK);
}

/* The acceptance criterion: no output holds the private key. */
static void test_private_key_in_no_output(void)
{
    fixture_start();
    const uint8_t *scalar = fake_device_scalar;
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(s_admin, &info) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(!tls_test_contains((const uint8_t *)&info, sizeof(info), scalar, 32));
    uint8_t buffer[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t length = active_der(buffer);
    CHECK(!tls_test_contains(buffer, length, scalar, 32));
    CHECK(crux_tls_certificate_request(s_admin, NULL, buffer, sizeof(buffer), &length) ==
          CRUX_TLS_CERTIFICATE_OK);
    CHECK(!tls_test_contains(buffer, length, scalar, 32));
    CHECK(crux_tls_certificate_install(s_admin, fixture_ec_self, sizeof(fixture_ec_self), &info) ==
          CRUX_TLS_CERTIFICATE_OK);
    CHECK(!tls_test_contains((const uint8_t *)&info, sizeof(info), scalar, 32));
    CHECK(crux_tls_certificate_regenerate(s_admin, NULL, &info) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(!tls_test_contains((const uint8_t *)&info, sizeof(info), scalar, 32));
    for (size_t i = 0; i < fake_events_count(); ++i) {
        CHECK(
            !tls_test_contains((const uint8_t *)fake_event(i), sizeof(*fake_event(i)), scalar, 32));
    }
    /* In RAM, only the PKCS #8 copy the TLS stack points to holds it. */
    CHECK(!crux_tls_certificate_test_ram_holds(scalar, 32));
    crux_tls_certificate_material_t material;
    CHECK(crux_tls_certificate_material_take(&material) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(tls_test_contains(material.private_key, material.private_key_length, scalar, 32));
}

static void test_dates_at(void)
{
    crux_tls_certificate_info_t info;
    memset(&info, 0, sizeof(info));
    info.not_before_s = 1000;
    info.not_after_s = 2000;
    CHECK(crux_tls_certificate_dates_at(&info, false, 1500) == CRUX_TLS_CERTIFICATE_DATES_UNKNOWN);
    CHECK(crux_tls_certificate_dates_at(NULL, true, 1500) == CRUX_TLS_CERTIFICATE_DATES_UNKNOWN);
    CHECK(crux_tls_certificate_dates_at(&info, true, 999) ==
          CRUX_TLS_CERTIFICATE_DATES_NOT_YET_VALID);
    CHECK(crux_tls_certificate_dates_at(&info, true, 1000) == CRUX_TLS_CERTIFICATE_DATES_VALID);
    CHECK(crux_tls_certificate_dates_at(&info, true, 2000) == CRUX_TLS_CERTIFICATE_DATES_VALID);
    CHECK(crux_tls_certificate_dates_at(&info, true, 2001) == CRUX_TLS_CERTIFICATE_DATES_EXPIRED);
    info.no_expiry = true;
    info.not_after_s = CRUX_TLS_CERTIFICATE_TIME_MAX_S;
    CHECK(crux_tls_certificate_dates_at(&info, true, CRUX_TLS_CERTIFICATE_TIME_MAX_S + 1) ==
          CRUX_TLS_CERTIFICATE_DATES_VALID);
}

int main(void)
{
    tls_test_case("first boot", test_first_boot);
    tls_test_case("later boot loads", test_later_boot_loads);
    tls_test_case("start arguments", test_start_arguments);
    tls_test_case("dated options", test_dated_options);
    tls_test_case("key failures", test_key_failures);
    tls_test_case("not a P-256 key", test_not_a_p256_key);
    tls_test_case("first save fails", test_first_save_fails);
    tls_test_case("stored other key", test_stored_other_key);
    tls_test_case("stored malformed", test_stored_malformed);
    run_header_cases();
    tls_test_case("store read failure at start", test_store_read_failure_at_start);
    tls_test_case("permissions", test_permissions);
    tls_test_case("not started", test_not_started);
    tls_test_case("copy", test_copy);
    tls_test_case("request", test_request);
    tls_test_case("regenerate", test_regenerate);
    tls_test_case("material", test_material);
    tls_test_case("material held blocks overwrite", test_material_held_blocks_overwrite);
    tls_test_case("contention", test_contention);
    tls_test_case("private key in no output", test_private_key_in_no_output);
    tls_test_case("dates at", test_dates_at);
    return tls_test_finish("tls_certificate_service");
}
