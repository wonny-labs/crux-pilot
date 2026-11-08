/*
 * The store. One sealed image in two fixed-size slots, so a record has no state apart from the
 * image that holds it. Saves go to the slot that is not active, and read back before it becomes
 * active. Portable logic over the File contract, so it is the same on Host and target.
 */
#include "credential_internal.h"

#include <crux/sync.h>
#include <string.h>

#define STORE_LOCK_WAIT_MS 0x7FFFFFFFu
#define SLOT_FILE_NAME "store.img"
#define SLOT_COUNT 2u
#define SECTOR_BYTES 512u

_Static_assert(CRUX_CREDENTIAL_SLOT_BYTES % SECTOR_BYTES == 0,
               "A slot is a whole number of sectors, so that a save writes no partial one");
_Static_assert(CRUX_CREDENTIAL_SLOT_BYTES <= 0x7FFFFFFFu, "A slot's size fits a File seek");
_Static_assert(CRUX_CREDENTIAL_PAYLOAD_BYTES >=
                   CRUX_CREDENTIAL_TABLE_HEADER_BYTES +
                       CRUX_CREDENTIAL_RECORD_KINDS * CRUX_CREDENTIAL_TABLE_ENTRY_BYTES,
               "A slot must at least hold the table");

typedef enum {
    SLOT_ABSENT,
    SLOT_SHORT, /* Smaller than a header, or holding a pending header. */
    SLOT_VALID,
    SLOT_CORRUPT,
    SLOT_UNREADABLE /* A result that proves nothing about which generation is newest. */
} slot_class_t;

typedef struct {
    slot_class_t kind;
    uint64_t generation;
    uint32_t records;
    size_t payload_length;
} slot_view_t;

/*
 * crux_credential_start() must return before any other operation starts, so started and the locks
 * need no lock of their own. published is under the info lock, which is never held across storage.
 * The rest is under the store lock, which is held across storage, and it orders below the Access
 * change lock.
 */
static struct {
    bool started;
    bool locks_created;
    crux_mutex_t store_lock;
    crux_mutex_t info_lock;
    crux_file_root_t roots[SLOT_COUNT];
    crux_credential_store_info_t published;
    crux_credential_state_t state;
    uint16_t protection;
    size_t active;
    uint64_t generation;
    size_t payload_length; /* Of plain, once loaded. */
    uint8_t sealed[CRUX_CREDENTIAL_SLOT_BYTES];
    uint8_t plain[CRUX_CREDENTIAL_PAYLOAD_BYTES];
} store;

static void secure_zero(void *memory, size_t length)
{
    volatile unsigned char *bytes = memory;
    for (size_t index = 0; index < length; ++index) {
        bytes[index] = 0;
    }
}

static void buffers_clear(void)
{
    secure_zero(store.sealed, sizeof(store.sealed));
    secure_zero(store.plain, sizeof(store.plain));
    store.payload_length = 0;
}

/* A failed lock means misuse, such as a call outside thread context. The caller fails closed. */
static bool lock_with_retry(crux_mutex_t mutex)
{
    crux_status_t status;
    do {
        status = crux_mutex_lock(mutex, STORE_LOCK_WAIT_MS);
    } while (status == CRUX_ERR_TIMEOUT);
    return status == CRUX_OK;
}

static void event_send(crux_credential_event_type_t type, crux_credential_result_t result)
{
    crux_credential_event_t event = {.type = type, .result = result, .state = store.state};
    crux_credential_port_event(&event);
}

static void event_send_key(crux_credential_event_type_t type, crux_credential_record_t record)
{
    crux_credential_event_t event = {
        .type = type,
        .result = CRUX_CREDENTIAL_OK,
        .state = store.state,
        .record = record,
    };
    crux_credential_port_event(&event);
}

static void event_send_slot_invalid(size_t index)
{
    crux_credential_event_t event = {
        .type = CRUX_CREDENTIAL_EVENT_SLOT_INVALID,
        .result = CRUX_CREDENTIAL_OK,
        .state = store.state,
        .slot = index == 0 ? 'a' : 'b',
    };
    crux_credential_port_event(&event);
}

/* Makes the state visible to readers that do not take the store lock, and then reports it. */
static void state_publish(crux_credential_state_t state)
{
    store.state = state;
    if (lock_with_retry(store.info_lock)) {
        store.published.state = state;
        (void)crux_mutex_unlock(store.info_lock);
    }
    event_send(CRUX_CREDENTIAL_EVENT_STATE, state == CRUX_CREDENTIAL_STATE_UNUSABLE
                                                ? CRUX_CREDENTIAL_ERR_UNUSABLE
                                                : CRUX_CREDENTIAL_OK);
}

