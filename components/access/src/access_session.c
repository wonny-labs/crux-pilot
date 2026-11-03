#include "access_internal.h"

#include <string.h>

/* Elapsed-time tests use subtraction, so a clock near its 64-bit limit cannot overflow them, and a
 * clock that ever ran backwards reads as expired. */
static bool slot_expired(const access_slot_t *slot, uint64_t now_ms)
{
    const crux_access_policy_t *policy = crux_access_state.policy;
    return now_ms - slot->activity_ms >= (uint64_t)policy->session_idle_limit_s * 1000u ||
           now_ms - slot->created_ms >= (uint64_t)policy->session_absolute_limit_s * 1000u;
}

static void note_expired(const access_slot_t *slot, access_expired_t *expired)
{
    if (expired->count < ACCESS_EXPIRED_EVENTS_MAX) {
        expired->origins[expired->count] = slot->origin;
        crux_access_copy_name(expired->users[expired->count], slot->user);
        expired->count += 1;
    }
}

void crux_access_slot_release(size_t slot_index)
{
    access_slot_t *slot = &crux_access_state.slots[slot_index];
    uint32_t generation = (slot->generation + 1u) & ACCESS_GENERATION_MASK;
    if (slot_index == 0 && generation == 0) {
        generation = 1;
    }
    memset(slot, 0, sizeof(*slot));
    slot->state = ACCESS_SLOT_FREE;
    slot->generation = generation;
}

static void slots_reclaim_expired(uint64_t now_ms, access_expired_t *expired)
{
    for (size_t index = 0; index < CRUX_ACCESS_SESSION_MAX; ++index) {
        access_slot_t *slot = &crux_access_state.slots[index];
        if (slot->state == ACCESS_SLOT_ACTIVE && slot_expired(slot, now_ms)) {
            note_expired(slot, expired);
            crux_access_slot_release(index);
        }
    }
}

static bool slot_claim_free(crux_access_channel_t channel, size_t *slot_index)
{
    /* Slot 0 is reserved: network channels start after it, and serial tries it first. */
    size_t first = channel == CRUX_ACCESS_CHANNEL_SERIAL ? 0u : 1u;
    for (size_t index = first; index < CRUX_ACCESS_SESSION_MAX; ++index) {
        if (crux_access_state.slots[index].state == ACCESS_SLOT_FREE) {
            crux_access_state.slots[index].state = ACCESS_SLOT_RESERVED;
            *slot_index = index;
            return true;
        }
    }
    return false;
}

bool crux_access_slot_reserve(const crux_access_origin_t *origin, uint64_t now_ms,
                              size_t *slot_index, access_expired_t *expired)
{
    if (slot_claim_free(origin->channel, slot_index)) {
        return true;
    }
    slots_reclaim_expired(now_ms, expired);
    return slot_claim_free(origin->channel, slot_index);
}

void crux_access_slot_cancel(size_t slot_index)
{
    crux_access_state.slots[slot_index].state = ACCESS_SLOT_FREE;
}

static void account_cap_enforce(const char *user)
{
    size_t count = 0;
    size_t oldest = 0;
    uint64_t oldest_sequence = UINT64_MAX;
    for (size_t index = 0; index < CRUX_ACCESS_SESSION_MAX; ++index) {
        const access_slot_t *slot = &crux_access_state.slots[index];
        if (slot->state == ACCESS_SLOT_ACTIVE && strcmp(slot->user, user) == 0) {
            count += 1;
            if (slot->sequence < oldest_sequence) {
                oldest_sequence = slot->sequence;
                oldest = index;
            }
        }
    }
    if (count >= CRUX_ACCESS_SESSIONS_PER_ACCOUNT) {
        crux_access_slot_release(oldest);
    }
}

crux_access_session_t crux_access_slot_activate(size_t slot_index, const char *user,
                                                size_t role_index, bool must_change_password,
                                                const crux_access_origin_t *origin, uint64_t now_ms)
{
    account_cap_enforce(user);
    access_slot_t *slot = &crux_access_state.slots[slot_index];
    crux_access_state.next_session_sequence += 1;
    slot->state = ACCESS_SLOT_ACTIVE;
    crux_access_copy_name(slot->user, user);
    slot->role_index = role_index;
    slot->must_change_password = must_change_password;
    slot->origin = *origin;
    slot->created_ms = now_ms;
    slot->activity_ms = now_ms;
    slot->sequence = crux_access_state.next_session_sequence;
    crux_access_session_t session = {((uint32_t)slot_index << ACCESS_SLOT_SHIFT) |
                                     slot->generation};
    return session;
}

static access_slot_t *slot_lookup(crux_access_session_t session)
{
    size_t slot_index = session.value >> ACCESS_SLOT_SHIFT;
    uint32_t generation = session.value & ACCESS_GENERATION_MASK;
    if (session.value == 0 || slot_index >= CRUX_ACCESS_SESSION_MAX) {
        return NULL;
    }
    access_slot_t *slot = &crux_access_state.slots[slot_index];
    if (slot->state != ACCESS_SLOT_ACTIVE || slot->generation != generation) {
        return NULL;
    }
    return slot;
}

access_slot_t *crux_access_slot_find(crux_access_session_t session, uint64_t now_ms,
                                     access_expired_t *expired)
{
    access_slot_t *slot = slot_lookup(session);
    if (slot != NULL && slot_expired(slot, now_ms)) {
        note_expired(slot, expired);
        crux_access_slot_release(session.value >> ACCESS_SLOT_SHIFT);
        return NULL;
    }
    return slot;
}

const access_slot_t *crux_access_slot_peek(crux_access_session_t session, uint64_t now_ms)
{
    const access_slot_t *slot = slot_lookup(session);
    return slot != NULL && !slot_expired(slot, now_ms) ? slot : NULL;
}

void crux_access_slots_end_account(const char *user, uint32_t keep_value)
{
    for (size_t index = 0; index < CRUX_ACCESS_SESSION_MAX; ++index) {
        const access_slot_t *slot = &crux_access_state.slots[index];
        uint32_t value = ((uint32_t)index << ACCESS_SLOT_SHIFT) | slot->generation;
        if (slot->state == ACCESS_SLOT_ACTIVE && strcmp(slot->user, user) == 0 &&
            value != keep_value) {
            crux_access_slot_release(index);
        }
    }
}

void crux_access_expired_emit(const access_expired_t *expired)
{
    for (size_t index = 0; index < expired->count; ++index) {
        crux_access_emit_session(CRUX_ACCESS_EVENT_SESSION_EXPIRED, CRUX_ACCESS_ERR_SESSION_INVALID,
                                 &expired->origins[index], expired->users[index], NULL, NULL);
    }
}

void crux_access_session_report(crux_access_result_t result, const access_expired_t *expired)
{
    crux_access_expired_emit(expired);
    if (result == CRUX_ACCESS_ERR_SESSION_INVALID && expired->count == 0) {
        crux_access_emit(CRUX_ACCESS_EVENT_SESSION_INVALID, result, NULL, NULL, false);
    }
}
