#include "file_service_port.h"
#include <crux/sync.h>
#include <stdio.h>
#include <string.h>

_Static_assert(
    CRUX_FILE_SERVICE_TRANSFER_MAX < CRUX_FILE_POOL_SIZE,
    "One File handle must stay free from the service for the credential store and project");
_Static_assert(CRUX_FILE_SERVICE_AREAS_MAX >= 1, "The file service needs an area");
_Static_assert(CRUX_FILE_SERVICE_DIR_MAX >= 1, "The file service needs a listing handle");
_Static_assert(CRUX_FILE_SERVICE_RESERVATIONS >= 2, "A rename reserves two names");
_Static_assert(CRUX_FILE_SERVICE_TEMP_SLOTS >= 1 && CRUX_FILE_SERVICE_TEMP_SLOTS <= 32,
               "Temporary names are tracked in a 32-bit mask");

#define STATE_LOCK_WAIT_MS 1000u
#define NAME_CAPACITY 64u
#define PATH_MAX_BYTES 80u
#define AREA_NAME_MAX 15u
#define TEMP_PREFIX ".crux-up-"
#define SIDECAR_PREFIX ".crux-co-"
#define RESERVED_PREFIX ".crux-"
/* Sidecar: magic, one length byte, the target name, and an additive check of all the bytes before.
 */
#define SIDECAR_MAGIC "CXCO"
#define SIDECAR_MAGIC_BYTES 4u
#define SIDECAR_BYTES (SIDECAR_MAGIC_BYTES + 1u + NAME_CAPACITY + 1u)

typedef enum { AREA_READY, AREA_NOT_READY, AREA_FAILED } area_status_t;

typedef struct {
    area_status_t status;
    uint64_t used_bytes;
    uint32_t entries;
    uint32_t claimed_slots; /* Temporary names a live upload has taken, so no other can. */
} area_state_t;

typedef enum { NEED_READ, NEED_WRITE } need_t;

typedef enum { KIND_READ, KIND_MANAGED, KIND_DIRECT } kind_t;

struct crux_file_service_transfer {
    bool live;
    kind_t kind;
    crux_access_session_t session;
    size_t area;
    crux_file_t *file;
    char name[NAME_CAPACITY];
    int temp_slot;
    uint64_t size;
    uint64_t position;
    bool position_known;
    bool holds_name;
};

struct crux_file_service_dir {
    bool live;
    crux_access_session_t session;
    size_t area;
    crux_file_dir_t *dir;
};

typedef struct {
    bool used;
    size_t area;
    char name[NAME_CAPACITY];
} reservation_t;

static const crux_file_service_config_t *config;
static crux_mutex_t lock;
static area_state_t areas[CRUX_FILE_SERVICE_AREAS_MAX];
static struct crux_file_service_transfer transfers[CRUX_FILE_SERVICE_TRANSFER_MAX];
static struct crux_file_service_dir listings[CRUX_FILE_SERVICE_DIR_MAX];
static reservation_t reservations[CRUX_FILE_SERVICE_RESERVATIONS];

/* The lock guards counters, pools and reservations, and it is never held across storage. */
static void state_lock(void)
{
    while (crux_mutex_lock(lock, STATE_LOCK_WAIT_MS) != CRUX_OK) {
    }
}

static void state_unlock(void)
{
    (void)crux_mutex_unlock(lock);
}

static crux_file_service_result_t from_file(crux_file_result_t result)
{
    switch (result) {
    case CRUX_FILE_OK:
    case CRUX_FILE_PARTIAL:
        return CRUX_FILE_SERVICE_OK;
    case CRUX_FILE_ERR_INVALID_ARGUMENT:
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    case CRUX_FILE_ERR_INVALID_STATE:
        return CRUX_FILE_SERVICE_ERR_BUSY;
    case CRUX_FILE_ERR_NOT_READY:
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    case CRUX_FILE_ERR_NOT_FOUND:
        return CRUX_FILE_SERVICE_ERR_NOT_FOUND;
    case CRUX_FILE_ERR_NO_SPACE:
        return CRUX_FILE_SERVICE_ERR_NO_SPACE;
    case CRUX_FILE_ERR_NO_RESOURCES:
        return CRUX_FILE_SERVICE_ERR_NO_RESOURCES;
    case CRUX_FILE_ERR_OUT_OF_RANGE:
        return CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE;
    case CRUX_FILE_ERR_EXISTS:
        return CRUX_FILE_SERVICE_ERR_EXISTS;
    default:
        return CRUX_FILE_SERVICE_ERR_IO;
    }
}