static crux_file_result_t read_all(crux_file_t *file, uint8_t *data, size_t length)
{
    size_t total = 0;
    while (total < length) {
        size_t count = 0;
        crux_file_result_t result = crux_file_read(file, data + total, length - total, &count);
        if (result != CRUX_FILE_OK) {
            return result;
        }
        if (count == 0) {
            return CRUX_FILE_ERR_IO;
        }
        total += count;
    }
    return CRUX_FILE_OK;
}

/* A short write is a failed write: it means the medium is full. */
static crux_file_result_t write_all(crux_file_t *file, const uint8_t *data, size_t length)
{
    size_t count = 0;
    crux_file_result_t result = crux_file_write(file, data, length, &count);
    return result == CRUX_FILE_PARTIAL ? CRUX_FILE_ERR_NO_SPACE : result;
}

/* Reads a slot's file into the sealed buffer, padded with zeros. kind is VALID when it was read. */
static slot_class_t slot_read_raw(size_t index, size_t *file_bytes)
{
    crux_file_info_t info;
    crux_file_result_t result = crux_file_stat(store.roots[index], SLOT_FILE_NAME, &info);
    if (result == CRUX_FILE_ERR_NOT_FOUND) {
        return SLOT_ABSENT;
    }
    if (result != CRUX_FILE_OK) {
        return SLOT_UNREADABLE;
    }
    if (info.size < CRUX_CREDENTIAL_HEADER_BYTES) {
        return SLOT_SHORT;
    }
    crux_file_t *file = NULL;
    result = crux_file_open_at(store.roots[index], SLOT_FILE_NAME, CRUX_FILE_READ, &file);
    if (result == CRUX_FILE_ERR_NOT_FOUND) {
        return SLOT_ABSENT;
    }
    if (result != CRUX_FILE_OK) {
        return SLOT_UNREADABLE;
    }
    size_t length =
        info.size < CRUX_CREDENTIAL_SLOT_BYTES ? (size_t)info.size : CRUX_CREDENTIAL_SLOT_BYTES;
    result = read_all(file, store.sealed, length);
    crux_file_result_t closed = crux_file_close(&file);
    if (result != CRUX_FILE_OK || closed != CRUX_FILE_OK) {
        return SLOT_UNREADABLE;
    }
    memset(store.sealed + length, 0, sizeof(store.sealed) - length);
    *file_bytes = length;
    return SLOT_VALID;
}

/* Classifies the sealed buffer, opening it into the plain buffer. */
static void image_open(size_t file_bytes, slot_view_t *view)
{
    crux_credential_header_t header;
    switch (crux_credential_header_decode(store.sealed, store.protection, file_bytes, &header)) {
    case CRUX_CREDENTIAL_HEADER_PENDING:
        view->kind = SLOT_SHORT;
        return;
    case CRUX_CREDENTIAL_HEADER_CORRUPT:
        view->kind = SLOT_CORRUPT;
        return;
    case CRUX_CREDENTIAL_HEADER_VALID:
        break;
    }
    const uint8_t *body = store.sealed + CRUX_CREDENTIAL_HEADER_BYTES;
    crux_credential_result_t opened = crux_credential_port_open(
        store.sealed, CRUX_CREDENTIAL_HEADER_BYTES, body, header.payload_length, header.nonce,
        body + header.payload_length, store.plain);
    if (opened != CRUX_CREDENTIAL_OK) {
        view->kind = opened == CRUX_CREDENTIAL_ERR_UNUSABLE ? SLOT_CORRUPT : SLOT_UNREADABLE;
        return;
    }
    if (!crux_credential_payload_valid(store.plain, header.payload_length, &view->records)) {
        view->kind = SLOT_CORRUPT;
        return;
    }
    view->kind = SLOT_VALID;
    view->generation = header.generation;
    view->payload_length = header.payload_length;
}

/* Reads a slot and classifies it, leaving its bytes in both buffers if it is valid. */
static void slot_classify(size_t index, slot_view_t *view)
{
    size_t file_bytes = 0;
    *view = (slot_view_t){.kind = slot_read_raw(index, &file_bytes)};
    if (view->kind == SLOT_VALID) {
        image_open(file_bytes, view);
    }
}

