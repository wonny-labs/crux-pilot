#include "crux_tls_certificate_adapter.h"
#include "tls_certificate_port.h"
#include "tls_x509.h"

#include <crux/sync.h>
#include <crux_credential_adapter.h>

#include <string.h>

/*
 * The certificate record in the credential store: a fixed header, then the DER certificate.
 *   0  4  "CXTC"
 *   4  1  format, 1
 *   5  1  source, crux_tls_certificate_source_t
 *   6  2  zero
 *   8  n  DER
 */
#define RECORD_HEADER_BYTES 8u
#define RECORD_FORMAT 1u
#define RECORD_BYTES (RECORD_HEADER_BYTES + CRUX_TLS_CERTIFICATE_DER_MAX)
#define KEY_SCALAR_OFFSET 36u
/* How long a release waits for an operation to finish, in milliseconds. */
#ifndef CRUX_TLS_CERTIFICATE_RELEASE_WAIT_MS
#define CRUX_TLS_CERTIFICATE_RELEASE_WAIT_MS 100u
#endif

_Static_assert(RECORD_BYTES == CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX,
               "a record of the largest certificate fills the store's record exactly");
_Static_assert(CRUX_TLS_CERTIFICATE_KEY_DER_BYTES == 138u, "the PKCS #8 form of a P-256 key");
_Static_assert(CRUX_TLS_CERTIFICATE_TIME_MAX_S == TLS_X509_TIME_MAX_S, "one latest date");

static const uint8_t record_magic[4] = {'C', 'X', 'T', 'C'};

typedef enum {
    STATE_NOT_STARTED = 0,
    STATE_NO_CERTIFICATE, /* The key is loaded and the stored certificate was refused. */
    STATE_ACTIVE
} state_t;

/* One certificate buffer. A TLS stack may hold pointers into it while it is taken. */
typedef struct {
    uint8_t record[RECORD_BYTES];
    size_t length; /* DER bytes, after the header. */
    tls_x509_view_t view;
    uint8_t fingerprint[32];
    char fingerprint_text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    unsigned taken;
} slot_t;

static state_t state;
static bool lock_made;
static crux_mutex_t lock;
static const crux_tls_certificate_options_t *start_options;
static uint8_t device_public[TLS_X509_P256_POINT_BYTES];
/* The PKCS #8 key, which a TLS stack keeps a pointer to. It holds the scalar for signing too. */
static uint8_t key_der[CRUX_TLS_CERTIFICATE_KEY_DER_BYTES];
static slot_t slots[2];
static size_t active;
static uint32_t generation;
static uint8_t request_scratch[CRUX_TLS_CERTIFICATE_CSR_MAX + 64u];
/* The key record read from the store, held only during start and cleared before it returns. A
 * static buffer rather than the stack, so that a test can see that it is cleared. */
