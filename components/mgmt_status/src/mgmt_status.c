#include "mgmt_status_port.h"

#include <crux/configuration.h>
#include <crux/sync.h>

#include <stdatomic.h>
#include <string.h>

_Static_assert(CRUX_MGMT_STATUS_REFRESH_MS >= 1u, "A zero period would read on every poll");

typedef enum {
    ITEM_UPTIME,
    ITEM_CLOCK,
    ITEM_WATCHDOG,
    ITEM_RESET_CAUSES,
    ITEM_RESET_SUPPORTED,
    ITEM_NET_LINK,
    ITEM_NET_ADMIN,
    ITEM_NET_IPV4,
    ITEM_VERSION_PLATFORM,
    ITEM_VERSION_PROJECT,
    ITEM_CREDENTIAL_FIRST,
    ITEM_CREDENTIAL_STATE = ITEM_CREDENTIAL_FIRST,
    ITEM_CREDENTIAL_BASIS,
    ITEM_CREDENTIAL_EVIDENCE,
    ITEM_CREDENTIAL_CONFIDENTIAL,
    ITEM_CREDENTIAL_FORGERY,
    ITEM_CREDENTIAL_ROLLBACK,
    ITEM_CREDENTIAL_LAST = ITEM_CREDENTIAL_ROLLBACK,
    ITEM_EVENTS_FIRST,
    ITEM_EVENTS_EMITTED = ITEM_EVENTS_FIRST,
    ITEM_EVENTS_DROPPED,
    ITEM_EVENTS_DELIVERED,
    ITEM_EVENTS_LOG_ERRORS,
    ITEM_EVENTS_LAST = ITEM_EVENTS_LOG_ERRORS,
    ITEM_BUILTIN_COUNT
} builtin_t;

static const char *const builtin_names[ITEM_BUILTIN_COUNT] = {
    [ITEM_UPTIME] = "system.uptime_ms",
    [ITEM_CLOCK] = "system.clock",
    [ITEM_WATCHDOG] = "system.watchdog_state",
    [ITEM_RESET_CAUSES] = "reset.causes",
    [ITEM_RESET_SUPPORTED] = "reset.supported",
    [ITEM_NET_LINK] = "net.link",
    [ITEM_NET_ADMIN] = "net.admin_up",
    [ITEM_NET_IPV4] = "net.ipv4",
    [ITEM_VERSION_PLATFORM] = "version.platform",
    [ITEM_VERSION_PROJECT] = "version.project",
    [ITEM_CREDENTIAL_STATE] = "security.credential_state",
    [ITEM_CREDENTIAL_BASIS] = "security.credential_basis",
    [ITEM_CREDENTIAL_EVIDENCE] = "security.credential_evidence",
    [ITEM_CREDENTIAL_CONFIDENTIAL] = "security.credential_confidential",
    [ITEM_CREDENTIAL_FORGERY] = "security.credential_forgery_detected",
    [ITEM_CREDENTIAL_ROLLBACK] = "security.credential_rollback_detected",
    [ITEM_EVENTS_EMITTED] = "security.events_emitted",
    [ITEM_EVENTS_DROPPED] = "security.events_dropped",
    [ITEM_EVENTS_DELIVERED] = "security.events_delivered",
    [ITEM_EVENTS_LOG_ERRORS] = "security.events_log_errors",
};

static const char *const watchdog_words[] = {
    [CRUX_WATCHDOG_UNAVAILABLE] = "unavailable", [CRUX_WATCHDOG_IDLE] = "idle",
    [CRUX_WATCHDOG_ACTIVE] = "active",           [CRUX_WATCHDOG_FAULTED] = "faulted",
    [CRUX_WATCHDOG_EXPIRED] = "expired",
};

static const char *const clock_words[] = {
    [CRUX_MGMT_STATUS_CLOCK_UNSET] = "unset",       [CRUX_MGMT_STATUS_CLOCK_UNKNOWN] = "unknown",
    [CRUX_MGMT_STATUS_CLOCK_RTC] = "rtc",           [CRUX_MGMT_STATUS_CLOCK_MANUAL] = "manual",
    [CRUX_MGMT_STATUS_CLOCK_EXTERNAL] = "external",
};

#define COUNT_OF(table) (sizeof(table) / sizeof((table)[0]))

