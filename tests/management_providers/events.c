/* The event providers and the status sources over the public contracts they call: the fake of the
 * Security event contract, a real Access session and a stand-in for the store's public getter. */
#include "config_service_port.h"
#include "credential_port.h"
#include "mgmt_status_port.h"
#include "network_settings_port.h"
#include "reboot_port.h"

#include "access_test.h"

#include <crux/security_event.h>
#include <security_event_fake.h>

#include <stdio.h>
#include <string.h>

static crux_access_session_t web_session(void)
{
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    access_fake_reset();
    crux_access_test_reset();
    CHECK(crux_access_init(&access_test_profile_reference.policy) == CRUX_ACCESS_OK);
    CHECK(crux_access_provision_first_account(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD,
                                              strlen(ACCESS_TEST_PASSWORD),
                                              access_test_profile_reference.administrator_role,
                                              &access_test_serial) == CRUX_ACCESS_OK);
    CHECK(crux_access_login_password(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD,
                                     strlen(ACCESS_TEST_PASSWORD), &access_test_web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_OK);
    return session;
}

static void config_events(void)
{
    static const struct {
        crux_config_service_event_t event;
        crux_security_event_id_t id;
        crux_security_event_result_t result;
    } rows[] = {
        {CRUX_CONFIG_SERVICE_EVENT_UPDATED, CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED,
         CRUX_SECURITY_EVENT_OK},
        {CRUX_CONFIG_SERVICE_EVENT_REJECTED, CRUX_SECURITY_EVENT_ID_CONFIG_REJECTED,
         CRUX_SECURITY_EVENT_INVALID},
        {CRUX_CONFIG_SERVICE_EVENT_SAVED, CRUX_SECURITY_EVENT_ID_CONFIG_SAVED,
         CRUX_SECURITY_EVENT_OK},
        {CRUX_CONFIG_SERVICE_EVENT_SAVE_FAILED, CRUX_SECURITY_EVENT_ID_CONFIG_SAVE_FAILED,
         CRUX_SECURITY_EVENT_FAILED},
    };
    crux_access_session_t session = web_session();
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        security_event_fake_reset();
        security_event_fake_boot();
        crux_config_service_port_event(rows[index].event, session, "net.hostname");
        CHECK(security_event_fake_count_of(rows[index].id) == 1u);
        const crux_security_event_record_t *record = security_event_fake_last(rows[index].id);
        CHECK(record != NULL);
        if (record != NULL) {
            CHECK(record->result == rows[index].result);
            CHECK(strcmp(record->subject, "net.hostname") == 0);
            CHECK(strcmp(record->user, ACCESS_TEST_ADMIN) == 0);
            CHECK(record->channel == access_test_web.channel);
            CHECK(record->source_length == access_test_web.source_length);
            CHECK(memcmp(record->source, access_test_web.source, access_test_web.source_length) ==
                  0);
        }
    }

    /* A session that has ended still leaves the record, naming nobody. */
    security_event_fake_reset();
    security_event_fake_boot();
    const crux_access_session_t ended = {0};
    crux_config_service_port_event(CRUX_CONFIG_SERVICE_EVENT_SAVED, ended, "net.hostname");
    const crux_security_event_record_t *record =
        security_event_fake_last(CRUX_SECURITY_EVENT_ID_CONFIG_SAVED);
    CHECK(record != NULL);
    if (record != NULL) {
        CHECK(record->user[0] == '\0');
        CHECK(record->channel == 0u);
        CHECK(record->source_length == 0u);
        CHECK(strcmp(record->subject, "net.hostname") == 0);
    }
}