static uint8_t key_record[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
static void (*on_changed)(void *context);
static void *on_changed_context;

static uint8_t *slot_der(slot_t *slot)
{
    return slot->record + RECORD_HEADER_BYTES;
}

static void wipe(void *buffer, size_t length)
{
    volatile uint8_t *bytes = buffer;
    while (length > 0u) {
        *bytes++ = 0;
        --length;
    }
}

static bool hash(const uint8_t *data, size_t length, uint8_t digest[32])
{
    return crux_tls_certificate_port_sha256(data, length, digest);
}

static bool sign_with_device_key(void *context, const uint8_t digest[32], uint8_t signature[64])
{
    (void)context;
    return crux_tls_certificate_port_sign(digest, signature, key_der + KEY_SCALAR_OFFSET);
}

/* ---- Events. */

typedef struct {
    uint8_t channel;
    uint8_t source_length;
    uint8_t source[16];
    char user[CRUX_ACCESS_NAME_SIZE];
} actor_t;

static void actor_of(crux_access_session_t session, actor_t *actor)
{
    memset(actor, 0, sizeof(*actor));
    if (crux_access_session_origin(session, &actor->channel, actor->source,
                                   &actor->source_length) != CRUX_ACCESS_OK) {
        memset(actor, 0, sizeof(*actor));
    }
    if (crux_access_session_user(session, actor->user, sizeof(actor->user)) != CRUX_ACCESS_OK) {
        actor->user[0] = '\0';
    }
}

static void emit(crux_tls_certificate_event_type_t type, crux_tls_certificate_reason_t reason,
                 bool failed, const actor_t *actor)
{
    crux_tls_certificate_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.reason = reason;
    event.failed = failed;
    if (actor != NULL) {
        event.channel = actor->channel;
        event.source_length = actor->source_length;
        memcpy(event.source, actor->source, sizeof(event.source));
        memcpy(event.user, actor->user, sizeof(event.user));
    }
    crux_tls_certificate_port_event(&event);
}

/* ---- Results. */

static crux_tls_certificate_result_t from_access(crux_access_result_t result)
{
    switch (result) {
    case CRUX_ACCESS_OK:
        return CRUX_TLS_CERTIFICATE_OK;
    case CRUX_ACCESS_ERR_DENIED:
        return CRUX_TLS_CERTIFICATE_ERR_DENIED;
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        return CRUX_TLS_CERTIFICATE_ERR_SESSION_INVALID;
    case CRUX_ACCESS_ERR_NOT_READY:
        return CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
    default:
        return CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    }
}

static crux_tls_certificate_result_t from_store(crux_credential_result_t result)
{
    switch (result) {
    case CRUX_CREDENTIAL_OK:
        return CRUX_TLS_CERTIFICATE_OK;
    case CRUX_CREDENTIAL_ERR_ENTROPY:
        return CRUX_TLS_CERTIFICATE_ERR_CRYPTO;
    case CRUX_CREDENTIAL_ERR_NOT_READY:
    case CRUX_CREDENTIAL_ERR_UNUSABLE:
        return CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
    case CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT:
        return CRUX_TLS_CERTIFICATE_ERR_INVALID;
    default:
        return CRUX_TLS_CERTIFICATE_ERR_STORE;
    }
}

static crux_tls_certificate_result_t from_parse(tls_x509_result_t result)
{
    return result == TLS_X509_OK        ? CRUX_TLS_CERTIFICATE_OK
           : result == TLS_X509_INVALID ? CRUX_TLS_CERTIFICATE_ERR_INVALID
                                        : CRUX_TLS_CERTIFICATE_ERR_MALFORMED;
}

static crux_tls_certificate_reason_t reason_of(crux_tls_certificate_result_t result)
{
    switch (result) {
    case CRUX_TLS_CERTIFICATE_ERR_INVALID:
        return CRUX_TLS_CERTIFICATE_REASON_INVALID;
    case CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH:
        return CRUX_TLS_CERTIFICATE_REASON_KEY_MISMATCH;
    case CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE:
        return CRUX_TLS_CERTIFICATE_REASON_TOO_LARGE;
    default:
        return CRUX_TLS_CERTIFICATE_REASON_MALFORMED;
    }
}

/* ---- Slots. */

/* Checks a certificate for this device and fills the slot's view and fingerprint. */
static crux_tls_certificate_result_t judge(slot_t *slot, size_t length)
{
    tls_x509_view_t view;
    crux_tls_certificate_result_t result =
        from_parse(tls_x509_parse(slot_der(slot), length, &view));
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        return result;
    }
    if (memcmp(view.public_key, device_public, sizeof(device_public)) != 0) {
        return CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH;
    }
    if (!hash(slot_der(slot), length, slot->fingerprint)) {
        return CRUX_TLS_CERTIFICATE_ERR_CRYPTO;
    }
    tls_x509_fingerprint_text(slot->fingerprint, slot->fingerprint_text);
    slot->view = view;
    slot->length = length;
    return CRUX_TLS_CERTIFICATE_OK;
}

static void set_header(slot_t *slot, crux_tls_certificate_source_t source)
{
    memcpy(slot->record, record_magic, sizeof(record_magic));
    slot->record[4] = RECORD_FORMAT;
    slot->record[5] = (uint8_t)source;
    slot->record[6] = 0;
    slot->record[7] = 0;
}

static crux_tls_certificate_source_t source_of(const slot_t *slot)
{
    return (crux_tls_certificate_source_t)slot->record[5];
}

static void describe(const slot_t *slot, crux_tls_certificate_info_t *out)
{
    memset(out, 0, sizeof(*out));
    out->source = source_of(slot);
    memcpy(out->fingerprint, slot->fingerprint, sizeof(out->fingerprint));
    memcpy(out->fingerprint_text, slot->fingerprint_text, sizeof(out->fingerprint_text));
    memcpy(out->subject, slot->view.subject, sizeof(out->subject));
    memcpy(out->issuer, slot->view.issuer, sizeof(out->issuer));
    out->not_before_s = slot->view.not_before_s;
    out->not_after_s = slot->view.not_after_s;
    out->no_expiry = slot->view.no_expiry;
    out->length = slot->length;
    out->checks = CRUX_TLS_CERTIFICATE_CHECKS;
}

