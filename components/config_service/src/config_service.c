#include "config_service_port.h"

#include <crux/sync.h>

#include <string.h>

_Static_assert(CRUX_CONFIG_SERVICE_MAX >= 1 && CRUX_CONFIG_SERVICE_MAX <= 32,
               "Unsaved updates are tracked in a 32-bit mask");
/* U32_MAX has ten digits, and CRUX_CONFIG_SERVICE_TEXT_SIZE is promised to hold any value. */
_Static_assert(CRUX_CONFIG_STRING_MAX >= 10, "A value's text must fit the promised buffer");

typedef enum { NEED_READ, NEED_WRITE } need_t;

static const crux_config_service_entry_t *table;
static size_t table_count;
static crux_config_type_t
    types[CRUX_CONFIG_SERVICE_MAX]; /* Fixed at start, so read with no lock. */
static bool started;
static bool lock_created;

/* Guards every parameter in the table, each validator's call, the provider and the marks below. A
 * save holds it across storage, so only another save ever waits on it. */
static crux_mutex_t lock;
static uint32_t unsaved;

static uint32_t bit_of(size_t index)
{
    return (uint32_t)1u << index;
}

static crux_config_service_result_t from_access(crux_access_result_t result)
{
    switch (result) {
    case CRUX_ACCESS_OK:
        return CRUX_CONFIG_SERVICE_OK;
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        return CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID;
    case CRUX_ACCESS_ERR_NOT_READY:
        return CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    default:
        return CRUX_CONFIG_SERVICE_ERR_DENIED;
    }
}

static crux_config_service_result_t from_lock(crux_status_t result)
{
    switch (result) {
    case CRUX_OK:
        return CRUX_CONFIG_SERVICE_OK;
    case CRUX_ERR_BUSY:
    case CRUX_ERR_TIMEOUT:
        return CRUX_CONFIG_SERVICE_ERR_BUSY;
    default:
        /* Includes a validator that called back and relocked. Nothing has changed. */
        return CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    }
}

static crux_config_service_result_t poll_lock(void)
{
    return from_lock(crux_mutex_lock(lock, 0));
}

static void unlock(void)
{
    (void)crux_mutex_unlock(lock);
}

static size_t find(const char *name)
{
    for (size_t i = 0; i < table_count; ++i) {
        if (strcmp(crux_config_key(table[i].parameter), name) == 0) {
            return i;
        }
    }
    return table_count;
}

/*
 * Check the session and the permission this use needs, which records a denial. The answer then
 * depends on what the session may see: a session with neither permission gets what a missing name
 * gets. A NULL write permission is treated as not held, with no call for it.
 */
static crux_config_service_result_t permitted(const crux_config_service_entry_t *entry,
                                              crux_access_session_t session, need_t need)
{
    const char *wanted = need == NEED_READ ? entry->read_permission : entry->write_permission;
    const char *other = need == NEED_READ ? entry->write_permission : entry->read_permission;
    crux_access_result_t result = crux_access_check(session, wanted ? wanted : other);
    if (result == CRUX_ACCESS_OK) {
        return wanted ? CRUX_CONFIG_SERVICE_OK : CRUX_CONFIG_SERVICE_ERR_DENIED;
    }
    if (result != CRUX_ACCESS_ERR_DENIED) {
        return from_access(result);
    }
    bool visible = false;
    if (wanted && other) {
        result = crux_access_permitted(session, other, &visible);
        if (result != CRUX_ACCESS_OK) {
            return from_access(result);
        }
    }
    return visible ? CRUX_CONFIG_SERVICE_ERR_DENIED : CRUX_CONFIG_SERVICE_ERR_NOT_FOUND;
}

/* The session comes first on every operation, whatever else is wrong with the call. */
static crux_config_service_result_t session_usable(crux_access_session_t session)
{
    char user[CRUX_ACCESS_NAME_SIZE];
    return from_access(crux_access_session_user(session, user, sizeof(user)));
}

