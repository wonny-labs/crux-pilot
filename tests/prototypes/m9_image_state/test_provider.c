/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * Provider conformance on the Host double: S1 to S12 (slot states), B1 to B4 (start), R1 to R8
 * (test request), C1 to C3 (confirm), and the argument and busy rules of the Behaviour section.
 */
#include "harness.h"
#include "image_state_fake.h"
#include "image_state_proto.h"

#include <stddef.h>

#define MAGIC_GOOD PROTO_MAGIC_GOOD
#define SET PROTO_FLAG_SET
#define UNSET PROTO_FLAG_UNSET
#define INVALID_FLAG PROTO_FLAG_INVALID

static proto_slot_view_t view(bool header_valid, crux_image_version_t version, proto_magic_t magic,
                              proto_flag_t image_ok, proto_flag_t copy_done)
{
    proto_slot_view_t v = {header_valid, version, magic, image_ok, copy_done};
    return v;
}

static const crux_image_version_t V100 = {1, 0, 0, 0};
static const crux_image_version_t V110 = {1, 1, 0, 0};

static crux_image_slot_state_t state_of(crux_image_slot_t slot)
{
    crux_image_slot_info_t info = {CRUX_IMAGE_SLOT_INVALID, {0, 0, 0, 0}};
    CHECK_INT(CRUX_IMAGE_OK, crux_image_slot_get(slot, &info));
    return info.state;
}

/* The running slot holds a confirmed 1.0.0 image, the other slot holds `other`. */
static void arrange(crux_image_slot_t running, const proto_slot_view_t *other)
{
    fake_init();
    proto_slot_view_t confirmed = view(true, V100, MAGIC_GOOD, SET, SET);
    proto_slot_view_t empty =
        view(false, (crux_image_version_t){0, 0, 0, 0}, PROTO_MAGIC_UNSET, UNSET, UNSET);
    fake_slot_put(running, &confirmed);
    fake_slot_put((crux_image_slot_t)(1u - running), other != NULL ? other : &empty);
    proto_shared_data_t shared = {true, running, PROTO_MODE_RAM_LOAD, true, {2, 4, 0, 0}};
    fake_shared_put(&shared);
    proto_provider_reset();
}

typedef struct {
    const char *name;
    proto_slot_view_t slot;
    crux_image_slot_state_t expected;
} state_row_t;