/* The buffer a change writes, or NULL while the TLS side holds it. */
static slot_t *writable_slot(void)
{
    slot_t *slot = state == STATE_ACTIVE ? &slots[1u - active] : &slots[0];
    return slot->taken == 0u ? slot : NULL;
}

static void make_active(slot_t *slot)
{
    active = (size_t)(slot - slots);
    generation += 1u;
    state = STATE_ACTIVE;
}

/* Builds a self-signed certificate into the slot, with a new serial. */
static crux_tls_certificate_result_t build_self_signed(slot_t *slot,
                                                       const crux_tls_certificate_names_t *names)
{
    tls_x509_self_signed_t input = {
        names, {0}, start_options->not_before_s, start_options->validity_days};
    if (!crux_tls_certificate_port_random(input.serial, sizeof(input.serial))) {
        return CRUX_TLS_CERTIFICATE_ERR_CRYPTO;
    }
    size_t length = 0;
    tls_x509_build_t built =
        tls_x509_build_self_signed(&input, device_public, hash, sign_with_device_key, NULL,
                                   slot_der(slot), CRUX_TLS_CERTIFICATE_DER_MAX, &length);
    if (built != TLS_X509_BUILT) {
        return built == TLS_X509_BAD_INPUT ? CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT
               : built == TLS_X509_NO_ROOM ? CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE
                                           : CRUX_TLS_CERTIFICATE_ERR_CRYPTO;
    }
    set_header(slot, CRUX_TLS_CERTIFICATE_SOURCE_SELF_SIGNED);
    return judge(slot, length);
}

static crux_tls_certificate_result_t save(slot_t *slot)
{
    return from_store(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, slot->record,
                                            RECORD_HEADER_BYTES + slot->length));
}

/* ---- Start. */

static bool options_valid(const crux_tls_certificate_options_t *options)
{
    return options != NULL && tls_x509_names_valid(&options->names) && options->not_before_s >= 0 &&
           options->not_before_s <= TLS_X509_TIME_MAX_S &&
           (int64_t)options->validity_days * 86400 <= TLS_X509_TIME_MAX_S - options->not_before_s;
}

static crux_tls_certificate_result_t load_key(void)
{
    crux_tls_certificate_result_t result = from_store(
        crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY, CRUX_CREDENTIAL_KEY_ECDSA_P256));
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        return result;
    }
    uint8_t *record = key_record;
    size_t length = 0;
    result = from_store(
        crux_credential_read(CRUX_CREDENTIAL_RECORD_TLS_KEY, record, sizeof(key_record), &length));
    crux_credential_key_view_t view;
    if (result == CRUX_TLS_CERTIFICATE_OK &&
        (crux_credential_key_parse(record, length, &view) != CRUX_CREDENTIAL_OK ||
         view.type != CRUX_CREDENTIAL_KEY_ECDSA_P256 ||
         view.public_length != TLS_X509_P256_POINT_BYTES ||
         view.private_length != TLS_X509_P256_SCALAR_BYTES)) {
        result = CRUX_TLS_CERTIFICATE_ERR_INVALID;
    }
    if (result == CRUX_TLS_CERTIFICATE_OK) {
        memcpy(device_public, view.public_key, sizeof(device_public));
        size_t der_length = 0;
        if (tls_x509_build_pkcs8(view.private_key, view.public_key, key_der, sizeof(key_der),
                                 &der_length) != TLS_X509_BUILT) {
            result = CRUX_TLS_CERTIFICATE_ERR_INVALID;
        }
    }
    wipe(key_record, sizeof(key_record));
    return result;
}

/* Decodes the stored record into slot 0. */
static crux_tls_certificate_result_t load_stored(size_t length)
{
    slot_t *slot = &slots[0];
    if (length < RECORD_HEADER_BYTES + 1u || memcmp(slot->record, record_magic, 4) != 0 ||
        slot->record[4] != RECORD_FORMAT ||
        (slot->record[5] != CRUX_TLS_CERTIFICATE_SOURCE_SELF_SIGNED &&
         slot->record[5] != CRUX_TLS_CERTIFICATE_SOURCE_INSTALLED) ||
        slot->record[6] != 0u || slot->record[7] != 0u) {
        return CRUX_TLS_CERTIFICATE_ERR_INVALID;
    }
    return judge(slot, length - RECORD_HEADER_BYTES);
}

