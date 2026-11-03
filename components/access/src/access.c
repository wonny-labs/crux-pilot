#include "access_internal.h"

#include <string.h>

access_state_t crux_access_state;

#define ACCESS_LOCK_WAIT_MS 0x7FFFFFFFu

size_t crux_access_bounded_length(const char *text, size_t limit)
{
    size_t length = 0;
    while (length <= limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

bool crux_access_name_valid(const char *name)
{
    if (name == NULL) {
        return false;
    }
    size_t length = crux_access_bounded_length(name, CRUX_ACCESS_NAME_SIZE - 1u);
    if (length == 0 || length >= CRUX_ACCESS_NAME_SIZE) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        char c = name[index];
        bool allowed =
            (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

void crux_access_copy_name(char destination[CRUX_ACCESS_NAME_SIZE], const char *source)
{
    size_t length = crux_access_bounded_length(source, CRUX_ACCESS_NAME_SIZE - 1u);
    if (length >= CRUX_ACCESS_NAME_SIZE) {
        length = CRUX_ACCESS_NAME_SIZE - 1u;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

void crux_access_secure_zero(void *memory, size_t length)
{
    volatile unsigned char *bytes = memory;
    for (size_t index = 0; index < length; ++index) {
        bytes[index] = 0;
    }
}

bool crux_access_equal_constant_time(const uint8_t *left, const uint8_t *right, size_t length)
{
    volatile uint8_t difference = 0;
    for (size_t index = 0; index < length; ++index) {
        difference = (uint8_t)(difference | (left[index] ^ right[index]));
    }
    return difference == 0;
}

/* A failed lock means misuse, such as a call outside thread context. The caller fails closed. */
static bool lock_with_retry(crux_mutex_t mutex)
{
    crux_status_t status;
    do {
        status = crux_mutex_lock(mutex, ACCESS_LOCK_WAIT_MS);
    } while (status == CRUX_ERR_TIMEOUT);
    return status == CRUX_OK;
}

bool crux_access_lock_state(void)
{
    return crux_access_state.locks_created && lock_with_retry(crux_access_state.state_lock);
}

void crux_access_unlock_state(void)
{
    (void)crux_mutex_unlock(crux_access_state.state_lock);
}

bool crux_access_lock_change(void)
{
    return crux_access_state.locks_created && lock_with_retry(crux_access_state.change_lock);
}

void crux_access_unlock_change(void)
{
    (void)crux_mutex_unlock(crux_access_state.change_lock);
}

static void event_send(crux_access_event_t *event, const crux_access_origin_t *origin)
{
    if (crux_access_origin_valid(origin)) {
        event->origin = *origin;
    }
    crux_access_port_event(event);
}

void crux_access_emit(crux_access_event_type_t type, crux_access_result_t result,
                      const crux_access_origin_t *origin, const char *account, bool role_missing)
{
    crux_access_event_t event = {.type = type, .result = result, .role_missing = role_missing};
    if (account != NULL) {
        crux_access_copy_name(event.account, account);
    }
    event_send(&event, origin);
}

void crux_access_emit_session(crux_access_event_type_t type, crux_access_result_t result,
                              const crux_access_origin_t *origin, const char *actor,
                              const char *account, const char *permission)
{
    crux_access_event_t event = {.type = type, .result = result};
    if (actor != NULL) {
        crux_access_copy_name(event.actor, actor);
    }
    if (account != NULL) {
        crux_access_copy_name(event.account, account);
    }
    if (permission != NULL) {
        size_t length = crux_access_bounded_length(permission, CRUX_ACCESS_PERMISSION_MAX);
        memcpy(event.permission, permission,
               length > CRUX_ACCESS_PERMISSION_MAX ? CRUX_ACCESS_PERMISSION_MAX : length);
    }
    event_send(&event, origin);
}

crux_access_result_t crux_access_refuse_not_ready(const crux_access_origin_t *origin)
{
    crux_access_emit(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, origin, NULL, false);
    return CRUX_ACCESS_ERR_NOT_READY;
}

bool crux_access_origin_valid(const crux_access_origin_t *origin)
{
    if (origin == NULL || origin->source_length > sizeof(origin->source)) {
        return false;
    }
    return origin->channel == CRUX_ACCESS_CHANNEL_SERIAL ||
           origin->channel == CRUX_ACCESS_CHANNEL_WEB || origin->channel == CRUX_ACCESS_CHANNEL_SSH;
}

bool crux_access_role_find(const char *role, size_t *index)
{
    const crux_access_policy_t *policy = crux_access_state.policy;
    if (role == NULL) {
        return false;
    }
    for (size_t candidate = 0; candidate < policy->role_count; ++candidate) {
        if (strcmp(policy->roles[candidate].name, role) == 0) {
            *index = candidate;
            return true;
        }
    }
    return false;
}

static bool role_holds(const crux_access_role_t *role, const char *permission)
{
    for (size_t index = 0; index < role->permission_count; ++index) {
        if (strcmp(role->permissions[index], permission) == 0) {
            return true;
        }
    }
    return false;
}

bool crux_access_role_is_administrator(size_t role_index)
{
    return role_holds(&crux_access_state.policy->roles[role_index], CRUX_ACCESS_PERMISSION_MANAGE);
}

bool crux_access_slot_permits(const access_slot_t *slot, const char *permission)
{
    if (slot->must_change_password) {
        return false;
    }
    return strcmp(permission, CRUX_ACCESS_PERMISSION_SELF) == 0 ||
           role_holds(&crux_access_state.policy->roles[slot->role_index], permission);
}

access_account_t *crux_access_table_find(access_table_t *table, const char *user, size_t *index)
{
    for (size_t candidate = 0; candidate < table->count; ++candidate) {
        if (strcmp(table->accounts[candidate].user, user) == 0) {
            if (index != NULL) {
                *index = candidate;
            }
            return &table->accounts[candidate];
        }
    }
    return NULL;
}

bool crux_access_password_acceptable(const char *password, size_t length)
{
    if (password == NULL || length > CRUX_ACCESS_PASSWORD_MAX) {
        return false;
    }
    if (crux_access_state.policy->password_acceptable != NULL) {
        return crux_access_state.policy->password_acceptable(password, length);
    }
    return length >= 12u;
}

static bool text_valid(const char *text, size_t maximum)
{
    if (text == NULL) {
        return false;
    }
    size_t length = crux_access_bounded_length(text, maximum);
    return length >= 1u && length <= maximum;
}

static bool role_valid(const crux_access_role_t *role)
{
    if (!text_valid(role->name, CRUX_ACCESS_NAME_SIZE - 1u)) {
        return false;
    }
    if (role->permission_count > 0 && role->permissions == NULL) {
        return false;
    }
    for (size_t index = 0; index < role->permission_count; ++index) {
        if (!text_valid(role->permissions[index], CRUX_ACCESS_PERMISSION_MAX)) {
            return false;
        }
        for (size_t earlier = 0; earlier < index; ++earlier) {
            if (strcmp(role->permissions[earlier], role->permissions[index]) == 0) {
                return false;
            }
        }
    }
    return true;
}

static bool policy_valid(const crux_access_policy_t *policy)
{
    if (policy == NULL || policy->roles == NULL || policy->role_count == 0 ||
        policy->session_idle_limit_s == 0 || policy->session_absolute_limit_s == 0 ||
        policy->session_idle_limit_s > policy->session_absolute_limit_s) {
        return false;
    }
    bool has_administrator = false;
    for (size_t index = 0; index < policy->role_count; ++index) {
        const crux_access_role_t *role = &policy->roles[index];
        if (!role_valid(role)) {
            return false;
        }
        for (size_t earlier = 0; earlier < index; ++earlier) {
            if (strcmp(policy->roles[earlier].name, role->name) == 0) {
                return false;
            }
        }
        has_administrator = has_administrator || role_holds(role, CRUX_ACCESS_PERMISSION_MANAGE);
    }
    return has_administrator;
}

static bool locks_create(void)
{
    if (crux_access_state.locks_created) {
        return true;
    }
    if (crux_mutex_create(&crux_access_state.state_lock) != CRUX_OK) {
        return false;
    }
    if (crux_mutex_create(&crux_access_state.change_lock) != CRUX_OK) {
        (void)crux_mutex_destroy(&crux_access_state.state_lock);
        return false;
    }
    crux_access_state.locks_created = true;
    return true;
}

/* Loading is the single step that decides provisioning, so a store that reads as unusable stays
 * that way. A retry must not let a medium inserted while running read as a first boot. */
static crux_access_result_t table_load(void)
{
    uint8_t *record = crux_access_state.record;
    const size_t capacity = sizeof(crux_access_state.record);
    size_t length = 0;
    crux_access_store_state_t state = crux_access_port_store_load(record, capacity, &length);
    if (state == CRUX_ACCESS_STORE_EMPTY) {
        crux_access_state.table.count = 0;
        crux_access_state.provisioning_open = true;
        return CRUX_ACCESS_OK;
    }
    bool loaded = state == CRUX_ACCESS_STORE_VALID && length <= capacity &&
                  crux_access_record_decode(record, length, &crux_access_state.table);
    crux_access_secure_zero(record, capacity);
    if (!loaded) {
        crux_access_state.store_unusable = true;
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    return CRUX_ACCESS_OK;
}

crux_access_result_t crux_access_init(const crux_access_policy_t *policy)
{
    access_state_t *state = &crux_access_state;
    if (state->ready) {
        return CRUX_ACCESS_ERR_ALREADY_EXISTS;
    }
    if (state->store_unusable) {
        return crux_access_refuse_not_ready(NULL);
    }
    if (!policy_valid(policy)) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    if (!locks_create() || !crux_access_port_random(state->hash_key, sizeof(state->hash_key))) {
        return crux_access_refuse_not_ready(NULL);
    }
    state->policy = policy;
    crux_access_result_t result = table_load();
    if (result != CRUX_ACCESS_OK) {
        crux_access_secure_zero(state->hash_key, sizeof(state->hash_key));
        return crux_access_refuse_not_ready(NULL);
    }
    for (size_t index = 0; index < state->table.count; ++index) {
        state->next_change_number += 1;
        state->change_numbers[index] = state->next_change_number;
    }
    for (size_t index = 0; index < CRUX_ACCESS_SESSION_MAX; ++index) {
        state->slots[index].generation = 1;
    }
    state->ready = true;
    return CRUX_ACCESS_OK;
}

crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission)
{
    if (!crux_access_state.ready) {
        return crux_access_refuse_not_ready(NULL);
    }
    if (permission == NULL || permission[0] == '\0') {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    access_expired_t expired = {0};
    crux_access_origin_t origin = {0};
    char actor[CRUX_ACCESS_NAME_SIZE] = {0};
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    uint64_t now_ms = crux_access_port_now_ms();
    crux_access_result_t result = CRUX_ACCESS_ERR_SESSION_INVALID;
    const access_slot_t *slot = crux_access_slot_find(session, now_ms, &expired);
    if (slot != NULL) {
        result =
            crux_access_slot_permits(slot, permission) ? CRUX_ACCESS_OK : CRUX_ACCESS_ERR_DENIED;
        if (result != CRUX_ACCESS_OK) {
            origin = slot->origin;
            crux_access_copy_name(actor, slot->user);
        }
    }
    crux_access_unlock_state();
    crux_access_session_report(result, &expired);
    if (result == CRUX_ACCESS_ERR_DENIED) {
        crux_access_emit_session(CRUX_ACCESS_EVENT_PERMISSION_DENIED, result, &origin, actor, NULL,
                                 permission);
    }
    return result;
}

crux_access_result_t crux_access_permitted(crux_access_session_t session, const char *permission,
                                           bool *permitted)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (permission == NULL || permission[0] == '\0' || permitted == NULL) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    crux_access_result_t result = CRUX_ACCESS_ERR_SESSION_INVALID;
    const access_slot_t *slot = crux_access_slot_peek(session, crux_access_port_now_ms());
    if (slot != NULL) {
        *permitted = crux_access_slot_permits(slot, permission);
        result = CRUX_ACCESS_OK;
    }
    crux_access_unlock_state();
    return result;
}

crux_access_result_t crux_access_session_origin(crux_access_session_t session, uint8_t *channel,
                                                uint8_t source[16], uint8_t *source_length)
{
    if (!crux_access_state.ready) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    if (channel == NULL || source == NULL || source_length == NULL) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    crux_access_result_t result = CRUX_ACCESS_ERR_SESSION_INVALID;
    const access_slot_t *slot = crux_access_slot_peek(session, crux_access_port_now_ms());
    if (slot != NULL) {
        *channel = (uint8_t)slot->origin.channel;
        *source_length = slot->origin.source_length;
        memset(source, 0, sizeof(slot->origin.source));
        memcpy(source, slot->origin.source, slot->origin.source_length);
        result = CRUX_ACCESS_OK;
    }
    crux_access_unlock_state();
    return result;
}

typedef enum { QUERY_USER, QUERY_ROLE } query_kind_t;

static crux_access_result_t session_name_query(crux_access_session_t session, query_kind_t kind,
                                               char *name, size_t size)
{
    if (!crux_access_state.ready) {
        return crux_access_refuse_not_ready(NULL);
    }
    if (name == NULL) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    access_expired_t expired = {0};
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    uint64_t now_ms = crux_access_port_now_ms();
    crux_access_result_t result = CRUX_ACCESS_ERR_SESSION_INVALID;
    const access_slot_t *slot = crux_access_slot_find(session, now_ms, &expired);
    if (slot != NULL) {
        const char *value = kind == QUERY_USER
                                ? slot->user
                                : crux_access_state.policy->roles[slot->role_index].name;
        size_t length = strlen(value);
        if (length < size) {
            memcpy(name, value, length + 1u);
            result = CRUX_ACCESS_OK;
        } else {
            result = CRUX_ACCESS_ERR_INVALID_ARGUMENT;
        }
    }
    crux_access_unlock_state();
    crux_access_session_report(result, &expired);
    return result;
}

crux_access_result_t crux_access_session_user(crux_access_session_t session, char *name,
                                              size_t size)
{
    return session_name_query(session, QUERY_USER, name, size);
}

crux_access_result_t crux_access_session_role(crux_access_session_t session, char *name,
                                              size_t size)
{
    return session_name_query(session, QUERY_ROLE, name, size);
}

crux_access_result_t crux_access_session_must_change_password(crux_access_session_t session,
                                                              bool *required)
{
    if (!crux_access_state.ready) {
        return crux_access_refuse_not_ready(NULL);
    }
    if (required == NULL) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    access_expired_t expired = {0};
    if (!crux_access_lock_state()) {
        return CRUX_ACCESS_ERR_NOT_READY;
    }
    uint64_t now_ms = crux_access_port_now_ms();
    crux_access_result_t result = CRUX_ACCESS_ERR_SESSION_INVALID;
    const access_slot_t *slot = crux_access_slot_find(session, now_ms, &expired);
    if (slot != NULL) {
        *required = slot->must_change_password;
        result = CRUX_ACCESS_OK;
    }
    crux_access_unlock_state();
    crux_access_session_report(result, &expired);
    return result;
}

#ifdef CRUX_ACCESS_TESTING
/* Simulates a reset for the instrumented test core. RAM is lost, and the locks are reused. */
void crux_access_test_reset(void);
void crux_access_test_set_generation(size_t slot, uint32_t generation);

void crux_access_test_reset(void)
{
    crux_mutex_t state_lock = crux_access_state.state_lock;
    crux_mutex_t change_lock = crux_access_state.change_lock;
    bool locks_created = crux_access_state.locks_created;
    memset(&crux_access_state, 0, sizeof(crux_access_state));
    crux_access_state.state_lock = state_lock;
    crux_access_state.change_lock = change_lock;
    crux_access_state.locks_created = locks_created;
}

void crux_access_test_set_generation(size_t slot, uint32_t generation)
{
    crux_access_state.slots[slot].generation = generation;
}
#endif
