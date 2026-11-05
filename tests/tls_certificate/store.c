/*
 * The module on the real credential store, over the fake File provider, with the store's test-only
 * protection: first boot, a power cycle, a refused upload that leaves the stored record alone, an
 * installed certificate across a power cycle, and an unusable store.
 */
#include "fakes.h"
#include "fixtures.h"
#include "tls_test.h"

#include <crux/tls_certificate.h>
#include <crux_access_adapter.h>
#include <crux_tls_certificate_adapter.h>

#include <string.h>

/* From the instrumented cores and the suites' helpers that this one links. */
void crux_tls_certificate_test_reset(void);
void test_fresh(void);
crux_credential_result_t test_boot(void);
extern unsigned access_test_failures, credential_test_failures;
extern const crux_access_origin_t access_test_web;
void access_test_boot(const crux_access_policy_t *policy);
crux_access_session_t access_test_session(const char *user, const char *password,
                                          const crux_access_origin_t *origin);

#define PASSWORD "correct horse battery"

static const char *const admin_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE,
                                                CRUX_TLS_CERTIFICATE_PERMISSION_MANAGE};
static const crux_access_role_t roles[] = {{"administrator", admin_permissions, 2}};
static const crux_access_policy_t policy = {roles, 1, 15u * 60u, 8u * 3600u, NULL};
static const crux_tls_certificate_options_t options = {
    {"Crux device", NULL, 0, NULL, 0, NULL, 0}, CRUX_TLS_CERTIFICATE_DEFAULT_NOT_BEFORE_S, 0};
static crux_access_session_t admin;

static void boot_access(void)
{
    static const crux_access_origin_t serial = {CRUX_ACCESS_CHANNEL_SERIAL, {0}, 0};
    access_test_boot(&policy);
    CHECK(crux_access_provision_first_account("admin", PASSWORD, strlen(PASSWORD),
                                              "administrator", &serial) == CRUX_ACCESS_OK);
    admin = access_test_session("admin", PASSWORD, &access_test_web);
}

/* A blank medium holding only the fixture's device key, so that uploads can match it. */
static void fresh_medium(void)
{
    fake_ports_reset();
    test_fresh();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    uint8_t record[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX] = {CRUX_CREDENTIAL_KEY_ECDSA_P256, 65, 32,
                                                            0};
    memcpy(record + 4, fixture_device_key, 65);
    memcpy(record + 4 + 65, fake_device_scalar, 32);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_KEY, record, sizeof(record)) ==
          CRUX_CREDENTIAL_OK);
    boot_access();
}

static void power_cycle(void)
{
    crux_tls_certificate_test_reset();
    fake_ports_reset();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
}

static size_t stored(uint8_t *record)
{
    size_t length = 0;
    CHECK(crux_credential_read(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, record,
                               CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX, &length) ==
          CRUX_CREDENTIAL_OK);
    return length;
}