static void network_events(void)
{
    static const struct {
        crux_network_settings_event_t event;
        crux_security_event_id_t id;
        crux_security_event_result_t result;
    } rows[] = {
        {CRUX_NETWORK_SETTINGS_EVENT_CHANGED, CRUX_SECURITY_EVENT_ID_NETWORK_CHANGED,
         CRUX_SECURITY_EVENT_OK},
        {CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED, CRUX_SECURITY_EVENT_ID_NETWORK_SAVE_FAILED,
         CRUX_SECURITY_EVENT_FAILED},
        {CRUX_NETWORK_SETTINGS_EVENT_SAVED_IGNORED, CRUX_SECURITY_EVENT_ID_NETWORK_SAVED_IGNORED,
         CRUX_SECURITY_EVENT_OK},
    };
    crux_access_session_t session = web_session();
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        security_event_fake_reset();
        security_event_fake_boot();
        crux_network_settings_port_event(rows[index].event, session);
        CHECK(security_event_fake_count() == 1u);
        CHECK(security_event_fake_count_of(rows[index].id) == 1u);
        const crux_security_event_record_t *record = security_event_fake_last(rows[index].id);
        CHECK(record != NULL);
        if (record != NULL) {
            CHECK(record->id == rows[index].id);
            CHECK(record->result == rows[index].result);
            /* No subject: an event of this service names no address and no value. */
            CHECK(record->subject[0] == '\0');
            CHECK(strcmp(record->user, ACCESS_TEST_ADMIN) == 0);
            CHECK(record->channel == access_test_web.channel);
            CHECK(record->source_length == access_test_web.source_length);
            CHECK(memcmp(record->source, access_test_web.source, access_test_web.source_length) ==
                  0);
        }
    }

    /* A session that has ended still leaves the record, naming nobody. */
    security_event_fake_reset();
    security_event_fake_boot();
    const crux_access_session_t ended = {0};
    crux_network_settings_port_event(CRUX_NETWORK_SETTINGS_EVENT_CHANGED, ended);
    const crux_security_event_record_t *record =
        security_event_fake_last(CRUX_SECURITY_EVENT_ID_NETWORK_CHANGED);
    CHECK(record != NULL);
    if (record != NULL) {
        CHECK(record->user[0] == '\0');
        CHECK(record->channel == 0u);
        CHECK(record->source_length == 0u);
    }
}

static void reboot_events(void)
{
    static const struct {
        crux_reboot_event_t event;
        crux_security_event_id_t id;
        crux_security_event_result_t result;
    } rows[] = {
        {CRUX_REBOOT_EVENT_REQUESTED, CRUX_SECURITY_EVENT_ID_SYSTEM_REBOOT_REQUESTED,
         CRUX_SECURITY_EVENT_OK},
        {CRUX_REBOOT_EVENT_REFUSED, CRUX_SECURITY_EVENT_ID_SYSTEM_REBOOT_REFUSED,
         CRUX_SECURITY_EVENT_DENIED},
        {CRUX_REBOOT_EVENT_FAILED, CRUX_SECURITY_EVENT_ID_SYSTEM_REBOOT_FAILED,
         CRUX_SECURITY_EVENT_FAILED},
    };
    crux_access_session_t session = web_session();
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        security_event_fake_reset();
        security_event_fake_boot();
        crux_reboot_port_event(rows[index].event, session);
        CHECK(security_event_fake_count() == 1u);
        CHECK(security_event_fake_count_of(rows[index].id) == 1u);
        const crux_security_event_record_t *record = security_event_fake_last(rows[index].id);
        CHECK(record != NULL);
        if (record != NULL) {
            CHECK(record->id == rows[index].id);
            CHECK(record->result == rows[index].result);
            /* No subject: the application's reason is policy data, and stays out of a line. */
            CHECK(record->subject[0] == '\0');
            CHECK(strcmp(record->user, ACCESS_TEST_ADMIN) == 0);
            CHECK(record->channel == access_test_web.channel);
            CHECK(record->source_length == access_test_web.source_length);
            CHECK(memcmp(record->source, access_test_web.source, access_test_web.source_length) ==
                  0);
        }
    }

    /* A session that has ended still leaves the record, naming nobody. */
    security_event_fake_reset();
    security_event_fake_boot();
    const crux_access_session_t ended = {0};
    crux_reboot_port_event(CRUX_REBOOT_EVENT_REFUSED, ended);
    const crux_security_event_record_t *record =
        security_event_fake_last(CRUX_SECURITY_EVENT_ID_SYSTEM_REBOOT_REFUSED);
    CHECK(record != NULL);
    if (record != NULL) {
        CHECK(record->user[0] == '\0');
        CHECK(record->channel == 0u);
        CHECK(record->source_length == 0u);
    }
}

/* Whether recorded events are still waiting for Logging, from the counts of the public contract. */
static void reboot_pending_events(void)
{
    static const struct {
        uint32_t emitted, delivered, log_errors;
        bool pending;
    } rows[] = {
        {0u, 0u, 0u, false},
        {5u, 5u, 0u, false},
        {5u, 3u, 0u, true},
        {5u, 3u, 2u, false}, /* a line that failed is no longer waiting */
        {5u, 3u, 1u, true},
        {5u, 6u, 0u, false},         /* a report of lost records is a line too, and reads low */
        {2u, 0xFFFFFFFFu, 0u, true}, /* both counts wrap, emitted three ahead */
        {0u, 0xFFFFFFFEu, 0u, true}, /* emitted wrapped past 2^32, two ahead */
        {0xFFFFFFFFu, 0xFFFFFFFFu, 0u, false},
    };

    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        crux_security_event_stats_t stats = {0};

        security_event_fake_reset();
        security_event_fake_boot();
        stats.emitted = rows[index].emitted;
        stats.delivered = rows[index].delivered;
        stats.log_errors = rows[index].log_errors;
        security_event_fake_stats_set(&stats);
        if (crux_reboot_port_events_pending() != rows[index].pending) {
            printf("  row %zu: expected pending %d\n", index, rows[index].pending ? 1 : 0);
        }
        CHECK(crux_reboot_port_events_pending() == rows[index].pending);
    }
}

