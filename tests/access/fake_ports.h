/* Fake Access ports with fault injection and a controllable clock. This is a test composition: a
 * fake verifier must never reach a product image. */
#ifndef CRUX_ACCESS_FAKE_PORTS_H
#define CRUX_ACCESS_FAKE_PORTS_H

#include "access_port.h"

typedef void (*access_fake_hook_t)(void *context);

typedef enum {
    ACCESS_FAKE_SAVE_OK,
    ACCESS_FAKE_SAVE_FAIL,
    /* A power loss during the save: the call reports failure, and the test must power cycle next.
     * BEFORE leaves the previous record readable, and AFTER leaves the new one. */
    ACCESS_FAKE_SAVE_POWER_LOSS_BEFORE,
    ACCESS_FAKE_SAVE_POWER_LOSS_AFTER
} access_fake_save_mode_t;

/* Restores every fake to its power-on default, including the medium. */
void access_fake_reset(void);

void access_fake_clock_set(uint64_t now_ms);
void access_fake_clock_advance(uint64_t delta_ms);

void access_fake_random_fail(bool fail);
void access_fake_hmac_fail(bool fail);
/* Makes every hash equal, so that all names and all sources share one bucket. */
void access_fake_hmac_fixed(bool fixed);

void access_fake_derive_fail(bool fail);
/* Runs once inside the next derivation, which proves that no lock is held across it. */
void access_fake_derive_hook(access_fake_hook_t hook, void *context);
unsigned access_fake_derive_count(void);
uint64_t access_fake_derive_iterations(void);
void access_fake_derive_counters_clear(void);

void access_fake_store_state(crux_access_store_state_t state);
void access_fake_store_set_record(const uint8_t *record, size_t length);
const uint8_t *access_fake_store_record(size_t *length);
void access_fake_store_save_mode(access_fake_save_mode_t mode);
/* Runs once inside the next save, under the change lock and before the record is committed. */
void access_fake_store_save_hook(access_fake_hook_t hook, void *context);
unsigned access_fake_store_saves(void);

size_t access_fake_event_count(void);
const crux_access_event_t *access_fake_event(size_t index);
size_t access_fake_event_count_of(crux_access_event_type_t type);
void access_fake_events_clear(void);

#endif