static void test_first_boot_and_power_cycle(void)
{
    fresh_medium();
    crux_tls_certificate_start_t how;
    CHECK(crux_tls_certificate_start(&options, &how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(how == CRUX_TLS_CERTIFICATE_GENERATED);
    crux_tls_certificate_info_t first;
    CHECK(crux_tls_certificate_get(admin, &first) == CRUX_TLS_CERTIFICATE_OK);
    uint8_t record[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX];
    size_t length = stored(record);
    CHECK(length == 8u + first.length);
    CHECK(memcmp(record, "CXTC\x01\x01\x00\x00", 8) == 0);

    power_cycle();
    CHECK(crux_tls_certificate_start(&options, &how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(how == CRUX_TLS_CERTIFICATE_STARTED);
    crux_tls_certificate_info_t second;
    CHECK(crux_tls_certificate_get(admin, &second) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(memcmp(first.fingerprint, second.fingerprint, 32) == 0);
    CHECK(fake_ports_sign_count() == 0u);
}

static void test_refused_upload_leaves_the_record(void)
{
    fresh_medium();
    crux_tls_certificate_start_t how;
    CHECK(crux_tls_certificate_start(&options, &how) == CRUX_TLS_CERTIFICATE_OK);
    uint8_t before[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX];
    size_t before_length = stored(before);
    CHECK(crux_tls_certificate_install(admin, fixture_other, sizeof(fixture_other), NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH);
    CHECK(crux_tls_certificate_install(admin, fixture_p384, sizeof(fixture_p384), NULL) ==
          CRUX_TLS_CERTIFICATE_ERR_INVALID);
    CHECK(crux_tls_certificate_install(admin, fixture_ec_self_pem, sizeof(fixture_ec_self_pem),
                                       NULL) == CRUX_TLS_CERTIFICATE_ERR_MALFORMED);
    uint8_t after[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX];
    CHECK(stored(after) == before_length && memcmp(after, before, before_length) == 0);
    CHECK(fake_events_count() == 4u);
}

static void test_installed_across_power_cycle(void)
{
    fresh_medium();
    crux_tls_certificate_start_t how;
    CHECK(crux_tls_certificate_start(&options, &how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(crux_tls_certificate_install(admin, fixture_ec_by_rsa, sizeof(fixture_ec_by_rsa), NULL) ==
          CRUX_TLS_CERTIFICATE_OK);
    power_cycle();
    CHECK(crux_tls_certificate_start(&options, &how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(how == CRUX_TLS_CERTIFICATE_STARTED);
    crux_tls_certificate_info_t info;
    CHECK(crux_tls_certificate_get(admin, &info) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(info.source == CRUX_TLS_CERTIFICATE_SOURCE_INSTALLED);
    CHECK(strcmp(info.issuer, "Example Test CA") == 0);
}

static void test_unusable_store(void)
{
    fresh_medium();
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    crux_tls_certificate_start_t how;
    CHECK(crux_tls_certificate_start(&options, &how) == CRUX_TLS_CERTIFICATE_ERR_NOT_READY);
    crux_tls_certificate_material_t material;
    CHECK(crux_tls_certificate_material_take(&material) == CRUX_TLS_CERTIFICATE_ERR_NOT_READY);
}

static void test_generated_key(void)
{
    /* With no key stored, the store generates one. Its fake generator does not make a point on the
     * curve, so the certificate it would certify is refused as invalid: the module never serves a
     * certificate whose key it could not check. */
    fake_ports_reset();
    test_fresh();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    boot_access();
    crux_tls_certificate_start_t how;
    CHECK(crux_tls_certificate_start(&options, &how) == CRUX_TLS_CERTIFICATE_ERR_INVALID);
    uint8_t point[CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX];
    size_t length = 0;
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_TLS_KEY, point, sizeof(point),
                                     &length) == CRUX_CREDENTIAL_OK);
    CHECK(length == 65u);
    uint8_t record[16];
    CHECK(crux_credential_read(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, record, sizeof(record),
                               &length) == CRUX_CREDENTIAL_ERR_NOT_FOUND);
}

static void finish_case(void)
{
    tls_test_failures += access_test_failures + credential_test_failures;
}

#define CASE(body)                                                                                 \
    static void case_##body(void)                                                                  \
    {                                                                                              \
        body();                                                                                    \
        finish_case();                                                                             \
    }

CASE(test_first_boot_and_power_cycle)
CASE(test_refused_upload_leaves_the_record)
CASE(test_installed_across_power_cycle)
CASE(test_unusable_store)
CASE(test_generated_key)

int main(void)
{
    tls_test_case("first boot and power cycle", case_test_first_boot_and_power_cycle);
    tls_test_case("refused upload leaves the record", case_test_refused_upload_leaves_the_record);
    tls_test_case("installed across power cycle", case_test_installed_across_power_cycle);
    tls_test_case("unusable store", case_test_unusable_store);
    tls_test_case("generated key", case_test_generated_key);
    return tls_test_finish("tls_certificate_store");
}