/* Replaces a slot's header with the pending header, so that an image a failed save may have left
 * complete cannot win at the next boot. If this fails too, it can: a stated limit. */
static void slot_invalidate(size_t index)
{
    uint8_t pending[CRUX_CREDENTIAL_HEADER_BYTES];
    crux_credential_header_encode_pending(pending);
    crux_file_t *file = NULL;
    if (crux_file_open_at(store.roots[index], SLOT_FILE_NAME, CRUX_FILE_READ_WRITE, &file) !=
        CRUX_FILE_OK) {
        return;
    }
    if (write_all(file, pending, sizeof(pending)) == CRUX_FILE_OK) {
        (void)crux_file_sync(file);
    }
    (void)crux_file_close(&file);
}

/*
 * Writes the sealed buffer to a slot. A slot with no full-size file is created. Such a slot, and
 * one that is alone because no valid slot exists, goes down with a pending header and gets its real
 * header last, so that a cut leaves it reading as short and not as corrupt. Any other slot is
 * written straight through, because the other slot still holds the old image.
 */
static bool slot_write(size_t index, bool alone, bool *touched)
{
    crux_file_info_t info;
    crux_file_result_t result = crux_file_stat(store.roots[index], SLOT_FILE_NAME, &info);
    if (result != CRUX_FILE_OK && result != CRUX_FILE_ERR_NOT_FOUND) {
        return false;
    }
    bool create = result == CRUX_FILE_ERR_NOT_FOUND || info.size < CRUX_CREDENTIAL_SLOT_BYTES;
    bool header_last = create || alone;
    crux_file_t *file = NULL;
    result = crux_file_open_at(store.roots[index], SLOT_FILE_NAME,
                               create ? CRUX_FILE_CREATE_REPLACE : CRUX_FILE_READ_WRITE, &file);
    if (result != CRUX_FILE_OK) {
        return false;
    }
    *touched = true;
    uint8_t header[CRUX_CREDENTIAL_HEADER_BYTES];
    if (header_last) {
        memcpy(header, store.sealed, sizeof(header));
        crux_credential_header_encode_pending(store.sealed);
    }
    result = write_all(file, store.sealed, sizeof(store.sealed));
    if (header_last) {
        memcpy(store.sealed, header, sizeof(header));
    }
    if (result == CRUX_FILE_OK) {
        result = crux_file_sync(file);
    }
    if (header_last && result == CRUX_FILE_OK) {
        result = crux_file_seek(file, 0);
    }
    if (header_last && result == CRUX_FILE_OK) {
        result = write_all(file, header, sizeof(header));
    }
    if (header_last && result == CRUX_FILE_OK) {
        result = crux_file_sync(file);
    }
    crux_file_result_t closed = crux_file_close(&file);
    return result == CRUX_FILE_OK && closed == CRUX_FILE_OK;
}

/* Writes the sealed buffer to a slot and proves it by reading it back. The buffers then hold what
 * the slot holds. After a failure, the slot is made short if anything may have reached it. */
static bool slot_commit(size_t index, bool alone, uint64_t generation)
{
    bool touched = false;
    bool committed = slot_write(index, alone, &touched);
    if (committed) {
        slot_view_t view;
        slot_classify(index, &view);
        committed = view.kind == SLOT_VALID && view.generation == generation;
    }
    if (!committed && touched) {
        slot_invalidate(index);
    }
    return committed;
}

/* Seals the plain buffer into the sealed buffer, ready to write to a slot. */
static crux_credential_result_t image_seal(uint64_t generation)
{
    crux_credential_header_t header = {
        .format = CRUX_CREDENTIAL_FORMAT,
        .protection = store.protection,
        .generation = generation,
        .payload_length = (uint32_t)store.payload_length,
    };
    if (crux_credential_port_random(header.nonce, sizeof(header.nonce)) != CRUX_CREDENTIAL_OK) {
        return CRUX_CREDENTIAL_ERR_ENTROPY;
    }
    memset(store.sealed, 0, sizeof(store.sealed));
    crux_credential_header_encode(&header, store.sealed);
    uint8_t *body = store.sealed + CRUX_CREDENTIAL_HEADER_BYTES;
    crux_credential_result_t sealed = crux_credential_port_seal(
        store.sealed, CRUX_CREDENTIAL_HEADER_BYTES, store.plain, store.payload_length, body,
        header.nonce, body + store.payload_length);
    if (sealed == CRUX_CREDENTIAL_OK || sealed == CRUX_CREDENTIAL_ERR_ENTROPY) {
        return sealed;
    }
    return CRUX_CREDENTIAL_ERR_STORE;
}

