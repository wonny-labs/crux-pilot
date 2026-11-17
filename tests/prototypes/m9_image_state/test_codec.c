/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * The attempt record codec (a prototype choice, P1) and the power loss of the record itself: the
 * record truncated, or corrupted, at byte N. Whatever the codec rejects, the update agent has no
 * attempt, and crux_image_outcome() then says NONE (case 1 of the outcome table).
 */
#include "attempt_codec.h"
#include "harness.h"
#include "image_state_proto.h"

#include <stddef.h>
#include <string.h>

static const crux_image_attempt_t sample = {1, {2, 3, 0x0405, 0x01020304u}, true};

static bool same_attempt(const crux_image_attempt_t *a, const crux_image_attempt_t *b)
{
    return a->slot == b->slot && a->version.major == b->version.major &&
           a->version.minor == b->version.minor && a->version.revision == b->version.revision &&
           a->version.build == b->version.build && a->requested == b->requested;
}

static void test_crc(void)
{
    CASE("P1 CRC-32 of the standard check string");
    CHECK(proto_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
    CASE("P1 CRC-32 of nothing");
    CHECK(proto_crc32((const uint8_t *)"", 0) == 0);
}

static void test_round_trip(void)
{
    static const crux_image_attempt_t attempts[] = {
        {0, {0, 0, 0, 0}, false},
        {1, {255, 255, 65535, 0xFFFFFFFFu}, true},
        {0, {1, 2, 3, 4}, true},
        {1, {1, 2, 3, 4}, false},
    };
    for (size_t i = 0; i < sizeof(attempts) / sizeof(attempts[0]); i++) {
        CASE("P1 round trip");
        uint8_t buffer[PROTO_ATTEMPT_SIZE];
        crux_image_attempt_t decoded = {9, {9, 9, 9, 9}, false};
        CHECK_INT(PROTO_CODEC_OK, proto_attempt_encode(&attempts[i], buffer, sizeof(buffer)));
        CHECK_INT(PROTO_CODEC_OK, proto_attempt_decode(buffer, sizeof(buffer), &decoded));
        CHECK(same_attempt(&attempts[i], &decoded));
    }

    CASE("P1 layout is fixed: little endian, 16 bytes");
    uint8_t buffer[PROTO_ATTEMPT_SIZE];
    CHECK_INT(PROTO_CODEC_OK, proto_attempt_encode(&sample, buffer, sizeof(buffer)));
    static const uint8_t expected_head[12] = {1, 1, 2, 3, 0x05, 0x04, 0x04, 0x03, 0x02, 0x01, 1, 0};
    CHECK(memcmp(buffer, expected_head, sizeof(expected_head)) == 0);
    uint32_t crc = proto_crc32(buffer, 12);
    CHECK_INT(crc & 0xFFu, buffer[12]);
    CHECK_INT((crc >> 8) & 0xFFu, buffer[13]);
    CHECK_INT((crc >> 16) & 0xFFu, buffer[14]);
    CHECK_INT((crc >> 24) & 0xFFu, buffer[15]);
}

static void test_arguments(void)
{
    uint8_t buffer[PROTO_ATTEMPT_SIZE];
    crux_image_attempt_t decoded;
    CASE("P1 encode arguments");
    CHECK_INT(PROTO_CODEC_ERR_ARGUMENT, proto_attempt_encode(NULL, buffer, sizeof(buffer)));
    CHECK_INT(PROTO_CODEC_ERR_ARGUMENT, proto_attempt_encode(&sample, NULL, sizeof(buffer)));
    CHECK_INT(PROTO_CODEC_ERR_SIZE, proto_attempt_encode(&sample, buffer, sizeof(buffer) - 1));
    CASE("P1 encode of a slot that does not exist");
    crux_image_attempt_t bad_slot = {2, {1, 0, 0, 0}, false};
    CHECK_INT(PROTO_CODEC_ERR_FIELD, proto_attempt_encode(&bad_slot, buffer, sizeof(buffer)));
    CASE("P1 decode arguments");
    CHECK_INT(PROTO_CODEC_OK, proto_attempt_encode(&sample, buffer, sizeof(buffer)));
    CHECK_INT(PROTO_CODEC_ERR_ARGUMENT, proto_attempt_decode(NULL, sizeof(buffer), &decoded));
    CHECK_INT(PROTO_CODEC_ERR_ARGUMENT, proto_attempt_decode(buffer, sizeof(buffer), NULL));
    CHECK_INT(PROTO_CODEC_ERR_SIZE, proto_attempt_decode(buffer, sizeof(buffer) + 1, &decoded));
}

/* A record whose fields are wrong but whose CRC is right, as a defective writer would produce. */
static void reseal(uint8_t *buffer)
{
    uint32_t crc = proto_crc32(buffer, 12);
    buffer[12] = (uint8_t)(crc & 0xFFu);
    buffer[13] = (uint8_t)((crc >> 8) & 0xFFu);
    buffer[14] = (uint8_t)((crc >> 16) & 0xFFu);
    buffer[15] = (uint8_t)((crc >> 24) & 0xFFu);
}

static void test_field_rules(void)
{
    static const struct {
        const char *name;
        size_t offset;
        uint8_t value;
        proto_codec_result_t expected;
    } rows[] = {
        {"format 0", 0, 0, PROTO_CODEC_ERR_FORMAT},
        {"format 2", 0, 2, PROTO_CODEC_ERR_FORMAT},
        {"slot 2", 1, 2, PROTO_CODEC_ERR_FIELD},
        {"slot 255", 1, 255, PROTO_CODEC_ERR_FIELD},
        {"requested 2", 10, 2, PROTO_CODEC_ERR_FIELD},
        {"reserved not zero", 11, 1, PROTO_CODEC_ERR_FIELD},
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        CASE(rows[i].name);
        uint8_t buffer[PROTO_ATTEMPT_SIZE];
        crux_image_attempt_t decoded;
        CHECK_INT(PROTO_CODEC_OK, proto_attempt_encode(&sample, buffer, sizeof(buffer)));
        buffer[rows[i].offset] = rows[i].value;
        reseal(buffer);
        CHECK_INT(rows[i].expected, proto_attempt_decode(buffer, sizeof(buffer), &decoded));
    }
}

static void test_truncation(void)
{
    uint8_t good[PROTO_ATTEMPT_SIZE];
    CHECK_INT(PROTO_CODEC_OK, proto_attempt_encode(&sample, good, sizeof(good)));

    /* The record cut at byte N, for each N, as a shorter stored length. */
    for (size_t n = 0; n < PROTO_ATTEMPT_SIZE; n++) {
        CASE("P1 truncated at byte N, shorter length");
        crux_image_attempt_t decoded;
        CHECK_INT(PROTO_CODEC_ERR_SIZE, proto_attempt_decode(good, n, &decoded));
    }

    /* The record cut at byte N on a medium that reads erased flash (0xFF) after the cut. */
    for (size_t n = 0; n < PROTO_ATTEMPT_SIZE; n++) {
        CASE("P1 truncated at byte N, erased tail");
        uint8_t torn[PROTO_ATTEMPT_SIZE];
        crux_image_attempt_t decoded;
        memset(torn, 0xFF, sizeof(torn));
        memcpy(torn, good, n);
        if (memcmp(torn, good, sizeof(torn)) == 0) {
            continue; /* the erased tail equals the real tail: nothing was lost */
        }
        CHECK(proto_attempt_decode(torn, sizeof(torn), &decoded) != PROTO_CODEC_OK);
    }

    /* The same with a zero tail, which a controller that clears the page first would read. */
    for (size_t n = 0; n < PROTO_ATTEMPT_SIZE; n++) {
        CASE("P1 truncated at byte N, zero tail");
        uint8_t torn[PROTO_ATTEMPT_SIZE];
        crux_image_attempt_t decoded;
        memset(torn, 0x00, sizeof(torn));
        memcpy(torn, good, n);
        if (memcmp(torn, good, sizeof(torn)) == 0) {
            continue; /* the zero tail equals the real tail: nothing was lost */
        }
        CHECK(proto_attempt_decode(torn, sizeof(torn), &decoded) != PROTO_CODEC_OK);
    }

    CASE("P1 the whole record, cut at its end, still decodes");
    crux_image_attempt_t decoded;
    CHECK_INT(PROTO_CODEC_OK, proto_attempt_decode(good, sizeof(good), &decoded));
}

static void test_corruption(void)
{
    uint8_t good[PROTO_ATTEMPT_SIZE];
    CHECK_INT(PROTO_CODEC_OK, proto_attempt_encode(&sample, good, sizeof(good)));
    static const uint8_t flips[] = {0x01, 0x10, 0x80, 0xFF};

    /* Four XOR patterns at each byte are rejected. CRC-32 catches every single-byte error. */
    for (size_t n = 0; n < PROTO_ATTEMPT_SIZE; n++) {
        for (size_t f = 0; f < sizeof(flips); f++) {
            CASE("P1 corrupted at byte N");
            uint8_t bad[PROTO_ATTEMPT_SIZE];
            crux_image_attempt_t decoded;
            memcpy(bad, good, sizeof(bad));
            bad[n] ^= flips[f];
            CHECK(proto_attempt_decode(bad, sizeof(bad), &decoded) != PROTO_CODEC_OK);
        }
    }
}

static void test_torn_record_means_no_attempt(void)
{
    /*
     * The outcome of an attempt that the provider would report as REVERTED, and what the agent gets
     * when the record of that attempt is cut at byte N: no attempt, hence NONE (P1).
     */
    crux_image_boot_record_t record = {0};
    record.running_slot = 0;
    record.running_confirmed = true;
    record.running_version = (crux_image_version_t){1, 0, 0, 0};
    record.inactive_slot = 1;
    record.slot_state_at_start[0] = CRUX_IMAGE_SLOT_RUNNING_CONFIRMED;
    record.slot_state_at_start[1] = CRUX_IMAGE_SLOT_EMPTY;
    crux_image_attempt_t attempt = {1, {1, 1, 0, 0}, true};
    uint8_t good[PROTO_ATTEMPT_SIZE];
    CHECK_INT(PROTO_CODEC_OK, proto_attempt_encode(&attempt, good, sizeof(good)));

    CASE("P1 intact record gives REVERTED");
    crux_image_attempt_t decoded;
    CHECK_INT(PROTO_CODEC_OK, proto_attempt_decode(good, sizeof(good), &decoded));
    CHECK_INT(CRUX_IMAGE_OUTCOME_REVERTED, crux_image_outcome(&record, &decoded));

    for (size_t n = 0; n < PROTO_ATTEMPT_SIZE; n++) {
        CASE("P1 record cut at byte N gives no attempt and the outcome NONE");
        uint8_t torn[PROTO_ATTEMPT_SIZE];
        memset(torn, 0xFF, sizeof(torn));
        memcpy(torn, good, n);
        const crux_image_attempt_t *usable =
            proto_attempt_decode(torn, sizeof(torn), &decoded) == PROTO_CODEC_OK ? &decoded : NULL;
        CHECK(usable == NULL);
        CHECK_INT(CRUX_IMAGE_OUTCOME_NONE, crux_image_outcome(&record, usable));
    }
}

void run_codec_tests(void)
{
    test_crc();
    test_round_trip();
    test_arguments();
    test_field_rules();
    test_truncation();
    test_corruption();
    test_torn_record_means_no_attempt();
}