static void test_slot_states_not_running(void)
{
    /*
     * The slot that is not running: the non-R rows of the proposal's slot-state table. That table
     * has 11 rows and the proposal says 12 cases, so the numbering is the prototype's: S1 to S6
     * are the rows, S11 is magic bad and S12 is a flag outside set and unset (the two causes of
     * the table's one INVALID row). S7 to S10 are the running-slot rows and follow below.
     */
    static const state_row_t rows[] = {
        {"S1 header invalid, trailer good",
         {false, {0, 0, 0, 0}, MAGIC_GOOD, SET, SET},
         CRUX_IMAGE_SLOT_EMPTY},
        {"S1 header invalid, trailer unset",
         {false, {0, 0, 0, 0}, PROTO_MAGIC_UNSET, UNSET, UNSET},
         CRUX_IMAGE_SLOT_EMPTY},
        {"S2 staged: magic unset",
         {true, {1, 1, 0, 0}, PROTO_MAGIC_UNSET, UNSET, UNSET},
         CRUX_IMAGE_SLOT_STAGED},
        {"S3 pending test",
         {true, {1, 1, 0, 0}, MAGIC_GOOD, UNSET, UNSET},
         CRUX_IMAGE_SLOT_PENDING_TEST},
        {"S4 pending permanent",
         {true, {1, 1, 0, 0}, MAGIC_GOOD, SET, UNSET},
         CRUX_IMAGE_SLOT_PENDING_PERMANENT},
        {"S5 tried", {true, {1, 1, 0, 0}, MAGIC_GOOD, UNSET, SET}, CRUX_IMAGE_SLOT_TRIED},
        {"S6 standby confirmed",
         {true, {1, 1, 0, 0}, MAGIC_GOOD, SET, SET},
         CRUX_IMAGE_SLOT_STANDBY_CONFIRMED},
        {"S11 magic bad",
         {true, {1, 1, 0, 0}, PROTO_MAGIC_BAD, UNSET, UNSET},
         CRUX_IMAGE_SLOT_INVALID},
        {"S12 image_ok outside set and unset",
         {true, {1, 1, 0, 0}, MAGIC_GOOD, INVALID_FLAG, UNSET},
         CRUX_IMAGE_SLOT_INVALID},
        {"S12 copy_done outside set and unset",
         {true, {1, 1, 0, 0}, MAGIC_GOOD, UNSET, INVALID_FLAG},
         CRUX_IMAGE_SLOT_INVALID},
        {"S12 both flags outside",
         {true, {1, 1, 0, 0}, MAGIC_GOOD, INVALID_FLAG, INVALID_FLAG},
         CRUX_IMAGE_SLOT_INVALID},
        /* Prototype choices, P3: the table is silent. */
        {"P3 magic unset with a flag outside",
         {true, {1, 1, 0, 0}, PROTO_MAGIC_UNSET, INVALID_FLAG, UNSET},
         CRUX_IMAGE_SLOT_INVALID},
        /* image_ok set with magic unset: a request would make it PENDING_PERMANENT, so INVALID. */
        {"P3 magic unset with image_ok set",
         {true, {1, 1, 0, 0}, PROTO_MAGIC_UNSET, SET, UNSET},
         CRUX_IMAGE_SLOT_INVALID},
        {"P3 magic unset with image_ok and copy_done set",
         {true, {1, 1, 0, 0}, PROTO_MAGIC_UNSET, SET, SET},
         CRUX_IMAGE_SLOT_INVALID},
        /* copy_done set with magic unset: a request would make it TRIED, erased at the reset. */
        {"P3 magic unset with copy_done set only",
         {true, {1, 1, 0, 0}, PROTO_MAGIC_UNSET, UNSET, SET},
         CRUX_IMAGE_SLOT_INVALID},
    };
    for (crux_image_slot_t running = 0; running < CRUX_IMAGE_SLOT_COUNT; running++) {
        crux_image_slot_t other = (crux_image_slot_t)(1u - running);
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
            CASE(rows[i].name);
            arrange(running, &rows[i].slot);
            CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
            CHECK_INT(rows[i].expected, state_of(other));
            crux_image_boot_record_t record;
            CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
            CHECK_INT(rows[i].expected, record.slot_state_at_start[other]);
            CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_CONFIRMED, record.slot_state_at_start[running]);
        }
    }
}

typedef struct {
    const char *name;
    proto_slot_view_t slot;
    crux_image_slot_state_t expected; /* ignored when start must fail */
    bool start_fails;
    bool confirmed;
} running_row_t;