static crux_credential_result_t save_fail(crux_credential_result_t result)
{
    event_send(CRUX_CREDENTIAL_EVENT_SAVE_FAILED, result);
    return result;
}

/*
 * Saves the plain buffer. A first save writes one image to both slots. Any other goes to the slot
 * that is not active, and if it adds a record it goes on to the other slot, so that an acknowledged
 * record is in both before this returns. A failure in that second write returns an error with the
 * new record already active, which is decision S7: a rollback would add states for a rare failure.
 */
static crux_credential_result_t image_save(bool adds_record)
{
    const bool first = store.state == CRUX_CREDENTIAL_STATE_EMPTY;
    if (!first && store.generation == UINT64_MAX) {
        return save_fail(CRUX_CREDENTIAL_ERR_STORE);
    }
    uint64_t generation = first ? 1u : store.generation + 1u;
    crux_credential_result_t sealed = image_seal(generation);
    if (sealed != CRUX_CREDENTIAL_OK) {
        return save_fail(sealed);
    }
    size_t target = first ? 0u : 1u - store.active;
    if (!slot_commit(target, first, generation)) {
        return save_fail(CRUX_CREDENTIAL_ERR_STORE);
    }
    store.active = target;
    store.generation = generation;
    if (first) {
        state_publish(CRUX_CREDENTIAL_STATE_VALID);
    } else if (!adds_record) {
        return CRUX_CREDENTIAL_OK;
    }
    size_t other = 1u - target;
    if (!first) {
        if (generation == UINT64_MAX) {
            return save_fail(CRUX_CREDENTIAL_ERR_STORE);
        }
        generation += 1u;
        /* Something was stored already, so a missing nonce here is not ENTROPY. */
        if (image_seal(generation) != CRUX_CREDENTIAL_OK) {
            return save_fail(CRUX_CREDENTIAL_ERR_STORE);
        }
    }
    if (!slot_commit(other, false, generation)) {
        return save_fail(CRUX_CREDENTIAL_ERR_STORE);
    }
    if (!first) {
        store.active = other;
        store.generation = generation;
    }
    return CRUX_CREDENTIAL_OK;
}

/* Puts the active image in the plain buffer, or an empty one if the store is EMPTY. */
static bool image_load(void)
{
    if (store.state == CRUX_CREDENTIAL_STATE_EMPTY) {
        store.payload_length = crux_credential_payload_empty(store.plain);
        return true;
    }
    slot_view_t view;
    slot_classify(store.active, &view);
    if (view.kind != SLOT_VALID || view.generation != store.generation) {
        return false;
    }
    store.payload_length = view.payload_length;
    return true;
}

static bool image_has(crux_credential_record_t record)
{
    const uint8_t *data = NULL;
    size_t length = 0;
    return crux_credential_payload_find(store.plain, record, &data, &length);
}

/* Takes the store lock for an operation that needs a usable store. */
static crux_credential_result_t store_enter_usable(void)
{
    if (!store.started || !lock_with_retry(store.store_lock)) {
        return CRUX_CREDENTIAL_ERR_NOT_READY;
    }
    if (store.state == CRUX_CREDENTIAL_STATE_UNUSABLE) {
        (void)crux_mutex_unlock(store.store_lock);
        return CRUX_CREDENTIAL_ERR_UNUSABLE;
    }
    return CRUX_CREDENTIAL_OK;
}

/* The image buffers are cleared before each operation returns. */
static void store_leave(void)
{
    buffers_clear();
    (void)crux_mutex_unlock(store.store_lock);
}

static bool declaration_acceptable(const crux_credential_store_info_t *declaration)
{
    if (declaration == NULL) {
        return false;
    }
    /* Only the store reports UNDECLARED, for a provider it did not accept. */
    if (declaration->basis == CRUX_CREDENTIAL_BASIS_UNDECLARED ||
        declaration->evidence == CRUX_CREDENTIAL_EVIDENCE_UNDECLARED) {
        return false;
    }
    bool can_be_confidential = declaration->basis == CRUX_CREDENTIAL_BASIS_HARDWARE_BOUND;
    if (declaration->confidential_against_medium_reader && !can_be_confidential) {
        return false;
    }
#ifndef CRUX_CREDENTIAL_TEST_COMPOSITION
    /* The fake is test only, and a product build that links it by mistake refuses it here. */
    if (declaration->basis == CRUX_CREDENTIAL_BASIS_NONE ||
        declaration->evidence == CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE) {
        return false;
    }
#endif
    return true;
}

