#include "access_internal.h"

#include <string.h>

/* Who is making a change, copied out of the session so that events need no lock. */
typedef struct {
    crux_access_origin_t origin;
    char user[CRUX_ACCESS_NAME_SIZE];
} actor_t;

typedef enum { REQUIRE_MANAGE, REQUIRE_MANAGE_OR_OWN, REQUIRE_SESSION_ONLY } requirement_t;

/* Decides with the state lock held. Report the outcome afterwards, with no lock held. */
static crux_access_result_t authorise_held(crux_access_session_t session, requirement_t requirement,
                                           const char *target, actor_t *actor,
                                           access_expired_t *expired)
{
    memset(actor, 0, sizeof(*actor));
    const access_slot_t *slot = crux_access_slot_find(session, crux_access_port_now_ms(), expired);
    if (slot == NULL) {
        return CRUX_ACCESS_ERR_SESSION_INVALID;
    }
    bool permitted = requirement == REQUIRE_SESSION_ONLY ||
                     crux_access_slot_permits(slot, CRUX_ACCESS_PERMISSION_MANAGE);
    if (!permitted && requirement == REQUIRE_MANAGE_OR_OWN && strcmp(slot->user, target) == 0) {
        permitted = crux_access_slot_permits(slot, CRUX_ACCESS_PERMISSION_SELF);
    }
    actor->origin = slot->origin;
    crux_access_copy_name(actor->user, slot->user);
    return permitted ? CRUX_ACCESS_OK : CRUX_ACCESS_ERR_DENIED;
}

static void authorise_report(crux_access_result_t result, const actor_t *actor,
                             const access_expired_t *expired)
{
    crux_access_expired_emit(expired);
    if (result == CRUX_ACCESS_ERR_DENIED) {
        crux_access_emit(CRUX_ACCESS_EVENT_PERMISSION_DENIED, result, &actor->origin, actor->user,
                         false);
    }
}

static crux_access_result_t authorise(crux_access_session_t session, requirement_t requirement,
                                      const char *target, actor_t *actor)
{
    access_expired_t expired = {0};
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    crux_access_result_t result = authorise_held(session, requirement, target, actor, &expired);
    crux_access_unlock_state();
    authorise_report(result, actor, &expired);
    return result;
}

/* Takes the change lock, checks the session again and starts a pending copy of the table. Writers
 * hold the change lock, so reading the table under it alone is safe. A refusal is reported only
 * after the change lock is released, because the event port runs with no lock held. */