crux_tls_certificate_result_t
crux_tls_certificate_start(const crux_tls_certificate_options_t *options,
                           crux_tls_certificate_start_t *how)
{
    if (!options_valid(options) || how == NULL) {
        return CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    }
    if (state != STATE_NOT_STARTED) {
        return CRUX_TLS_CERTIFICATE_ERR_BUSY;
    }
    if (!lock_made) {
        if (crux_mutex_create(&lock) != CRUX_OK) {
            return CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
        }
        lock_made = true;
    }
    start_options = options;
    crux_tls_certificate_result_t result = load_key();
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        wipe(key_der, sizeof(key_der));
        return result;
    }
    size_t length = 0;
    crux_credential_result_t read = crux_credential_read(
        CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, slots[0].record, sizeof(slots[0].record), &length);
    if (read == CRUX_CREDENTIAL_ERR_NOT_FOUND) {
        result = build_self_signed(&slots[0], &options->names);
        if (result == CRUX_TLS_CERTIFICATE_OK) {
            result = save(&slots[0]);
        }
        if (result != CRUX_TLS_CERTIFICATE_OK) {
            wipe(key_der, sizeof(key_der));
            return result;
        }
        make_active(&slots[0]);
        emit(CRUX_TLS_CERTIFICATE_EVENT_REPLACED, CRUX_TLS_CERTIFICATE_REASON_GENERATED, false,
             NULL);
        *how = CRUX_TLS_CERTIFICATE_GENERATED;
        return CRUX_TLS_CERTIFICATE_OK;
    }
    if (read != CRUX_CREDENTIAL_OK) {
        wipe(key_der, sizeof(key_der));
        return read == CRUX_CREDENTIAL_ERR_TOO_LARGE ? CRUX_TLS_CERTIFICATE_ERR_INVALID
                                                     : from_store(read);
    }
    result = load_stored(length);
    if (result == CRUX_TLS_CERTIFICATE_ERR_CRYPTO) {
        wipe(key_der, sizeof(key_der));
        return result;
    }
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        /* Fail closed: keep the key for an operator's recovery, serve nothing, replace nothing. */
        state = STATE_NO_CERTIFICATE;
        emit(CRUX_TLS_CERTIFICATE_EVENT_UNUSABLE, reason_of(result), false, NULL);
        return result;
    }
    make_active(&slots[0]);
    *how = CRUX_TLS_CERTIFICATE_STARTED;
    return CRUX_TLS_CERTIFICATE_OK;
}

/* ---- Operations with a session. */

static crux_tls_certificate_result_t enter(crux_access_session_t session)
{
    crux_tls_certificate_result_t result =
        from_access(crux_access_check(session, CRUX_TLS_CERTIFICATE_PERMISSION_MANAGE));
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        return result;
    }
    if (!lock_made) {
        return CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
    }
    if (crux_mutex_lock(lock, 0) != CRUX_OK) {
        return CRUX_TLS_CERTIFICATE_ERR_BUSY;
    }
    if (state == STATE_NOT_STARTED) {
        (void)crux_mutex_unlock(lock);
        return CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
    }
    return CRUX_TLS_CERTIFICATE_OK;
}

static void leave(void)
{
    (void)crux_mutex_unlock(lock);
}

static void announce(void)
{
    void (*changed)(void *) = on_changed;
    if (changed != NULL) {
        changed(on_changed_context);
    }
}

crux_tls_certificate_result_t crux_tls_certificate_get(crux_access_session_t session,
                                                       crux_tls_certificate_info_t *out)
{
    if (out == NULL) {
        return CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    }
    crux_tls_certificate_result_t result = enter(session);
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        return result;
    }
    if (state != STATE_ACTIVE) {
        result = CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
    } else {
        describe(&slots[active], out);
    }
    leave();
    return result;
}

crux_tls_certificate_result_t crux_tls_certificate_copy(crux_access_session_t session, uint8_t *der,
                                                        size_t capacity, size_t *length)
{
    if (length == NULL || (der == NULL && capacity > 0u)) {
        return CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    }
    *length = 0;
    crux_tls_certificate_result_t result = enter(session);
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        return result;
    }
    if (state != STATE_ACTIVE) {
        result = CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
    } else {
        slot_t *slot = &slots[active];
        *length = slot->length;
        if (capacity < slot->length || der == NULL) {
            result = CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE;
        } else {
            memcpy(der, slot_der(slot), slot->length);
        }
    }
    leave();
    return result;
}

