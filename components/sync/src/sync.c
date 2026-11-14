#include "sync_internal.h"
#include <stdatomic.h>
#include <stddef.h>

/*
 * Slot metadata takes no lock. Each slot has one 32-bit state word:
 *
 *   bits 0..13  admitted operations (users)
 *   bit  14     mutex held
 *   bit  15     live
 *   bits 16..31 low 16 bits of the generation, while live
 *
 * A word without the live bit is FREE, CLAIMED (create or destroy in progress) or RETIRED.
 * Admission is one compare-and-swap that checks live and the generation tag and adds a user;
 * the full generation is then compared under that reference. Destroy succeeds only by swapping
 * "live, own reference only, not held" for CLAIMED. A mutex's held flag and owner change only
 * while the changing thread holds a reference. No Sync lock is held while a native object is
 * locked, unlocked, given or taken, so Sync adds nothing to the kernel's priority inheritance.
 */
#define STATE_USERS_MASK 0x3FFFU
#define STATE_HELD 0x4000U
#define STATE_LIVE 0x8000U
#define STATE_TAG_SHIFT 16U
#define STATE_TAG_MASK 0xFFFF0000U
#define STATE_FREE 0U
#define STATE_CLAIMED 1U
#define STATE_RETIRED 2U

_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && sizeof(unsigned int) == sizeof(uint32_t),
               "Sync slot metadata needs lock-free 32-bit atomics");

typedef struct {
    atomic_uint state;
    atomic_uint generation; /* Written only while CLAIMED. */
} lifetime_t;

typedef struct {
    lifetime_t life;
    _Atomic(sync_native_identity_t) owner; /* Meaningful only while STATE_HELD is set. */
    sync_native_mutex_t native;
} mutex_slot_t;

typedef struct {
    lifetime_t life;
    sync_native_semaphore_t native;
} semaphore_slot_t;

static mutex_slot_t mutexes[CRUX_SYNC_MUTEX_COUNT];
static semaphore_slot_t semaphores[CRUX_SYNC_SEMAPHORE_COUNT];

static unsigned int live_state(uint32_t generation)
{
    return STATE_LIVE | ((generation & 0xFFFFU) << STATE_TAG_SHIFT);
}

static void release(lifetime_t *life)
{
    (void)atomic_fetch_sub_explicit(&life->state, 1U, memory_order_release);
}