/* Rewrites the other slot from the active one, so that the state a failed second write leaves
 * cannot last until damage arrives. The sealed bytes are copied, so it needs no nonce and no key.
 */
static bool slot_repair(void)
{
    slot_view_t view;
    slot_classify(store.active, &view);
    if (view.kind != SLOT_VALID || view.generation != store.generation ||
        !slot_commit(1u - store.active, false, store.generation)) {
        (void)save_fail(CRUX_CREDENTIAL_ERR_STORE);
        return false;
    }
    return true;
}

/* A repair finished a save that returned an error, so a key it added is announced only now. */
static void keys_announce(uint32_t records)
{
    static const crux_credential_record_t keys[] = {CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                                    CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY};
    for (size_t index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        if ((records & (1u << (uint32_t)keys[index])) != 0) {
            event_send_key(CRUX_CREDENTIAL_EVENT_KEY_GENERATED, keys[index]);
        }
    }
}

static void store_load(void)
{
    slot_view_t views[SLOT_COUNT];
    for (size_t index = 0; index < SLOT_COUNT; ++index) {
        slot_classify(index, &views[index]);
        if (views[index].kind == SLOT_CORRUPT) {
            event_send_slot_invalid(index);
        }
    }
    const slot_class_t a = views[0].kind;
    const slot_class_t b = views[1].kind;
    if (a == SLOT_UNREADABLE || b == SLOT_UNREADABLE) {
        state_publish(CRUX_CREDENTIAL_STATE_UNUSABLE);
        return;
    }
    if (a != SLOT_VALID && b != SLOT_VALID) {
        state_publish(a == SLOT_CORRUPT || b == SLOT_CORRUPT ? CRUX_CREDENTIAL_STATE_UNUSABLE
                                                             : CRUX_CREDENTIAL_STATE_EMPTY);
        return;
    }
    /* Both valid ties, which a first save produces, take slot A. */
    size_t active = 0;
    if (a != SLOT_VALID || (b == SLOT_VALID && views[1].generation > views[0].generation)) {
        active = 1;
    }
    size_t other = 1u - active;
    store.active = active;
    store.generation = views[active].generation;
    state_publish(CRUX_CREDENTIAL_STATE_VALID);
    uint32_t missing = views[active].records & ~views[other].records;
    if (views[other].kind == SLOT_VALID && missing != 0 && slot_repair()) {
        keys_announce(missing);
    }
}

crux_credential_result_t crux_credential_start(crux_file_root_t slot_a, crux_file_root_t slot_b)
{
    if (store.started || slot_a == slot_b) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    if (!store.locks_created) {
        if (crux_mutex_create(&store.store_lock) != CRUX_OK) {
            return CRUX_CREDENTIAL_ERR_NOT_READY;
        }
        if (crux_mutex_create(&store.info_lock) != CRUX_OK) {
            (void)crux_mutex_destroy(&store.store_lock);
            return CRUX_CREDENTIAL_ERR_NOT_READY;
        }
        store.locks_created = true;
    }
    if (!lock_with_retry(store.store_lock)) {
        return CRUX_CREDENTIAL_ERR_NOT_READY;
    }
    store.roots[0] = slot_a;
    store.roots[1] = slot_b;
    store.started = true;
    store.published = (crux_credential_store_info_t){
        .basis = CRUX_CREDENTIAL_BASIS_UNDECLARED,
        .evidence = CRUX_CREDENTIAL_EVIDENCE_UNDECLARED,
    };
    const crux_credential_store_info_t *declaration = crux_credential_port_declaration();
    if (declaration_acceptable(declaration)) {
        store.published = *declaration;
        store.protection = (uint16_t)declaration->basis;
        store_load();
    } else {
        state_publish(CRUX_CREDENTIAL_STATE_UNUSABLE);
    }
    crux_credential_result_t result = store.state == CRUX_CREDENTIAL_STATE_UNUSABLE
                                          ? CRUX_CREDENTIAL_ERR_UNUSABLE
                                          : CRUX_CREDENTIAL_OK;
    store_leave();
    return result;
}

crux_credential_result_t crux_credential_roots(crux_file_root_t roots[2])
{
    if (roots == NULL) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    if (!store.started) {
        return CRUX_CREDENTIAL_ERR_NOT_READY;
    }
    roots[0] = store.roots[0];
    roots[1] = store.roots[1];
    return CRUX_CREDENTIAL_OK;
}