crux_tls_certificate_result_t
crux_tls_certificate_request(crux_access_session_t session,
                             const crux_tls_certificate_names_t *names, uint8_t *der,
                             size_t capacity, size_t *length)
{
    if (length == NULL || der == NULL) {
        return CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    }
    *length = 0;
    crux_tls_certificate_result_t result = enter(session);
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        return result;
    }
    const crux_tls_certificate_names_t *chosen = names != NULL ? names : &start_options->names;
    size_t built_length = 0;
    tls_x509_build_t built =
        tls_x509_build_request(chosen, device_public, hash, sign_with_device_key, NULL,
                               request_scratch, sizeof(request_scratch), &built_length);
    if (built == TLS_X509_BAD_INPUT) {
        result = CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    } else if (built != TLS_X509_BUILT) {
        result = built == TLS_X509_NO_ROOM ? CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE
                                           : CRUX_TLS_CERTIFICATE_ERR_CRYPTO;
    } else if (capacity < built_length) {
        result = CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE;
    } else {
        memcpy(der, request_scratch, built_length);
        *length = built_length;
    }
    leave();
    return result;
}

/* A refusal after the permission check: logged, and nothing changes. */
static crux_tls_certificate_result_t refuse(crux_tls_certificate_result_t result,
                                            const actor_t *actor)
{
    emit(CRUX_TLS_CERTIFICATE_EVENT_REJECTED, reason_of(result), false, actor);
    leave();
    return result;
}

/* Saves the slot and makes it active, or logs the failed save and keeps the previous one. */
static crux_tls_certificate_result_t commit(slot_t *slot, crux_tls_certificate_reason_t reason,
                                            const actor_t *actor, crux_tls_certificate_info_t *out)
{
    crux_tls_certificate_result_t result = save(slot);
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        emit(CRUX_TLS_CERTIFICATE_EVENT_REPLACED, CRUX_TLS_CERTIFICATE_REASON_SAVE_FAILED, true,
             actor);
        leave();
        return result;
    }
    make_active(slot);
    if (out != NULL) {
        describe(slot, out);
    }
    emit(CRUX_TLS_CERTIFICATE_EVENT_REPLACED, reason, false, actor);
    leave();
    announce();
    return CRUX_TLS_CERTIFICATE_OK;
}

crux_tls_certificate_result_t crux_tls_certificate_install(crux_access_session_t session,
                                                           const uint8_t *der, size_t length,
                                                           crux_tls_certificate_info_t *out)
{
    if (der == NULL || length == 0u) {
        return CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    }
    crux_tls_certificate_result_t result = enter(session);
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        return result;
    }
    slot_t *slot = writable_slot();
    if (slot == NULL) {
        leave();
        return CRUX_TLS_CERTIFICATE_ERR_BUSY;
    }
    actor_t actor;
    actor_of(session, &actor);
    if (length > CRUX_TLS_CERTIFICATE_DER_MAX) {
        return refuse(CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE, &actor);
    }
    memcpy(slot_der(slot), der, length);
    set_header(slot, CRUX_TLS_CERTIFICATE_SOURCE_INSTALLED);
    result = judge(slot, length);
    if (result == CRUX_TLS_CERTIFICATE_ERR_CRYPTO) {
        leave();
        return result;
    }
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        return refuse(result, &actor);
    }
    return commit(slot, CRUX_TLS_CERTIFICATE_REASON_INSTALLED, &actor, out);
}

crux_tls_certificate_result_t
crux_tls_certificate_regenerate(crux_access_session_t session,
                                const crux_tls_certificate_names_t *names,
                                crux_tls_certificate_info_t *out)
{
    if (names != NULL && !tls_x509_names_valid(names)) {
        return CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    }
    crux_tls_certificate_result_t result = enter(session);
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        return result;
    }
    slot_t *slot = writable_slot();
    if (slot == NULL) {
        leave();
        return CRUX_TLS_CERTIFICATE_ERR_BUSY;
    }
    actor_t actor;
    actor_of(session, &actor);
    result = build_self_signed(slot, names != NULL ? names : &start_options->names);
    if (result != CRUX_TLS_CERTIFICATE_OK) {
        leave();
        return result;
    }
    return commit(slot, CRUX_TLS_CERTIFICATE_REASON_REGENERATED, &actor, out);
}