/* Take a reference that keeps this lifetime, and so its generation, until release(). */
static crux_status_t admit(lifetime_t *life, uint32_t generation)
{
    /* A handle that is already stale must not pin a later lifetime, even briefly. */
    if (atomic_load_explicit(&life->generation, memory_order_relaxed) != generation) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    const unsigned int live = live_state(generation);
    unsigned int state = atomic_load_explicit(&life->state, memory_order_relaxed);
    do {
        if ((state & (STATE_LIVE | STATE_TAG_MASK)) != live) {
            return CRUX_ERR_INVALID_ARGUMENT;
        }
        if ((state & STATE_USERS_MASK) == STATE_USERS_MASK) {
            return CRUX_ERR_NO_RESOURCES;
        }
    } while (!atomic_compare_exchange_weak_explicit(&life->state, &state, state + 1U,
                                                    memory_order_acquire, memory_order_relaxed));
    /* The tag holds 16 bits; the reference now pins the full generation for this check. */
    if (atomic_load_explicit(&life->generation, memory_order_relaxed) != generation) {
        release(life);
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    return CRUX_OK;
}

/* Exchange the caller's only reference to an idle object for exclusive access. */
static bool close_admitted(lifetime_t *life, uint32_t generation)
{
    unsigned int expected = live_state(generation) | 1U;
    return atomic_compare_exchange_strong_explicit(&life->state, &expected, STATE_CLAIMED,
                                                   memory_order_acq_rel, memory_order_relaxed);
}

static bool claim(lifetime_t *life)
{
    unsigned int expected = STATE_FREE;
    return atomic_compare_exchange_strong_explicit(&life->state, &expected, STATE_CLAIMED,
                                                   memory_order_acquire, memory_order_relaxed);
}

static uint32_t next_generation(lifetime_t *life)
{
    /* A FREE slot is below UINT32_MAX: end_lifetime() retires the slot that reaches it. */
    uint32_t generation = atomic_load_explicit(&life->generation, memory_order_relaxed) + 1U;
    atomic_store_explicit(&life->generation, generation, memory_order_relaxed);
    return generation;
}

static void publish(lifetime_t *life, uint32_t generation)
{
    atomic_store_explicit(&life->state, live_state(generation), memory_order_release);
}

static void end_lifetime(lifetime_t *life)
{
    bool exhausted = atomic_load_explicit(&life->generation, memory_order_relaxed) == UINT32_MAX;
    atomic_store_explicit(&life->state, exhausted ? STATE_RETIRED : STATE_FREE,
                          memory_order_release);
}

static mutex_slot_t *mutex_slot(crux_mutex_t handle)
{
    if (handle.slot == 0 || handle.slot > CRUX_SYNC_MUTEX_COUNT) {
        return NULL;
    }
    return &mutexes[handle.slot - 1];
}

static semaphore_slot_t *semaphore_slot(crux_semaphore_t handle)
{
    if (handle.slot == 0 || handle.slot > CRUX_SYNC_SEMAPHORE_COUNT) {
        return NULL;
    }
    return &semaphores[handle.slot - 1];
}

/* Only the caller can have stored its own identity, so a match is exact. */
static bool held_by_caller(mutex_slot_t *slot)
{
    unsigned int state = atomic_load_explicit(&slot->life.state, memory_order_acquire);
    return (state & STATE_HELD) != 0 &&
           crux_sync_identity_equal(atomic_load_explicit(&slot->owner, memory_order_relaxed),
                                    crux_sync_current_identity());
}

crux_status_t crux_mutex_create(crux_mutex_t *out)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < CRUX_SYNC_MUTEX_COUNT; ++i) {
        mutex_slot_t *slot = &mutexes[i];
        if (!claim(&slot->life)) {
            continue;
        }
        uint32_t generation = next_generation(&slot->life);
        crux_status_t result = crux_mutex_impl_create(&slot->native);
        if (result != CRUX_OK) {
            end_lifetime(&slot->life);
            return result;
        }
        publish(&slot->life, generation);
        *out = (crux_mutex_t){.slot = i + 1, .generation = generation};
        return CRUX_OK;
    }
    return CRUX_ERR_NO_RESOURCES;
}

crux_status_t crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    sync_native_deadline_t deadline;
    crux_status_t result = crux_sync_make_deadline(timeout_ms, &deadline);
    if (result != CRUX_OK) {
        return result;
    }
    mutex_slot_t *slot = mutex_slot(mutex);
    if (slot == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    result = admit(&slot->life, mutex.generation);
    if (result != CRUX_OK) {
        return result;
    }
    if (held_by_caller(slot)) {
        release(&slot->life);
        return CRUX_ERR_INVALID_STATE;
    }
    /* The reference pins this slot while the native wait runs. */
    result = crux_mutex_impl_lock(&slot->native, &deadline);
    if (result == CRUX_OK) {
        atomic_store_explicit(&slot->owner, crux_sync_current_identity(), memory_order_relaxed);
        (void)atomic_fetch_or_explicit(&slot->life.state, STATE_HELD, memory_order_release);
    }
    release(&slot->life);
    return result;
}

crux_status_t crux_mutex_unlock(crux_mutex_t mutex)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    mutex_slot_t *slot = mutex_slot(mutex);
    if (slot == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    crux_status_t result = admit(&slot->life, mutex.generation);
    if (result != CRUX_OK) {
        return result;
    }
    if (!held_by_caller(slot)) {
        release(&slot->life);
        return CRUX_ERR_INVALID_STATE;
    }
    /* A new owner can record itself as soon as the native unlock hands the mutex over, so the
     * flag is cleared before it and nothing of the slot is written after it but the release. */
    (void)atomic_fetch_and_explicit(&slot->life.state, ~STATE_HELD, memory_order_relaxed);
    result = crux_mutex_impl_unlock(&slot->native);
    if (result != CRUX_OK) {
        (void)atomic_fetch_or_explicit(&slot->life.state, STATE_HELD, memory_order_relaxed);
    }
    release(&slot->life);
    return result;
}

