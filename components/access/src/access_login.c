#include "access_internal.h"

#include <string.h>

/* Any fixed values do: a dummy match always fails. */
static const uint8_t dummy_salt[CRUX_ACCESS_SALT_SIZE] = {0x5A, 0xA5};
static const uint8_t dummy_derived[CRUX_ACCESS_DERIVED_SIZE] = {0xA5, 0x5A};

/* The copy of a record that verification works on, with no lock held. */
typedef struct {
    bool known;
    uint64_t change_number;
    uint32_t algorithm;
    uint32_t iterations;
    uint8_t salt[CRUX_ACCESS_SALT_SIZE];
    uint8_t derived[CRUX_ACCESS_DERIVED_SIZE];
} verification_t;

static void verification_prepare(const char *user, verification_t *verification)
{
    size_t index = 0;
    const access_account_t *account =
        crux_access_table_find(&crux_access_state.table, user, &index);
    verification->known = account != NULL;
    if (account == NULL) {
        verification->change_number = 0;
        verification->algorithm = CRUX_ACCESS_VERIFIER_PBKDF2_HMAC_SHA256;
        verification->iterations = CRUX_ACCESS_VERIFIER_ITERATIONS;
        memcpy(verification->salt, dummy_salt, sizeof(verification->salt));
        memcpy(verification->derived, dummy_derived, sizeof(verification->derived));
        return;
    }
    verification->change_number = crux_access_state.change_numbers[index];
    verification->algorithm = account->algorithm;
    verification->iterations = account->iterations;
    memcpy(verification->salt, account->salt, sizeof(verification->salt));
    memcpy(verification->derived, account->derived, sizeof(verification->derived));
}

/* Derives and compares in constant time, clearing internal copies. False means a provider failure.
 */
static bool verification_run(verification_t *verification, const char *password, size_t length,
                             bool *matched)
{
    uint8_t candidate[CRUX_ACCESS_DERIVED_SIZE];
    bool derived = crux_access_port_derive(verification->algorithm, verification->iterations,
                                           verification->salt, password, length, candidate);
    *matched = derived &&
               crux_access_equal_constant_time(candidate, verification->derived, sizeof(candidate));
    crux_access_secure_zero(candidate, sizeof(candidate));
    crux_access_secure_zero(verification->derived, sizeof(verification->derived));
    return derived;
}

static bool *verifying_flag(crux_access_channel_t channel)
{
    return channel == CRUX_ACCESS_CHANNEL_SERIAL ? &crux_access_state.serial_verifying
                                                 : &crux_access_state.network_verifying;
}

/* Steps 2 and 3 of the login sequence, in one lock hold: nothing can change between the delay check
 * and claiming the channel's verification slot. */
static crux_access_result_t verification_claim(crux_access_channel_t channel,
                                               access_delay_buckets_t buckets, uint64_t now_ms,
                                               uint32_t *retry_after_ms)
{
    *retry_after_ms = crux_access_delay_remaining_ms(buckets, now_ms);
    if (*retry_after_ms != 0) {
        return CRUX_ACCESS_ERR_RETRY_LATER;
    }
    bool *verifying = verifying_flag(channel);
    if (*verifying) {
        return CRUX_ACCESS_ERR_BUSY;
    }
    *verifying = true;
    return CRUX_ACCESS_OK;
}

