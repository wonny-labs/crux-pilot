/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * End-to-end cases over the fake bootloader model: a full update, a revert, a refused image, the
 * power-interruption points of the proposal (O8, R8, the copy_done failure, a lost confirm) and
 * the reason for the strictly-newer rule. The model is the proposal's own table, not MCUboot.
 */
#include "harness.h"
#include "image_state_fake.h"
#include "image_state_proto.h"

#include <stddef.h>

static const crux_image_version_t V100 = {1, 0, 0, 0};
static const crux_image_version_t V110 = {1, 1, 0, 0};

/* A reset: the bootloader runs, then the application starts and the provider starts afresh. */
static crux_image_result_t reset_board_and_start(void)
{
    fake_boot();
    proto_provider_reset();
    return crux_image_start();
}

static crux_image_slot_state_t state_of(crux_image_slot_t slot)
{
    crux_image_slot_info_t info = {CRUX_IMAGE_SLOT_INVALID, {0, 0, 0, 0}};
    CHECK_INT(CRUX_IMAGE_OK, crux_image_slot_get(slot, &info));
    return info.state;
}

static crux_image_outcome_t outcome_for(const crux_image_attempt_t *attempt)
{
    crux_image_boot_record_t record;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    return crux_image_outcome(&record, attempt);
}

/* Confirmed 1.0.0 in slot 0, running. The agent stages 1.1.0 in slot 1 and writes the attempt. */
static void stage_update(crux_image_attempt_t *attempt)
{
    fake_init();
    CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
    fake_stage(1, V110);
    attempt->slot = 1;
    attempt->version = V110;
    attempt->requested = false;
}

static void test_full_update(void)
{
    CASE("N1-like full update: test, confirm, reset, the new image stays");
    crux_image_attempt_t attempt;
    stage_update(&attempt);
    CHECK_INT(CRUX_IMAGE_SLOT_STAGED, state_of(1));
    CHECK_INT(CRUX_IMAGE_OUTCOME_NOT_BOOTED, outcome_for(&attempt));
    attempt.requested = true;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());

    crux_image_boot_record_t record;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK_INT(1, record.running_slot);
    CHECK(!record.running_confirmed);
    CHECK_INT(1, record.running_version.minor);
    CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_TESTING, record.slot_state_at_start[1]);
    CHECK_INT(CRUX_IMAGE_SLOT_STANDBY_CONFIRMED, record.slot_state_at_start[0]);
    CHECK_INT(CRUX_IMAGE_OUTCOME_TESTING, outcome_for(&attempt));

    CHECK_INT(CRUX_IMAGE_OK, crux_image_confirm());
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK_INT(1, record.running_slot);
    CHECK(record.running_confirmed);
    CHECK_INT(CRUX_IMAGE_OUTCOME_CONFIRMED, outcome_for(&attempt));
    CHECK_INT(CRUX_IMAGE_SLOT_STANDBY_CONFIRMED, state_of(0));

    CASE("N1-like and the next update goes back to slot 0");
    fake_stage(0, (crux_image_version_t){1, 2, 0, 0});
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(0));
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK_INT(0, record.running_slot);
    CHECK_INT(2, record.running_version.minor);
}

static void test_revert(void)
{
    CASE("T2-like revert: test, no confirm, reset, the previous image runs");
    crux_image_attempt_t attempt;
    stage_update(&attempt);
    attempt.requested = true;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_OUTCOME_TESTING, outcome_for(&attempt));
    /* No confirm. The next reset erases the whole slot. */
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    crux_image_boot_record_t record;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK_INT(0, record.running_slot);
    CHECK(record.running_confirmed);
    CHECK_INT(CRUX_IMAGE_SLOT_EMPTY, record.slot_state_at_start[1]);
    CHECK_INT(CRUX_IMAGE_OUTCOME_REVERTED, outcome_for(&attempt));

    CASE("T2-like repeated resets without a confirm stay reverted");
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_OUTCOME_REVERTED, outcome_for(&attempt));
    CHECK_INT(CRUX_IMAGE_SLOT_EMPTY, state_of(1));
}

static void test_refused_image(void)
{
    CASE("T3-like refused image: validation fails, the previous image runs, TRIED then EMPTY");
    crux_image_attempt_t attempt;
    stage_update(&attempt);
    fake_slot_set_image_valid(1, false);
    attempt.requested = true;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    crux_image_boot_record_t record;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK_INT(0, record.running_slot);
    CHECK_INT(CRUX_IMAGE_SLOT_TRIED, record.slot_state_at_start[1]);
    CHECK_INT(CRUX_IMAGE_OUTCOME_REVERTED, outcome_for(&attempt));
    CHECK_INT(CRUX_IMAGE_ERR_SLOT_STATE, crux_image_request_test(1));

    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_SLOT_EMPTY, state_of(1));
    CHECK_INT(CRUX_IMAGE_OUTCOME_REVERTED, outcome_for(&attempt));
}