crux_status_t crux_mutex_destroy(crux_mutex_t *mutex)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (mutex == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    mutex_slot_t *slot = mutex_slot(*mutex);
    if (slot == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    crux_status_t result = admit(&slot->life, mutex->generation);
    if (result != CRUX_OK) {
        return result == CRUX_ERR_NO_RESOURCES ? CRUX_ERR_BUSY : result;
    }
    if (!close_admitted(&slot->life, mutex->generation)) {
        release(&slot->life);
        return CRUX_ERR_BUSY;
    }
    result = crux_mutex_impl_destroy(&slot->native);
    if (result != CRUX_OK) {
        publish(&slot->life, mutex->generation);
        return result;
    }
    end_lifetime(&slot->life);
    *mutex = (crux_mutex_t){0};
    return CRUX_OK;
}

crux_status_t crux_semaphore_create(uint32_t initial, uint32_t maximum, crux_semaphore_t *out)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (maximum == 0 || maximum > INT32_MAX || initial > maximum) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    for (uint32_t i = 0; i < CRUX_SYNC_SEMAPHORE_COUNT; ++i) {
        semaphore_slot_t *slot = &semaphores[i];
        if (!claim(&slot->life)) {
            continue;
        }
        uint32_t generation = next_generation(&slot->life);
        crux_status_t result = crux_semaphore_impl_create(&slot->native, initial, maximum);
        if (result != CRUX_OK) {
            end_lifetime(&slot->life);
            return result;
        }
        publish(&slot->life, generation);
        *out = (crux_semaphore_t){.slot = i + 1, .generation = generation};
        return CRUX_OK;
    }
    return CRUX_ERR_NO_RESOURCES;
}

crux_status_t crux_semaphore_take(crux_semaphore_t semaphore, uint32_t timeout_ms)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    sync_native_deadline_t deadline;
    crux_status_t result = crux_sync_make_deadline(timeout_ms, &deadline);
    if (result != CRUX_OK) {
        return result;
    }
    semaphore_slot_t *slot = semaphore_slot(semaphore);
    if (slot == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    result = admit(&slot->life, semaphore.generation);
    if (result != CRUX_OK) {
        return result;
    }
    /* The reference pins this slot while the native wait runs. */
    result = crux_semaphore_impl_take(&slot->native, &deadline);
    release(&slot->life);
    return result;
}

crux_status_t crux_semaphore_give(crux_semaphore_t semaphore)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    semaphore_slot_t *slot = semaphore_slot(semaphore);
    if (slot == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    crux_status_t result = admit(&slot->life, semaphore.generation);
    if (result != CRUX_OK) {
        return result;
    }
    result = crux_semaphore_impl_give(&slot->native);
    release(&slot->life);
    return result;
}

crux_status_t crux_semaphore_destroy(crux_semaphore_t *semaphore)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (semaphore == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    semaphore_slot_t *slot = semaphore_slot(*semaphore);
    if (slot == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    crux_status_t result = admit(&slot->life, semaphore->generation);
    if (result != CRUX_OK) {
        return result == CRUX_ERR_NO_RESOURCES ? CRUX_ERR_BUSY : result;
    }
    if (!close_admitted(&slot->life, semaphore->generation)) {
        release(&slot->life);
        return CRUX_ERR_BUSY;
    }
    result = crux_semaphore_impl_destroy(&slot->native);
    if (result != CRUX_OK) {
        publish(&slot->life, semaphore->generation);
        return result;
    }
    end_lifetime(&slot->life);
    *semaphore = (crux_semaphore_t){0};
    return CRUX_OK;
}