crux_access_result_t crux_access_verify_current(const char *user, const char *password,
                                                size_t length, const crux_access_origin_t *origin,
                                                uint32_t *retry_after_ms, uint64_t *change_number)
{
    access_delay_buckets_t buckets;
    if (!crux_access_delay_buckets(origin, user, &buckets) || !crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    verification_t verification = {0};
    crux_access_result_t result =
        verification_claim(origin->channel, buckets, crux_access_port_now_ms(), retry_after_ms);
    if (result == CRUX_ACCESS_OK) {
        verification_prepare(user, &verification);
    }
    crux_access_unlock_state();
    if (result != CRUX_ACCESS_OK) {
        if (result == CRUX_ACCESS_ERR_RETRY_LATER) {
            crux_access_emit(CRUX_ACCESS_EVENT_DELAY_APPLIED, result, origin, NULL, false);
        }
        return result;
    }

    bool matched = false;
    bool derived = verification_run(&verification, password, length, &matched);

    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (derived && matched && verification.known) {
        if (buckets.name != NULL) {
            memset(buckets.name, 0, sizeof(*buckets.name));
        }
    } else if (derived) {
        crux_access_delay_record_failure(buckets, crux_access_port_now_ms());
    }
    *verifying_flag(origin->channel) = false;
    crux_access_unlock_state();
    if (!derived) {
        crux_access_emit(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_NOT_READY, origin, user,
                         false);
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (!matched || !verification.known) {
        crux_access_emit(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_DENIED, origin, user,
                         false);
        return CRUX_ACCESS_ERR_DENIED;
    }
    *change_number = verification.change_number;
    return CRUX_ACCESS_OK;
}

typedef struct {
    bool session_created;
    bool role_missing;
    bool account_known;
    bool upgrade;
    uint64_t change_number;
    char account_name[CRUX_ACCESS_NAME_SIZE];
} login_outcome_t;

static bool account_may_login(const access_account_t *account, size_t *role_index,
                              login_outcome_t *outcome)
{
    if (!crux_access_role_find(account->role, role_index)) {
        outcome->role_missing = true;
        return false;
    }
    return account->enabled;
}

/* Step 7 and 8: read the record again, and in one lock hold create the session or record a failure.
 */
static crux_access_session_t login_conclude(const char *user, const crux_access_origin_t *origin,
                                            const verification_t *verification, bool matched,
                                            size_t slot, access_delay_buckets_t buckets,
                                            uint64_t now_ms, login_outcome_t *outcome)
{
    crux_access_session_t session = {0};
    size_t index = 0;
    size_t role_index = 0;
    const access_account_t *account =
        crux_access_table_find(&crux_access_state.table, user, &index);
    bool success = false;
    if (account != NULL) {
        outcome->account_known = true;
        crux_access_copy_name(outcome->account_name, account->user);
        bool unchanged = verification->known &&
                         crux_access_state.change_numbers[index] == verification->change_number;
        success = account_may_login(account, &role_index, outcome) && unchanged && matched;
    }
    if (success) {
        session = crux_access_slot_activate(slot, user, role_index, account->must_change_password,
                                            origin, now_ms);
        outcome->session_created = true;
        if (buckets.name != NULL) {
            memset(buckets.name, 0, sizeof(*buckets.name));
        }
        outcome->upgrade = account->iterations < CRUX_ACCESS_VERIFIER_ITERATIONS;
        outcome->change_number = verification->change_number;
    } else {
        crux_access_delay_record_failure(buckets, now_ms);
        crux_access_slot_cancel(slot);
    }
    return session;
}

crux_access_result_t crux_access_login_password(const char *user, const char *password,
                                                size_t length, const crux_access_origin_t *origin,
                                                crux_access_session_t *session,
                                                uint32_t *retry_after_ms)
{
    access_state_t *state = &crux_access_state;
    if (!state->ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (password == NULL || session == NULL || retry_after_ms == NULL ||
        !crux_access_name_valid(user) || !crux_access_origin_valid(origin)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    session->value = 0;
    *retry_after_ms = 0;
    if (length > CRUX_ACCESS_PASSWORD_MAX) {
        crux_access_emit(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_DENIED, origin, NULL,
                         false);
        return CRUX_ACCESS_ERR_DENIED;
    }
    access_delay_buckets_t buckets;
    if (!crux_access_delay_buckets(origin, user, &buckets)) {
        crux_access_emit(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_NOT_READY, origin, NULL,
                         false);
        return CRUX_ACCESS_ERR_NOT_READY;
    }

    access_expired_t expired = {0};
    verification_t verification = {0};
    size_t slot = 0;
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    uint64_t now_ms = crux_access_port_now_ms();
    crux_access_result_t result =
        verification_claim(origin->channel, buckets, now_ms, retry_after_ms);
    if (result == CRUX_ACCESS_OK) {
        if (crux_access_slot_reserve(origin->channel, now_ms, &slot, &expired)) {
            verification_prepare(user, &verification);
        } else {
            *verifying_flag(origin->channel) = false;
            result = CRUX_ACCESS_ERR_NO_RESOURCES;
        }
    }
    crux_access_unlock_state();
    crux_access_expired_emit(&expired);
    if (result != CRUX_ACCESS_OK) {
        if (result == CRUX_ACCESS_ERR_RETRY_LATER) {
            crux_access_emit(CRUX_ACCESS_EVENT_DELAY_APPLIED, result, origin, NULL, false);
        }
        return result;
    }

    bool matched = false;
    bool derived = verification_run(&verification, password, length, &matched);

    login_outcome_t outcome = {0};
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    now_ms = crux_access_port_now_ms();
    if (derived) {
        *session =
            login_conclude(user, origin, &verification, matched, slot, buckets, now_ms, &outcome);
    } else {
        crux_access_slot_cancel(slot);
    }
    *verifying_flag(origin->channel) = false;
    crux_access_unlock_state();
    if (!derived) {
        crux_access_emit(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_NOT_READY, origin, NULL,
                         false);
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (!outcome.session_created) {
        crux_access_emit(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_DENIED, origin,
                         outcome.account_known ? outcome.account_name : NULL, outcome.role_missing);
        return CRUX_ACCESS_ERR_DENIED;
    }
    crux_access_emit(CRUX_ACCESS_EVENT_LOGIN_SUCCESS, CRUX_ACCESS_OK, origin, outcome.account_name,
                     false);
    if (outcome.upgrade) {
        crux_access_change_upgrade(user, outcome.change_number, password, length);
    }
    return CRUX_ACCESS_OK;
}

/* An account that cannot log in by key lists no keys, so the pre-check treats it like an unknown
 * name. crux_access_login_verified() repeats this check, in case the account changed meanwhile. */
static const access_account_t *account_listing_key(const char *user, const uint8_t *key,
                                                   size_t key_length, size_t *role_index)
{
    const access_account_t *account = crux_access_table_find(&crux_access_state.table, user, NULL);
    if (account == NULL || !account->enabled || account->must_change_password ||
        !crux_access_role_find(account->role, role_index) ||
        key_length > CRUX_ACCESS_KEY_SIZE_MAX) {
        return NULL;
    }
    for (size_t index = 0; index < account->key_count; ++index) {
        const access_key_t *candidate = &account->keys[index];
        if (candidate->length == key_length && memcmp(candidate->bytes, key, key_length) == 0) {
            return account;
        }
    }
    return NULL;
}

/* Whether a name belongs to an account, and if so whether its role has left the policy. */
static bool account_describe(const char *user, bool *role_missing)
{
    size_t role_index = 0;
    const access_account_t *account = crux_access_table_find(&crux_access_state.table, user, NULL);
    *role_missing = account != NULL && !crux_access_role_find(account->role, &role_index);
    return account != NULL;
}

crux_access_result_t crux_access_key_is_authorised(const char *user, const uint8_t *key,
                                                   size_t key_length, bool signed_request,
                                                   const crux_access_origin_t *origin,
                                                   uint32_t *retry_after_ms)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (key == NULL || key_length == 0 || retry_after_ms == NULL || !crux_access_name_valid(user) ||
        !crux_access_origin_valid(origin)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    *retry_after_ms = 0;
    access_delay_buckets_t buckets;
    if (!crux_access_delay_buckets(origin, NULL, &buckets)) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    uint64_t now_ms = crux_access_port_now_ms();
    crux_access_result_t result;
    bool known = false;
    bool role_missing = false;
    char account_name[CRUX_ACCESS_NAME_SIZE] = {0};
    *retry_after_ms = crux_access_delay_remaining_ms(buckets, now_ms);
    if (*retry_after_ms != 0) {
        result = CRUX_ACCESS_ERR_RETRY_LATER;
    } else {
        size_t role_index = 0;
        result = account_listing_key(user, key, key_length, &role_index) != NULL
                     ? CRUX_ACCESS_OK
                     : CRUX_ACCESS_ERR_DENIED;
        if (result == CRUX_ACCESS_ERR_DENIED && signed_request) {
            crux_access_delay_record_failure(buckets, now_ms);
            known = account_describe(user, &role_missing);
            if (known) {
                crux_access_copy_name(account_name, user);
            }
        }
    }
    crux_access_unlock_state();
    if (result == CRUX_ACCESS_ERR_RETRY_LATER) {
        crux_access_emit(CRUX_ACCESS_EVENT_DELAY_APPLIED, result, origin, NULL, false);
    } else if (result == CRUX_ACCESS_ERR_DENIED && signed_request) {
        crux_access_emit(CRUX_ACCESS_EVENT_KEY_FAILURE, result, origin, known ? account_name : NULL,
                         role_missing);
    }
    return result;
}

crux_access_result_t crux_access_login_verified(const char *user, const uint8_t *key,
                                                size_t key_length,
                                                const crux_access_origin_t *origin,
                                                crux_access_session_t *session)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (key == NULL || key_length == 0 || session == NULL || !crux_access_name_valid(user) ||
        !crux_access_origin_valid(origin)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    session->value = 0;
    access_expired_t expired = {0};
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    uint64_t now_ms = crux_access_port_now_ms();
    crux_access_result_t result = CRUX_ACCESS_ERR_DENIED;
    size_t role_index = 0;
    size_t slot = 0;
    bool role_missing = false;
    bool known = account_describe(user, &role_missing);
    if (account_listing_key(user, key, key_length, &role_index) != NULL) {
        if (crux_access_slot_reserve(origin->channel, now_ms, &slot, &expired)) {
            *session = crux_access_slot_activate(slot, user, role_index, false, origin, now_ms);
            result = CRUX_ACCESS_OK;
        } else {
            result = CRUX_ACCESS_ERR_NO_RESOURCES;
        }
    }
    crux_access_unlock_state();
    crux_access_expired_emit(&expired);
    crux_access_emit(result == CRUX_ACCESS_OK ? CRUX_ACCESS_EVENT_LOGIN_SUCCESS
                                              : CRUX_ACCESS_EVENT_LOGIN_FAILURE,
                     result, origin, known ? user : NULL, role_missing);
    return result;
}

void crux_access_report_key_failure(const char *user, const crux_access_origin_t *origin)
{
    if (!crux_access_state.ready || !crux_access_origin_valid(origin)) {
        return;
    }
    access_delay_buckets_t buckets;
    bool hashed = crux_access_delay_buckets(origin, NULL, &buckets);
    if (!crux_access_lock_state()) {
        return;
    }
    uint64_t now_ms = crux_access_port_now_ms();
    /* An attempt inside a window neither counts nor extends it. */
    if (hashed && crux_access_delay_remaining_ms(buckets, now_ms) == 0) {
        crux_access_delay_record_failure(buckets, now_ms);
    }
    bool role_missing = false;
    bool known = crux_access_name_valid(user) && account_describe(user, &role_missing);
    crux_access_unlock_state();
    crux_access_emit(CRUX_ACCESS_EVENT_KEY_FAILURE, CRUX_ACCESS_ERR_DENIED, origin,
                     known ? user : NULL, role_missing);
}

crux_access_result_t crux_access_touch(crux_access_session_t session)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    access_expired_t expired = {0};
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    uint64_t now_ms = crux_access_port_now_ms();
    crux_access_result_t result = CRUX_ACCESS_ERR_SESSION_INVALID;
    access_slot_t *slot = crux_access_slot_find(session, now_ms, &expired);
    if (slot != NULL) {
        slot->activity_ms = now_ms;
        result = CRUX_ACCESS_OK;
    }
    crux_access_unlock_state();
    crux_access_expired_emit(&expired);
    return result;
}

void crux_access_logout(crux_access_session_t *session)
{
    if (session == NULL) {
        return;
    }
    crux_access_session_t ended = *session;
    session->value = 0;
    if (!crux_access_state.ready) {
        return;
    }
    access_expired_t expired = {0};
    if (!crux_access_lock_state()) {
        return;
    }
    uint64_t now_ms = crux_access_port_now_ms();
    crux_access_event_t event = {.type = CRUX_ACCESS_EVENT_LOGOUT, .result = CRUX_ACCESS_OK};
    const access_slot_t *slot = crux_access_slot_find(ended, now_ms, &expired);
    if (slot != NULL) {
        event.origin = slot->origin;
        crux_access_copy_name(event.account, slot->user);
        crux_access_slot_release(ended.value >> ACCESS_SLOT_SHIFT);
    }
    crux_access_unlock_state();
    crux_access_expired_emit(&expired);
    if (slot != NULL) {
        crux_access_port_event(&event);
    }
}