static void test_slot_states_running(void)
{
    static const running_row_t rows[] = {
        {"S7 running confirmed",
         {true, {1, 0, 0, 0}, MAGIC_GOOD, SET, SET},
         CRUX_IMAGE_SLOT_RUNNING_CONFIRMED,
         false,
         true},
        {"S7 running confirmed, copy_done unset (any)",
         {true, {1, 0, 0, 0}, MAGIC_GOOD, SET, UNSET},
         CRUX_IMAGE_SLOT_RUNNING_CONFIRMED,
         false,
         true},
        {"S7 running confirmed, copy_done outside (any)",
         {true, {1, 0, 0, 0}, MAGIC_GOOD, SET, INVALID_FLAG},
         CRUX_IMAGE_SLOT_RUNNING_CONFIRMED,
         false,
         true},
        {"S8 running testing",
         {true, {1, 0, 0, 0}, MAGIC_GOOD, UNSET, SET},
         CRUX_IMAGE_SLOT_RUNNING_TESTING,
         false,
         false},
        {"S9 running testing, copy_done write failed",
         {true, {1, 0, 0, 0}, MAGIC_GOOD, UNSET, UNSET},
         CRUX_IMAGE_SLOT_RUNNING_TESTING,
         false,
         false},
        {"S10 running, magic unset: start fails",
         {true, {1, 0, 0, 0}, PROTO_MAGIC_UNSET, UNSET, UNSET},
         CRUX_IMAGE_SLOT_EMPTY,
         true,
         false},
        {"S10 running, magic bad: start fails",
         {true, {1, 0, 0, 0}, PROTO_MAGIC_BAD, SET, SET},
         CRUX_IMAGE_SLOT_EMPTY,
         true,
         false},
        /* Prototype choices, P3: the table is silent, so the provider fails closed. */
        {"P3 running, header invalid: start fails",
         {false, {0, 0, 0, 0}, MAGIC_GOOD, SET, SET},
         CRUX_IMAGE_SLOT_EMPTY,
         true,
         false},
        {"P3 running, image_ok outside: start fails",
         {true, {1, 0, 0, 0}, MAGIC_GOOD, INVALID_FLAG, SET},
         CRUX_IMAGE_SLOT_EMPTY,
         true,
         false},
        {"P3 running, unset image_ok and copy_done outside: start fails",
         {true, {1, 0, 0, 0}, MAGIC_GOOD, UNSET, INVALID_FLAG},
         CRUX_IMAGE_SLOT_EMPTY,
         true,
         false},
    };
    for (crux_image_slot_t running = 0; running < CRUX_IMAGE_SLOT_COUNT; running++) {
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
            CASE(rows[i].name);
            arrange(running, NULL);
            fake_slot_put(running, &rows[i].slot);
            proto_provider_reset();
            if (rows[i].start_fails) {
                CHECK_INT(CRUX_IMAGE_ERR_BOOT_STATE, crux_image_start());
                continue;
            }
            CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
            CHECK_INT(rows[i].expected, state_of(running));
            crux_image_boot_record_t record;
            CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
            CHECK(record.running_confirmed == rows[i].confirmed);
            CHECK_INT(rows[i].expected, record.slot_state_at_start[running]);
        }
    }
}

static void test_argument_order(void)
{
    /* P6: before any start, argument errors come first for the getters, and the started check
     * comes first for request_test. */
    crux_image_boot_record_t record;
    crux_image_slot_info_t info;
    CASE("P6 before start: getters check their arguments first");
    arrange(0, NULL);
    CHECK_INT(CRUX_IMAGE_ERR_INVALID_ARGUMENT, crux_image_boot_record_get(NULL));
    CHECK_INT(CRUX_IMAGE_ERR_INVALID_ARGUMENT, crux_image_slot_get(0, NULL));
    CHECK_INT(CRUX_IMAGE_ERR_INVALID_ARGUMENT, crux_image_slot_get(2, &info));
    CHECK_INT(CRUX_IMAGE_ERR_INVALID_ARGUMENT, crux_image_slot_get(255, &info));
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_boot_record_get(&record));
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_slot_get(0, &info));

    CASE("P6 before start: request_test is not ready, even for a slot that does not exist");
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_request_test(2));
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_request_test(0));
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_request_test(1));

    CASE("P6 after start: a slot that does not exist is refused before the running-slot check");
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    CHECK_INT(CRUX_IMAGE_ERR_INVALID_ARGUMENT, crux_image_request_test(2));
    CHECK_INT(CRUX_IMAGE_ERR_ACTIVE_SLOT, crux_image_request_test(0));
    CHECK_INT(0, fake_write_count());
}

static void test_read_failure(void)
{
    /* P2: start fails if reading either slot fails, the running slot or the inactive one. */
    for (crux_image_slot_t failing = 0; failing < CRUX_IMAGE_SLOT_COUNT; failing++) {
        CASE("P2 start fails when reading a slot fails");
        arrange(0, NULL);
        fake_fail_slot_read(failing, true);
        CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_start());
        CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_confirm());
        fake_fail_slot_read(failing, false);
        CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    }

    /* After start, a failing read is reported by the live calls and writes nothing. */
    CASE("P2 live calls report a failing read");
    proto_slot_view_t staged = view(true, V110, PROTO_MAGIC_UNSET, UNSET, UNSET);
    arrange(0, &staged);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    crux_image_slot_info_t info;
    fake_fail_slot_read(1, true);
    CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_slot_get(1, &info));
    CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_request_test(1));
    CHECK_INT(0, fake_write_count());
    fake_fail_slot_read(1, false);
    fake_fail_slot_read(0, true);
    CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_confirm());
    CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_request_test(1));
    CHECK_INT(0, fake_write_count());
}