crux_credential_state_t crux_credential_state(void)
{
    crux_credential_state_t state = CRUX_CREDENTIAL_STATE_UNUSABLE;
    if (store.started && lock_with_retry(store.info_lock)) {
        state = store.published.state;
        (void)crux_mutex_unlock(store.info_lock);
    }
    return state;
}

crux_status_t crux_credential_store_get_info(crux_credential_store_info_t *out)
{
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!store.started || !lock_with_retry(store.info_lock)) {
        return CRUX_ERR_NOT_READY;
    }
    *out = store.published;
    (void)crux_mutex_unlock(store.info_lock);
    return CRUX_OK;
}

/* Copies part of the loaded image to a caller's buffer, by the rules a read states. */
static crux_credential_result_t copy_out(const uint8_t *data, size_t data_length, void *buffer,
                                         size_t capacity, size_t *length)
{
    *length = data_length;
    if (data_length > capacity || buffer == NULL) {
        return CRUX_CREDENTIAL_ERR_TOO_LARGE;
    }
    memcpy(buffer, data, data_length);
    return CRUX_CREDENTIAL_OK;
}

crux_credential_result_t crux_credential_read(crux_credential_record_t record, void *buffer,
                                              size_t capacity, size_t *length)
{
    if (!crux_credential_record_known((uint32_t)record) || length == NULL ||
        (buffer == NULL && capacity != 0)) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    *length = 0;
    crux_credential_result_t result = store_enter_usable();
    if (result != CRUX_CREDENTIAL_OK) {
        return result;
    }
    const uint8_t *data = NULL;
    size_t data_length = 0;
    if (!image_load()) {
        result = CRUX_CREDENTIAL_ERR_READ;
    } else if (!crux_credential_payload_find(store.plain, record, &data, &data_length)) {
        result = CRUX_CREDENTIAL_ERR_NOT_FOUND;
    } else {
        result = copy_out(data, data_length, buffer, capacity, length);
    }
    store_leave();
    return result;
}

static bool key_record_kind(crux_credential_record_t record)
{
    return record == CRUX_CREDENTIAL_RECORD_TLS_KEY ||
           record == CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY;
}

crux_credential_result_t crux_credential_write(crux_credential_record_t record, const void *data,
                                               size_t length)
{
    if (!crux_credential_record_known((uint32_t)record) || data == NULL || length == 0) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    if (length > crux_credential_record_limit(record)) {
        return CRUX_CREDENTIAL_ERR_TOO_LARGE;
    }
    if (key_record_kind(record)) {
        crux_credential_key_view_t view;
        if (crux_credential_key_parse(data, length, &view) != CRUX_CREDENTIAL_OK) {
            return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
        }
    }
    crux_credential_result_t result = store_enter_usable();
    if (result != CRUX_CREDENTIAL_OK) {
        return result;
    }
    if (!image_load()) {
        result = save_fail(CRUX_CREDENTIAL_ERR_STORE);
    } else {
        bool adds_record = !image_has(record);
        result = crux_credential_payload_set(record, data, length, store.plain,
                                             &store.payload_length, sizeof(store.plain))
                     ? image_save(adds_record)
                     : CRUX_CREDENTIAL_ERR_TOO_LARGE;
        /* A key's change is always recorded, whichever path made it. */
        if (result == CRUX_CREDENTIAL_OK && !adds_record && key_record_kind(record)) {
            event_send_key(CRUX_CREDENTIAL_EVENT_KEY_REPLACED, record);
        }
    }
    store_leave();
    return result;
}

static bool key_type_known(crux_credential_key_type_t type)
{
    return type == CRUX_CREDENTIAL_KEY_ECDSA_P256 || type == CRUX_CREDENTIAL_KEY_ED25519;
}

/* Lays out a key record, and checks that the generator gave the sizes its type states. */
static bool key_record_build(crux_credential_key_type_t type, uint8_t *key_record,
                             const uint8_t *public_key, const uint8_t *private_key,
                             const size_t lengths[2])
{
    if (lengths[0] > CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX ||
        lengths[0] + lengths[1] >
            CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX - CRUX_CREDENTIAL_KEY_HEADER_BYTES) {
        return false;
    }
    key_record[0] = (uint8_t)type;
    key_record[1] = (uint8_t)lengths[0];
    key_record[2] = (uint8_t)lengths[1];
    key_record[3] = 0;
    memcpy(key_record + CRUX_CREDENTIAL_KEY_HEADER_BYTES, public_key, lengths[0]);
    memcpy(key_record + CRUX_CREDENTIAL_KEY_HEADER_BYTES + lengths[0], private_key, lengths[1]);
    crux_credential_key_view_t view;
    return crux_credential_key_parse(key_record,
                                     CRUX_CREDENTIAL_KEY_HEADER_BYTES + lengths[0] + lengths[1],
                                     &view) == CRUX_CREDENTIAL_OK &&
           view.type == type;
}