/* Fixed at start, so read with no lock. */
static bool started;
static bool lock_created;
static bool has_credential;
static bool has_events;
static char platform_version[CRUX_MGMT_STATUS_TEXT_SIZE];
static char project_version[CRUX_MGMT_STATUS_TEXT_SIZE];
static crux_status_t reset_status;
static crux_reset_cause_t reset_cause;
static const crux_mgmt_status_item_t *project_items;
static size_t project_item_count;

/*
 * The snapshot, decision C7. A caller that wins the polled lock and finds the answer older than the
 * period reads the sources, holding the lock, and publishes. A caller that loses the poll answers
 * from what is published and never waits behind a refresh. So that it can read with no lock, the
 * answer is one word, which is never seen half written. Zero means that no refresh has completed.
 */
#define WORD_REFRESHED 1u
#define WORD_WATCHDOG_RESULT_SHIFT 1u
#define WORD_WATCHDOG_STATE_SHIFT 5u
#define WORD_CLOCK_RESULT_SHIFT 8u
#define WORD_CLOCK_VALUE_SHIFT 12u
#define WORD_RESULT_MASK 0xFu
#define WORD_VALUE_MASK 0x7u

static crux_mutex_t snapshot_lock;
static uint64_t snapshot_ms; /* Guarded by the lock. */
static bool snapshot_taken;  /* Guarded by the lock. */
static atomic_uint_least32_t snapshot_word;

/* A result a client may see: a Base result, or an I/O failure for anything else from a source. */
static crux_status_t known_result(crux_status_t result)
{
    return crux_mgmt_status_result_name(result) != NULL ? result : CRUX_ERR_IO;
}

static uint32_t result_code(crux_status_t result)
{
    return (uint32_t) - (int)known_result(result);
}

static crux_status_t result_of_code(uint32_t code)
{
    return (crux_status_t) - (int)code;
}

static uint32_t read_snapshot_sources(void)
{
    crux_system_status_t system = {0};
    crux_status_t system_result = crux_mgmt_status_port_system(&system);
    crux_status_t watchdog_result =
        system_result != CRUX_OK ? system_result : system.watchdog_status;
    uint32_t watchdog_state = (uint32_t)system.watchdog_state;
    if (watchdog_result == CRUX_OK && watchdog_state >= COUNT_OF(watchdog_words)) {
        watchdog_result = CRUX_ERR_IO;
    }

    crux_mgmt_status_clock_t clock = CRUX_MGMT_STATUS_CLOCK_UNSET;
    crux_status_t clock_result = crux_mgmt_status_port_clock(&clock);
    if (clock_result == CRUX_OK && (uint32_t)clock >= COUNT_OF(clock_words)) {
        clock_result = CRUX_ERR_IO;
    }

    uint32_t word = WORD_REFRESHED;
    word |= result_code(watchdog_result) << WORD_WATCHDOG_RESULT_SHIFT;
    word |= result_code(clock_result) << WORD_CLOCK_RESULT_SHIFT;
    if (watchdog_result == CRUX_OK) {
        word |= watchdog_state << WORD_WATCHDOG_STATE_SHIFT;
    }
    if (clock_result == CRUX_OK) {
        word |= (uint32_t)clock << WORD_CLOCK_VALUE_SHIFT;
    }
    return word;
}

/* Returns the answer to use. A clock that fails or runs backwards reads nothing new: with no age to
 * go by, the limit on how often the sources are read matters more than a fresh answer. */
static uint32_t current_snapshot(void)
{
    if (crux_mutex_lock(snapshot_lock, 0) == CRUX_OK) {
        uint64_t now_ms = 0;
        if (crux_mgmt_status_port_uptime_ms(&now_ms) == CRUX_OK &&
            (!snapshot_taken ||
             (now_ms >= snapshot_ms && now_ms - snapshot_ms >= CRUX_MGMT_STATUS_REFRESH_MS))) {
            snapshot_ms = now_ms;
            snapshot_taken = true;
            atomic_store(&snapshot_word, read_snapshot_sources());
        }
        (void)crux_mutex_unlock(snapshot_lock);
    }
    return atomic_load(&snapshot_word);
}

static void set_failed(crux_mgmt_status_entry_t *entry, crux_status_t result)
{
    entry->status = known_result(result);
    memset(&entry->value, 0, sizeof(entry->value));
}