static void test_start(void)
{
    /* B1: the running-slot key missing, then the later calls are not ready. */
    CASE("B1 shared data absent");
    arrange(0, NULL);
    fake_shared_set_absent(true);
    CHECK_INT(CRUX_IMAGE_ERR_NO_BOOT_DATA, crux_image_start());

    CASE("B1 running-slot key missing");
    arrange(0, NULL);
    proto_shared_data_t shared = {false, 0, PROTO_MODE_RAM_LOAD, true, {2, 4, 0, 0}};
    fake_shared_put(&shared);
    CHECK_INT(CRUX_IMAGE_ERR_NO_BOOT_DATA, crux_image_start());
    crux_image_boot_record_t record;
    crux_image_slot_info_t info;
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_confirm());
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_request_test(1));
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_boot_record_get(&record));
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_slot_get(0, &info));
    CHECK_INT(0, fake_write_count());

    CASE("B1 a failed start is not cached: the provider never guesses (P2)");
    shared.running_slot_present = true;
    fake_shared_put(&shared);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());

    CASE("B1 before any start");
    arrange(0, NULL);
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_confirm());

    /* B2: a mode other than RAM load. */
    CASE("B2 mode other than RAM load");
    arrange(0, NULL);
    shared = (proto_shared_data_t){true, 0, PROTO_MODE_OTHER, true, {2, 4, 0, 0}};
    fake_shared_put(&shared);
    CHECK_INT(CRUX_IMAGE_ERR_BOOT_STATE, crux_image_start());
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_confirm());

    CASE("B2 mode key missing (P2)");
    shared.mode = PROTO_MODE_MISSING;
    fake_shared_put(&shared);
    CHECK_INT(CRUX_IMAGE_ERR_NO_BOOT_DATA, crux_image_start());

    CASE("B2 running slot above 1 (P2)");
    shared = (proto_shared_data_t){true, 2, PROTO_MODE_RAM_LOAD, true, {2, 4, 0, 0}};
    fake_shared_put(&shared);
    CHECK_INT(CRUX_IMAGE_ERR_BOOT_STATE, crux_image_start());

    /* B3: the bootloader version present, absent, and present with zero fields. */
    CASE("B3 bootloader version present");
    arrange(0, NULL);
    shared = (proto_shared_data_t){true, 0, PROTO_MODE_RAM_LOAD, true, {2, 4, 1, 7}};
    fake_shared_put(&shared);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK(record.bootloader_version_known);
    CHECK_INT(2, record.bootloader_version.major);
    CHECK_INT(4, record.bootloader_version.minor);
    CHECK_INT(1, record.bootloader_version.revision);
    CHECK_INT(7, record.bootloader_version.build);

    CASE("B3 bootloader version absent: start still succeeds");
    arrange(0, NULL);
    shared.bootloader_version_present = false;
    fake_shared_put(&shared);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK(!record.bootloader_version_known);

    CASE("B3 bootloader version present with zero fields is known (P4)");
    arrange(0, NULL);
    shared = (proto_shared_data_t){true, 0, PROTO_MODE_RAM_LOAD, true, {0, 0, 0, 0}};
    fake_shared_put(&shared);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK(record.bootloader_version_known);
    CHECK_INT(0, record.bootloader_version.major);

    /* The record's other fields, for each running slot. */
    for (crux_image_slot_t running = 0; running < CRUX_IMAGE_SLOT_COUNT; running++) {
        CASE("record fields");
        proto_slot_view_t staged = view(true, V110, PROTO_MAGIC_UNSET, UNSET, UNSET);
        arrange(running, &staged);
        proto_slot_view_t run =
            view(true, (crux_image_version_t){1, 2, 3, 5}, MAGIC_GOOD, SET, SET);
        fake_slot_put(running, &run);
        CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
        CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
        CHECK_INT(running, record.running_slot);
        CHECK_INT(1u - running, record.inactive_slot);
        CHECK_INT(1, record.running_version.major);
        CHECK_INT(2, record.running_version.minor);
        CHECK_INT(3, record.running_version.revision);
        CHECK_INT(5, record.running_version.build);
        CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_CONFIRMED, record.slot_state_at_start[running]);
        CHECK_INT(CRUX_IMAGE_SLOT_STAGED, record.slot_state_at_start[1u - running]);
    }

    /* B4: start twice returns the same record, and the record is a snapshot. */
    CASE("B4 start twice");
    proto_slot_view_t trial = view(true, V100, MAGIC_GOOD, UNSET, SET);
    arrange(0, NULL);
    fake_slot_put(0, &trial);
    proto_provider_reset();
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    crux_image_boot_record_t first;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&first));
    fake_stage(1, V110); /* the medium changes after start */
    shared.running_slot = 1;
    fake_shared_put(&shared);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    crux_image_boot_record_t second;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&second));
    CHECK_INT(first.running_slot, second.running_slot);
    CHECK_INT(first.slot_state_at_start[1], second.slot_state_at_start[1]);
    CHECK(second.running_confirmed == first.running_confirmed);

    CASE("B4 the record does not change after a confirm in this boot");
    CHECK_INT(CRUX_IMAGE_OK, crux_image_confirm());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&second));
    CHECK(!second.running_confirmed);
    CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_CONFIRMED, state_of(0));

    CASE("arguments");
    CHECK_INT(CRUX_IMAGE_ERR_INVALID_ARGUMENT, crux_image_boot_record_get(NULL));
    CHECK_INT(CRUX_IMAGE_ERR_INVALID_ARGUMENT, crux_image_slot_get(0, NULL));
    CHECK_INT(CRUX_IMAGE_ERR_INVALID_ARGUMENT, crux_image_slot_get(2, &info));
}

