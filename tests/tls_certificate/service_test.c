#include "service_test.h"

#include <string.h>

crux_access_session_t s_admin, s_operator, s_viewer;

static const char *const admin_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE,
                                                CRUX_TLS_CERTIFICATE_PERMISSION_MANAGE};
static const char *const operator_permissions[] = {CRUX_TLS_CERTIFICATE_PERMISSION_MANAGE};
static const char *const viewer_permissions[] = {"status.read"};
static const crux_access_role_t roles[] = {
    {"administrator", admin_permissions, 2},
    {"operator", operator_permissions, 1},
    {"viewer", viewer_permissions, 1},
};
static const crux_access_policy_t policy = {roles, 3, 15u * 60u, 8u * 3600u, NULL};

static const char *const fixture_dns[] = {"crux.local"};
static const uint8_t fixture_ipv4[][4] = {{192, 0, 2, 10}};
const crux_tls_certificate_options_t fixture_options = {
    {"Crux device", fixture_dns, 1, fixture_ipv4, 1, NULL, 0},
    CRUX_TLS_CERTIFICATE_DEFAULT_NOT_BEFORE_S,
    0};

static const crux_access_origin_t serial_origin = {CRUX_ACCESS_CHANNEL_SERIAL, {0}, 0};

void fixture_boot(void)
{
    fake_ports_reset();
    fake_store_reset();
    access_test_boot(&policy);
    CHECK(crux_access_provision_first_account("admin", FIXTURE_PASSWORD, strlen(FIXTURE_PASSWORD),
                                              "administrator", &serial_origin) == CRUX_ACCESS_OK);
    s_admin = access_test_session("admin", FIXTURE_PASSWORD, &access_test_web);
    CHECK(crux_access_account_create(s_admin, "operator", "operator", FIXTURE_PASSWORD,
                                     strlen(FIXTURE_PASSWORD)) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(s_admin, "viewer", "viewer", FIXTURE_PASSWORD,
                                     strlen(FIXTURE_PASSWORD)) == CRUX_ACCESS_OK);
    s_operator = access_test_session("operator", FIXTURE_PASSWORD, &access_test_web);
    s_viewer = access_test_session("viewer", FIXTURE_PASSWORD, &access_test_web);
    CHECK(access_test_failures == 0u);
}

void fixture_start(void)
{
    fixture_boot();
    crux_tls_certificate_start_t how = CRUX_TLS_CERTIFICATE_STARTED;
    CHECK(crux_tls_certificate_start(&fixture_options, &how) == CRUX_TLS_CERTIFICATE_OK);
    CHECK(how == CRUX_TLS_CERTIFICATE_GENERATED);
}

crux_tls_certificate_result_t fixture_restart(crux_tls_certificate_start_t *how)
{
    crux_tls_certificate_test_reset();
    fake_ports_reset();
    return crux_tls_certificate_start(&fixture_options, how);
}

size_t active_der(uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX])
{
    size_t length = 0;
    CHECK(crux_tls_certificate_copy(s_admin, der, CRUX_TLS_CERTIFICATE_DER_MAX, &length) ==
          CRUX_TLS_CERTIFICATE_OK);
    return length;
}

size_t record_of(const uint8_t *der, size_t length, crux_tls_certificate_source_t source,
                 uint8_t *record)
{
    static const uint8_t header[] = {'C', 'X', 'T', 'C', 1, 0, 0, 0};
    memcpy(record, header, sizeof(header));
    record[5] = (uint8_t)source;
    memcpy(record + sizeof(header), der, length);
    return sizeof(header) + length;
}
