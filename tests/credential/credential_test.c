#include "credential_test.h"

#include <stdlib.h>

unsigned credential_test_checks, credential_test_failures;
char credential_test_context[96];

static uint8_t io_buffer[CRUX_CREDENTIAL_SLOT_BYTES * 2u];
static uint8_t other_buffer[CRUX_CREDENTIAL_SLOT_BYTES * 2u];
static uint8_t payload_buffer[CRUX_CREDENTIAL_PAYLOAD_BYTES];
static uint8_t expect_buffer[CRUX_CREDENTIAL_SLOT_BYTES];

int credential_test_finish(const char *suite)
{
    printf("%s: %u checks, %u failures\n", suite, credential_test_checks, credential_test_failures);
    return credential_test_failures == 0 ? 0 : 1;
}

static crux_file_root_t slot_root(size_t slot)
{
    return slot == TEST_SLOT_A ? TEST_ROOT_A : TEST_ROOT_B;
}

crux_credential_result_t test_boot(void)
{
    crux_credential_test_reset();
    return crux_credential_start(TEST_ROOT_A, TEST_ROOT_B);
}

void test_fresh(void)
{
    credential_test_context[0] = '\0';
    credential_fake_reset();
    credential_medium_blank();
    crux_credential_test_reset();
}

void test_fresh_empty(void)
{
    test_fresh();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
}

void test_image_seal(uint8_t image[CRUX_CREDENTIAL_SLOT_BYTES], uint64_t generation,
                     uint16_t format, uint16_t protection, const uint8_t *payload,
                     size_t payload_length)
{
    crux_credential_header_t header = {
        .format = format,
        .protection = protection,
        .generation = generation,
        .payload_length = (uint32_t)payload_length,
    };
    if (crux_credential_port_random(header.nonce, sizeof(header.nonce)) != CRUX_CREDENTIAL_OK) {
        abort();
    }
    memset(image, 0, CRUX_CREDENTIAL_SLOT_BYTES);
    crux_credential_header_encode(&header, image);
    uint8_t *sealed = image + CRUX_CREDENTIAL_HEADER_BYTES;
    if (crux_credential_port_seal(image, CRUX_CREDENTIAL_HEADER_BYTES, payload, payload_length,
                                  sealed, header.nonce,
                                  sealed + payload_length) != CRUX_CREDENTIAL_OK) {
        abort();
    }
}

void test_image_build(uint8_t image[CRUX_CREDENTIAL_SLOT_BYTES], uint64_t generation,
                      const test_record_t *records, size_t count)
{
    size_t length = crux_credential_payload_empty(payload_buffer);
    for (size_t index = 0; index < count; ++index) {
        if (!crux_credential_payload_set(records[index].record, records[index].data,
                                         records[index].length, payload_buffer, &length,
                                         sizeof(payload_buffer))) {
            abort();
        }
    }
    test_image_seal(image, generation, CRUX_CREDENTIAL_FORMAT, (uint16_t)CRUX_CREDENTIAL_BASIS_NONE,
                    payload_buffer, length);
}

bool test_slot_exists(size_t slot)
{
    crux_file_info_t info;
    return crux_file_stat(slot_root(slot), TEST_SLOT_NAME, &info) == CRUX_FILE_OK;
}

void test_slot_remove(size_t slot)
{
    crux_file_result_t result = crux_file_remove_at(slot_root(slot), TEST_SLOT_NAME);
    CHECK(result == CRUX_FILE_OK || result == CRUX_FILE_ERR_NOT_FOUND);
}

void test_slot_write(size_t slot, const void *data, size_t length)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    CHECK(crux_file_open_at(slot_root(slot), TEST_SLOT_NAME, CRUX_FILE_CREATE_REPLACE, &file) ==
          CRUX_FILE_OK);
    if (length > 0) {
        CHECK(crux_file_write(file, data, length, &count) == CRUX_FILE_OK);
    }
    CHECK(crux_file_sync(file) == CRUX_FILE_OK);
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);
}

size_t test_slot_read(size_t slot, void *data, size_t capacity)
{
    crux_file_t *file = NULL;
    size_t total = 0;
    if (crux_file_open_at(slot_root(slot), TEST_SLOT_NAME, CRUX_FILE_READ, &file) != CRUX_FILE_OK) {
        return 0;
    }
    while (total < capacity) {
        size_t count = 0;
        CHECK(crux_file_read(file, (uint8_t *)data + total, capacity - total, &count) ==
              CRUX_FILE_OK);
        if (count == 0) {
            break;
        }
        total += count;
    }
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);
    return total;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): every helper takes the slot first.
void test_slot_flip(size_t slot, size_t offset, uint8_t mask)
{
    size_t length = test_slot_read(slot, io_buffer, sizeof(io_buffer));
    CHECK(offset < length);
    io_buffer[offset] ^= mask;
    test_slot_write(slot, io_buffer, length);
}

bool test_slot_generation(size_t slot, uint64_t *generation)
{
    size_t length = test_slot_read(slot, io_buffer, CRUX_CREDENTIAL_HEADER_BYTES);
    crux_credential_header_t header;
    if (length != CRUX_CREDENTIAL_HEADER_BYTES ||
        crux_credential_header_decode(io_buffer, (uint16_t)CRUX_CREDENTIAL_BASIS_NONE,
                                      CRUX_CREDENTIAL_SLOT_BYTES,
                                      &header) != CRUX_CREDENTIAL_HEADER_VALID) {
        return false;
    }
    *generation = header.generation;
    return true;
}

void test_slot_swap(void)
{
    size_t length_a = test_slot_read(TEST_SLOT_A, io_buffer, sizeof(io_buffer));
    size_t length_b = test_slot_read(TEST_SLOT_B, other_buffer, sizeof(other_buffer));
    test_slot_write(TEST_SLOT_A, other_buffer, length_b);
    test_slot_write(TEST_SLOT_B, io_buffer, length_a);
}

void test_expect_record(crux_credential_record_t record, const void *data, size_t length)
{
    size_t read_length = 0;
    CHECK(length <= sizeof(expect_buffer));
    CHECK(crux_credential_read(record, expect_buffer, sizeof(expect_buffer), &read_length) ==
          CRUX_CREDENTIAL_OK);
    CHECK(read_length == length);
    CHECK(read_length == length && memcmp(expect_buffer, data, length) == 0);
}

size_t test_key_record(crux_credential_key_type_t type, uint8_t *record, uint8_t fill)
{
    size_t public_length = type == CRUX_CREDENTIAL_KEY_ECDSA_P256 ? 65u : 32u;
    record[0] = (uint8_t)type;
    record[1] = (uint8_t)public_length;
    record[2] = 32u;
    record[3] = 0;
    memset(record + CRUX_CREDENTIAL_KEY_HEADER_BYTES, fill, public_length + 32u);
    return CRUX_CREDENTIAL_KEY_HEADER_BYTES + public_length + 32u;
}