static void expect_no_write_request(crux_image_slot_t slot, crux_image_result_t expected)
{
    size_t before = fake_write_count();
    CHECK_INT(expected, crux_image_request_test(slot));
    CHECK_INT(before, fake_write_count());
}

static void test_request(void)
{
    /* R1: from slot 0 for slot 1 and from slot 1 for slot 0, the inactive slot is written. */
    for (crux_image_slot_t running = 0; running < CRUX_IMAGE_SLOT_COUNT; running++) {
        CASE("R1 request writes the inactive slot");
        crux_image_slot_t other = (crux_image_slot_t)(1u - running);
        proto_slot_view_t staged = view(true, V110, PROTO_MAGIC_UNSET, UNSET, UNSET);
        arrange(running, &staged);
        CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
        CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(other));
        CHECK_INT(2, fake_write_count());
        CHECK_INT(FAKE_WRITE_MAGIC, fake_write_op(0));
        CHECK_INT(other, fake_write_slot(0));
        CHECK_INT(FAKE_WRITE_SWAP_INFO_TEST, fake_write_op(1));
        CHECK_INT(other, fake_write_slot(1));
        CHECK_INT(CRUX_IMAGE_SLOT_PENDING_TEST, state_of(other));
        CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_CONFIRMED, state_of(running));
    }

    /* R2: the running slot is refused and nothing is written. */
    for (crux_image_slot_t running = 0; running < CRUX_IMAGE_SLOT_COUNT; running++) {
        CASE("R2 request naming the running slot");
        proto_slot_view_t staged = view(true, V110, PROTO_MAGIC_UNSET, UNSET, UNSET);
        arrange(running, &staged);
        CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
        expect_no_write_request(running, CRUX_IMAGE_ERR_ACTIVE_SLOT);
        expect_no_write_request(2, CRUX_IMAGE_ERR_INVALID_ARGUMENT);
        expect_no_write_request(255, CRUX_IMAGE_ERR_INVALID_ARGUMENT);
    }

    /* R3: the running image is on trial. */
    CASE("R3 running image on trial");
    proto_slot_view_t staged = view(true, V110, PROTO_MAGIC_UNSET, UNSET, UNSET);
    arrange(0, &staged);
    proto_slot_view_t trial = view(true, V100, MAGIC_GOOD, UNSET, SET);
    fake_slot_put(0, &trial);
    proto_provider_reset();
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    expect_no_write_request(1, CRUX_IMAGE_ERR_NOT_CONFIRMED);
    CASE("R3 the running-slot check comes before the confirmed check");
    expect_no_write_request(0, CRUX_IMAGE_ERR_ACTIVE_SLOT);
    CASE("R3 after a confirm in this boot the request passes (live state)");
    CHECK_INT(CRUX_IMAGE_OK, crux_image_confirm());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));

    /* R4: no valid header. */
    CASE("R4 empty slot");
    arrange(0, NULL);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    expect_no_write_request(1, CRUX_IMAGE_ERR_NO_IMAGE);
    CASE("R4 no header with a good trailer");
    proto_slot_view_t headless = view(false, V110, MAGIC_GOOD, SET, SET);
    fake_slot_put(1, &headless);
    expect_no_write_request(1, CRUX_IMAGE_ERR_NO_IMAGE);

    /* R5: older, equal and newer than the running 1.0.0 image, whose build number is `build`. */
    static const struct {
        const char *name;
        uint32_t running_build;
        crux_image_version_t version;
        crux_image_result_t expected;
    } versions[] = {
        {"R5 older revision", 0, {0, 9, 9, 0}, CRUX_IMAGE_ERR_NOT_NEWER},
        {"R5 older major", 0, {0, 255, 65535, 0}, CRUX_IMAGE_ERR_NOT_NEWER},
        {"R5 equal", 0, {1, 0, 0, 0}, CRUX_IMAGE_ERR_NOT_NEWER},
        {"R5 equal, higher build", 0, {1, 0, 0, 999}, CRUX_IMAGE_ERR_NOT_NEWER},
        {"R5 equal, lower build", 10, {1, 0, 0, 0}, CRUX_IMAGE_ERR_NOT_NEWER},
        {"R5 newer revision", 0, {1, 0, 1, 0}, CRUX_IMAGE_OK},
        {"R5 newer minor", 0, {1, 1, 0, 0}, CRUX_IMAGE_OK},
        {"R5 newer major, lower minor", 0, {2, 0, 0, 0}, CRUX_IMAGE_OK},
        {"R5 newer revision, lower build than the running image", 10, {1, 0, 1, 0}, CRUX_IMAGE_OK},
        {"R5 older revision, higher build than the running image",
         10,
         {0, 9, 9, 999},
         CRUX_IMAGE_ERR_NOT_NEWER},
    };
    for (crux_image_slot_t running = 0; running < CRUX_IMAGE_SLOT_COUNT; running++) {
        for (size_t i = 0; i < sizeof(versions) / sizeof(versions[0]); i++) {
            CASE(versions[i].name);
            proto_slot_view_t candidate =
                view(true, versions[i].version, PROTO_MAGIC_UNSET, UNSET, UNSET);
            arrange(running, &candidate);
            proto_slot_view_t current =
                view(true, (crux_image_version_t){1, 0, 0, versions[i].running_build}, MAGIC_GOOD,
                     SET, SET);
            fake_slot_put(running, &current);
            proto_provider_reset();
            CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
            CHECK_INT(versions[i].expected,
                      crux_image_request_test((crux_image_slot_t)(1u - running)));
            CHECK_INT(versions[i].expected == CRUX_IMAGE_OK ? 2 : 0, fake_write_count());
        }
    }

    /* R6: a newer image in a slot that is not STAGED, among them the stale confirmed trailer. */
    static const state_row_t refused[] = {
        {"R6 stale confirmed trailer (standby)",
         {true, {1, 1, 0, 0}, MAGIC_GOOD, SET, SET},
         CRUX_IMAGE_SLOT_STANDBY_CONFIRMED},
        {"R6 stale confirmed trailer (pending permanent)",
         {true, {1, 1, 0, 0}, MAGIC_GOOD, SET, UNSET},
         CRUX_IMAGE_SLOT_PENDING_PERMANENT},
        {"R6 tried", {true, {1, 1, 0, 0}, MAGIC_GOOD, UNSET, SET}, CRUX_IMAGE_SLOT_TRIED},
        {"R6 invalid trailer",
         {true, {1, 1, 0, 0}, PROTO_MAGIC_BAD, UNSET, UNSET},
         CRUX_IMAGE_SLOT_INVALID},
        /* P3: a request must never create PENDING_PERMANENT (I2), so these are not STAGED. */
        {"R6 P3 magic unset, image_ok set (a request would be permanent)",
         {true, {1, 1, 0, 0}, PROTO_MAGIC_UNSET, SET, UNSET},
         CRUX_IMAGE_SLOT_INVALID},
        {"R6 P3 magic unset, copy_done set (a request would read TRIED)",
         {true, {1, 1, 0, 0}, PROTO_MAGIC_UNSET, UNSET, SET},
         CRUX_IMAGE_SLOT_INVALID},
    };
    for (crux_image_slot_t running = 0; running < CRUX_IMAGE_SLOT_COUNT; running++) {
        for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
            CASE(refused[i].name);
            arrange(running, &refused[i].slot);
            CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
            CHECK_INT(refused[i].expected, state_of((crux_image_slot_t)(1u - running)));
            expect_no_write_request((crux_image_slot_t)(1u - running), CRUX_IMAGE_ERR_SLOT_STATE);
        }
    }

    /* The order of the checks: no image, then not newer, then the slot state. */
    CASE("order: not newer is reported before the slot state");
    proto_slot_view_t standby_old = view(true, V100, MAGIC_GOOD, SET, SET);
    arrange(0, &standby_old);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    expect_no_write_request(1, CRUX_IMAGE_ERR_NOT_NEWER);

    /* R7: a second request for a pending slot returns OK and writes nothing. */
    CASE("R7 repeat request");
    arrange(0, &staged);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));
    CHECK_INT(2, fake_write_count());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));
    CHECK_INT(2, fake_write_count());
    CASE("R7 a pending slot that is no longer newer is still refused (order of checks)");
    proto_slot_view_t pending_equal = view(true, V100, MAGIC_GOOD, UNSET, UNSET);
    arrange(0, &pending_equal);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    expect_no_write_request(1, CRUX_IMAGE_ERR_NOT_NEWER);

    /* R8: the stock order, and a failure after the magic leaves the slot PENDING_TEST. */
    CASE("R8 failure of the magic write");
    arrange(0, &staged);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    fake_fail_nth_write(1);
    CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_request_test(1));
    CHECK_INT(0, fake_write_count());
    CHECK_INT(CRUX_IMAGE_SLOT_STAGED, state_of(1));
    CASE("R8 a retry after a failed request");
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));
    CHECK_INT(CRUX_IMAGE_SLOT_PENDING_TEST, state_of(1));

    CASE("R8 failure of the swap-info write, after the magic");
    arrange(0, &staged);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    fake_fail_nth_write(2);
    CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_request_test(1));
    CHECK_INT(1, fake_write_count());
    CHECK_INT(FAKE_WRITE_MAGIC, fake_write_op(0));
    CHECK_INT(CRUX_IMAGE_SLOT_PENDING_TEST, state_of(1));
}