/* Finds the terminator inside the size of a text value, and holds the text to the character rule.
 */
static bool text_valid(const char *text, size_t *length)
{
    const char *end = text != NULL ? memchr(text, '\0', CRUX_MGMT_STATUS_TEXT_SIZE) : NULL;
    if (end == NULL || !crux_config_text_valid(text, (size_t)(end - text))) {
        return false;
    }
    *length = (size_t)(end - text);
    return true;
}

static void set_text(crux_mgmt_status_entry_t *entry, const char *text)
{
    size_t length = 0;
    if (!text_valid(text, &length)) {
        set_failed(entry, CRUX_ERR_IO);
        return;
    }
    entry->status = CRUX_OK;
    entry->value.type = CRUX_MGMT_STATUS_TEXT;
    memcpy(entry->value.data.text, text, length + 1);
}

static void set_bool(crux_mgmt_status_entry_t *entry, bool value)
{
    entry->status = CRUX_OK;
    entry->value.type = CRUX_MGMT_STATUS_BOOL;
    entry->value.data.boolean = value;
}

static void set_u32(crux_mgmt_status_entry_t *entry, uint32_t value)
{
    entry->status = CRUX_OK;
    entry->value.type = CRUX_MGMT_STATUS_U32;
    entry->value.data.u32 = value;
}

static void fill_uptime(crux_mgmt_status_entry_t *entry)
{
    uint64_t uptime_ms = 0;
    crux_status_t result = crux_mgmt_status_port_uptime_ms(&uptime_ms);
    if (result != CRUX_OK) {
        set_failed(entry, result);
        return;
    }
    entry->status = CRUX_OK;
    entry->value.type = CRUX_MGMT_STATUS_U64;
    entry->value.data.u64 = uptime_ms;
}

/* Where one item's answer sits in the word, and its words. */
typedef struct {
    uint32_t result_shift;
    uint32_t value_shift;
    const char *const *words;
} snapshot_field_t;

static const snapshot_field_t clock_field = {WORD_CLOCK_RESULT_SHIFT, WORD_CLOCK_VALUE_SHIFT,
                                             clock_words};
static const snapshot_field_t watchdog_field = {WORD_WATCHDOG_RESULT_SHIFT,
                                                WORD_WATCHDOG_STATE_SHIFT, watchdog_words};

static void fill_from_snapshot(crux_mgmt_status_entry_t *entry, const snapshot_field_t *field)
{
    uint32_t word = current_snapshot();
    if ((word & WORD_REFRESHED) == 0) {
        set_failed(entry, CRUX_ERR_BUSY);
        return;
    }
    crux_status_t result = result_of_code((word >> field->result_shift) & WORD_RESULT_MASK);
    if (result != CRUX_OK) {
        set_failed(entry, result);
        return;
    }
    set_text(entry, field->words[(word >> field->value_shift) & WORD_VALUE_MASK]);
}

static void fill_reset(crux_mgmt_status_entry_t *entry, uint32_t value)
{
    if (reset_status != CRUX_OK) {
        set_failed(entry, reset_status);
        return;
    }
    set_u32(entry, value);
}

static size_t format_octet(char *out, uint8_t octet)
{
    size_t length = 0;
    if (octet >= 100u) {
        out[length++] = (char)('0' + octet / 100u);
    }
    if (octet >= 10u) {
        out[length++] = (char)('0' + (octet / 10u) % 10u);
    }
    out[length++] = (char)('0' + octet % 10u);
    return length;
}

static void fill_net(builtin_t item, crux_mgmt_status_entry_t *entry)
{
    crux_mgmt_status_net_t net = {0};
    crux_status_t result = crux_mgmt_status_port_net(&net);
    if (result != CRUX_OK) {
        set_failed(entry, result);
    } else if (item == ITEM_NET_ADMIN) {
        set_bool(entry, net.admin_up);
    } else if (item == ITEM_NET_LINK) {
        if (net.link_known) {
            set_bool(entry, net.link_up);
        } else {
            set_failed(entry, CRUX_ERR_NO_DATA);
        }
    } else if (!net.ipv4_available) {
        set_failed(entry, CRUX_ERR_NO_DATA);
    } else {
        char text[16];
        size_t length = 0;
        for (size_t i = 0; i < 4; ++i) {
            if (i > 0) {
                text[length++] = '.';
            }
            length += format_octet(&text[length], net.ipv4[i]);
        }
        text[length] = '\0';
        set_text(entry, text);
    }
}