static void status_event_counts(void)
{
    const crux_security_event_stats_t stats = {
        .emitted = 11, .dropped = 22, .delivered = 33, .log_errors = 44, .high_water = 55};
    crux_mgmt_status_event_counts_t counts = {0};
    security_event_fake_reset();
    security_event_fake_stats_set(&stats);
    CHECK(crux_mgmt_status_port_has_security_events());
    CHECK(crux_mgmt_status_port_security_events(&counts) == CRUX_OK);
    CHECK(counts.emitted == 11u);
    CHECK(counts.dropped == 22u);
    CHECK(counts.delivered == 33u);
    CHECK(counts.log_errors == 44u);
}

static crux_status_t store_answer;
static crux_credential_store_info_t store_info;

crux_status_t crux_credential_store_get_info(crux_credential_store_info_t *out)
{
    if (store_answer == CRUX_OK) {
        *out = store_info;
    }
    return store_answer;
}

static void status_credential_words(void)
{
    static const struct {
        crux_credential_state_t state;
        crux_credential_basis_t basis;
        crux_credential_evidence_t evidence;
        const char *state_word;
        const char *basis_word;
        const char *evidence_word;
    } rows[] = {
        {CRUX_CREDENTIAL_STATE_UNUSABLE, CRUX_CREDENTIAL_BASIS_NONE,
         CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE, "unusable", "none", "host_fake"},
        {CRUX_CREDENTIAL_STATE_EMPTY, CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY,
         CRUX_CREDENTIAL_EVIDENCE_SOFTWARE, "empty", "device_id_key", "software"},
        {CRUX_CREDENTIAL_STATE_VALID, CRUX_CREDENTIAL_BASIS_HARDWARE_BOUND,
         CRUX_CREDENTIAL_EVIDENCE_PHYSICAL, "valid", "hardware_bound", "physical"},
        {CRUX_CREDENTIAL_STATE_UNUSABLE, CRUX_CREDENTIAL_BASIS_UNDECLARED,
         CRUX_CREDENTIAL_EVIDENCE_UNDECLARED, "unusable", "undeclared", "undeclared"},
    };
    CHECK(crux_mgmt_status_port_has_credential());
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        crux_mgmt_status_credential_t words = {0};
        store_answer = CRUX_OK;
        store_info = (crux_credential_store_info_t){
            .state = rows[index].state,
            .basis = rows[index].basis,
            .evidence = rows[index].evidence,
            .confidential_against_medium_reader = index == 2u,
            .forgery_detected_without_device_id = index == 1u,
            .rollback_detected = index == 0u,
        };
        CHECK(crux_mgmt_status_port_credential(&words) == CRUX_OK);
        CHECK(strcmp(words.state, rows[index].state_word) == 0);
        CHECK(strcmp(words.basis, rows[index].basis_word) == 0);
        CHECK(strcmp(words.evidence, rows[index].evidence_word) == 0);
        CHECK(words.confidential == (index == 2u));
        CHECK(words.forgery_detected == (index == 1u));
        CHECK(words.rollback_detected == (index == 0u));
    }
    crux_mgmt_status_credential_t untouched = {0};
    store_answer = CRUX_ERR_NOT_READY;
    CHECK(crux_mgmt_status_port_credential(&untouched) == CRUX_ERR_NOT_READY);
    CHECK(untouched.state == NULL);
}

static void credential_event_row(const crux_credential_event_t *event, crux_security_event_id_t id,
                                 crux_security_event_result_t result, const char *subject)
{
    security_event_fake_reset();
    security_event_fake_boot();
    crux_credential_port_event(event);
    CHECK(security_event_fake_count() == 1u);
    const crux_security_event_record_t *record = security_event_fake_last(id);
    CHECK(record != NULL);
    if (record != NULL) {
        CHECK(record->result == result);
        CHECK(strcmp(record->subject, subject) == 0);
        /* The store acts for no session. */
        CHECK(record->user[0] == '\0');
        CHECK(record->channel == 0u);
    }
}