static void test_confirm(void)
{
    /* C1: confirm on trial writes image_ok on the running slot, a second call writes nothing. */
    for (crux_image_slot_t running = 0; running < CRUX_IMAGE_SLOT_COUNT; running++) {
        crux_image_slot_t other = (crux_image_slot_t)(1u - running);
        CASE("C1/C2 confirm touches the running slot only");
        proto_slot_view_t standby = view(true, V100, MAGIC_GOOD, SET, SET);
        arrange(running, &standby);
        proto_slot_view_t trial = view(true, V110, MAGIC_GOOD, UNSET, SET);
        fake_slot_put(running, &trial);
        proto_provider_reset();
        CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
        CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_TESTING, state_of(running));
        CHECK_INT(CRUX_IMAGE_OK, crux_image_confirm());
        CHECK_INT(1, fake_write_count());
        CHECK_INT(FAKE_WRITE_IMAGE_OK, fake_write_op(0));
        CHECK_INT(running, fake_write_slot(0));
        CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_CONFIRMED, state_of(running));
        CHECK_INT(CRUX_IMAGE_SLOT_STANDBY_CONFIRMED, state_of(other));
        proto_slot_view_t untouched = fake_slot_peek(other);
        CHECK_INT(PROTO_FLAG_SET, untouched.image_ok);
        CHECK_INT(CRUX_IMAGE_OK, crux_image_confirm());
        CHECK_INT(1, fake_write_count());
    }

    CASE("C1 confirm of an image that is already confirmed writes nothing");
    arrange(0, NULL);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_confirm());
    CHECK_INT(0, fake_write_count());

    /* C3: a write failure. */
    CASE("C3 write failure");
    proto_slot_view_t trial = view(true, V100, MAGIC_GOOD, UNSET, SET);
    arrange(0, NULL);
    fake_slot_put(0, &trial);
    proto_provider_reset();
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    fake_fail_nth_write(1);
    CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_confirm());
    CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_TESTING, state_of(0));
    CASE("C3 a retry after the failure");
    CHECK_INT(CRUX_IMAGE_OK, crux_image_confirm());
    CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_CONFIRMED, state_of(0));
}