static void fill_credential(builtin_t item, crux_mgmt_status_entry_t *entry)
{
    crux_mgmt_status_credential_t credential = {0};
    crux_status_t result = crux_mgmt_status_port_credential(&credential);
    if (result != CRUX_OK) {
        set_failed(entry, result);
        return;
    }
    switch (item) {
    case ITEM_CREDENTIAL_STATE:
        set_text(entry, credential.state);
        break;
    case ITEM_CREDENTIAL_BASIS:
        set_text(entry, credential.basis);
        break;
    case ITEM_CREDENTIAL_EVIDENCE:
        set_text(entry, credential.evidence);
        break;
    case ITEM_CREDENTIAL_CONFIDENTIAL:
        set_bool(entry, credential.confidential);
        break;
    case ITEM_CREDENTIAL_FORGERY:
        set_bool(entry, credential.forgery_detected);
        break;
    default:
        set_bool(entry, credential.rollback_detected);
        break;
    }
}

static void fill_events(builtin_t item, crux_mgmt_status_entry_t *entry)
{
    crux_mgmt_status_event_counts_t counts = {0};
    crux_status_t result = crux_mgmt_status_port_security_events(&counts);
    if (result != CRUX_OK) {
        set_failed(entry, result);
        return;
    }
    switch (item) {
    case ITEM_EVENTS_EMITTED:
        set_u32(entry, counts.emitted);
        break;
    case ITEM_EVENTS_DROPPED:
        set_u32(entry, counts.dropped);
        break;
    case ITEM_EVENTS_DELIVERED:
        set_u32(entry, counts.delivered);
        break;
    default:
        set_u32(entry, counts.log_errors);
        break;
    }
}

static void fill_builtin(builtin_t item, crux_mgmt_status_entry_t *entry)
{
    entry->name = builtin_names[item];
    switch (item) {
    case ITEM_UPTIME:
        fill_uptime(entry);
        break;
    case ITEM_CLOCK:
        fill_from_snapshot(entry, &clock_field);
        break;
    case ITEM_WATCHDOG:
        fill_from_snapshot(entry, &watchdog_field);
        break;
    case ITEM_RESET_CAUSES:
        fill_reset(entry, reset_cause.causes);
        break;
    case ITEM_RESET_SUPPORTED:
        fill_reset(entry, reset_cause.supported);
        break;
    case ITEM_NET_LINK:
    case ITEM_NET_ADMIN:
    case ITEM_NET_IPV4:
        fill_net(item, entry);
        break;
    case ITEM_VERSION_PLATFORM:
        set_text(entry, platform_version);
        break;
    case ITEM_VERSION_PROJECT:
        set_text(entry, project_version);
        break;
    default:
        if (item <= ITEM_CREDENTIAL_LAST) {
            fill_credential(item, entry);
        } else {
            fill_events(item, entry);
        }
        break;
    }
}

/* A source's answer, zeroed first, and held to the same rules as a built-in's. */
static void fill_project(const crux_mgmt_status_item_t *item, crux_mgmt_status_entry_t *entry)
{
    entry->name = item->name;
    crux_mgmt_status_value_t value;
    memset(&value, 0, sizeof(value));
    crux_status_t result = item->read(item->context, &value);
    if (result != CRUX_OK) {
        set_failed(entry, result);
        return;
    }
    switch (value.type) {
    case CRUX_MGMT_STATUS_BOOL:
    case CRUX_MGMT_STATUS_U32:
    case CRUX_MGMT_STATUS_U64:
        entry->status = CRUX_OK;
        entry->value = value;
        break;
    case CRUX_MGMT_STATUS_TEXT:
        /* It needs a terminator inside its array, which set_text finds or refuses. */
        set_text(entry, value.data.text);
        break;
    default:
        set_failed(entry, CRUX_ERR_IO);
        break;
    }
}

static bool builtin_linked(builtin_t item)
{
    if (item >= ITEM_CREDENTIAL_FIRST && item <= ITEM_CREDENTIAL_LAST) {
        return has_credential;
    }
    if (item >= ITEM_EVENTS_FIRST && item <= ITEM_EVENTS_LAST) {
        return has_events;
    }
    return true;
}