static void credential_events(void)
{
    static const struct {
        crux_credential_state_t state;
        crux_security_event_result_t result;
        const char *word;
    } states[] = {
        {CRUX_CREDENTIAL_STATE_UNUSABLE, CRUX_SECURITY_EVENT_FAILED, "unusable"},
        {CRUX_CREDENTIAL_STATE_EMPTY, CRUX_SECURITY_EVENT_OK, "empty"},
        {CRUX_CREDENTIAL_STATE_VALID, CRUX_SECURITY_EVENT_OK, "valid"},
    };
    for (size_t index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        const crux_credential_event_t event = {.type = CRUX_CREDENTIAL_EVENT_STATE,
                                               .state = states[index].state};
        credential_event_row(&event, CRUX_SECURITY_EVENT_ID_CREDENTIAL_STATE, states[index].result,
                             states[index].word);
    }

    const crux_credential_event_t slot_a = {.type = CRUX_CREDENTIAL_EVENT_SLOT_INVALID,
                                            .result = CRUX_CREDENTIAL_ERR_UNUSABLE,
                                            .slot = 'a'};
    credential_event_row(&slot_a, CRUX_SECURITY_EVENT_ID_CREDENTIAL_SLOT_INVALID,
                         CRUX_SECURITY_EVENT_FAILED, "a");
    const crux_credential_event_t slot_b = {.type = CRUX_CREDENTIAL_EVENT_SLOT_INVALID,
                                            .result = CRUX_CREDENTIAL_ERR_UNUSABLE,
                                            .slot = 'b'};
    credential_event_row(&slot_b, CRUX_SECURITY_EVENT_ID_CREDENTIAL_SLOT_INVALID,
                         CRUX_SECURITY_EVENT_FAILED, "b");

    const crux_credential_event_t save_failed = {.type = CRUX_CREDENTIAL_EVENT_SAVE_FAILED,
                                                 .result = CRUX_CREDENTIAL_ERR_STORE};
    credential_event_row(&save_failed, CRUX_SECURITY_EVENT_ID_CREDENTIAL_SAVE_FAILED,
                         CRUX_SECURITY_EVENT_FAILED, "");
    const crux_credential_event_t no_room = {.type = CRUX_CREDENTIAL_EVENT_SAVE_FAILED,
                                             .result = CRUX_CREDENTIAL_ERR_TOO_LARGE};
    credential_event_row(&no_room, CRUX_SECURITY_EVENT_ID_CREDENTIAL_SAVE_FAILED,
                         CRUX_SECURITY_EVENT_LIMIT, "");

    static const struct {
        crux_credential_record_t record;
        const char *word;
    } keys[] = {
        {CRUX_CREDENTIAL_RECORD_ACCOUNTS, "accounts"},
        {CRUX_CREDENTIAL_RECORD_TLS_KEY, "tls_key"},
        {CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, "tls_certificate"},
        {CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, "ssh_host_key"},
    };
    for (size_t index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        const crux_credential_event_t generated = {.type = CRUX_CREDENTIAL_EVENT_KEY_GENERATED,
                                                   .record = keys[index].record};
        credential_event_row(&generated, CRUX_SECURITY_EVENT_ID_CREDENTIAL_KEY_GENERATED,
                             CRUX_SECURITY_EVENT_OK, keys[index].word);
        const crux_credential_event_t replaced = {.type = CRUX_CREDENTIAL_EVENT_KEY_REPLACED,
                                                  .record = keys[index].record};
        credential_event_row(&replaced, CRUX_SECURITY_EVENT_ID_CREDENTIAL_KEY_REPLACED,
                             CRUX_SECURITY_EVENT_OK, keys[index].word);
    }

    const crux_credential_event_t erased = {.type = CRUX_CREDENTIAL_EVENT_ERASED,
                                            .result = CRUX_CREDENTIAL_OK};
    credential_event_row(&erased, CRUX_SECURITY_EVENT_ID_CREDENTIAL_ERASED, CRUX_SECURITY_EVENT_OK,
                         "");
    const crux_credential_event_t erase_failed = {.type = CRUX_CREDENTIAL_EVENT_ERASED,
                                                  .result = CRUX_CREDENTIAL_ERR_STORE};
    credential_event_row(&erase_failed, CRUX_SECURITY_EVENT_ID_CREDENTIAL_ERASED,
                         CRUX_SECURITY_EVENT_FAILED, "");
}

int main(void)
{
    config_events();
    network_events();
    reboot_events();
    reboot_pending_events();
    status_event_counts();
    status_credential_words();
    credential_events();
    return access_test_finish("management event providers");
}