crux_tls_certificate_dates_t crux_tls_certificate_dates_at(const crux_tls_certificate_info_t *info,
                                                           bool clock_set, int64_t now_s)
{
    if (info == NULL || !clock_set) {
        return CRUX_TLS_CERTIFICATE_DATES_UNKNOWN;
    }
    if (now_s < info->not_before_s) {
        return CRUX_TLS_CERTIFICATE_DATES_NOT_YET_VALID;
    }
    if (!info->no_expiry && now_s > info->not_after_s) {
        return CRUX_TLS_CERTIFICATE_DATES_EXPIRED;
    }
    return CRUX_TLS_CERTIFICATE_DATES_VALID;
}

/* ---- For a composition. */

crux_tls_certificate_result_t
crux_tls_certificate_material_take(crux_tls_certificate_material_t *out)
{
    if (out == NULL) {
        return CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    }
    if (!lock_made) {
        return CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
    }
    if (crux_mutex_lock(lock, 0) != CRUX_OK) {
        return CRUX_TLS_CERTIFICATE_ERR_BUSY;
    }
    crux_tls_certificate_result_t result = CRUX_TLS_CERTIFICATE_OK;
    if (state != STATE_ACTIVE) {
        result = CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
    } else {
        slot_t *slot = &slots[active];
        slot->taken += 1u;
        out->certificate = slot_der(slot);
        out->certificate_length = slot->length;
        out->private_key = key_der;
        out->private_key_length = sizeof(key_der);
        out->generation = generation;
    }
    leave();
    return result;
}

crux_tls_certificate_result_t
crux_tls_certificate_material_release(const crux_tls_certificate_material_t *material)
{
    if (material == NULL || !lock_made) {
        return CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    }
    if (crux_mutex_lock(lock, CRUX_TLS_CERTIFICATE_RELEASE_WAIT_MS) != CRUX_OK) {
        return CRUX_TLS_CERTIFICATE_ERR_BUSY;
    }
    crux_tls_certificate_result_t result = CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    for (size_t i = 0; i < 2u; ++i) {
        if (material->certificate == slot_der(&slots[i]) && slots[i].taken > 0u) {
            slots[i].taken -= 1u;
            result = CRUX_TLS_CERTIFICATE_OK;
        }
    }
    leave();
    return result;
}

void crux_tls_certificate_on_change(void (*changed)(void *context), void *context)
{
    on_changed = changed;
    on_changed_context = context;
}

crux_tls_certificate_result_t
crux_tls_certificate_fingerprint(char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE])
{
    if (text == NULL) {
        return CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT;
    }
    if (!lock_made) {
        return CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
    }
    if (crux_mutex_lock(lock, 0) != CRUX_OK) {
        return CRUX_TLS_CERTIFICATE_ERR_BUSY;
    }
    crux_tls_certificate_result_t result = CRUX_TLS_CERTIFICATE_OK;
    if (state != STATE_ACTIVE) {
        result = CRUX_TLS_CERTIFICATE_ERR_NOT_READY;
    } else {
        memcpy(text, slots[active].fingerprint_text, CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE);
    }
    leave();
    return result;
}

#ifdef CRUX_TLS_CERTIFICATE_TESTING
/* A reset for the Host suites: RAM is lost, as at a power cycle. The lock is kept. */
void crux_tls_certificate_test_reset(void);
void crux_tls_certificate_test_reset(void)
{
    state = STATE_NOT_STARTED;
    start_options = NULL;
    wipe(device_public, sizeof(device_public));
    wipe(key_der, sizeof(key_der));
    wipe(slots, sizeof(slots));
    active = 0;
    generation = 0;
    on_changed = NULL;
    on_changed_context = NULL;
}

/* True if the module's RAM holds these bytes anywhere but the key it must keep for TLS. */
bool crux_tls_certificate_test_ram_holds(const uint8_t *bytes, size_t length);
bool crux_tls_certificate_test_ram_holds(const uint8_t *bytes, size_t length)
{
    const uint8_t *areas[] = {(const uint8_t *)slots, request_scratch, device_public, key_record};
    const size_t sizes[] = {sizeof(slots), sizeof(request_scratch), sizeof(device_public),
                            sizeof(key_record)};
    for (size_t a = 0; a < 4u; ++a) {
        for (size_t i = 0; i + length <= sizes[a]; ++i) {
            if (memcmp(areas[a] + i, bytes, length) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* True if the PKCS #8 copy holds only zeros. */
bool crux_tls_certificate_test_key_cleared(void);
bool crux_tls_certificate_test_key_cleared(void)
{
    for (size_t i = 0; i < sizeof(key_der); ++i) {
        if (key_der[i] != 0u) {
            return false;
        }
    }
    return true;
}

#endif