static crux_config_service_result_t enter(crux_access_session_t session, const char *name,
                                          need_t need, size_t *index)
{
    *index = name ? find(name) : table_count;
    if (*index == table_count) {
        crux_config_service_result_t result = session_usable(session);
        if (result != CRUX_CONFIG_SERVICE_OK) {
            return result;
        }
        return name ? CRUX_CONFIG_SERVICE_ERR_NOT_FOUND : CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    return permitted(&table[*index], session, need);
}

static void fill_item(size_t index, bool can_read, bool can_write, bool marked,
                      crux_config_service_item_t *item)
{
    const crux_config_service_entry_t *entry = &table[index];
    item->name = crux_config_key(entry->parameter);
    item->type = types[index];
    item->activation = entry->activation;
    item->sensitive = entry->sensitive;
    item->readable = can_read && !entry->sensitive;
    item->writable = can_write;
    item->unsaved = marked && can_write;
}

static bool permission_valid(const char *permission)
{
    return permission != NULL && permission[0] != '\0' &&
           memchr(permission, '\0', CRUX_ACCESS_PERMISSION_MAX + 1u) != NULL;
}

static bool entry_valid(const crux_config_service_entry_t *entry)
{
    if (crux_config_key(entry->parameter) == NULL || !permission_valid(entry->read_permission)) {
        return false;
    }
    if (entry->write_permission != NULL && !permission_valid(entry->write_permission)) {
        return false;
    }
    if (entry->sensitive && entry->write_permission == NULL) {
        return false;
    }
    return entry->activation == CRUX_CONFIG_ACTIVATION_APPLICATION ||
           entry->activation == CRUX_CONFIG_ACTIVATION_RESTART ||
           entry->activation == CRUX_CONFIG_ACTIVATION_SAFE_STATE;
}

static bool table_valid(const crux_config_service_entry_t *entries, size_t count)
{
    if (count > CRUX_CONFIG_SERVICE_MAX || (count > 0 && entries == NULL)) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!entry_valid(&entries[i])) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(crux_config_key(entries[i].parameter),
                       crux_config_key(entries[j].parameter)) == 0) {
                return false;
            }
        }
    }
    return true;
}