/* Generates a key and saves it into the loaded image. The key is not used before this returns OK.
 */
static crux_credential_result_t key_generate(crux_credential_record_t record,
                                             crux_credential_key_type_t type, bool adds_record)
{
    uint8_t public_key[CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX];
    uint8_t private_key[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX - CRUX_CREDENTIAL_KEY_HEADER_BYTES];
    uint8_t key_record[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    size_t lengths[2] = {0, 0};
    crux_credential_result_t result = CRUX_CREDENTIAL_ERR_ENTROPY;
    if (crux_credential_port_generate_key(type, public_key, sizeof(public_key), private_key,
                                          sizeof(private_key), lengths) == CRUX_CREDENTIAL_OK &&
        key_record_build(type, key_record, public_key, private_key, lengths)) {
        size_t length = CRUX_CREDENTIAL_KEY_HEADER_BYTES + lengths[0] + lengths[1];
        result = crux_credential_payload_set(record, key_record, length, store.plain,
                                             &store.payload_length, sizeof(store.plain))
                     ? image_save(adds_record)
                     : CRUX_CREDENTIAL_ERR_TOO_LARGE;
    }
    secure_zero(private_key, sizeof(private_key));
    secure_zero(key_record, sizeof(key_record));
    return result;
}

/*
 * A key is acknowledged only once both slots hold it, and every later image then carries it.
 * Without that, a key used after a failed second write could rot away with its one slot and read as
 * absent, and the next call would silently generate another. A slot that lacks the key proves that
 * it was never announced, while a corrupt or unreadable one might have held it.
 */
static crux_credential_result_t key_acknowledge(crux_credential_record_t record)
{
    slot_view_t other;
    slot_classify(1u - store.active, &other);
    if (other.kind == SLOT_VALID && (other.records & (1u << (uint32_t)record)) != 0) {
        return CRUX_CREDENTIAL_OK;
    }
    if (!slot_repair()) {
        return CRUX_CREDENTIAL_ERR_STORE;
    }
    if (other.kind != SLOT_CORRUPT && other.kind != SLOT_UNREADABLE) {
        event_send_key(CRUX_CREDENTIAL_EVENT_KEY_GENERATED, record);
    }
    return CRUX_CREDENTIAL_OK;
}

crux_credential_result_t crux_credential_key_ensure(crux_credential_record_t record,
                                                    crux_credential_key_type_t type)
{
    if (!key_record_kind(record) || !key_type_known(type)) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    crux_credential_result_t result = store_enter_usable();
    if (result != CRUX_CREDENTIAL_OK) {
        return result;
    }
    if (!image_load()) {
        /* Absence is unproven, so nothing may be generated. */
        result = CRUX_CREDENTIAL_ERR_READ;
    } else if (!image_has(record)) {
        result = key_generate(record, type, true);
        if (result == CRUX_CREDENTIAL_OK) {
            event_send_key(CRUX_CREDENTIAL_EVENT_KEY_GENERATED, record);
        }
    } else {
        result = key_acknowledge(record);
    }
    store_leave();
    return result;
}

crux_credential_result_t crux_credential_key_replace(crux_credential_record_t record,
                                                     crux_credential_key_type_t type)
{
    if (!key_record_kind(record) || !key_type_known(type)) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    crux_credential_result_t result = store_enter_usable();
    if (result != CRUX_CREDENTIAL_OK) {
        return result;
    }
    if (!image_load()) {
        result = CRUX_CREDENTIAL_ERR_READ;
    } else if (!image_has(record)) {
        result = CRUX_CREDENTIAL_ERR_NOT_FOUND;
    } else {
        result = key_generate(record, type, false);
        if (result == CRUX_CREDENTIAL_OK) {
            event_send_key(CRUX_CREDENTIAL_EVENT_KEY_REPLACED, record);
        }
    }
    store_leave();
    return result;
}

crux_credential_result_t crux_credential_key_public(crux_credential_record_t record, void *buffer,
                                                    size_t capacity, size_t *length)
{
    if (!key_record_kind(record) || length == NULL || (buffer == NULL && capacity != 0)) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    *length = 0;
    crux_credential_result_t result = store_enter_usable();
    if (result != CRUX_CREDENTIAL_OK) {
        return result;
    }
    const uint8_t *data = NULL;
    size_t data_length = 0;
    crux_credential_key_view_t view;
    result = CRUX_CREDENTIAL_ERR_READ;
    if (image_load()) {
        if (!crux_credential_payload_find(store.plain, record, &data, &data_length)) {
            result = CRUX_CREDENTIAL_ERR_NOT_FOUND;
        } else if (crux_credential_key_parse(data, data_length, &view) == CRUX_CREDENTIAL_OK) {
            result = copy_out(view.public_key, view.public_length, buffer, capacity, length);
        }
        /* Otherwise a record opened but cannot be split, which only a key holder could store. */
    }
    store_leave();
    return result;
}

/* Overwrites a slot with zeros and syncs it. An absent slot has nothing to overwrite. */
static bool slot_zero(size_t index)
{
    crux_file_info_t info;
    crux_file_result_t result = crux_file_stat(store.roots[index], SLOT_FILE_NAME, &info);
    if (result == CRUX_FILE_ERR_NOT_FOUND) {
        return true;
    }
    if (result != CRUX_FILE_OK) {
        return false;
    }
    crux_file_t *file = NULL;
    if (crux_file_open_at(store.roots[index], SLOT_FILE_NAME, CRUX_FILE_READ_WRITE, &file) !=
        CRUX_FILE_OK) {
        return false;
    }
    /* The sealed buffer serves as the zeros, because the store lock is held and it was cleared. */
    result = CRUX_FILE_OK;
    for (uint64_t done = 0; result == CRUX_FILE_OK && done < info.size;) {
        uint64_t left = info.size - done;
        size_t chunk = left < sizeof(store.sealed) ? (size_t)left : sizeof(store.sealed);
        result = write_all(file, store.sealed, chunk);
        done += chunk;
    }
    if (result == CRUX_FILE_OK) {
        result = crux_file_sync(file);
    }
    crux_file_result_t closed = crux_file_close(&file);
    return result == CRUX_FILE_OK && closed == CRUX_FILE_OK;
}

crux_credential_result_t crux_credential_erase(void)
{
    if (!store.started || !lock_with_retry(store.store_lock)) {
        return CRUX_CREDENTIAL_ERR_NOT_READY;
    }
    /* From here provisioning needs a reset, whatever the steps below manage to do. */
    if (store.state != CRUX_CREDENTIAL_STATE_UNUSABLE) {
        state_publish(CRUX_CREDENTIAL_STATE_UNUSABLE);
    }
    buffers_clear();
    bool complete = true;
    for (size_t index = 0; index < SLOT_COUNT; ++index) {
        complete = slot_zero(index) && complete;
    }
    for (size_t index = 0; index < SLOT_COUNT; ++index) {
        crux_file_result_t removed = crux_file_remove_at(store.roots[index], SLOT_FILE_NAME);
        complete = (removed == CRUX_FILE_OK || removed == CRUX_FILE_ERR_NOT_FOUND) && complete;
    }
    crux_credential_result_t result = complete ? CRUX_CREDENTIAL_OK : CRUX_CREDENTIAL_ERR_STORE;
    event_send(CRUX_CREDENTIAL_EVENT_ERASED, result);
    store_leave();
    return result;
}

#ifdef CRUX_CREDENTIAL_TESTING
/* Simulates a reset for the instrumented test core. RAM is lost, and the locks are reused. */
void crux_credential_test_reset(void);
bool crux_credential_test_buffers_clear(void);

void crux_credential_test_reset(void)
{
    crux_mutex_t store_lock = store.store_lock;
    crux_mutex_t info_lock = store.info_lock;
    bool locks_created = store.locks_created;
    memset(&store, 0, sizeof(store));
    store.store_lock = store_lock;
    store.info_lock = info_lock;
    store.locks_created = locks_created;
}

bool crux_credential_test_buffers_clear(void)
{
    uint8_t difference = 0;
    for (size_t index = 0; index < sizeof(store.sealed); ++index) {
        difference = (uint8_t)(difference | store.sealed[index]);
    }
    for (size_t index = 0; index < sizeof(store.plain); ++index) {
        difference = (uint8_t)(difference | store.plain[index]);
    }
    return difference == 0;
}
#endif
