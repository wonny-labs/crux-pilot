/* Shared helpers for the credential store suites. */
#ifndef CRUX_CREDENTIAL_TEST_H
#define CRUX_CREDENTIAL_TEST_H

#include "credential_internal.h"
#include "fake_ports.h"

#include <stdio.h>
#include <string.h>

extern unsigned credential_test_checks, credential_test_failures;
/* Names the case under way, such as a cut point, on a failure line. */
extern char credential_test_context[96];

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        ++credential_test_checks;                                                                  \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, credential_test_context,           \
                   #expression);                                                                   \
            ++credential_test_failures;                                                            \
        }                                                                                          \
    } while (0)

/* Reports the totals, and is the suite's exit status. */
int credential_test_finish(const char *suite);

/* Available only in the instrumented core built for tests with CRUX_CREDENTIAL_TESTING. */
void crux_credential_test_reset(void);
bool crux_credential_test_buffers_clear(void);

enum {
    TEST_ROOT_A = 1,
    TEST_ROOT_B = 2,
    TEST_ROOT_UNBOUND = 7,
};

#define TEST_SLOT_NAME "store.img"
#define TEST_SLOT_A 0u
#define TEST_SLOT_B 1u

/* Removes both slot files and clears any fault. Each provider's main supplies this. */
void credential_medium_blank(void);

/* A reset: RAM is lost and the store starts again on the same medium. */
crux_credential_result_t test_boot(void);
/* A blank medium, default fakes and a store that has not started. */
void test_fresh(void);
/* test_fresh() and a boot that must give EMPTY. */
void test_fresh_empty(void);

typedef struct {
    crux_credential_record_t record;
    const void *data;
    size_t length;
} test_record_t;

/* Builds a full-size slot image as the store would seal it. */
void test_image_build(uint8_t image[CRUX_CREDENTIAL_SLOT_BYTES], uint64_t generation,
                      const test_record_t *records, size_t count);
/* The same with chosen header fields and a payload that need not be well formed. */
void test_image_seal(uint8_t image[CRUX_CREDENTIAL_SLOT_BYTES], uint64_t generation,
                     uint16_t format, uint16_t protection, const uint8_t *payload,
                     size_t payload_length);

/* The slot files, through File alone, so that they work on every provider. */
bool test_slot_exists(size_t slot);
void test_slot_remove(size_t slot);
void test_slot_write(size_t slot, const void *data, size_t length);
/* Returns the bytes read, up to capacity. */
size_t test_slot_read(size_t slot, void *data, size_t capacity);
void test_slot_flip(size_t slot, size_t offset, uint8_t mask);
/* Decodes a slot's generation, or returns false if its header is not valid. */
bool test_slot_generation(size_t slot, uint64_t *generation);
void test_slot_swap(void);

/* The record's bytes must be exactly these. */
void test_expect_record(crux_credential_record_t record, const void *data, size_t length);
/* A key record of the given type, as the store's own generator would lay one out. */
size_t test_key_record(crux_credential_key_type_t type, uint8_t *record, uint8_t fill);

#endif