crux_config_service_result_t crux_config_service_start(const crux_config_service_entry_t *entries,
                                                       size_t count)
{
    if (started || !table_valid(entries, count)) {
        return CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (!lock_created) {
        if (crux_mutex_create(&lock) != CRUX_OK) {
            return CRUX_CONFIG_SERVICE_ERR_NOT_READY;
        }
        lock_created = true;
    }
    for (size_t i = 0; i < count; ++i) {
        crux_config_value_t value;
        if (crux_config_read(entries[i].parameter, &value) != CRUX_CONFIG_OK) {
            return CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
        }
        types[i] = value.type;
    }
    table = entries;
    table_count = count;
    started = true;
    return CRUX_CONFIG_SERVICE_OK;
}

crux_config_service_result_t crux_config_service_next(crux_access_session_t session, size_t *cursor,
                                                      crux_config_service_item_t *item)
{
    if (!started) {
        return CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    }
    crux_config_service_result_t result = session_usable(session);
    if (result != CRUX_CONFIG_SERVICE_OK) {
        return result;
    }
    if (cursor == NULL || item == NULL) {
        return CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    for (size_t position = *cursor; position < table_count; ++position) {
        const crux_config_service_entry_t *entry = &table[position];
        bool can_read = false;
        bool can_write = false;
        crux_access_result_t access =
            crux_access_permitted(session, entry->read_permission, &can_read);
        if (access == CRUX_ACCESS_OK && entry->write_permission != NULL) {
            access = crux_access_permitted(session, entry->write_permission, &can_write);
        }
        if (access != CRUX_ACCESS_OK) {
            return from_access(access);
        }
        if (!can_read && !can_write) {
            continue;
        }
        result = poll_lock();
        if (result != CRUX_CONFIG_SERVICE_OK) {
            return result;
        }
        bool marked = (unsaved & bit_of(position)) != 0;
        unlock();
        fill_item(position, can_read, can_write, marked, item);
        *cursor = position + 1;
        return CRUX_CONFIG_SERVICE_OK;
    }
    if (*cursor < table_count) {
        *cursor = table_count;
    }
    return CRUX_CONFIG_SERVICE_END;
}

/* The one text form of a value, for web and CLI alike. */
static crux_config_service_result_t format_value(const crux_config_value_t *value, char *text,
                                                 size_t capacity, size_t *length)
{
    char digits[11];
    const char *source;
    size_t size;
    switch (value->type) {
    case CRUX_CONFIG_BOOL:
        source = value->data.boolean ? "true" : "false";
        size = strlen(source);
        break;
    case CRUX_CONFIG_U32: {
        uint32_t rest = value->data.u32;
        size_t position = sizeof(digits);
        do {
            digits[--position] = (char)('0' + rest % 10u);
            rest /= 10u;
        } while (rest != 0);
        source = &digits[position];
        size = sizeof(digits) - position;
        break;
    }
    default:
        source = value->data.string.text;
        size = value->data.string.length;
        break;
    }
    if (size >= capacity) {
        return CRUX_CONFIG_SERVICE_ERR_TOO_SMALL;
    }
    memcpy(text, source, size);
    text[size] = '\0';
    *length = size;
    return CRUX_CONFIG_SERVICE_OK;
}

crux_config_service_result_t crux_config_service_read(crux_access_session_t session,
                                                      const char *name, char *text, size_t capacity,
                                                      size_t *length, crux_config_type_t *type)
{
    if (!started) {
        return CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    }
    size_t index = 0;
    crux_config_service_result_t result = enter(session, name, NEED_READ, &index);
    bool may_see = result == CRUX_CONFIG_SERVICE_OK || result == CRUX_CONFIG_SERVICE_ERR_DENIED;
    if (may_see && index < table_count && table[index].sensitive) {
        return CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY;
    }
    if (result != CRUX_CONFIG_SERVICE_OK) {
        return result;
    }
    if (text == NULL || length == NULL || type == NULL) {
        return CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    result = poll_lock();
    if (result != CRUX_CONFIG_SERVICE_OK) {
        return result;
    }
    crux_config_value_t value;
    if (crux_config_read(table[index].parameter, &value) != CRUX_CONFIG_OK) {
        result = CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    } else {
        /* A string borrows the parameter's storage, so it is copied before the lock goes. */
        result = format_value(&value, text, capacity, length);
    }
    unlock();
    if (result == CRUX_CONFIG_SERVICE_OK) {
        *type = types[index];
    }
    return result;
}

/* Decimal, with no sign or leading zeros, and an overflow check. Reads only length bytes. */
static bool parse_u32(const char *text, size_t length, uint32_t *out)
{
    if (length == 0 || length > 10 || (text[0] == '0' && length > 1)) {
        return false;
    }
    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
    }
    if (value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool parse_bool(const char *text, size_t length, bool *out)
{
    if (length == 4 && memcmp(text, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (length == 5 && memcmp(text, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

/*
 * Turn a client's text into a candidate. A string is copied to terminate it, into a buffer the
 * caller clears. Longer than any parameter can hold is a character-rule refusal, the same as the
 * capacity check on a shorter parameter, and never a copy.
 */
static crux_config_service_result_t parse_candidate(crux_config_type_t type, const char *text,
                                                    size_t length, char *buffer,
                                                    crux_config_value_t *candidate)
{
    candidate->type = type;
    if (type == CRUX_CONFIG_BOOL) {
        return parse_bool(text, length, &candidate->data.boolean)
                   ? CRUX_CONFIG_SERVICE_OK
                   : CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (type == CRUX_CONFIG_U32) {
        return parse_u32(text, length, &candidate->data.u32)
                   ? CRUX_CONFIG_SERVICE_OK
                   : CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (length > CRUX_CONFIG_STRING_MAX) {
        return CRUX_CONFIG_SERVICE_ERR_REJECTED;
    }
    memcpy(buffer, text, length);
    buffer[length] = '\0';
    candidate->data.string.text = buffer;
    candidate->data.string.length = length;
    return CRUX_CONFIG_SERVICE_OK;
}

static void clear_buffer(char *buffer, size_t size)
{
    volatile char *bytes = buffer;
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

/* Applies a candidate under the polled lock, and marks it. Lock results pass through. */
static crux_config_service_result_t apply(size_t index, const crux_config_value_t *candidate)
{
    crux_config_service_result_t result = poll_lock();
    if (result != CRUX_CONFIG_SERVICE_OK) {
        return result;
    }
    crux_config_result_t updated = crux_config_update(table[index].parameter, candidate);
    if (updated == CRUX_CONFIG_OK) {
        unsaved |= bit_of(index);
        result = CRUX_CONFIG_SERVICE_OK;
    } else if (updated == CRUX_CONFIG_ERR_REJECTED) {
        result = CRUX_CONFIG_SERVICE_ERR_REJECTED;
    } else {
        result = CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    }
    unlock();
    return result;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters): the accepted contract fixes this signature
crux_config_service_result_t crux_config_service_update(crux_access_session_t session,
                                                        const char *name, const char *text,
                                                        size_t length,
                                                        crux_config_service_item_t *applied)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    if (!started) {
        return CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    }
    size_t index = 0;
    crux_config_service_result_t result = enter(session, name, NEED_WRITE, &index);
    if (result != CRUX_CONFIG_SERVICE_OK) {
        return result;
    }
    if (text == NULL || applied == NULL) {
        return CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    const crux_config_service_entry_t *entry = &table[index];

    /* For the metadata only. A failed probe says not readable, and refuses nothing here. */
    bool can_read = false;
    if (crux_access_permitted(session, entry->read_permission, &can_read) != CRUX_ACCESS_OK) {
        can_read = false;
    }

    char buffer[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    crux_config_value_t candidate;
    result = parse_candidate(types[index], text, length, buffer, &candidate);
    if (result == CRUX_CONFIG_SERVICE_OK) {
        result = apply(index, &candidate);
    }
    clear_buffer(buffer, sizeof(buffer));

    const char *key = crux_config_key(entry->parameter);
    if (result == CRUX_CONFIG_SERVICE_OK) {
        fill_item(index, can_read, true, true, applied);
        crux_config_service_port_event(CRUX_CONFIG_SERVICE_EVENT_UPDATED, session, key);
    } else if (result == CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT ||
               result == CRUX_CONFIG_SERVICE_ERR_REJECTED) {
        crux_config_service_port_event(CRUX_CONFIG_SERVICE_EVENT_REJECTED, session, key);
    }
    return result;
}

/*
 * Save each marked parameter in table order under the lock, stopping at the first failure. Reports
 * which it saved and which failed, for a caller's events, because no event runs under the lock.
 */
static crux_config_service_result_t save_marked(crux_config_service_save_report_t *report,
                                                uint32_t *saved, size_t *failed)
{
    report->saved = 0;
    report->failed_name = NULL;
    report->failed_result = CRUX_CONFIG_OK;
    *saved = 0;
    *failed = table_count;
    crux_config_service_result_t result =
        from_lock(crux_mutex_lock(lock, CRUX_CONFIG_SERVICE_SAVE_WAIT_MS));
    if (result != CRUX_CONFIG_SERVICE_OK) {
        return result;
    }
    for (size_t i = 0; i < table_count; ++i) {
        if ((unsaved & bit_of(i)) == 0) {
            continue;
        }
        crux_config_result_t saved_result = crux_config_save(table[i].parameter);
        if (saved_result != CRUX_CONFIG_OK) {
            /* It may already have changed stored bytes, so the mark stays on. */
            report->failed_name = crux_config_key(table[i].parameter);
            report->failed_result = saved_result;
            *failed = i;
            result = CRUX_CONFIG_SERVICE_ERR_STORE;
            break;
        }
        unsaved &= ~bit_of(i);
        *saved |= bit_of(i);
        ++report->saved;
    }
    unlock();
    return result;
}

crux_config_service_result_t crux_config_service_save(crux_access_session_t session,
                                                      crux_config_service_save_report_t *report)
{
    if (!started) {
        return CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    }
    crux_config_service_result_t result =
        from_access(crux_access_check(session, CRUX_CONFIG_SERVICE_PERMISSION_SAVE));
    if (result != CRUX_CONFIG_SERVICE_OK) {
        return result;
    }
    if (report == NULL) {
        return CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    uint32_t saved = 0;
    size_t failed = table_count;
    result = save_marked(report, &saved, &failed);
    for (size_t i = 0; i < table_count; ++i) {
        if ((saved & bit_of(i)) != 0) {
            crux_config_service_port_event(CRUX_CONFIG_SERVICE_EVENT_SAVED, session,
                                           crux_config_key(table[i].parameter));
        }
    }
    if (failed != table_count) {
        crux_config_service_port_event(CRUX_CONFIG_SERVICE_EVENT_SAVE_FAILED, session,
                                       report->failed_name);
    }
    return result;
}

crux_config_service_result_t
crux_config_service_save_local(crux_config_service_save_report_t *report)
{
    if (!started) {
        return CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    }
    if (report == NULL) {
        return CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    uint32_t saved = 0;
    size_t failed = table_count;
    return save_marked(report, &saved, &failed);
}

crux_config_service_result_t crux_config_service_try_read(const char *name,
                                                          crux_config_value_t *out,
                                                          char *text_buffer, size_t capacity)
{
    if (!started) {
        return CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    }
    if (name == NULL || out == NULL) {
        return CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    size_t index = find(name);
    if (index == table_count) {
        return CRUX_CONFIG_SERVICE_ERR_NOT_FOUND;
    }
    if (types[index] == CRUX_CONFIG_STRING && text_buffer == NULL) {
        return CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    crux_config_service_result_t result = poll_lock();
    if (result != CRUX_CONFIG_SERVICE_OK) {
        return result;
    }
    crux_config_value_t value;
    if (crux_config_read(table[index].parameter, &value) != CRUX_CONFIG_OK) {
        result = CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    } else if (value.type == CRUX_CONFIG_STRING) {
        if (value.data.string.length >= capacity) {
            result = CRUX_CONFIG_SERVICE_ERR_TOO_SMALL;
        } else {
            memcpy(text_buffer, value.data.string.text, value.data.string.length + 1);
            value.data.string.text = text_buffer;
        }
    }
    unlock();
    if (result == CRUX_CONFIG_SERVICE_OK) {
        *out = value;
    }
    return result;
}

crux_config_service_result_t crux_config_service_try_update(const char *name,
                                                            const crux_config_value_t *candidate)
{
    if (!started) {
        return CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    }
    if (name == NULL || candidate == NULL) {
        return CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT;
    }
    size_t index = find(name);
    if (index == table_count) {
        return CRUX_CONFIG_SERVICE_ERR_NOT_FOUND;
    }
    return apply(index, candidate);
}