static crux_access_result_t change_begin(crux_access_session_t session, requirement_t requirement,
                                         const char *target, actor_t *actor)
{
    if (!crux_access_lock_change()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (!crux_access_lock_state()) {
        crux_access_unlock_change();
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    access_expired_t expired = {0};
    crux_access_result_t result = authorise_held(session, requirement, target, actor, &expired);
    crux_access_unlock_state();
    if (result != CRUX_ACCESS_OK) {
        crux_access_unlock_change();
        authorise_report(result, actor, &expired);
        return result;
    }
    crux_access_state.pending = crux_access_state.table;
    return CRUX_ACCESS_OK;
}

static void change_abandon(void)
{
    crux_access_secure_zero(&crux_access_state.pending, sizeof(crux_access_state.pending));
    crux_access_unlock_change();
}

/* What applying a change does to sessions. keep_value keeps the session that made the change. */
typedef struct {
    const char *changed_user;
    bool end_sessions;
    uint32_t keep_value;
} change_effect_t;

static uint64_t change_number_for(const char *user, bool changed)
{
    size_t index = 0;
    if (!changed && crux_access_table_find(&crux_access_state.table, user, &index) != NULL) {
        return crux_access_state.change_numbers[index];
    }
    crux_access_state.next_change_number += 1;
    return crux_access_state.next_change_number;
}

/* Save, then apply. A save failure leaves both copies unchanged, and a login that completes during
 * the save sees the old record until applying the change ends its session. */
static crux_access_result_t change_commit(change_effect_t effect)
{
    uint8_t *record = crux_access_state.record;
    const size_t capacity = sizeof(crux_access_state.record);
    size_t length = crux_access_record_encode(&crux_access_state.pending, record, capacity);
    bool saved = length != 0 && crux_access_port_store_save(record, length);
    crux_access_secure_zero(record, capacity);
    if (!saved) {
        crux_access_secure_zero(&crux_access_state.pending, sizeof(crux_access_state.pending));
        return CRUX_ACCESS_ERR_STORE;
    }
    if (!crux_access_lock_state()) {
        crux_access_secure_zero(&crux_access_state.pending, sizeof(crux_access_state.pending));
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    access_state_t *state = &crux_access_state;
    uint64_t numbers[CRUX_ACCESS_ACCOUNT_MAX] = {0};
    for (size_t index = 0; index < crux_access_state.pending.count; ++index) {
        const char *user = crux_access_state.pending.accounts[index].user;
        numbers[index] = change_number_for(user, strcmp(user, effect.changed_user) == 0);
    }
    state->table = crux_access_state.pending;
    memcpy(state->change_numbers, numbers, sizeof(numbers));
    if (effect.end_sessions) {
        crux_access_slots_end_account(effect.changed_user, effect.keep_value);
    }
    if (effect.keep_value != 0) {
        /* The session may have logged out during the save, and its slot may have been reused. */
        access_slot_t *kept = &state->slots[effect.keep_value >> ACCESS_SLOT_SHIFT];
        if (kept->state == ACCESS_SLOT_ACTIVE &&
            kept->generation == (effect.keep_value & ACCESS_GENERATION_MASK)) {
            kept->must_change_password = false;
        }
    }
    crux_access_unlock_state();
    crux_access_secure_zero(&crux_access_state.pending, sizeof(crux_access_state.pending));
    return CRUX_ACCESS_OK;
}

static bool verifier_make(access_account_t *account, const char *password, size_t length)
{
    account->algorithm = CRUX_ACCESS_VERIFIER_PBKDF2_HMAC_SHA256;
    account->iterations = CRUX_ACCESS_VERIFIER_ITERATIONS;
    return crux_access_port_random(account->salt, sizeof(account->salt)) &&
           crux_access_port_derive(account->algorithm, account->iterations, account->salt, password,
                                   length, account->derived);
}

static void verifier_copy(access_account_t *destination, const access_account_t *source)
{
    destination->algorithm = source->algorithm;
    destination->iterations = source->iterations;
    memcpy(destination->salt, source->salt, sizeof(destination->salt));
    memcpy(destination->derived, source->derived, sizeof(destination->derived));
}

static bool account_is_enabled_administrator(const access_account_t *account)
{
    size_t role_index = 0;
    return account->enabled && crux_access_role_find(account->role, &role_index) &&
           crux_access_role_is_administrator(role_index);
}

static bool is_last_administrator(const access_table_t *table, const access_account_t *account)
{
    if (!account_is_enabled_administrator(account)) {
        return false;
    }
    size_t administrators = 0;
    for (size_t index = 0; index < table->count; ++index) {
        administrators += account_is_enabled_administrator(&table->accounts[index]) ? 1u : 0u;
    }
    return administrators == 1;
}

static void emit_change(crux_access_event_type_t type, crux_access_result_t result,
                        const actor_t *actor, const char *user)
{
    if (result == CRUX_ACCESS_OK || result == CRUX_ACCESS_ERR_STORE) {
        crux_access_emit(type, result, &actor->origin, user, false);
    }
}

crux_access_result_t crux_access_account_list(crux_access_session_t session,
                                              crux_access_account_info_t *accounts, size_t capacity,
                                              size_t *count)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (count == NULL || (accounts == NULL && capacity != 0)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    actor_t actor;
    access_expired_t expired = {0};
    crux_access_result_t result = authorise_held(session, REQUIRE_MANAGE, NULL, &actor, &expired);
    if (result == CRUX_ACCESS_OK) {
        const access_table_t *table = &crux_access_state.table;
        *count = table->count;
        for (size_t index = 0; index < table->count && index < capacity; ++index) {
            const access_account_t *account = &table->accounts[index];
            crux_access_account_info_t *info = &accounts[index];
            memset(info, 0, sizeof(*info));
            crux_access_copy_name(info->user, account->user);
            crux_access_copy_name(info->role, account->role);
            info->enabled = account->enabled;
            info->must_change_password = account->must_change_password;
            info->key_count = account->key_count;
        }
    }
    crux_access_unlock_state();
    authorise_report(result, &actor, &expired);
    return result;
}

/* A preview under the state lock, so that a request the table must refuse wastes no derivation. The
 * change lock repeats the same check after derivation. */
static crux_access_result_t create_precheck(access_table_t *table, const char *user)
{
    if (crux_access_table_find(table, user, NULL) != NULL) {
        return CRUX_ACCESS_ERR_ALREADY_EXISTS;
    }
    return table->count >= CRUX_ACCESS_ACCOUNT_MAX ? CRUX_ACCESS_ERR_NO_RESOURCES : CRUX_ACCESS_OK;
}

static crux_access_result_t table_preview_create(const char *user)
{
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    crux_access_result_t result = create_precheck(&crux_access_state.table, user);
    crux_access_unlock_state();
    return result;
}

static void pending_append(const access_account_t *account)
{
    crux_access_state.pending.accounts[crux_access_state.pending.count] = *account;
    crux_access_state.pending.count += 1;
}

crux_access_result_t crux_access_account_create(crux_access_session_t session, const char *user,
                                                const char *role, const char *password,
                                                size_t length)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (password == NULL || role == NULL || !crux_access_name_valid(user)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    actor_t actor;
    crux_access_result_t result = authorise(session, REQUIRE_MANAGE, NULL, &actor);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    size_t role_index = 0;
    if (!crux_access_role_find(role, &role_index)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    if (!crux_access_password_acceptable(password, length)) {
        return CRUX_ACCESS_ERR_PASSWORD_REJECTED;
    }
    result = table_preview_create(user);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    access_account_t fresh = {.enabled = true};
    crux_access_copy_name(fresh.user, user);
    crux_access_copy_name(fresh.role, role);
    if (!verifier_make(&fresh, password, length)) {
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    result = change_begin(session, REQUIRE_MANAGE, NULL, &actor);
    if (result != CRUX_ACCESS_OK) {
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return result;
    }
    result = create_precheck(&crux_access_state.pending, user);
    if (result != CRUX_ACCESS_OK) {
        change_abandon();
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return result;
    }
    pending_append(&fresh);
    crux_access_secure_zero(&fresh, sizeof(fresh));
    result = change_commit((change_effect_t){user, false, 0});
    crux_access_unlock_change();
    emit_change(CRUX_ACCESS_EVENT_ACCOUNT_CREATED, result, &actor, user);
    return result;
}

crux_access_result_t crux_access_account_remove(crux_access_session_t session, const char *user)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (!crux_access_name_valid(user)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    actor_t actor;
    crux_access_result_t result = change_begin(session, REQUIRE_MANAGE, NULL, &actor);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    size_t index = 0;
    const access_account_t *account =
        crux_access_table_find(&crux_access_state.pending, user, &index);
    if (account == NULL) {
        change_abandon();
        return CRUX_ACCESS_ERR_NOT_FOUND;
    }
    if (is_last_administrator(&crux_access_state.pending, account)) {
        change_abandon();
        return CRUX_ACCESS_ERR_LAST_ADMINISTRATOR;
    }
    for (size_t next = index + 1u; next < crux_access_state.pending.count; ++next) {
        crux_access_state.pending.accounts[next - 1u] = crux_access_state.pending.accounts[next];
    }
    crux_access_state.pending.count -= 1;
    memset(&crux_access_state.pending.accounts[crux_access_state.pending.count], 0,
           sizeof(crux_access_state.pending.accounts[0]));
    result = change_commit((change_effect_t){user, true, 0});
    crux_access_unlock_change();
    emit_change(CRUX_ACCESS_EVENT_ACCOUNT_REMOVED, result, &actor, user);
    return result;
}

crux_access_result_t crux_access_account_disable(crux_access_session_t session, const char *user)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (!crux_access_name_valid(user)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    actor_t actor;
    crux_access_result_t result = change_begin(session, REQUIRE_MANAGE, NULL, &actor);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    access_account_t *account = crux_access_table_find(&crux_access_state.pending, user, NULL);
    if (account == NULL) {
        change_abandon();
        return CRUX_ACCESS_ERR_NOT_FOUND;
    }
    if (!account->enabled) {
        change_abandon();
        return CRUX_ACCESS_OK;
    }
    if (is_last_administrator(&crux_access_state.pending, account)) {
        change_abandon();
        return CRUX_ACCESS_ERR_LAST_ADMINISTRATOR;
    }
    account->enabled = false;
    result = change_commit((change_effect_t){user, true, 0});
    crux_access_unlock_change();
    emit_change(CRUX_ACCESS_EVENT_ACCOUNT_DISABLED, result, &actor, user);
    return result;
}

crux_access_result_t crux_access_account_enable(crux_access_session_t session, const char *user)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (!crux_access_name_valid(user)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    actor_t actor;
    crux_access_result_t result = change_begin(session, REQUIRE_MANAGE, NULL, &actor);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    access_account_t *account = crux_access_table_find(&crux_access_state.pending, user, NULL);
    if (account == NULL) {
        change_abandon();
        return CRUX_ACCESS_ERR_NOT_FOUND;
    }
    if (account->enabled) {
        change_abandon();
        return CRUX_ACCESS_OK;
    }
    account->enabled = true;
    result = change_commit((change_effect_t){user, false, 0});
    crux_access_unlock_change();
    emit_change(CRUX_ACCESS_EVENT_ACCOUNT_ENABLED, result, &actor, user);
    return result;
}

crux_access_result_t crux_access_account_set_role(crux_access_session_t session, const char *user,
                                                  const char *role)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (role == NULL || !crux_access_name_valid(user)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    actor_t actor;
    crux_access_result_t result = change_begin(session, REQUIRE_MANAGE, NULL, &actor);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    size_t role_index = 0;
    if (!crux_access_role_find(role, &role_index)) {
        change_abandon();
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    access_account_t *account = crux_access_table_find(&crux_access_state.pending, user, NULL);
    if (account == NULL) {
        change_abandon();
        return CRUX_ACCESS_ERR_NOT_FOUND;
    }
    if (strcmp(account->role, role) == 0) {
        change_abandon();
        return CRUX_ACCESS_OK;
    }
    if (is_last_administrator(&crux_access_state.pending, account)) {
        change_abandon();
        return CRUX_ACCESS_ERR_LAST_ADMINISTRATOR;
    }
    crux_access_copy_name(account->role, role);
    result = change_commit((change_effect_t){user, true, 0});
    crux_access_unlock_change();
    emit_change(CRUX_ACCESS_EVENT_ACCOUNT_ROLE_CHANGED, result, &actor, user);
    return result;
}

static bool account_exists(const char *user)
{
    if (!crux_access_lock_state()) {
        return false;
    }
    bool exists = crux_access_table_find(&crux_access_state.table, user, NULL) != NULL;
    crux_access_unlock_state();
    return exists;
}

crux_access_result_t crux_access_account_reset_password(crux_access_session_t session,
                                                        const char *user, const char *password,
                                                        size_t length)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (password == NULL || !crux_access_name_valid(user)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    actor_t actor;
    crux_access_result_t result = authorise(session, REQUIRE_MANAGE, NULL, &actor);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    if (!account_exists(user)) {
        return CRUX_ACCESS_ERR_NOT_FOUND;
    }
    if (!crux_access_password_acceptable(password, length)) {
        return CRUX_ACCESS_ERR_PASSWORD_REJECTED;
    }
    access_account_t fresh = {0};
    if (!verifier_make(&fresh, password, length)) {
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    result = change_begin(session, REQUIRE_MANAGE, NULL, &actor);
    if (result != CRUX_ACCESS_OK) {
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return result;
    }
    access_account_t *account = crux_access_table_find(&crux_access_state.pending, user, NULL);
    if (account == NULL) {
        change_abandon();
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return CRUX_ACCESS_ERR_NOT_FOUND;
    }
    verifier_copy(account, &fresh);
    crux_access_secure_zero(&fresh, sizeof(fresh));
    account->must_change_password = true;
    result = change_commit((change_effect_t){user, true, 0});
    crux_access_unlock_change();
    emit_change(CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_RESET, result, &actor, user);
    return result;
}

crux_access_result_t crux_access_account_change_password(crux_access_session_t session,
                                                         const char *current, size_t current_length,
                                                         const char *password, size_t length,
                                                         uint32_t *retry_after_ms)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (current == NULL || password == NULL || retry_after_ms == NULL) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    *retry_after_ms = 0;
    actor_t actor;
    crux_access_result_t result = authorise(session, REQUIRE_SESSION_ONLY, NULL, &actor);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    if (!crux_access_password_acceptable(password, length)) {
        return CRUX_ACCESS_ERR_PASSWORD_REJECTED;
    }
    if (current_length > CRUX_ACCESS_PASSWORD_MAX) {
        crux_access_emit(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_DENIED, &actor.origin,
                         actor.user, false);
        return CRUX_ACCESS_ERR_DENIED;
    }
    uint64_t change_number = 0;
    result = crux_access_verify_current(actor.user, &actor.origin, current, current_length,
                                        retry_after_ms, &change_number);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    access_account_t fresh = {0};
    if (!verifier_make(&fresh, password, length)) {
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    result = change_begin(session, REQUIRE_SESSION_ONLY, NULL, &actor);
    if (result != CRUX_ACCESS_OK) {
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return result;
    }
    size_t index = 0;
    access_account_t *account =
        crux_access_table_find(&crux_access_state.pending, actor.user, &index);
    if (account == NULL || crux_access_state.change_numbers[index] != change_number) {
        change_abandon();
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return CRUX_ACCESS_ERR_DENIED;
    }
    verifier_copy(account, &fresh);
    crux_access_secure_zero(&fresh, sizeof(fresh));
    account->must_change_password = false;
    result = change_commit((change_effect_t){actor.user, true, session.value});
    crux_access_unlock_change();
    emit_change(CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_CHANGED, result, &actor, actor.user);
    return result;
}

static const access_key_t *key_find(const access_account_t *account, const uint8_t *key,
                                    size_t key_length, size_t *index)
{
    for (size_t candidate = 0; candidate < account->key_count; ++candidate) {
        const access_key_t *listed = &account->keys[candidate];
        if (listed->length == key_length && memcmp(listed->bytes, key, key_length) == 0) {
            *index = candidate;
            return listed;
        }
    }
    return NULL;
}

crux_access_result_t crux_access_account_add_key(crux_access_session_t session, const char *user,
                                                 const uint8_t *key, size_t key_length)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (key == NULL || !crux_access_key_acceptable(key, key_length) ||
        !crux_access_name_valid(user)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    actor_t actor;
    crux_access_result_t result = change_begin(session, REQUIRE_MANAGE_OR_OWN, user, &actor);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    access_account_t *account = crux_access_table_find(&crux_access_state.pending, user, NULL);
    size_t index = 0;
    if (account == NULL) {
        change_abandon();
        return CRUX_ACCESS_ERR_NOT_FOUND;
    }
    if (key_find(account, key, key_length, &index) != NULL) {
        change_abandon();
        return CRUX_ACCESS_ERR_ALREADY_EXISTS;
    }
    if (account->key_count >= CRUX_ACCESS_KEYS_PER_ACCOUNT) {
        change_abandon();
        return CRUX_ACCESS_ERR_NO_RESOURCES;
    }
    access_key_t *added = &account->keys[account->key_count];
    added->length = key_length;
    memcpy(added->bytes, key, key_length);
    account->key_count += 1;
    result = change_commit((change_effect_t){user, false, 0});
    crux_access_unlock_change();
    emit_change(CRUX_ACCESS_EVENT_ACCOUNT_KEY_ADDED, result, &actor, user);
    return result;
}

crux_access_result_t crux_access_account_remove_key(crux_access_session_t session, const char *user,
                                                    const uint8_t *key, size_t key_length)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (key == NULL || key_length == 0 || !crux_access_name_valid(user)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    actor_t actor;
    crux_access_result_t result = change_begin(session, REQUIRE_MANAGE_OR_OWN, user, &actor);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    access_account_t *account = crux_access_table_find(&crux_access_state.pending, user, NULL);
    size_t index = 0;
    if (account == NULL || key_find(account, key, key_length, &index) == NULL) {
        change_abandon();
        return CRUX_ACCESS_ERR_NOT_FOUND;
    }
    for (size_t next = index + 1u; next < account->key_count; ++next) {
        account->keys[next - 1u] = account->keys[next];
    }
    account->key_count -= 1;
    memset(&account->keys[account->key_count], 0, sizeof(account->keys[0]));
    result = change_commit((change_effect_t){user, true, 0});
    crux_access_unlock_change();
    emit_change(CRUX_ACCESS_EVENT_ACCOUNT_KEY_REMOVED, result, &actor, user);
    return result;
}

/* Provisioning closes before the save, and reopens only after a save that failed. A lost lock at
 * either step leaves it closed until a reset, so a retry cannot replace an account the medium may
 * already hold. */
static crux_access_result_t provision_commit(const access_account_t *fresh, const char *user)
{
    if (!crux_access_lock_change()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (!crux_access_lock_state()) {
        crux_access_unlock_change();
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    bool open = crux_access_state.provisioning_open;
    crux_access_state.provisioning_open = false;
    crux_access_unlock_state();
    if (!open) {
        crux_access_unlock_change();
        return CRUX_ACCESS_ERR_DENIED;
    }
    crux_access_state.pending = crux_access_state.table;
    pending_append(fresh);
    crux_access_result_t result = change_commit((change_effect_t){user, false, 0});
    if (result == CRUX_ACCESS_ERR_STORE) {
        if (crux_access_lock_state()) {
            crux_access_state.provisioning_open = true;
            crux_access_unlock_state();
        } else {
            result = CRUX_ACCESS_ERR_NOT_READY;
        }
    }
    crux_access_unlock_change();
    return result;
}

static bool provisioning_is_open(void)
{
    if (!crux_access_lock_state()) {
        return false;
    }
    bool open = crux_access_state.provisioning_open;
    crux_access_unlock_state();
    return open;
}

crux_access_result_t crux_access_provision_first_account(const char *user, const char *password,
                                                         size_t length, const char *role,
                                                         const crux_access_origin_t *origin)
{
    if (crux_access_state.store_unusable) {
        return CRUX_ACCESS_ERR_DENIED;
    }
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (password == NULL || role == NULL || !crux_access_name_valid(user) ||
        !crux_access_origin_valid(origin)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    if (origin->channel != CRUX_ACCESS_CHANNEL_SERIAL || !provisioning_is_open()) {
        return CRUX_ACCESS_ERR_DENIED;
    }
    size_t role_index = 0;
    if (!crux_access_role_find(role, &role_index) ||
        !crux_access_role_is_administrator(role_index)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    if (!crux_access_password_acceptable(password, length)) {
        return CRUX_ACCESS_ERR_PASSWORD_REJECTED;
    }
    access_account_t fresh = {.enabled = true};
    crux_access_copy_name(fresh.user, user);
    crux_access_copy_name(fresh.role, role);
    if (!verifier_make(&fresh, password, length)) {
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    crux_access_result_t result = provision_commit(&fresh, user);
    crux_access_secure_zero(&fresh, sizeof(fresh));
    if (result == CRUX_ACCESS_OK || result == CRUX_ACCESS_ERR_STORE) {
        crux_access_emit(CRUX_ACCESS_EVENT_PROVISIONED, result, origin, user, false);
    }
    return result;
}

void crux_access_change_upgrade(const char *user, uint64_t change_number, const char *password,
                                size_t length)
{
    access_account_t fresh = {0};
    if (!verifier_make(&fresh, password, length) || !crux_access_lock_change()) {
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return;
    }
    crux_access_state.pending = crux_access_state.table;
    size_t index = 0;
    access_account_t *account = crux_access_table_find(&crux_access_state.pending, user, &index);
    if (account == NULL || crux_access_state.change_numbers[index] != change_number) {
        change_abandon();
        crux_access_secure_zero(&fresh, sizeof(fresh));
        return;
    }
    verifier_copy(account, &fresh);
    crux_access_secure_zero(&fresh, sizeof(fresh));
    (void)change_commit((change_effect_t){user, false, 0});
    crux_access_unlock_change();
}