static void test_power_interruptions(void)
{
    /* O8: a power cut during staging, before the request flag is set. */
    CASE("O8 cut during staging: the newer staged slot reads EMPTY after a reset, NOT_BOOTED");
    crux_image_attempt_t attempt;
    stage_update(&attempt);
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_SLOT_EMPTY, state_of(1));
    CHECK_INT(CRUX_IMAGE_OUTCOME_NOT_BOOTED, outcome_for(&attempt));

    /* O8: the flag was set, and the cut came before the request writes landed. The stated limit. */
    CASE("O8 cut after requested was set, before the request writes: REVERTED, the stated limit");
    stage_update(&attempt);
    attempt.requested = true; /* the request call never started */
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_SLOT_EMPTY, state_of(1));
    CHECK_INT(CRUX_IMAGE_OUTCOME_REVERTED, outcome_for(&attempt));

    /* R8: a cut at each write of the request. */
    CASE("R8 cut at the magic write: nothing landed, the slot is erased at the reset");
    stage_update(&attempt);
    attempt.requested = true;
    fake_fail_nth_write(1);
    CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_request_test(1));
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_SLOT_EMPTY, state_of(1));
    CHECK_INT(CRUX_IMAGE_OUTCOME_REVERTED, outcome_for(&attempt));

    CASE("R8 cut at the swap-info write: the magic landed, the image boots as a test");
    stage_update(&attempt);
    attempt.requested = true;
    fake_fail_nth_write(2);
    CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_request_test(1));
    CHECK_INT(CRUX_IMAGE_SLOT_PENDING_TEST, state_of(1));
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    crux_image_boot_record_t record;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK_INT(1, record.running_slot);
    CHECK_INT(CRUX_IMAGE_OUTCOME_TESTING, outcome_for(&attempt));

    /* The copy_done write fails, which the bootloader only logs. */
    CASE("S9 copy_done write fails, image valid: it runs, treated as on trial");
    stage_update(&attempt);
    attempt.requested = true;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));
    fake_set_copy_done_write_fails(true);
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK_INT(1, record.running_slot);
    CHECK(!record.running_confirmed);
    CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_TESTING, record.slot_state_at_start[1]);

    CASE("O6 copy_done write fails and validation fails: the request is not consumed, NOT_BOOTED");
    stage_update(&attempt);
    attempt.requested = true;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));
    fake_slot_set_image_valid(1, false);
    fake_set_copy_done_write_fails(true);
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_SLOT_RUNNING_CONFIRMED, state_of(0));
    CHECK_INT(CRUX_IMAGE_SLOT_PENDING_TEST, state_of(1));
    CHECK_INT(CRUX_IMAGE_OUTCOME_NOT_BOOTED, outcome_for(&attempt));

    /* A confirm that did not land. */
    CASE("C3 confirm lost to a write failure, then a reset: the image is reverted");
    stage_update(&attempt);
    attempt.requested = true;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_request_test(1));
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    fake_fail_nth_write(1);
    CHECK_INT(CRUX_IMAGE_ERR_IO, crux_image_confirm());
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK_INT(0, record.running_slot);
    CHECK_INT(CRUX_IMAGE_OUTCOME_REVERTED, outcome_for(&attempt));

    /* No slot can be booted at all: the provider fails closed and never guesses. */
    CASE("no slot boots: the shared data has no running slot and start fails closed");
    fake_init();
    fake_stage(0, V100); /* a slot with a header and no trailer: the bootloader erases it */
    CHECK_INT(CRUX_IMAGE_ERR_NO_BOOT_DATA, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_ERR_NOT_READY, crux_image_confirm());
}

static void test_equal_version_reason(void)
{
    /*
     * I4 and the version rules: an equal image in slot 1 is never selected from slot 0, and an
     * equal image in slot 0 is selected from slot 1. The model shows why equal is refused: the
     * request would be a silent no-op in one direction and a boot in the other.
     */
    CASE("equal version in slot 1 while running slot 0: never selected, stays pending");
    fake_init();
    proto_slot_view_t pending = {true, V100, PROTO_MAGIC_GOOD, PROTO_FLAG_UNSET, PROTO_FLAG_UNSET};
    fake_slot_put(1, &pending);
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    crux_image_boot_record_t record;
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK_INT(0, record.running_slot);
    CHECK_INT(CRUX_IMAGE_SLOT_PENDING_TEST, state_of(1));

    CASE("equal version in slot 0 while running slot 1: selected");
    fake_init();
    proto_slot_view_t running = {true, V100, PROTO_MAGIC_GOOD, PROTO_FLAG_SET, PROTO_FLAG_SET};
    fake_slot_put(1, &running);
    fake_slot_put(0, &pending);
    proto_shared_data_t shared = {true, 1, PROTO_MODE_RAM_LOAD, true, {2, 4, 0, 0}};
    fake_shared_put(&shared);
    CHECK_INT(CRUX_IMAGE_OK, reset_board_and_start());
    CHECK_INT(CRUX_IMAGE_OK, crux_image_boot_record_get(&record));
    CHECK_INT(0, record.running_slot);

    CASE("the provider refuses the equal request in both directions");
    for (crux_image_slot_t running_slot = 0; running_slot < CRUX_IMAGE_SLOT_COUNT; running_slot++) {
        fake_init();
        proto_slot_view_t same = {true, V100, PROTO_MAGIC_GOOD, PROTO_FLAG_SET, PROTO_FLAG_SET};
        proto_slot_view_t staged_equal = {true, V100, PROTO_MAGIC_UNSET, PROTO_FLAG_UNSET,
                                          PROTO_FLAG_UNSET};
        fake_slot_put(running_slot, &same);
        fake_slot_put((crux_image_slot_t)(1u - running_slot), &staged_equal);
        shared.running_slot = running_slot;
        fake_shared_put(&shared);
        proto_provider_reset();
        CHECK_INT(CRUX_IMAGE_OK, crux_image_start());
        CHECK_INT(CRUX_IMAGE_ERR_NOT_NEWER,
                  crux_image_request_test((crux_image_slot_t)(1u - running_slot)));
    }
}

void run_scenario_tests(void)
{
    test_full_update();
    test_revert();
    test_refused_image();
    test_power_interruptions();
    test_equal_version_reason();
}