static crux_file_service_result_t from_access(crux_access_result_t result)
{
    switch (result) {
    case CRUX_ACCESS_OK:
        return CRUX_FILE_SERVICE_OK;
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        return CRUX_FILE_SERVICE_ERR_SESSION_INVALID;
    case CRUX_ACCESS_ERR_DENIED:
        return CRUX_FILE_SERVICE_ERR_DENIED;
    case CRUX_ACCESS_ERR_INVALID_ARGUMENT:
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    default:
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
}

static void event(crux_file_service_event_t type, crux_access_session_t session, size_t area,
                  const char *name, const char *new_name)
{
    crux_file_service_port_event(type, session, config->areas[area].name, name, new_name, false);
}

/* A medium that fails is recorded, with only the area, because a client's name can hold anything.
 */
static crux_file_service_result_t storage_result(crux_file_result_t result,
                                                 crux_access_session_t session, size_t area)
{
    crux_file_service_result_t mapped = from_file(result);
    if (mapped == CRUX_FILE_SERVICE_ERR_NO_SPACE || mapped == CRUX_FILE_SERVICE_ERR_IO) {
        crux_file_service_port_event(CRUX_FILE_SERVICE_EVENT_STORAGE_FAILED, session,
                                     config->areas[area].name, NULL, NULL,
                                     mapped == CRUX_FILE_SERVICE_ERR_NO_SPACE);
    }
    return mapped;
}

/* The File name rule, which File keeps private and which resolve must apply with no storage call.
 */
static bool name_valid(const char *name)
{
    if (!name || !*name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
    for (size_t i = 0;; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == 0) {
            return true;
        }
        if (i >= NAME_CAPACITY - 1 ||
            !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
}

static bool client_name_valid(const char *name)
{
    return name_valid(name) && strncmp(name, RESERVED_PREFIX, strlen(RESERVED_PREFIX)) != 0;
}

static bool area_name_valid(const char *name)
{
    if (!name || !*name || strlen(name) > AREA_NAME_MAX) {
        return false;
    }
    for (const char *p = name; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) {
            return false;
        }
    }
    return true;
}

static size_t area_index(const char *name, size_t length)
{
    for (size_t i = 0; i < config->area_count; i++) {
        const char *declared = config->areas[i].name;
        if (strlen(declared) == length && strncmp(declared, name, length) == 0) {
            return i;
        }
    }
    return config->area_count;
}

static crux_file_service_result_t area_available(size_t area)
{
    switch (areas[area].status) {
    case AREA_READY:
        return CRUX_FILE_SERVICE_OK;
    case AREA_NOT_READY:
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    default:
        return CRUX_FILE_SERVICE_ERR_IO;
    }
}

/*
 * Check the session and the permission that this use needs, which records a denial. The answer then
 * depends on what the session may see: a session with neither permission gets what a missing area
 * gets, and the probe of the other permission records nothing more.
 */
static crux_file_service_result_t permitted(crux_access_session_t session, size_t area, need_t need)
{
    const crux_file_area_t *declared = &config->areas[area];
    const char *wanted = need == NEED_READ ? declared->read_permission : declared->write_permission;
    const char *other = need == NEED_READ ? declared->write_permission : declared->read_permission;
    crux_access_result_t result = crux_access_check(session, wanted ? wanted : other);
    if (result == CRUX_ACCESS_OK) {
        return wanted ? CRUX_FILE_SERVICE_OK : CRUX_FILE_SERVICE_ERR_DENIED;
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
    return visible ? CRUX_FILE_SERVICE_ERR_DENIED : CRUX_FILE_SERVICE_ERR_NOT_FOUND;
}

/* The common start of an operation on a named area, with the area's own state checked last. */
static crux_file_service_result_t enter_area(crux_access_session_t session, const char *name,
                                             need_t need, size_t *area)
{
    if (!config) {
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
    *area = name ? area_index(name, strlen(name)) : config->area_count;
    if (*area == config->area_count) {
        char user[CRUX_ACCESS_NAME_SIZE];
        crux_access_result_t result = crux_access_session_user(session, user, sizeof(user));
        if (result != CRUX_ACCESS_OK) {
            return from_access(result);
        }
        return name ? CRUX_FILE_SERVICE_ERR_NOT_FOUND : CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    crux_file_service_result_t result = permitted(session, *area, need);
    return result != CRUX_FILE_SERVICE_OK ? result : area_available(*area);
}

static bool reservation_held(size_t area, const char *name)
{
    for (size_t i = 0; i < CRUX_FILE_SERVICE_RESERVATIONS; i++) {
        if (reservations[i].used && reservations[i].area == area &&
            strcmp(reservations[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static void reservation_put(size_t index, size_t area, const char *name)
{
    reservations[index].used = true;
    reservations[index].area = area;
    strcpy(reservations[index].name, name);
}

/* Take one or two names, or neither, so that a stat and the open after it cannot be split. */
static crux_file_service_result_t reserve(size_t area, const char *first, const char *second)
{
    crux_file_service_result_t result = CRUX_FILE_SERVICE_ERR_NO_RESOURCES;
    state_lock();
    size_t a = CRUX_FILE_SERVICE_RESERVATIONS;
    size_t b = CRUX_FILE_SERVICE_RESERVATIONS;
    for (size_t i = 0; i < CRUX_FILE_SERVICE_RESERVATIONS; i++) {
        if (!reservations[i].used) {
            if (a == CRUX_FILE_SERVICE_RESERVATIONS) {
                a = i;
            } else if (b == CRUX_FILE_SERVICE_RESERVATIONS) {
                b = i;
            }
        }
    }
    if (reservation_held(area, first) || (second && reservation_held(area, second))) {
        result = CRUX_FILE_SERVICE_ERR_BUSY;
    } else if (a != CRUX_FILE_SERVICE_RESERVATIONS &&
               (!second || b != CRUX_FILE_SERVICE_RESERVATIONS)) {
        reservation_put(a, area, first);
        if (second) {
            reservation_put(b, area, second);
        }
        result = CRUX_FILE_SERVICE_OK;
    }
    state_unlock();
    return result;
}

static void release(size_t area, const char *name)
{
    state_lock();
    for (size_t i = 0; i < CRUX_FILE_SERVICE_RESERVATIONS; i++) {
        if (reservations[i].used && reservations[i].area == area &&
            strcmp(reservations[i].name, name) == 0) {
            reservations[i].used = false;
            break;
        }
    }
    state_unlock();
}

static void counters_add(size_t area, uint64_t bytes, uint32_t entries)
{
    state_lock();
    areas[area].used_bytes += bytes;
    areas[area].entries += entries;
    state_unlock();
}

static void counters_remove(size_t area, uint64_t bytes, uint32_t entries)
{
    state_lock();
    areas[area].used_bytes -= bytes < areas[area].used_bytes ? bytes : areas[area].used_bytes;
    areas[area].entries -= entries < areas[area].entries ? entries : areas[area].entries;
    state_unlock();
}

/* Reserve room under the lock, so that two transfers cannot both pass the same check. */
static bool room_reserve(size_t area, uint64_t bytes, uint32_t entries)
{
    const crux_file_area_t *declared = &config->areas[area];
    state_lock();
    /* Only what grows is checked, so that an area over a bound can still overwrite. */
    bool fits = (bytes == 0 || (bytes <= declared->max_area_bytes &&
                                areas[area].used_bytes <= declared->max_area_bytes - bytes)) &&
                (entries == 0 || areas[area].entries + entries <= declared->max_entries);
    if (fits) {
        areas[area].used_bytes += bytes;
        areas[area].entries += entries;
    }
    state_unlock();
    return fits;
}

static bool has_partial_suffix(const char *name, const char *suffix)
{
    size_t length = strlen(name);
    size_t suffix_length = strlen(suffix);
    return length > suffix_length && strcmp(&name[length - suffix_length], suffix) == 0;
}

/*
 * The stale partial rule: remove files with the area's declared suffix, in listing order, until the
 * reservation fits. A file with a handle or a reservation stays. A missing listing handle leaves
 * nothing removed, and the caller then reports the limit.
 */
static bool room_reserve_or_clear(crux_access_session_t session, size_t area, uint64_t bytes,
                                  uint32_t entries)
{
    if (room_reserve(area, bytes, entries)) {
        return true;
    }
    const crux_file_area_t *declared = &config->areas[area];
    /* More than the whole area never fits, so it costs no partial file. */
    if (!declared->partial_suffix || bytes > declared->max_area_bytes) {
        return false;
    }
    crux_file_dir_t *dir = NULL;
    if (crux_file_dir_open(declared->root, &dir) != CRUX_FILE_OK) {
        return false;
    }
    bool fits = false;
    for (;;) {
        char name[NAME_CAPACITY];
        crux_file_info_t info;
        bool end = false;
        if (crux_file_dir_read(dir, name, sizeof(name), &info, &end) != CRUX_FILE_OK || end) {
            break;
        }
        /* A suffix can match the service's own names, and those are not partial files. */
        if (strncmp(name, RESERVED_PREFIX, strlen(RESERVED_PREFIX)) == 0 ||
            !has_partial_suffix(name, declared->partial_suffix) ||
            reserve(area, name, NULL) != CRUX_FILE_SERVICE_OK) {
            continue;
        }
        crux_file_result_t removed = crux_file_remove_at(declared->root, name);
        release(area, name);
        if (removed == CRUX_FILE_OK) {
            counters_remove(area, info.size, 1);
            event(CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED, session, area, name, NULL);
            if (room_reserve(area, bytes, entries)) {
                fits = true;
                break;
            }
        }
    }
    (void)crux_file_dir_close(&dir);
    return fits;
}

static void temp_name(char *name, const char *prefix, int slot)
{
    (void)snprintf(name, NAME_CAPACITY, "%s%d", prefix, slot);
}

static int slot_of_name(const char *name, const char *prefix)
{
    size_t length = strlen(prefix);
    if (strncmp(name, prefix, length) != 0 || !name[length]) {
        return -1;
    }
    int slot = 0;
    for (const char *p = &name[length]; *p; p++) {
        if (*p < '0' || *p > '9' || slot >= CRUX_FILE_SERVICE_TEMP_SLOTS) {
            return -1;
        }
        slot = slot * 10 + (*p - '0');
    }
    return slot < CRUX_FILE_SERVICE_TEMP_SLOTS ? slot : -1;
}

static uint8_t sidecar_check(const uint8_t *bytes, size_t length)
{
    uint8_t sum = 0x5a;
    for (size_t i = 0; i < length; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum;
}

static crux_file_result_t sidecar_write(crux_file_root_t root, int slot, const char *target)
{
    char name[NAME_CAPACITY];
    uint8_t record[SIDECAR_BYTES];
    size_t length = strlen(target);
    memcpy(record, SIDECAR_MAGIC, SIDECAR_MAGIC_BYTES);
    record[SIDECAR_MAGIC_BYTES] = (uint8_t)length;
    memcpy(&record[SIDECAR_MAGIC_BYTES + 1], target, length);
    size_t body = SIDECAR_MAGIC_BYTES + 1 + length;
    record[body] = sidecar_check(record, body);

    temp_name(name, SIDECAR_PREFIX, slot);
    crux_file_t *file = NULL;
    crux_file_result_t result = crux_file_open_at(root, name, CRUX_FILE_CREATE_REPLACE, &file);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    size_t count = 0;
    result = crux_file_write(file, record, body + 1, &count);
    if (result == CRUX_FILE_PARTIAL) {
        result = CRUX_FILE_ERR_IO;
    }
    if (result == CRUX_FILE_OK) {
        result = crux_file_sync(file);
    }
    crux_file_result_t closed = crux_file_close(&file);
    return result != CRUX_FILE_OK ? result : closed;
}

typedef enum { SIDECAR_VALID, SIDECAR_TORN, SIDECAR_UNREADABLE } sidecar_state_t;

/* A torn record means no removal or rename began. An unreadable one says nothing at all, and the
 * temporary may be the only copy of the file, so the two must stay apart. */
static sidecar_state_t sidecar_read(crux_file_root_t root, int slot, char *target)
{
    char name[NAME_CAPACITY];
    uint8_t record[SIDECAR_BYTES];
    temp_name(name, SIDECAR_PREFIX, slot);
    crux_file_t *file = NULL;
    crux_file_result_t result = crux_file_open_at(root, name, CRUX_FILE_READ, &file);
    if (result != CRUX_FILE_OK) {
        return result == CRUX_FILE_ERR_NOT_FOUND ? SIDECAR_TORN : SIDECAR_UNREADABLE;
    }
    size_t count = 0;
    result = crux_file_read(file, record, sizeof(record), &count);
    (void)crux_file_close(&file);
    if (result != CRUX_FILE_OK) {
        return SIDECAR_UNREADABLE;
    }
    if (count < SIDECAR_MAGIC_BYTES + 2 ||
        memcmp(record, SIDECAR_MAGIC, SIDECAR_MAGIC_BYTES) != 0) {
        return SIDECAR_TORN;
    }
    size_t length = record[SIDECAR_MAGIC_BYTES];
    size_t body = SIDECAR_MAGIC_BYTES + 1 + length;
    if (length >= NAME_CAPACITY || count != body + 1 ||
        record[body] != sidecar_check(record, body)) {
        return SIDECAR_TORN;
    }
    memcpy(target, &record[SIDECAR_MAGIC_BYTES + 1], length);
    target[length] = '\0';
    return client_name_valid(target) ? SIDECAR_VALID : SIDECAR_TORN;
}

/* Remove a temporary file that start found, keeping the counters true. A missing one is fine. */
static bool recovery_remove(size_t area, const char *name)
{
    crux_file_root_t root = config->areas[area].root;
    crux_file_info_t info;
    crux_file_result_t result = crux_file_stat(root, name, &info);
    if (result == CRUX_FILE_ERR_NOT_FOUND) {
        return true;
    }
    if (result != CRUX_FILE_OK || crux_file_remove_at(root, name) != CRUX_FILE_OK) {
        return false;
    }
    counters_remove(area, info.size, 1);
    return true;
}

/* The table of states in docs/file-service-contract.md. False stops the area until a reset. */
static bool recover_slot(size_t area, int slot, bool has_temp, bool has_sidecar)
{
    const crux_access_session_t none = {0};
    crux_file_root_t root = config->areas[area].root;
    char temp[NAME_CAPACITY];
    char sidecar[NAME_CAPACITY];
    char target[NAME_CAPACITY];
    temp_name(temp, TEMP_PREFIX, slot);
    temp_name(sidecar, SIDECAR_PREFIX, slot);

    if (!has_temp) {
        return recovery_remove(area, sidecar);
    }
    sidecar_state_t state = has_sidecar ? sidecar_read(root, slot, target) : SIDECAR_TORN;
    if (state == SIDECAR_UNREADABLE) {
        return false;
    }
    if (state == SIDECAR_TORN) {
        return recovery_remove(area, temp) && recovery_remove(area, sidecar);
    }

    crux_file_info_t temp_info;
    crux_file_info_t target_info;
    crux_file_result_t result = crux_file_stat(root, temp, &temp_info);
    if (result != CRUX_FILE_OK) {
        return false;
    }
    result = crux_file_stat(root, target, &target_info);
    if (result == CRUX_FILE_ERR_NOT_FOUND) {
        /* The removal happened, so all that was left was the rename. */
        if (crux_file_rename(root, temp, target) != CRUX_FILE_OK) {
            return false;
        }
        return recovery_remove(area, sidecar);
    }
    if (result != CRUX_FILE_OK) {
        return false;
    }
    if (target_info.size != temp_info.size) {
        /* The removal had not begun, so the old target stands. */
        return recovery_remove(area, temp) && recovery_remove(area, sidecar);
    }
    /* It may be an unfinished removal, or share clusters with the target after a cut during rename,
     * and removing it would free them. It counts against the limit and its slot stays skipped. */
    crux_file_service_port_event(CRUX_FILE_SERVICE_EVENT_RECOVERY_NEEDED, none,
                                 config->areas[area].name, target, NULL, false);
    return true;
}

/* Count an area and clear what a cut left, in that order, so that a listing never sees a removal.
 */
static void scan_area(size_t area)
{
    crux_file_root_t root = config->areas[area].root;
    crux_file_dir_t *dir = NULL;
    uint32_t temps = 0;
    uint32_t sidecars = 0;

    crux_file_result_t result = crux_file_dir_open(root, &dir);
    if (result != CRUX_FILE_OK) {
        areas[area].status = result == CRUX_FILE_ERR_NOT_READY ? AREA_NOT_READY : AREA_FAILED;
        return;
    }
    for (;;) {
        char name[NAME_CAPACITY];
        crux_file_info_t info;
        bool end = false;
        result = crux_file_dir_read(dir, name, sizeof(name), &info, &end);
        if (result != CRUX_FILE_OK || end) {
            break;
        }
        areas[area].used_bytes += info.size;
        areas[area].entries++;
        int temp = slot_of_name(name, TEMP_PREFIX);
        int sidecar = slot_of_name(name, SIDECAR_PREFIX);
        if (temp >= 0) {
            temps |= 1u << temp;
        }
        if (sidecar >= 0) {
            sidecars |= 1u << sidecar;
        }
    }
    (void)crux_file_dir_close(&dir);
    if (result != CRUX_FILE_OK) {
        areas[area].status = AREA_FAILED;
        return;
    }
    for (int slot = 0; slot < CRUX_FILE_SERVICE_TEMP_SLOTS; slot++) {
        bool has_temp = (temps & (1u << slot)) != 0;
        bool has_sidecar = (sidecars & (1u << slot)) != 0;
        if ((has_temp || has_sidecar) && !recover_slot(area, slot, has_temp, has_sidecar)) {
            areas[area].status = AREA_FAILED;
            return;
        }
    }
    areas[area].status = AREA_READY;
}

static bool area_declaration_valid(const crux_file_area_t *a)
{
    if (!area_name_valid(a->name) || !a->read_permission || !*a->read_permission ||
        (a->write_permission && !*a->write_permission) || a->max_file_bytes == 0 ||
        a->max_file_bytes > INT32_MAX || a->max_area_bytes == 0 || a->max_entries == 0) {
        return false;
    }
    if ((a->write_modes & ~(CRUX_FILE_SERVICE_MANAGED | CRUX_FILE_SERVICE_DIRECT)) != 0) {
        return false;
    }
    /* A writable area needs a mode, and an area with a mode needs a permission to write. */
    if ((a->write_permission != NULL) != (a->write_modes != 0)) {
        return false;
    }
    if (a->partial_suffix && (!*a->partial_suffix || strncmp(a->partial_suffix, RESERVED_PREFIX,
                                                             strlen(RESERVED_PREFIX)) == 0)) {
        return false;
    }
    return true;
}

static bool config_valid(const crux_file_service_config_t *c)
{
    if (!c || !c->areas || c->area_count == 0 || c->area_count > CRUX_FILE_SERVICE_AREAS_MAX) {
        return false;
    }
    if (c->reserved_root_count == 0 ? !c->no_credential_store : !c->reserved_roots) {
        return false;
    }
    for (size_t i = 0; i < c->area_count; i++) {
        if (!area_declaration_valid(&c->areas[i])) {
            return false;
        }
        for (size_t j = 0; j < i; j++) {
            if (strcmp(c->areas[i].name, c->areas[j].name) == 0 ||
                c->areas[i].root == c->areas[j].root) {
                return false;
            }
        }
        for (size_t j = 0; j < c->reserved_root_count; j++) {
            if (c->areas[i].root == c->reserved_roots[j]) {
                return false;
            }
        }
    }
    return true;
}

crux_file_service_result_t crux_file_service_start(const crux_file_service_config_t *c)
{
    if (config) {
        return CRUX_FILE_SERVICE_ERR_EXISTS;
    }
    if (!config_valid(c)) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (crux_mutex_create(&lock) != CRUX_OK) {
        return CRUX_FILE_SERVICE_ERR_NO_RESOURCES;
    }
    /* The scan uses the helpers that read config, and no client can call yet. */
    config = c;
    for (size_t i = 0; i < c->area_count; i++) {
        scan_area(i);
    }
    return CRUX_FILE_SERVICE_OK;
}

crux_file_service_result_t crux_file_service_resolve(const char *path,
                                                     const crux_file_area_t **area, char *name,
                                                     size_t capacity)
{
    if (!config) {
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
    if (!path || !area || !name || capacity < NAME_CAPACITY) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    size_t length = 0;
    while (length <= PATH_MAX_BYTES && path[length]) {
        length++;
    }
    if (length > PATH_MAX_BYTES || length < 2 || path[0] != '/') {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    const char *area_start = &path[1];
    const char *slash = strchr(area_start, '/');
    size_t area_length = slash ? (size_t)(slash - area_start) : strlen(area_start);
    const char *leaf = slash ? slash + 1 : "";
    if (area_length == 0 || area_length > AREA_NAME_MAX) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    char area_name[AREA_NAME_MAX + 1];
    memcpy(area_name, area_start, area_length);
    area_name[area_length] = '\0';
    if (!area_name_valid(area_name) || (*leaf && !client_name_valid(leaf))) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    size_t index = area_index(area_start, area_length);
    if (index == config->area_count) {
        return CRUX_FILE_SERVICE_ERR_NOT_FOUND;
    }
    *area = &config->areas[index];
    strcpy(name, leaf);
    return CRUX_FILE_SERVICE_OK;
}

crux_file_service_result_t crux_file_service_area_next(crux_access_session_t session,
                                                       size_t *cursor,
                                                       const crux_file_area_t **area,
                                                       bool *writable)
{
    if (!config) {
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
    if (!cursor || !area || !writable) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    char user[CRUX_ACCESS_NAME_SIZE];
    crux_access_result_t checked = crux_access_session_user(session, user, sizeof(user));
    if (checked != CRUX_ACCESS_OK) {
        return from_access(checked);
    }
    while (*cursor < config->area_count) {
        const crux_file_area_t *candidate = &config->areas[(*cursor)++];
        bool can_read = false;
        bool can_write = false;
        checked = crux_access_permitted(session, candidate->read_permission, &can_read);
        if (checked == CRUX_ACCESS_OK && candidate->write_permission) {
            checked = crux_access_permitted(session, candidate->write_permission, &can_write);
        }
        if (checked != CRUX_ACCESS_OK) {
            return from_access(checked);
        }
        if (can_read || can_write) {
            *area = candidate;
            *writable = can_write;
            return CRUX_FILE_SERVICE_OK;
        }
    }
    return CRUX_FILE_SERVICE_END;
}

crux_file_service_result_t crux_file_service_dir_open(crux_access_session_t session,
                                                      const char *area,
                                                      crux_file_service_dir_t **out)
{
    if (!out || *out) {
        return config ? CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT : CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
    size_t index = 0;
    crux_file_service_result_t result = enter_area(session, area, NEED_READ, &index);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }

    state_lock();
    struct crux_file_service_dir *listing = NULL;
    for (size_t i = 0; i < CRUX_FILE_SERVICE_DIR_MAX && !listing; i++) {
        if (!listings[i].live) {
            listing = &listings[i];
            listing->live = true;
        }
    }
    state_unlock();
    if (!listing) {
        return CRUX_FILE_SERVICE_ERR_NO_RESOURCES;
    }

    listing->session = session;
    listing->area = index;
    listing->dir = NULL;
    crux_file_result_t opened = crux_file_dir_open(config->areas[index].root, &listing->dir);
    if (opened != CRUX_FILE_OK) {
        state_lock();
        listing->live = false;
        state_unlock();
        return storage_result(opened, session, index);
    }
    *out = listing;
    return CRUX_FILE_SERVICE_OK;
}

static bool listing_valid(const crux_file_service_dir_t *dir)
{
    for (size_t i = 0; i < CRUX_FILE_SERVICE_DIR_MAX; i++) {
        if (dir == &listings[i] && listings[i].live) {
            return true;
        }
    }
    return false;
}

crux_file_service_result_t crux_file_service_dir_read(crux_access_session_t session,
                                                      crux_file_service_dir_t *dir, char *name,
                                                      size_t capacity, crux_file_info_t *info,
                                                      bool *end)
{
    if (!config) {
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
    if (!listing_valid(dir) || !name || !capacity || !info || !end) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (dir->session.value != session.value) {
        return CRUX_FILE_SERVICE_ERR_DENIED;
    }
    name[0] = '\0';
    *end = false;
    crux_file_service_result_t checked = permitted(session, dir->area, NEED_READ);
    if (checked != CRUX_FILE_SERVICE_OK) {
        return checked;
    }
    for (;;) {
        crux_file_result_t result = crux_file_dir_read(dir->dir, name, capacity, info, end);
        if (result != CRUX_FILE_OK) {
            return storage_result(result, session, dir->area);
        }
        if (*end || strncmp(name, RESERVED_PREFIX, strlen(RESERVED_PREFIX)) != 0) {
            return CRUX_FILE_SERVICE_OK;
        }
    }
}

crux_file_service_result_t crux_file_service_dir_close(crux_file_service_dir_t **dir)
{
    if (!config) {
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
    if (!dir || !listing_valid(*dir)) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    struct crux_file_service_dir *listing = *dir;
    *dir = NULL;
    crux_file_result_t result = crux_file_dir_close(&listing->dir);
    state_lock();
    listing->live = false;
    state_unlock();
    return from_file(result);
}

/* A name that a client sent and the rule refused is recorded with its area only. */
static crux_file_service_result_t check_name(crux_access_session_t session, size_t area,
                                             const char *name)
{
    if (client_name_valid(name)) {
        return CRUX_FILE_SERVICE_OK;
    }
    event(CRUX_FILE_SERVICE_EVENT_NAME_REJECTED, session, area, NULL, NULL);
    return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
}

crux_file_service_result_t crux_file_service_stat(crux_access_session_t session, const char *area,
                                                  const char *name, crux_file_info_t *out)
{
    size_t index = 0;
    crux_file_service_result_t result = enter_area(session, area, NEED_READ, &index);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    if (!out) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    result = check_name(session, index, name);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    return storage_result(crux_file_stat(config->areas[index].root, name, out), session, index);
}

crux_file_service_result_t crux_file_service_remove(crux_access_session_t session, const char *area,
                                                    const char *name)
{
    size_t index = 0;
    crux_file_service_result_t result = enter_area(session, area, NEED_WRITE, &index);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    result = check_name(session, index, name);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    result = reserve(index, name, NULL);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    crux_file_root_t root = config->areas[index].root;
    crux_file_info_t info;
    crux_file_result_t file_result = crux_file_stat(root, name, &info);
    if (file_result == CRUX_FILE_OK) {
        file_result = crux_file_remove_at(root, name);
    }
    if (file_result == CRUX_FILE_OK) {
        counters_remove(index, info.size, 1);
        event(CRUX_FILE_SERVICE_EVENT_REMOVED, session, index, name, NULL);
    }
    release(index, name);
    return storage_result(file_result, session, index);
}

crux_file_service_result_t crux_file_service_rename(crux_access_session_t session, const char *area,
                                                    const char *from, const char *to)
{
    size_t index = 0;
    crux_file_service_result_t result = enter_area(session, area, NEED_WRITE, &index);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    result = check_name(session, index, from);
    if (result == CRUX_FILE_SERVICE_OK) {
        result = check_name(session, index, to);
    }
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    /* The same name twice needs one entry, and File then reports what it finds. */
    result = reserve(index, from, strcmp(from, to) == 0 ? NULL : to);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    crux_file_result_t renamed = crux_file_rename(config->areas[index].root, from, to);
    if (renamed == CRUX_FILE_OK) {
        event(CRUX_FILE_SERVICE_EVENT_RENAMED, session, index, from, to);
    }
    release(index, from);
    if (strcmp(from, to) != 0) {
        release(index, to);
    }
    return storage_result(renamed, session, index);
}

static struct crux_file_service_transfer *transfer_take(void)
{
    struct crux_file_service_transfer *taken = NULL;
    state_lock();
    for (size_t i = 0; i < CRUX_FILE_SERVICE_TRANSFER_MAX && !taken; i++) {
        if (!transfers[i].live) {
            taken = &transfers[i];
            memset(taken, 0, sizeof(*taken));
            taken->live = true;
            taken->temp_slot = -1;
        }
    }
    state_unlock();
    return taken;
}

static void transfer_give_back(struct crux_file_service_transfer *t)
{
    state_lock();
    t->live = false;
    state_unlock();
}

static bool transfer_valid(const crux_file_service_transfer_t *t)
{
    for (size_t i = 0; i < CRUX_FILE_SERVICE_TRANSFER_MAX; i++) {
        if (t == &transfers[i] && transfers[i].live) {
            return true;
        }
    }
    return false;
}

static bool slot_claim(size_t area, int slot)
{
    state_lock();
    bool free_slot = (areas[area].claimed_slots & (1u << slot)) == 0;
    if (free_slot) {
        areas[area].claimed_slots |= 1u << slot;
    }
    state_unlock();
    return free_slot;
}

static void slot_release(size_t area, int slot)
{
    state_lock();
    areas[area].claimed_slots &= ~(1u << slot);
    state_unlock();
}

/*
 * Claim a temporary name with no file under it, so that a kept file is never reused and an old
 * sidecar can never name a new upload. The claim comes before the checks, so that two uploads
 * cannot both find one name free, and it lasts until the transfer retires.
 */
static crux_file_service_result_t temp_slot_find(crux_access_session_t session, size_t area,
                                                 int *slot)
{
    crux_file_root_t root = config->areas[area].root;
    for (int i = 0; i < CRUX_FILE_SERVICE_TEMP_SLOTS; i++) {
        if (!slot_claim(area, i)) {
            continue;
        }
        char temp[NAME_CAPACITY];
        char sidecar[NAME_CAPACITY];
        crux_file_info_t info;
        temp_name(temp, TEMP_PREFIX, i);
        temp_name(sidecar, SIDECAR_PREFIX, i);
        crux_file_result_t found = crux_file_stat(root, temp, &info);
        if (found == CRUX_FILE_ERR_NOT_FOUND) {
            found = crux_file_stat(root, sidecar, &info);
        }
        if (found == CRUX_FILE_ERR_NOT_FOUND) {
            *slot = i;
            return CRUX_FILE_SERVICE_OK;
        }
        slot_release(area, i);
        if (found != CRUX_FILE_OK) {
            return storage_result(found, session, area);
        }
    }
    return CRUX_FILE_SERVICE_ERR_NO_RESOURCES;
}

static crux_file_service_result_t declared_size_fits(crux_access_session_t session, size_t area,
                                                     uint64_t declared_size)
{
    const crux_file_area_t *a = &config->areas[area];
    if (declared_size > a->max_file_bytes) {
        return CRUX_FILE_SERVICE_ERR_LIMIT;
    }
    if (declared_size == 0) {
        return CRUX_FILE_SERVICE_OK;
    }
    /* Only a check: the writes reserve what they use. */
    if (!room_reserve_or_clear(session, area, declared_size, 0)) {
        return CRUX_FILE_SERVICE_ERR_LIMIT;
    }
    counters_remove(area, declared_size, 0);
    return CRUX_FILE_SERVICE_OK;
}

static crux_file_service_result_t open_managed(struct crux_file_service_transfer *t,
                                               crux_access_session_t session,
                                               uint64_t declared_size)
{
    crux_file_service_result_t result = declared_size_fits(session, t->area, declared_size);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    if (!room_reserve_or_clear(session, t->area, 0, 1)) {
        return CRUX_FILE_SERVICE_ERR_LIMIT;
    }
    result = temp_slot_find(session, t->area, &t->temp_slot);
    if (result != CRUX_FILE_SERVICE_OK) {
        counters_remove(t->area, 0, 1);
        return result;
    }
    char temp[NAME_CAPACITY];
    temp_name(temp, TEMP_PREFIX, t->temp_slot);
    crux_file_root_t root = config->areas[t->area].root;
    crux_file_result_t opened = crux_file_open_at(root, temp, CRUX_FILE_CREATE_REPLACE, &t->file);
    if (opened == CRUX_FILE_OK) {
        return CRUX_FILE_SERVICE_OK;
    }
    /* A failed create may still have made the file. A refused one reached no file, and it is not
     * this upload's to remove. */
    crux_file_result_t removed = CRUX_FILE_OK;
    if (opened != CRUX_FILE_ERR_INVALID_STATE) {
        removed = crux_file_remove_at(root, temp);
    }
    if (removed == CRUX_FILE_OK || removed == CRUX_FILE_ERR_NOT_FOUND) {
        counters_remove(t->area, 0, 1);
    }
    slot_release(t->area, t->temp_slot);
    return storage_result(opened, session, t->area);
}

static crux_file_service_result_t open_create(struct crux_file_service_transfer *t,
                                              crux_access_session_t session, uint64_t already_used,
                                              bool replacing)
{
    crux_file_root_t root = config->areas[t->area].root;
    if (!replacing && !room_reserve_or_clear(session, t->area, 0, 1)) {
        return CRUX_FILE_SERVICE_ERR_LIMIT;
    }
    /* A failed CREATE_REPLACE may still change the file, so the counters move first. */
    counters_remove(t->area, already_used, 0);
    crux_file_result_t result =
        crux_file_open_at(root, t->name, CRUX_FILE_CREATE_REPLACE, &t->file);
    if (result == CRUX_FILE_OK) {
        if (!replacing) {
            event(CRUX_FILE_SERVICE_EVENT_CREATED, session, t->area, t->name, NULL);
        }
        return CRUX_FILE_SERVICE_OK;
    }
    if (result == CRUX_FILE_ERR_INVALID_STATE) {
        /* Refused before it reached the file. */
        counters_add(t->area, already_used, 0);
        if (!replacing) {
            counters_remove(t->area, 0, 1);
        }
        return CRUX_FILE_SERVICE_ERR_BUSY;
    }
    crux_file_info_t after;
    crux_file_result_t checked = crux_file_stat(root, t->name, &after);
    if (checked == CRUX_FILE_OK) {
        counters_add(t->area, after.size, 0);
    } else if (checked == CRUX_FILE_ERR_NOT_FOUND) {
        counters_remove(t->area, 0, 1);
    }
    return storage_result(result, session, t->area);
}

static crux_file_service_result_t open_direct(struct crux_file_service_transfer *t,
                                              crux_access_session_t session,
                                              crux_file_service_open_t mode)
{
    crux_file_root_t root = config->areas[t->area].root;
    if (mode == CRUX_FILE_SERVICE_OPEN_DIRECT) {
        return storage_result(crux_file_open_at(root, t->name, CRUX_FILE_READ_WRITE, &t->file),
                              session, t->area);
    }
    crux_file_info_t info;
    crux_file_result_t found = crux_file_stat(root, t->name, &info);
    if (found != CRUX_FILE_OK && found != CRUX_FILE_ERR_NOT_FOUND) {
        return storage_result(found, session, t->area);
    }
    if (found == CRUX_FILE_ERR_NOT_FOUND) {
        return open_create(t, session, 0, false);
    }
    switch (mode) {
    case CRUX_FILE_SERVICE_OPEN_DIRECT_EXCLUSIVE:
        return CRUX_FILE_SERVICE_ERR_EXISTS;
    case CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE:
        return open_create(t, session, info.size, true);
    default:
        return storage_result(crux_file_open_at(root, t->name, CRUX_FILE_READ_WRITE, &t->file),
                              session, t->area);
    }
}

/* Remove what an upload has written if asked, release what it holds, and report the removal. */
static crux_file_result_t transfer_retire(struct crux_file_service_transfer *t, bool remove_temp)
{
    crux_file_result_t removed = CRUX_FILE_OK;
    if (remove_temp) {
        char temp[NAME_CAPACITY];
        temp_name(temp, TEMP_PREFIX, t->temp_slot);
        removed = crux_file_remove_at(config->areas[t->area].root, temp);
        if (removed == CRUX_FILE_OK || removed == CRUX_FILE_ERR_NOT_FOUND) {
            counters_remove(t->area, t->size, 1);
            removed = CRUX_FILE_OK;
        }
    }
    if (t->temp_slot >= 0) {
        slot_release(t->area, t->temp_slot);
    }
    if (t->holds_name) {
        release(t->area, t->name);
    }
    transfer_give_back(t);
    return removed;
}

crux_file_service_result_t crux_file_service_open(crux_access_session_t session, const char *area,
                                                  const char *name, crux_file_service_open_t mode,
                                                  uint64_t declared_size,
                                                  crux_file_service_transfer_t **out)
{
    if (!config) {
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
    if (!out || *out || mode < CRUX_FILE_SERVICE_OPEN_READ ||
        mode > CRUX_FILE_SERVICE_OPEN_DIRECT_EXCLUSIVE) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    bool reading = mode == CRUX_FILE_SERVICE_OPEN_READ;
    size_t index = 0;
    crux_file_service_result_t result =
        enter_area(session, area, reading ? NEED_READ : NEED_WRITE, &index);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    result = check_name(session, index, name);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    bool managed = mode == CRUX_FILE_SERVICE_OPEN_MANAGED;
    uint8_t offered = config->areas[index].write_modes;
    if (!reading &&
        (offered & (managed ? CRUX_FILE_SERVICE_MANAGED : CRUX_FILE_SERVICE_DIRECT)) == 0) {
        return CRUX_FILE_SERVICE_ERR_DENIED;
    }

    struct crux_file_service_transfer *t = transfer_take();
    if (!t) {
        return CRUX_FILE_SERVICE_ERR_NO_RESOURCES;
    }
    t->kind = reading ? KIND_READ : managed ? KIND_MANAGED : KIND_DIRECT;
    t->session = session;
    t->area = index;
    t->position_known = true;
    strcpy(t->name, name);
    if (!reading) {
        result = reserve(index, name, NULL);
        if (result != CRUX_FILE_SERVICE_OK) {
            transfer_give_back(t);
            return result;
        }
        t->holds_name = true;
    }

    if (reading) {
        result = storage_result(
            crux_file_open_at(config->areas[index].root, name, CRUX_FILE_READ, &t->file), session,
            index);
    } else if (managed) {
        result = open_managed(t, session, declared_size);
    } else {
        result = open_direct(t, session, mode);
    }
    if (result == CRUX_FILE_SERVICE_OK) {
        crux_file_result_t sized = crux_file_size(t->file, &t->size);
        if (sized != CRUX_FILE_OK) {
            /* Without a size no later check is sound, so the file does not stay open. */
            (void)crux_file_close(&t->file);
            if (managed) {
                (void)transfer_retire(t, true);
                return storage_result(sized, session, index);
            }
            result = storage_result(sized, session, index);
        }
    }
    if (result != CRUX_FILE_SERVICE_OK) {
        if (t->holds_name) {
            release(index, name);
        }
        transfer_give_back(t);
        return result;
    }
    *out = t;
    return CRUX_FILE_SERVICE_OK;
}

static crux_file_service_result_t transfer_enter(crux_access_session_t session,
                                                 const crux_file_service_transfer_t *t, need_t need)
{
    if (!config) {
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
    if (!transfer_valid(t)) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    if (t->session.value != session.value) {
        return CRUX_FILE_SERVICE_ERR_DENIED;
    }
    return permitted(session, t->area, need);
}

/* Move only when the offset differs, because the provider's seek reads the end of the file. */
static crux_file_service_result_t transfer_seek(struct crux_file_service_transfer *t,
                                                uint64_t offset)
{
    if (t->position_known && t->position == offset) {
        return CRUX_FILE_SERVICE_OK;
    }
    t->position_known = false;
    crux_file_result_t result = crux_file_seek(t->file, offset);
    if (result != CRUX_FILE_OK) {
        return storage_result(result, t->session, t->area);
    }
    t->position = offset;
    t->position_known = true;
    return CRUX_FILE_SERVICE_OK;
}

crux_file_service_result_t crux_file_service_read(crux_access_session_t session,
                                                  crux_file_service_transfer_t *transfer,
                                                  uint64_t offset, void *buffer, size_t length,
                                                  size_t *transferred)
{
    crux_file_service_result_t result = transfer_enter(session, transfer, NEED_READ);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    if (!transferred || (length && !buffer)) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    *transferred = 0;
    if (transfer->kind == KIND_MANAGED) {
        return CRUX_FILE_SERVICE_ERR_DENIED;
    }
    result = transfer_seek(transfer, offset);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    crux_file_result_t read_result = crux_file_read(transfer->file, buffer, length, transferred);
    transfer->position += *transferred;
    if (read_result != CRUX_FILE_OK) {
        transfer->position_known = false;
    }
    return storage_result(read_result, session, transfer->area);
}

static uint64_t growth_of(const struct crux_file_service_transfer *t, uint64_t end)
{
    return end > t->size ? end - t->size : 0;
}

crux_file_service_result_t crux_file_service_write(crux_access_session_t session,
                                                   crux_file_service_transfer_t *transfer,
                                                   uint64_t offset, const void *buffer,
                                                   size_t length, size_t *transferred)
{
    crux_file_service_result_t result = transfer_enter(
        session, transfer,
        transfer && transfer_valid(transfer) && transfer->kind == KIND_READ ? NEED_READ
                                                                            : NEED_WRITE);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    if (!transferred || (length && !buffer)) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    *transferred = 0;
    if (transfer->kind == KIND_READ) {
        return CRUX_FILE_SERVICE_ERR_DENIED;
    }
    if (offset > UINT64_MAX - length || offset > transfer->size ||
        (transfer->kind == KIND_MANAGED && offset != transfer->position)) {
        return CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE;
    }
    const crux_file_area_t *a = &config->areas[transfer->area];
    uint64_t end = offset + length;
    uint64_t growth = growth_of(transfer, end);
    if (end > a->max_file_bytes && growth > 0) {
        return CRUX_FILE_SERVICE_ERR_LIMIT;
    }
    if (!room_reserve_or_clear(session, transfer->area, growth, 0)) {
        return CRUX_FILE_SERVICE_ERR_LIMIT;
    }

    result = transfer_seek(transfer, offset);
    if (result != CRUX_FILE_SERVICE_OK) {
        counters_remove(transfer->area, growth, 0);
        return result;
    }
    crux_file_result_t written = crux_file_write(transfer->file, buffer, length, transferred);

    /* Settle against the accepted count, which survives any error. */
    uint64_t accepted_end = offset + *transferred;
    uint64_t kept = growth_of(transfer, accepted_end);
    counters_remove(transfer->area, growth - kept, 0);
    transfer->size += kept;
    transfer->position = accepted_end;
    if (written != CRUX_FILE_OK && written != CRUX_FILE_PARTIAL) {
        transfer->position_known = false;
    }
    return storage_result(written, session, transfer->area);
}

crux_file_service_result_t crux_file_service_size(crux_access_session_t session,
                                                  crux_file_service_transfer_t *transfer,
                                                  uint64_t *size)
{
    crux_file_service_result_t result = transfer_enter(
        session, transfer,
        transfer && transfer_valid(transfer) && transfer->kind == KIND_READ ? NEED_READ
                                                                            : NEED_WRITE);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    if (!size) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    *size = transfer->size;
    return CRUX_FILE_SERVICE_OK;
}

/* A handle on the target stops a commit before it changes anything. Opening for writing is refused
 * next to any other handle, so it also finds a reader outside the service. */
static crux_file_result_t target_free(const struct crux_file_service_transfer *t, bool *exists,
                                      uint64_t *old_size)
{
    crux_file_root_t root = config->areas[t->area].root;
    crux_file_t *probe = NULL;
    crux_file_result_t result = crux_file_open_at(root, t->name, CRUX_FILE_READ_WRITE, &probe);
    if (result == CRUX_FILE_ERR_NOT_FOUND) {
        *exists = false;
        *old_size = 0;
        return CRUX_FILE_OK;
    }
    if (result != CRUX_FILE_OK) {
        return result;
    }
    *exists = true;
    result = crux_file_size(probe, old_size);
    crux_file_result_t closed = crux_file_close(&probe);
    return result != CRUX_FILE_OK ? result : closed;
}

/* Nothing that the target depends on has changed, and the upload is gone. */
static crux_file_service_result_t commit_abandoned(crux_file_service_transfer_t **transfer,
                                                   crux_file_result_t failure)
{
    struct crux_file_service_transfer *t = *transfer;
    crux_access_session_t session = t->session;
    size_t area = t->area;
    *transfer = NULL;
    (void)transfer_retire(t, true);
    /* ERR_BUSY and ERR_NO_RESOURCES tell a caller that it still holds the upload. */
    crux_file_service_result_t mapped = storage_result(failure, session, area);
    return mapped == CRUX_FILE_SERVICE_ERR_BUSY || mapped == CRUX_FILE_SERVICE_ERR_NO_RESOURCES
               ? CRUX_FILE_SERVICE_ERR_IO
               : mapped;
}

/* The commit changed nothing, so the caller keeps the upload and can write on, retry or close. */
static crux_file_service_result_t commit_refused(crux_file_service_transfer_t **transfer,
                                                 crux_file_service_result_t refusal)
{
    struct crux_file_service_transfer *t = *transfer;
    char temp[NAME_CAPACITY];
    temp_name(temp, TEMP_PREFIX, t->temp_slot);
    crux_file_result_t reopened =
        crux_file_open_at(config->areas[t->area].root, temp, CRUX_FILE_READ_WRITE, &t->file);
    if (reopened == CRUX_FILE_OK) {
        t->position_known = false;
        return refusal;
    }
    return commit_abandoned(transfer, CRUX_FILE_ERR_IO);
}

/* The target may be gone, so both files stay, and the next start finishes or undoes the commit. */
static crux_file_service_result_t commit_failed(crux_file_service_transfer_t **transfer)
{
    struct crux_file_service_transfer *t = *transfer;
    *transfer = NULL;
    event(CRUX_FILE_SERVICE_EVENT_COMMIT_FAILED, t->session, t->area, t->name, NULL);
    /* The temporary counts against the limit now. The sidecar counts from the next start's scan. */
    (void)transfer_retire(t, false);
    return CRUX_FILE_SERVICE_ERR_IO;
}

crux_file_service_result_t crux_file_service_commit(crux_access_session_t session,
                                                    crux_file_service_transfer_t **transfer)
{
    if (!config) {
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
    if (!transfer) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    struct crux_file_service_transfer *t = *transfer;
    crux_file_service_result_t result = transfer_enter(session, t, NEED_WRITE);
    if (result != CRUX_FILE_SERVICE_OK) {
        return result;
    }
    if (t->kind != KIND_MANAGED) {
        return CRUX_FILE_SERVICE_ERR_DENIED;
    }

    /* Closing the temporary first keeps the service to its share of the File handles while the
     * probe and the sidecar each take one. */
    crux_file_root_t root = config->areas[t->area].root;
    crux_file_result_t stepped = crux_file_sync(t->file);
    crux_file_result_t closed = crux_file_close(&t->file);
    stepped = stepped != CRUX_FILE_OK ? stepped : closed;
    if (stepped != CRUX_FILE_OK) {
        return commit_abandoned(transfer, stepped);
    }

    bool target_exists = false;
    uint64_t old_size = 0;
    stepped = target_free(t, &target_exists, &old_size);
    if (stepped == CRUX_FILE_ERR_INVALID_STATE || stepped == CRUX_FILE_ERR_NO_RESOURCES) {
        return commit_refused(transfer, from_file(stepped));
    }
    if (stepped != CRUX_FILE_OK) {
        return commit_abandoned(transfer, stepped);
    }

    char temp[NAME_CAPACITY];
    char sidecar[NAME_CAPACITY];
    temp_name(temp, TEMP_PREFIX, t->temp_slot);
    temp_name(sidecar, SIDECAR_PREFIX, t->temp_slot);
    stepped = sidecar_write(root, t->temp_slot, t->name);
    if (stepped != CRUX_FILE_OK) {
        (void)crux_file_remove_at(root, sidecar);
        if (stepped == CRUX_FILE_ERR_INVALID_STATE || stepped == CRUX_FILE_ERR_NO_RESOURCES) {
            return commit_refused(transfer, from_file(stepped));
        }
        return commit_abandoned(transfer, stepped);
    }

    /* The sidecar is on the medium, so the next start can finish or undo every step below. */
    if (target_exists) {
        stepped = crux_file_remove_at(root, t->name);
        if (stepped == CRUX_FILE_ERR_INVALID_STATE) {
            /* A reader arrived since the probe. File refused before it reached the target. */
            (void)crux_file_remove_at(root, sidecar);
            return commit_refused(transfer, CRUX_FILE_SERVICE_ERR_BUSY);
        }
        crux_file_info_t info;
        if (stepped == CRUX_FILE_OK || stepped == CRUX_FILE_ERR_NOT_FOUND ||
            crux_file_stat(root, t->name, &info) == CRUX_FILE_ERR_NOT_FOUND) {
            counters_remove(t->area, old_size, 1);
        }
        if (stepped != CRUX_FILE_OK && stepped != CRUX_FILE_ERR_NOT_FOUND) {
            /* FatFs can report an error after the entry has gone, so nothing here is removed. */
            return commit_failed(transfer);
        }
    }
    if (crux_file_rename(root, temp, t->name) != CRUX_FILE_OK) {
        return commit_failed(transfer);
    }
    /* A sidecar left here is removed at the next start, and it only skips its slot until then. */
    (void)crux_file_remove_at(root, sidecar);
    event(CRUX_FILE_SERVICE_EVENT_CREATED, session, t->area, t->name, NULL);
    *transfer = NULL;
    (void)transfer_retire(t, false);
    return CRUX_FILE_SERVICE_OK;
}

crux_file_service_result_t crux_file_service_close(crux_file_service_transfer_t **transfer)
{
    if (!config) {
        return CRUX_FILE_SERVICE_ERR_NOT_READY;
    }
    if (!transfer || !transfer_valid(*transfer)) {
        return CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT;
    }
    struct crux_file_service_transfer *t = *transfer;
    *transfer = NULL;
    crux_file_result_t closed = crux_file_close(&t->file);
    crux_file_result_t removed = transfer_retire(t, t->kind == KIND_MANAGED);
    return from_file(closed != CRUX_FILE_OK ? closed : removed);
}
