/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * Pure functions of the proposal: V1 (version compare) and O1 to O7 (the outcome table).
 */
#include "harness.h"
#include "image_state_proto.h"

#include <stddef.h>

typedef struct {
    const char *name;
    crux_image_version_t a;
    crux_image_version_t b;
    int expected;
} version_vector_t;

static void test_version_compare(void)
{
    /* V1: major, minor, revision in that order, equal, and the build number never compared. */
    static const version_vector_t vectors[] = {
        {"equal", {1, 2, 3, 0}, {1, 2, 3, 0}, 0},
        {"revision greater", {1, 2, 4, 0}, {1, 2, 3, 0}, 1},
        {"revision less", {1, 2, 3, 0}, {1, 2, 4, 0}, -1},
        {"minor beats revision", {1, 3, 0, 0}, {1, 2, 9, 0}, 1},
        {"minor less beats revision", {1, 2, 9, 0}, {1, 3, 0, 0}, -1},
        {"major beats minor and revision", {2, 0, 0, 0}, {1, 255, 65535, 0}, 1},
        {"major less beats minor and revision", {1, 255, 65535, 0}, {2, 0, 0, 0}, -1},
        {"build ignored when equal otherwise", {1, 2, 3, 7}, {1, 2, 3, 9}, 0},
        {"build ignored, higher build lower revision", {1, 2, 3, 99}, {1, 2, 4, 0}, -1},
        {"build ignored, lower build higher revision", {1, 2, 4, 0}, {1, 2, 3, 99}, 1},
        {"revision is 16 bit", {1, 2, 256, 0}, {1, 2, 255, 0}, 1},
        {"largest values equal", {255, 255, 65535, 0}, {255, 255, 65535, 0}, 0},
        {"largest revision differs by one", {255, 255, 65535, 0}, {255, 255, 65534, 0}, 1},
        {"zero equals zero", {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    };
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        CASE(vectors[i].name);
        CHECK_INT(vectors[i].expected, crux_image_version_compare(&vectors[i].a, &vectors[i].b));
        /* The order is antisymmetric. */
        CHECK_INT(-vectors[i].expected, crux_image_version_compare(&vectors[i].b, &vectors[i].a));
    }
}

typedef struct {
    crux_image_slot_state_t state;
    bool requested;
    crux_image_outcome_t expected;
} outcome_row_t;

/*
 * Cases 4 to 7 of the outcome table, for an attempt on the slot that is not running, with each
 * state that this slot had at start. A requested attempt on a state that the table does not
 * cover (RUNNING_*) gives NONE: a prototype choice, see prototype-notes.md, P9.
 */
static const outcome_row_t outcome_rows[] = {
    {CRUX_IMAGE_SLOT_EMPTY, false, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_EMPTY, true, CRUX_IMAGE_OUTCOME_REVERTED},
    {CRUX_IMAGE_SLOT_STAGED, false, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_STAGED, true, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_PENDING_TEST, false, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_PENDING_TEST, true, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_PENDING_PERMANENT, false, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_PENDING_PERMANENT, true, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_RUNNING_CONFIRMED, false, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_RUNNING_CONFIRMED, true, CRUX_IMAGE_OUTCOME_NONE},
    {CRUX_IMAGE_SLOT_RUNNING_TESTING, false, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_RUNNING_TESTING, true, CRUX_IMAGE_OUTCOME_NONE},
    {CRUX_IMAGE_SLOT_STANDBY_CONFIRMED, false, CRUX_IMAGE_OUTCOME_NONE},
    {CRUX_IMAGE_SLOT_STANDBY_CONFIRMED, true, CRUX_IMAGE_OUTCOME_NONE},
    {CRUX_IMAGE_SLOT_TRIED, false, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_TRIED, true, CRUX_IMAGE_OUTCOME_REVERTED},
    {CRUX_IMAGE_SLOT_INVALID, false, CRUX_IMAGE_OUTCOME_NOT_BOOTED},
    {CRUX_IMAGE_SLOT_INVALID, true, CRUX_IMAGE_OUTCOME_REVERTED},
};

static crux_image_boot_record_t make_record(crux_image_slot_t running, bool confirmed,
                                            crux_image_slot_state_t other_state)
{
    crux_image_boot_record_t record = {0};
    record.running_slot = running;
    record.running_confirmed = confirmed;
    record.running_version = (crux_image_version_t){1, 2, 3, 5};
    record.inactive_slot = (crux_image_slot_t)(1u - running);
    record.slot_state_at_start[running] =
        confirmed ? CRUX_IMAGE_SLOT_RUNNING_CONFIRMED : CRUX_IMAGE_SLOT_RUNNING_TESTING;
    record.slot_state_at_start[record.inactive_slot] = other_state;
    return record;
}

static void test_outcome(void)
{
    for (crux_image_slot_t running = 0; running < CRUX_IMAGE_SLOT_COUNT; running++) {
        crux_image_slot_t other = (crux_image_slot_t)(1u - running);

        /* O1: no attempt. */
        CASE("O1 no attempt");
        crux_image_boot_record_t record = make_record(running, true, CRUX_IMAGE_SLOT_EMPTY);
        CHECK_INT(CRUX_IMAGE_OUTCOME_NONE, crux_image_outcome(&record, NULL));
        CASE("P9 NULL record");
        crux_image_attempt_t some = {running, {1, 2, 3, 5}, true};
        CHECK_INT(CRUX_IMAGE_OUTCOME_NONE, crux_image_outcome(NULL, NULL));
        CHECK_INT(CRUX_IMAGE_OUTCOME_NONE, crux_image_outcome(NULL, &some));

        /* O2: the attempted image runs. The build number does not count. */
        for (int confirmed = 0; confirmed < 2; confirmed++) {
            CASE("O2 attempted image runs");
            record = make_record(running, confirmed != 0, CRUX_IMAGE_SLOT_EMPTY);
            crux_image_attempt_t attempt = {running, {1, 2, 3, 99}, true};
            CHECK_INT(confirmed != 0 ? CRUX_IMAGE_OUTCOME_CONFIRMED : CRUX_IMAGE_OUTCOME_TESTING,
                      crux_image_outcome(&record, &attempt));
            attempt.requested = false; /* the request flag plays no part in this case */
            CHECK_INT(confirmed != 0 ? CRUX_IMAGE_OUTCOME_CONFIRMED : CRUX_IMAGE_OUTCOME_TESTING,
                      crux_image_outcome(&record, &attempt));
        }

        /* O3: the slot was reused, so the versions differ on any of the three compared fields. */
        static const crux_image_version_t other_versions[] = {
            {2, 2, 3, 5}, {1, 3, 3, 5}, {1, 2, 4, 5}, {0, 2, 3, 5}, {1, 1, 3, 5}, {1, 2, 2, 5},
        };
        for (size_t i = 0; i < sizeof(other_versions) / sizeof(other_versions[0]); i++) {
            CASE("O3 stale, slot reused");
            record = make_record(running, false, CRUX_IMAGE_SLOT_EMPTY);
            crux_image_attempt_t attempt = {running, other_versions[i], true};
            CHECK_INT(CRUX_IMAGE_OUTCOME_NONE, crux_image_outcome(&record, &attempt));
        }

        /* O4 to O7: the attempted slot is not the running one. */
        for (size_t i = 0; i < sizeof(outcome_rows) / sizeof(outcome_rows[0]); i++) {
            CASE("O4-O7 P9 attempt on the other slot");
            record = make_record(running, true, outcome_rows[i].state);
            crux_image_attempt_t attempt = {other, {2, 0, 0, 0}, outcome_rows[i].requested};
            CHECK_INT(outcome_rows[i].expected, crux_image_outcome(&record, &attempt));
        }

        /* An attempt that names a slot that does not exist is no attempt (prototype choice, P9). */
        CASE("P9 attempt on a slot that does not exist");
        record = make_record(running, true, CRUX_IMAGE_SLOT_EMPTY);
        crux_image_attempt_t bad = {2, {2, 0, 0, 0}, true};
        CHECK_INT(CRUX_IMAGE_OUTCOME_NONE, crux_image_outcome(&record, &bad));
    }
}

void run_pure_tests(void)
{
    test_version_compare();
    test_outcome();
}