static crux_status_t from_access(crux_access_result_t result)
{
    return result == CRUX_ACCESS_ERR_SESSION_INVALID ? CRUX_ERR_INVALID_STATE : CRUX_ERR_NOT_READY;
}

crux_status_t crux_mgmt_status_next(crux_access_session_t session, size_t *cursor,
                                    crux_mgmt_status_entry_t *entry)
{
    if (!started) {
        return CRUX_ERR_NOT_READY;
    }
    /* Runs on each call. It also answers for every built-in item, which share one permission. */
    bool may_read_builtin = false;
    crux_access_result_t access =
        crux_access_permitted(session, CRUX_MGMT_STATUS_PERMISSION_READ, &may_read_builtin);
    if (access != CRUX_ACCESS_OK) {
        return from_access(access);
    }
    if (cursor == NULL || entry == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }

    size_t total = (size_t)ITEM_BUILTIN_COUNT + project_item_count;
    for (size_t position = *cursor; position < total; ++position) {
        crux_mgmt_status_entry_t found;
        memset(&found, 0, sizeof(found));
        if (position < ITEM_BUILTIN_COUNT) {
            if (!may_read_builtin || !builtin_linked((builtin_t)position)) {
                continue;
            }
            fill_builtin((builtin_t)position, &found);
        } else {
            const crux_mgmt_status_item_t *item = &project_items[position - ITEM_BUILTIN_COUNT];
            bool may_read = false;
            access = crux_access_permitted(session, item->permission, &may_read);
            if (access != CRUX_ACCESS_OK) {
                return from_access(access);
            }
            if (!may_read) {
                continue;
            }
            fill_project(item, &found);
        }
        *entry = found;
        *cursor = position + 1;
        return CRUX_OK;
    }
    if (*cursor < total) {
        *cursor = total;
    }
    return CRUX_ERR_NO_DATA;
}

static bool permission_valid(const char *permission)
{
    return permission != NULL && permission[0] != '\0' &&
           memchr(permission, '\0', CRUX_ACCESS_PERMISSION_MAX + 1u) != NULL;
}

static bool name_valid(const char *name)
{
    if (name == NULL) {
        return false;
    }
    size_t length = 0;
    for (; name[length] != '\0'; ++length) {
        char c = name[length];
        bool allowed =
            (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!allowed || length == CRUX_MGMT_STATUS_NAME_MAX) {
            return false;
        }
    }
    if (length == 0) {
        return false;
    }
    static const char *const reserved[] = {"system.", "reset.", "net.", "version.", "security."};
    for (size_t i = 0; i < COUNT_OF(reserved); ++i) {
        if (strncmp(name, reserved[i], strlen(reserved[i])) == 0) {
            return false;
        }
    }
    return true;
}

static bool items_valid(const crux_mgmt_status_item_t *items, size_t count)
{
    if (count > CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX || (count > 0 && items == NULL)) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!name_valid(items[i].name) || !permission_valid(items[i].permission) ||
            items[i].read == NULL) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(items[i].name, items[j].name) == 0) {
                return false;
            }
        }
    }
    return true;
}

crux_status_t crux_mgmt_status_start(const crux_mgmt_status_config_t *config)
{
    if (started) {
        return CRUX_ERR_INVALID_STATE;
    }
    size_t platform_length = 0;
    size_t project_length = 0;
    if (config == NULL || !text_valid(config->platform_version, &platform_length) ||
        !text_valid(config->project_version, &project_length) ||
        !items_valid(config->project_items, config->project_item_count)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!lock_created) {
        crux_status_t result = crux_mutex_create(&snapshot_lock);
        if (result != CRUX_OK) {
            return result;
        }
        lock_created = true;
    }
    memcpy(platform_version, config->platform_version, platform_length + 1);
    memcpy(project_version, config->project_version, project_length + 1);
    reset_status = known_result(config->boot_reset_cause_status);
    reset_cause = config->boot_reset_cause;
    project_items = config->project_items;
    project_item_count = config->project_item_count;
    has_credential = crux_mgmt_status_port_has_credential();
    has_events = crux_mgmt_status_port_has_security_events();
    started = true;
    return CRUX_OK;
}