static crux_image_result_t hook_request_result;
static crux_image_result_t hook_confirm_result;
static unsigned hook_calls;

static void reenter(void)
{
    hook_calls++;
    if (hook_calls == 1) {
        hook_request_result = crux_image_request_test(1);
        hook_confirm_result = crux_image_confirm();
    }
}

static void test_busy(void)
{
    /*
     * The proposal has one internal lock and ERR_BUSY after a bounded wait. A single thread cannot
     * wait, so the prototype reports ERR_BUSY at once when a call arrives inside another (P5).
     */
    CASE("P5 BUSY re-entry during a request");
    proto_slot_view_t staged = view(true, V110, PROTO_MAGIC_UNSET, UNSET, UNSET);
    arrange(0, &staged);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    hook_calls = 0;
    fake_set_write_hook(reenter);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));
    fake_set_write_hook(NULL);
    CHECK_INT(CRUX_IMAGE_ERR_BUSY, hook_request_result);
    CHECK_INT(CRUX_IMAGE_ERR_BUSY, hook_confirm_result);
    CHECK_INT(2, fake_write_count());
    CASE("P5 BUSY the lock is released after the call");
    CHECK_INT(CRUX_IMAGE_OK, crux_image_confirm());
}

static void test_live_trailer_change(void)
{
    /* P3, P7: the running slot's live trailer stops being a supported one after start. */
    CASE("P3 live running trailer becomes bad: slot_get reports INVALID, the writes refuse");
    proto_slot_view_t staged = view(true, V110, PROTO_MAGIC_UNSET, UNSET, UNSET);
    arrange(0, &staged);
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    proto_slot_view_t broken = view(true, V100, PROTO_MAGIC_BAD, SET, SET);
    fake_slot_put(0, &broken);
    CHECK_INT(CRUX_IMAGE_SLOT_INVALID, state_of(0));
    CHECK_INT(CRUX_IMAGE_ERR_BOOT_STATE, crux_image_confirm());
    expect_no_write_request(1, CRUX_IMAGE_ERR_BOOT_STATE);
    CHECK_INT(0, fake_write_count());
}

void run_provider_tests(void)
{
    test_slot_states_not_running();
    test_slot_states_running();
    test_start();
    test_argument_order();
    test_read_failure();
    test_request();
    test_confirm();
    test_busy();
    test_live_trailer_change();
}
