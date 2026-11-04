/*
 * Fake credential ports with fault injection. TEST ONLY: the CRC stands in for a tag, the keystream
 * is not encryption, and the random source is deterministic. A real provider seals with AES-256-GCM
 * through PSA, which waits on Issue #142. The fake refuses to compile outside a test composition,
 * and a product build of the store refuses its declaration at start.
 */
#ifndef CRUX_CREDENTIAL_FAKE_PORTS_H
#define CRUX_CREDENTIAL_FAKE_PORTS_H

#include "credential_port.h"

/* Restores every fake to its power-on default. */
void credential_fake_reset(void);

/* Stands for the key derived from the device ID. Another value is another board. */
void credential_fake_device_key(uint32_t key);

/* What the provider declares. NULL means that it cannot run. */
void credential_fake_declare(const crux_credential_store_info_t *declaration);
/* The fake's own declaration: BASIS_NONE, EVIDENCE_HOST_FAKE, and every claim false. */
const crux_credential_store_info_t *credential_fake_default_declaration(void);

/* The call that comes after skip more calls fails, once. */
void credential_fake_random_fail(unsigned skip);
void credential_fake_seal_fail(unsigned skip, crux_credential_result_t result);
void credential_fake_open_fail(unsigned skip, crux_credential_result_t result);

void credential_fake_keygen_fail(bool fail);
/* The generator reports lengths that do not suit the type. */
void credential_fake_keygen_bad_lengths(bool bad);
unsigned credential_fake_keygen_count(void);

/* Every nonce sealed with since the reset, up to a limit. */
size_t credential_fake_nonce_count(void);
const uint8_t *credential_fake_nonce(size_t index);

size_t credential_fake_event_count(void);
const crux_credential_event_t *credential_fake_event(size_t index);
size_t credential_fake_event_count_of(crux_credential_event_type_t type);
/* How many slot_invalid events named this slot, 'a' or 'b'. */
size_t credential_fake_slot_invalid_count(char slot);
void credential_fake_events_clear(void);
/* Runs inside every event until cleared with NULL, which shows what an event can call. */
void credential_fake_event_hook(void (*hook)(const crux_credential_event_t *event));

#endif
