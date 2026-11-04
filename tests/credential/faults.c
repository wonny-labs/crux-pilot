/*
 * Power loss and failures, on the File fake. The fake's medium model is simpler than FatFs and
 * proves nothing about a real card: these cases show that the store's sequence holds under the
 * model, and that a report of old, new or UNUSABLE is what a cut leaves.
 */
#include "credential_test.h"
#include "fake_file.h"

#include <crux/sync.h>

static uint8_t image_a[CRUX_CREDENTIAL_SLOT_BYTES];
static uint8_t image_b[CRUX_CREDENTIAL_SLOT_BYTES];

static const uint8_t value_old[] = "the old value of this record";
static const uint8_t value_new[] = "the new value of this record, which is longer";
static const uint8_t certificate[] = "certificate bytes";

/*
 * Cuts land between these. 0 is the fake's default, with one cut per File operation. A cut at each
 * byte needs a build with a small slot, to stay quick, and that build also reaches the slot's
 * limits.
 */
#define FAULTS_SMALL_SLOT (CRUX_CREDENTIAL_SLOT_BYTES <= 2048u)
#if FAULTS_SMALL_SLOT
static const size_t units[] = {0u, 512u, 1u};
#else
static const size_t units[] = {0u, 512u};
#endif

/* Fails every store and info lock on demand, as a call outside thread context would. */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __real_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __real_crux_mutex_create(crux_mutex_t *out);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_mutex_create(crux_mutex_t *out);
static bool locks_fail;
static unsigned creates_until_failure;

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms)
{
    return locks_fail ? CRUX_ERR_INVALID_STATE : __real_crux_mutex_lock(mutex, timeout_ms);
}

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_mutex_create(crux_mutex_t *out)
{
    if (creates_until_failure > 0) {
        creates_until_failure -= 1;
        if (creates_until_failure == 0) {
            return CRUX_ERR_NO_RESOURCES;
        }
    }
    return __real_crux_mutex_create(out);
}

static crux_credential_result_t write_record(crux_credential_record_t record, const uint8_t *data,
                                             size_t length)
{
    return crux_credential_write(record, data, length);
}

static crux_credential_result_t read_record(crux_credential_record_t record)
{
    static uint8_t buffer[CRUX_CREDENTIAL_SLOT_BYTES];
    size_t length = 0;
    return crux_credential_read(record, buffer, sizeof(buffer), &length);
}

static bool record_is(crux_credential_record_t record, const uint8_t *data, size_t length)
{
    static uint8_t buffer[CRUX_CREDENTIAL_SLOT_BYTES];
    size_t read_length = 0;
    return crux_credential_read(record, buffer, sizeof(buffer), &read_length) ==
               CRUX_CREDENTIAL_OK &&
           read_length == length && memcmp(buffer, data, length) == 0;
}

#define ACCOUNTS CRUX_CREDENTIAL_RECORD_ACCOUNTS
#define CERTIFICATE CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE
#define HOST_KEY CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY

/*
 * One walk: run the setup and then an action, with a cut before each of the action's mutations in
 * turn, and hand each result to check after a reset.
 */
typedef struct {
    uint32_t cut;
    uint32_t total;
} walk_point_t;

typedef struct {
    const char *name;
    void (*setup)(void);
    void (*action)(void);
    void (*check)(walk_point_t point);
} walk_t;

static size_t walk_unit;
static crux_credential_result_t last_result;
static unsigned count_empty, count_valid, count_unusable;

static void walk_begin(void (*setup)(void))
{
    test_fresh();
    crux_file_fake_write_through(walk_unit);
    setup();
}

static void walk_run(const walk_t *walk)
{
    for (size_t unit = 0; unit < sizeof(units) / sizeof(units[0]); ++unit) {
        walk_unit = units[unit];
        count_empty = count_valid = count_unusable = 0;
        walk_begin(walk->setup);
        uint32_t before = crux_file_fake_mutations();
        walk->action();
        CHECK(last_result == CRUX_CREDENTIAL_OK);
        uint32_t total = crux_file_fake_mutations() - before;
        CHECK(total > 0);
        for (uint32_t cut = 0; cut <= total; ++cut) {
            walk_begin(walk->setup);
            (void)snprintf(credential_test_context, sizeof(credential_test_context),
                           "%s, unit %zu, cut %u of %u", walk->name, walk_unit, (unsigned)cut,
                           (unsigned)total);
            crux_file_fake_cut_after(cut, NULL);
            walk->action();
            CHECK(crux_credential_test_buffers_clear());
            crux_file_fake_power_on();
            credential_fake_events_clear();
            (void)test_boot();
            switch (crux_credential_state()) {
            case CRUX_CREDENTIAL_STATE_EMPTY:
                count_empty += 1;
                break;
            case CRUX_CREDENTIAL_STATE_VALID:
                count_valid += 1;
                break;
            case CRUX_CREDENTIAL_STATE_UNUSABLE:
                count_unusable += 1;
                break;
            }
            walk->check((walk_point_t){cut, total});
        }
        printf("%s, unit %zu: %u cuts gave %u EMPTY, %u VALID, %u UNUSABLE\n", walk->name,
               walk_unit, (unsigned)(total + 1u), count_empty, count_valid, count_unusable);
        credential_test_context[0] = '\0';
    }
}

static void setup_blank(void)
{
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
}

static void action_write_new(void)
{
    last_result = write_record(ACCOUNTS, value_new, sizeof(value_new));
}

/* After any cut in a first save the store is usable, and a repeat of the save succeeds, or it holds
 * the new value. Only a cut inside the header write of a byte walk can leave a corrupt slot. */
static void check_first_save(walk_point_t point)
{
    switch (crux_credential_state()) {
    case CRUX_CREDENTIAL_STATE_EMPTY:
        CHECK(read_record(ACCOUNTS) == CRUX_CREDENTIAL_ERR_NOT_FOUND);
        CHECK(point.cut < point.total);
        CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_OK);
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(record_is(ACCOUNTS, value_new, sizeof(value_new)));
        break;
    case CRUX_CREDENTIAL_STATE_VALID:
        CHECK(record_is(ACCOUNTS, value_new, sizeof(value_new)));
        break;
    case CRUX_CREDENTIAL_STATE_UNUSABLE:
        CHECK(walk_unit == 1u);
        CHECK(point.cut < point.total);
        break;
    }
}

static const walk_t walk_first_save = {
    "first save",
    setup_blank,
    action_write_new,
    check_first_save,
};

static void setup_one_value(void)
{
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
}

static void action_replace(void)
{
    last_result = write_record(ACCOUNTS, value_new, sizeof(value_new));
}

/* Old or new, and never anything else, including EMPTY, once a save has completed. */
static void check_replace(walk_point_t point)
{
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    bool is_old = record_is(ACCOUNTS, value_old, sizeof(value_old));
    bool is_new = record_is(ACCOUNTS, value_new, sizeof(value_new));
    CHECK(is_old || is_new);
    CHECK(point.cut < point.total || is_new);
}

static const walk_t walk_replace = {
    "replacing a record",
    setup_one_value,
    action_replace,
    check_replace,
};

/* A second replacement writes the other slot, so a walk covers each direction. */
static void setup_two_values(void)
{
    setup_one_value();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
}

static const walk_t walk_replace_other = {
    "replacing a record in the other slot",
    setup_two_values,
    action_replace,
    check_replace,
};

static void action_add(void)
{
    last_result = write_record(CERTIFICATE, certificate, sizeof(certificate));
}

/* The other records stay, and the new one is absent or complete. A repeat of the save succeeds. */
static void check_add(walk_point_t point)
{
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
    crux_credential_result_t result = read_record(CERTIFICATE);
    if (result == CRUX_CREDENTIAL_ERR_NOT_FOUND) {
        CHECK(point.cut < point.total);
        CHECK(write_record(CERTIFICATE, certificate, sizeof(certificate)) == CRUX_CREDENTIAL_OK);
    } else {
        CHECK(result == CRUX_CREDENTIAL_OK);
    }
    CHECK(record_is(CERTIFICATE, certificate, sizeof(certificate)));
}

static const walk_t walk_add = {
    "adding a record",
    setup_one_value,
    action_add,
    check_add,
};

static uint8_t acknowledged_public[CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX];
static size_t acknowledged_length;

static void action_key_ensure(void)
{
    last_result = crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519);
    acknowledged_length = 0;
    if (last_result == CRUX_CREDENTIAL_OK) {
        CHECK(crux_credential_key_public(HOST_KEY, acknowledged_public, sizeof(acknowledged_public),
                                         &acknowledged_length) == CRUX_CREDENTIAL_OK);
    }
}

/* An acknowledged key never changes. An unacknowledged one may be replaced, as it was never used.
 */
static void check_key_ensure(walk_point_t point)
{
    if (crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE) {
        CHECK(walk_unit == 1u);
        CHECK(point.cut < point.total);
        CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) ==
              CRUX_CREDENTIAL_ERR_UNUSABLE);
        return;
    }
    CHECK(acknowledged_length == 0 || point.cut == point.total);
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    uint8_t public_key[CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX];
    size_t length = 0;
    CHECK(crux_credential_key_public(HOST_KEY, public_key, sizeof(public_key), &length) ==
          CRUX_CREDENTIAL_OK);
    if (acknowledged_length != 0) {
        CHECK(length == acknowledged_length);
        CHECK(memcmp(public_key, acknowledged_public, length) == 0);
    }
}

static const walk_t walk_key_ensure = {
    "generating a device key",
    setup_blank,
    action_key_ensure,
    check_key_ensure,
};

static void action_key_replace(void)
{
    last_result = crux_credential_key_replace(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519);
}

static void setup_key(void)
{
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_public(HOST_KEY, acknowledged_public, sizeof(acknowledged_public),
                                     &acknowledged_length) == CRUX_CREDENTIAL_OK);
}

/* A rotation that a cut interrupts leaves a usable key, the old or the new, and never no key. */
static void check_key_replace(walk_point_t point)
{
    (void)point;
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    uint8_t public_key[CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX];
    size_t length = 0;
    CHECK(crux_credential_key_public(HOST_KEY, public_key, sizeof(public_key), &length) ==
          CRUX_CREDENTIAL_OK);
    CHECK(length == 32u);
}

static const walk_t walk_key_replace = {
    "rotating a device key",
    setup_key,
    action_key_replace,
    check_key_replace,
};

/* A slot left without a record by a failed second write, repaired at the next start. */
static void setup_needs_repair(void)
{
    const test_record_t one = {ACCOUNTS, value_old, sizeof(value_old)};
    const test_record_t both[] = {one, {CERTIFICATE, certificate, sizeof(certificate)}};
    test_image_build(image_a, 3, &one, 1);
    test_image_build(image_b, 4, both, 2);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_write(TEST_SLOT_B, image_b, sizeof(image_b));
}

static void action_repair(void)
{
    last_result = test_boot();
}

/* The record stays readable from the newer slot, and a torn older slot is rewritten by the next
 * save, so that both hold it again. */
static void check_repair(walk_point_t point)
{
    (void)point;
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(record_is(CERTIFICATE, certificate, sizeof(certificate)));
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_OK);
    for (size_t damaged = TEST_SLOT_A; damaged <= TEST_SLOT_B; ++damaged) {
        CHECK(test_slot_read(damaged, image_a, sizeof(image_a)) == CRUX_CREDENTIAL_SLOT_BYTES);
        test_slot_flip(damaged, CRUX_CREDENTIAL_HEADER_BYTES, 0x80);
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(record_is(CERTIFICATE, certificate, sizeof(certificate)));
        test_slot_write(damaged, image_a, sizeof(image_a));
    }
}

static const walk_t walk_repair = {
    "the repair at start",
    setup_needs_repair,
    action_repair,
    check_repair,
};

static void action_erase(void)
{
    last_result = crux_credential_erase();
}

static void setup_two_records(void)
{
    setup_one_value();
    CHECK(write_record(CERTIFICATE, certificate, sizeof(certificate)) == CRUX_CREDENTIAL_OK);
}

/* Any point of an erase reads as a valid slot, as a corrupt store or as gone, and repeating it ends
 * the same way from each. */
static void check_erase(walk_point_t point)
{
    crux_credential_state_t state = crux_credential_state();
    if (state == CRUX_CREDENTIAL_STATE_VALID) {
        CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
    }
    CHECK(point.cut < point.total || state == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
}

static const walk_t walk_erase = {
    "erasing",
    setup_two_records,
    action_erase,
    check_erase,
};

/* A first save cut after its pending slot reached full size must stay old-or-new when repeated:
 * this slot is written in place, and its real header must still go last. */
static void setup_pending_slot(void)
{
    static uint8_t pending[CRUX_CREDENTIAL_SLOT_BYTES];
    memset(pending, 0, sizeof(pending));
    crux_credential_header_encode_pending(pending);
    test_slot_write(TEST_SLOT_A, pending, sizeof(pending));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
}

static const walk_t walk_first_save_over_pending = {
    "first save over a pending slot",
    setup_pending_slot,
    action_write_new,
    check_first_save,
};

static void test_power_loss_walks(void)
{
    walk_run(&walk_first_save);
    CHECK(FAULTS_SMALL_SLOT == (count_unusable > 0));
    walk_run(&walk_first_save_over_pending);
    CHECK(FAULTS_SMALL_SLOT == (count_unusable > 0));
    walk_run(&walk_replace);
    CHECK(count_unusable == 0 && count_empty == 0);
    walk_run(&walk_replace_other);
    CHECK(count_unusable == 0 && count_empty == 0);
    walk_run(&walk_add);
    CHECK(count_unusable == 0 && count_empty == 0);
    walk_run(&walk_key_ensure);
    walk_run(&walk_key_replace);
    CHECK(count_unusable == 0 && count_empty == 0);
    walk_run(&walk_repair);
    CHECK(count_unusable == 0 && count_empty == 0);
    walk_run(&walk_erase);
    CHECK(count_unusable > 0 && count_empty > 0 && count_valid > 0);
}

/* Every bit of the header matters: a slot with any one flipped is corrupt, never absent or short.
 */
static void test_flipped_header_bits(void)
{
    for (size_t damaged = TEST_SLOT_A; damaged <= TEST_SLOT_B; ++damaged) {
        size_t other = damaged == TEST_SLOT_A ? TEST_SLOT_B : TEST_SLOT_A;
        char name = damaged == TEST_SLOT_A ? 'a' : 'b';
        test_fresh_empty();
        CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
        CHECK(test_slot_read(damaged, image_a, sizeof(image_a)) == CRUX_CREDENTIAL_SLOT_BYTES);
        for (size_t offset = 0; offset < CRUX_CREDENTIAL_HEADER_BYTES; ++offset) {
            for (unsigned bit = 0; bit < 8u; ++bit) {
                (void)snprintf(credential_test_context, sizeof(credential_test_context),
                               "header byte %zu bit %u of slot %c", offset, bit, name);
                uint8_t mask = (uint8_t)(1u << bit);
                test_slot_flip(damaged, offset, mask);
                credential_fake_events_clear();
                CHECK(test_boot() == CRUX_CREDENTIAL_OK);
                CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
                CHECK(credential_fake_slot_invalid_count(name) == 1u);
                CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
                test_slot_flip(damaged, offset, mask);
            }
        }
        credential_test_context[0] = '\0';

        /* A zeroed header is corrupt too, which is what a sector read back as zeros gives. */
        memset(image_b, 0, CRUX_CREDENTIAL_HEADER_BYTES);
        memcpy(image_b + CRUX_CREDENTIAL_HEADER_BYTES, image_a + CRUX_CREDENTIAL_HEADER_BYTES,
               sizeof(image_b) - CRUX_CREDENTIAL_HEADER_BYTES);
        test_slot_write(damaged, image_b, sizeof(image_b));
        credential_fake_events_clear();
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(credential_fake_slot_invalid_count(name) == 1u);
        CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));

        /* With the other slot gone as well, a corrupt slot does not open provisioning. */
        test_slot_remove(other);
        CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
    }
}

/* A flip in the sealed bytes or the tag is corrupt. Padding is outside the tag and is not checked.
 */
static void test_flipped_payload_and_padding(void)
{
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    const size_t payload =
        CRUX_CREDENTIAL_TABLE_HEADER_BYTES + CRUX_CREDENTIAL_TABLE_ENTRY_BYTES + sizeof(value_old);
    const size_t end = CRUX_CREDENTIAL_HEADER_BYTES + payload + CRUX_CREDENTIAL_TAG_BYTES;
    for (size_t offset = CRUX_CREDENTIAL_HEADER_BYTES; offset < end; ++offset) {
        (void)snprintf(credential_test_context, sizeof(credential_test_context), "sealed byte %zu",
                       offset);
        test_slot_flip(TEST_SLOT_A, offset, 0x10);
        credential_fake_events_clear();
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(credential_fake_slot_invalid_count('a') == 1u);
        test_slot_flip(TEST_SLOT_A, offset, 0x10);
    }
    credential_test_context[0] = '\0';
    test_slot_flip(TEST_SLOT_A, end, 0x10);
    test_slot_flip(TEST_SLOT_A, CRUX_CREDENTIAL_SLOT_BYTES - 1u, 0x10);
    credential_fake_events_clear();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SLOT_INVALID) == 0u);
}

/* A read error cannot prove which generation is newest, so any at start is UNUSABLE. */
static void test_read_errors_at_start(void)
{
    static const struct {
        crux_file_fake_op_t op;
        crux_file_result_t result;
    } faults[] = {
        {CRUX_FILE_FAKE_STAT, CRUX_FILE_ERR_IO},
        {CRUX_FILE_FAKE_STAT, CRUX_FILE_ERR_NOT_READY},
        {CRUX_FILE_FAKE_OPEN, CRUX_FILE_ERR_IO},
        {CRUX_FILE_FAKE_OPEN, CRUX_FILE_ERR_NOT_READY},
        {CRUX_FILE_FAKE_OPEN, CRUX_FILE_ERR_NO_RESOURCES},
        {CRUX_FILE_FAKE_READ, CRUX_FILE_ERR_IO},
        {CRUX_FILE_FAKE_CLOSE, CRUX_FILE_ERR_IO},
    };
    for (size_t index = 0; index < sizeof(faults) / sizeof(faults[0]); ++index) {
        for (unsigned skip = 0; skip < 2u; ++skip) {
            (void)snprintf(credential_test_context, sizeof(credential_test_context),
                           "fault %zu in slot %u", index, skip);
            test_fresh_empty();
            CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
            crux_file_fake_fail(faults[index].op, skip, faults[index].result);
            CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
            CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
            /* Nothing was lost, and the next reset finds both slots. */
            CHECK(test_boot() == CRUX_CREDENTIAL_OK);
            CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
        }
    }
    credential_test_context[0] = '\0';

    /* A blank medium with a read error is not a first boot either. */
    test_fresh();
    crux_file_fake_fail(CRUX_FILE_FAKE_STAT, 1, CRUX_FILE_ERR_IO);
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);

    /* A provider that cannot run proves nothing about a slot, as a read error does not. */
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    credential_fake_open_fail(0, CRUX_CREDENTIAL_ERR_READ);
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SLOT_INVALID) == 0u);
}

/*
 * A failed save leaves RAM and the active image alone. What the medium holds at the next boot
 * depends on whether the store could then invalidate the slot, which decisions S7 and AC8 state.
 */
typedef struct {
    const char *name;
    crux_file_fake_op_t op; /* The call that fails, and how many of its kind pass first. */
    unsigned skip;
    crux_file_fake_op_t stop_op; /* The same for the invalidation that follows it. */
    unsigned stop_skip;
} save_fault_t;

/* A save opens the active slot to load it, then the target to write it and to read it back. */
static const save_fault_t save_faults[] = {
    {"sync", CRUX_FILE_FAKE_SYNC, 0, CRUX_FILE_FAKE_OPEN, 2},
    {"read back", CRUX_FILE_FAKE_READ, 1, CRUX_FILE_FAKE_OPEN, 3},
    {"read back open", CRUX_FILE_FAKE_OPEN, 2, CRUX_FILE_FAKE_WRITE, 1},
};

static void test_failed_save_and_invalidation(void)
{
    for (size_t unit = 0; unit < 2u; ++unit) {
        for (size_t index = 0; index < sizeof(save_faults) / sizeof(save_faults[0]); ++index) {
            const save_fault_t *fault = &save_faults[index];
            for (int invalidation_fails = 0; invalidation_fails < 2; ++invalidation_fails) {
                (void)snprintf(credential_test_context, sizeof(credential_test_context),
                               "%s, unit %zu, invalidation %s", fault->name, units[unit],
                               invalidation_fails ? "fails" : "works");
                test_fresh();
                crux_file_fake_write_through(units[unit]);
                CHECK(test_boot() == CRUX_CREDENTIAL_OK);
                CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
                credential_fake_events_clear();
                crux_file_fake_fail(fault->op, fault->skip, CRUX_FILE_ERR_IO);
                if (invalidation_fails) {
                    crux_file_fake_fail(fault->stop_op, fault->stop_skip, CRUX_FILE_ERR_IO);
                }
                CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) ==
                      CRUX_CREDENTIAL_ERR_STORE);
                CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SAVE_FAILED) == 1u);
                CHECK(crux_credential_test_buffers_clear());
                /* RAM and the active image are unchanged. */
                CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
                CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));

                CHECK(test_boot() == CRUX_CREDENTIAL_OK);
                if (invalidation_fails) {
                    /* The stated limit: a failed save can win at the next boot. */
                    CHECK(record_is(ACCOUNTS, value_new, sizeof(value_new)));
                } else {
                    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
                    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SLOT_INVALID) == 0u);
                }

                /* A retry succeeds, in place, over whatever the failure left. */
                CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_OK);
                CHECK(test_boot() == CRUX_CREDENTIAL_OK);
                CHECK(record_is(ACCOUNTS, value_new, sizeof(value_new)));
            }
        }
    }
    credential_test_context[0] = '\0';

    /* A write that fails partway leaves a torn slot, which invalidation then makes short. */
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    crux_file_fake_fail(CRUX_FILE_FAKE_WRITE, 0, CRUX_FILE_ERR_IO);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    credential_fake_events_clear();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SLOT_INVALID) == 0u);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));

    /* A short write is a failed write. */
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    crux_file_fake_limit_write(100);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    crux_file_fake_limit_write(SIZE_MAX);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));

    /* A failed read of the active slot stops a save before it writes anything. */
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    crux_file_fake_fail(CRUX_FILE_FAKE_READ, 0, CRUX_FILE_ERR_IO);
    unsigned writes = crux_file_fake_calls(CRUX_FILE_FAKE_WRITE);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_file_fake_calls(CRUX_FILE_FAKE_WRITE) == writes);

    /* A read back that does not open is a failure. */
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    credential_fake_open_fail(1, CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));

    /* A card that acknowledged a write and dropped it reads back a valid older image. Its
     * generation gives it away, so the save fails, and the store does not take the older image for
     * the new. */
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    crux_file_fake_drop_writes(true);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    crux_file_fake_drop_writes(false);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
    /* The same on a first save, where nothing valid exists to read back: the store stays EMPTY. */
    test_fresh_empty();
    crux_file_fake_drop_writes(true);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_ERR_STORE);
    crux_file_fake_drop_writes(false);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);

    /* Each other step of a save can fail, and each is reported the same way. Slot A's stat comes
     * first in a save, for the load. */
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    unsigned opens = crux_file_fake_calls(CRUX_FILE_FAKE_OPEN);
    crux_file_fake_fail(CRUX_FILE_FAKE_STAT, 1, CRUX_FILE_ERR_IO);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    /* Only the load's open: nothing touched the target, so there was nothing to invalidate. */
    CHECK(crux_file_fake_calls(CRUX_FILE_FAKE_OPEN) == opens + 1u);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
    crux_file_fake_fail(CRUX_FILE_FAKE_CLOSE, 1, CRUX_FILE_ERR_IO);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
    credential_fake_seal_fail(0, CRUX_CREDENTIAL_ERR_READ);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    credential_fake_seal_fail(0, CRUX_CREDENTIAL_ERR_ENTROPY);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_ENTROPY);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
    CHECK(crux_credential_test_buffers_clear());
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));

    /* The steps only a slot that is created goes through. */
    test_fresh_empty();
    crux_file_fake_fail(CRUX_FILE_FAKE_SEEK, 0, CRUX_FILE_ERR_IO);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    crux_file_fake_fail(CRUX_FILE_FAKE_SYNC, 1, CRUX_FILE_ERR_IO);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    crux_file_fake_fail(CRUX_FILE_FAKE_CLOSE, 0, CRUX_FILE_ERR_IO);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
}

/* Erase carries on past a failed step, reports it, and can be repeated. */
static void test_failed_erase_steps(void)
{
    static const struct {
        crux_file_fake_op_t op;
        unsigned skip;
    } faults[] = {
        {CRUX_FILE_FAKE_STAT, 0},   {CRUX_FILE_FAKE_OPEN, 1},  {CRUX_FILE_FAKE_WRITE, 0},
        {CRUX_FILE_FAKE_SYNC, 1},   {CRUX_FILE_FAKE_CLOSE, 0}, {CRUX_FILE_FAKE_REMOVE, 0},
        {CRUX_FILE_FAKE_REMOVE, 1},
    };
    for (size_t index = 0; index < sizeof(faults) / sizeof(faults[0]); ++index) {
        (void)snprintf(credential_test_context, sizeof(credential_test_context), "erase fault %zu",
                       index);
        test_fresh_empty();
        CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
        credential_fake_events_clear();
        crux_file_fake_fail(faults[index].op, faults[index].skip, CRUX_FILE_ERR_IO);
        CHECK(crux_credential_erase() == CRUX_CREDENTIAL_ERR_STORE);
        CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
        const crux_credential_event_t *last =
            credential_fake_event(credential_fake_event_count() - 1u);
        CHECK(last->type == CRUX_CREDENTIAL_EVENT_ERASED);
        CHECK(last->result == CRUX_CREDENTIAL_ERR_STORE);
        /* The operator repeats it, and that ends the same as an erase that never failed. */
        CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
        CHECK(!test_slot_exists(TEST_SLOT_A) && !test_slot_exists(TEST_SLOT_B));
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    }
    credential_test_context[0] = '\0';
}

/* S7: a failed second write returns an error, and the added record is active and survives a reset.
 */
static void test_failed_second_write(void)
{
    static const save_fault_t second_faults[] = {
        /* The first commit used one write, sync and read back, and the load used one read. */
        {"second write", CRUX_FILE_FAKE_WRITE, 1, CRUX_FILE_FAKE_OP_COUNT, 0},
        {"second sync", CRUX_FILE_FAKE_SYNC, 1, CRUX_FILE_FAKE_OP_COUNT, 0},
        {"second read back", CRUX_FILE_FAKE_READ, 2, CRUX_FILE_FAKE_OP_COUNT, 0},
    };
    for (size_t index = 0; index < sizeof(second_faults) / sizeof(second_faults[0]); ++index) {
        (void)snprintf(credential_test_context, sizeof(credential_test_context), "%s",
                       second_faults[index].name);
        test_fresh_empty();
        CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
        credential_fake_events_clear();
        crux_file_fake_fail(second_faults[index].op, second_faults[index].skip, CRUX_FILE_ERR_IO);
        CHECK(write_record(CERTIFICATE, certificate, sizeof(certificate)) ==
              CRUX_CREDENTIAL_ERR_STORE);
        CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SAVE_FAILED) == 1u);

        /* Active at once, and after a reset. */
        CHECK(record_is(CERTIFICATE, certificate, sizeof(certificate)));
        CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(record_is(CERTIFICATE, certificate, sizeof(certificate)));

        /* The next save, of anything, puts the record in both slots. */
        CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_OK);
        for (size_t damaged = TEST_SLOT_A; damaged <= TEST_SLOT_B; ++damaged) {
            CHECK(test_slot_read(damaged, image_a, sizeof(image_a)) == CRUX_CREDENTIAL_SLOT_BYTES);
            test_slot_flip(damaged, CRUX_CREDENTIAL_HEADER_BYTES, 0x80);
            CHECK(test_boot() == CRUX_CREDENTIAL_OK);
            CHECK(record_is(CERTIFICATE, certificate, sizeof(certificate)));
            test_slot_write(damaged, image_a, sizeof(image_a));
        }
    }
    credential_test_context[0] = '\0';

    /* No nonce for the second write is the same case: something was stored, so it is not ENTROPY.
     */
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    credential_fake_random_fail(1);
    CHECK(write_record(CERTIFICATE, certificate, sizeof(certificate)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(record_is(CERTIFICATE, certificate, sizeof(certificate)));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(record_is(CERTIFICATE, certificate, sizeof(certificate)));

    /* The first save follows the same rule, and reaches VALID when slot A is complete. */
    test_fresh_empty();
    crux_file_fake_fail(CRUX_FILE_FAKE_WRITE, 2, CRUX_FILE_ERR_IO);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));

    /* A failure in the first slot of a first save leaves the store EMPTY. */
    test_fresh_empty();
    crux_file_fake_fail(CRUX_FILE_FAKE_WRITE, 1, CRUX_FILE_ERR_IO);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(read_record(ACCOUNTS) == CRUX_CREDENTIAL_ERR_NOT_FOUND);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);

    /* A repair that fails leaves the store usable from the newer slot, and says so. */
    test_fresh();
    setup_needs_repair();
    crux_file_fake_fail(CRUX_FILE_FAKE_WRITE, 0, CRUX_FILE_ERR_IO);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SAVE_FAILED) == 1u);
    CHECK(record_is(CERTIFICATE, certificate, sizeof(certificate)));

    /* An accepted limit, by a narrow path. A repair that fails before touching the older slot
     * leaves the record in one slot, and each boot tries again. If the newer slot rots first, the
     * record reads as absent. The opens are one per slot at start, one to read the newer slot, and
     * then the repair's own. */
    test_fresh();
    setup_needs_repair();
    crux_file_fake_fail(CRUX_FILE_FAKE_OPEN, 3, CRUX_FILE_ERR_IO);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SAVE_FAILED) == 1u);
    CHECK(test_slot_read(TEST_SLOT_A, image_b, sizeof(image_b)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(memcmp(image_a, image_b, sizeof(image_a)) == 0);
    test_slot_flip(TEST_SLOT_B, CRUX_CREDENTIAL_HEADER_BYTES, 0x80);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(read_record(CERTIFICATE) == CRUX_CREDENTIAL_ERR_NOT_FOUND);
}

/* File takes at most one handle, and reports a full pool as a failed save or read. */
static void test_full_file_pool(void)
{
    crux_file_t *held[CRUX_FILE_POOL_SIZE] = {NULL};
    static const char *const names[CRUX_FILE_POOL_SIZE] = {"held0", "held1", "held2", "held3"};

    test_fresh_empty();
    for (size_t index = 0; index < CRUX_FILE_POOL_SIZE; ++index) {
        CHECK(crux_file_open_at(TEST_ROOT_A, names[index], CRUX_FILE_CREATE_REPLACE,
                                &held[index]) == CRUX_FILE_OK);
    }
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(crux_file_close(&held[0]) == CRUX_FILE_OK);

    /* One free handle is all the store needs. */
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);

    CHECK(crux_file_open_at(TEST_ROOT_A, names[0], CRUX_FILE_READ, &held[0]) == CRUX_FILE_OK);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(read_record(ACCOUNTS) == CRUX_CREDENTIAL_ERR_READ);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) ==
          CRUX_CREDENTIAL_ERR_READ);
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_ERR_STORE);

    for (size_t index = 0; index < CRUX_FILE_POOL_SIZE; ++index) {
        CHECK(crux_file_close(&held[index]) == CRUX_FILE_OK);
    }
}

/* A full medium at the first save, and a slot that the image no longer fits. */
static void test_full_store(void)
{
    /* The volume cannot take a slot: a failed save, and still a store that can be retried. */
    test_fresh_empty();
    crux_file_fake_set_capacity(CRUX_CREDENTIAL_SLOT_BYTES / 2u);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(read_record(ACCOUNTS) == CRUX_CREDENTIAL_ERR_NOT_FOUND);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    crux_file_fake_set_capacity(UINT64_MAX);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);

#if FAULTS_SMALL_SLOT
    /* This build's slot is small, so records can outgrow it. Nothing changes when they do. */
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    static uint8_t big[CRUX_CREDENTIAL_SLOT_BYTES];
    memset(big, 0x61, sizeof(big));
    unsigned writes = crux_file_fake_calls(CRUX_FILE_FAKE_WRITE);
    CHECK(write_record(CERTIFICATE, big, CRUX_CREDENTIAL_PAYLOAD_BYTES) ==
          CRUX_CREDENTIAL_ERR_TOO_LARGE);
    CHECK(crux_file_fake_calls(CRUX_FILE_FAKE_WRITE) == writes);
    CHECK(crux_credential_test_buffers_clear());
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
    CHECK(read_record(CERTIFICATE) == CRUX_CREDENTIAL_ERR_NOT_FOUND);

    /* The largest payload that fits, exactly. */
    size_t room = CRUX_CREDENTIAL_PAYLOAD_BYTES - CRUX_CREDENTIAL_TABLE_HEADER_BYTES -
                  CRUX_CREDENTIAL_TABLE_ENTRY_BYTES;
    test_fresh_empty();
    CHECK(write_record(CERTIFICATE, big, room) == CRUX_CREDENTIAL_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(record_is(CERTIFICATE, big, room));
    CHECK(write_record(CERTIFICATE, big, room + 1u) == CRUX_CREDENTIAL_ERR_TOO_LARGE);
    CHECK(write_record(ACCOUNTS, value_old, 1u) == CRUX_CREDENTIAL_ERR_TOO_LARGE);
    CHECK(record_is(CERTIFICATE, big, room));

    /* A key that does not fit is not saved, and is not generated again by a later call that fits.
     */
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) ==
          CRUX_CREDENTIAL_ERR_TOO_LARGE);
    CHECK(crux_credential_test_buffers_clear());
    CHECK(record_is(CERTIFICATE, big, room));
#endif
}

/* While running, a medium that left: reads fail, saves fail, and the state stays what it was. */
static void test_medium_removed_while_running(void)
{
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    credential_fake_events_clear();
    crux_file_fake_remove_medium();
    CHECK(read_record(ACCOUNTS) == CRUX_CREDENTIAL_ERR_READ);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) ==
          CRUX_CREDENTIAL_ERR_READ);
    CHECK(credential_fake_keygen_count() == 0u);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    crux_credential_store_info_t info;
    CHECK(crux_credential_store_get_info(&info) == CRUX_OK);
    CHECK(info.state == CRUX_CREDENTIAL_STATE_VALID);

    /* An erase with no medium fails, and the state still ends UNUSABLE. */
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_ERASED) == 1u);

    /* Reinsertion needs a reset, and until then the store does not look at the new medium. */
    crux_file_fake_power_on();
    CHECK(read_record(ACCOUNTS) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));

    /* A blank medium appearing under an EMPTY store does nothing, and one replacing a VALID store's
     * medium is not read as a first boot. */
    test_slot_remove(TEST_SLOT_A);
    test_slot_remove(TEST_SLOT_B);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(read_record(ACCOUNTS) == CRUX_CREDENTIAL_ERR_READ);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(!test_slot_exists(TEST_SLOT_A) && !test_slot_exists(TEST_SLOT_B));

    /* A different image of the right form appearing at the active slot is not taken for it. */
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    const test_record_t other = {ACCOUNTS, value_new, sizeof(value_new)};
    test_image_build(image_a, 50, &other, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    CHECK(read_record(ACCOUNTS) == CRUX_CREDENTIAL_ERR_READ);
}

static crux_status_t info_during_storage;
static crux_credential_state_t state_during_storage;

static void read_info_during_write(void)
{
    crux_credential_store_info_t info;
    info_during_storage = crux_credential_store_get_info(&info);
    state_during_storage = crux_credential_state();
}

static void record_info_during_event(const crux_credential_event_t *event)
{
    (void)event;
    crux_credential_store_info_t info;
    info_during_storage = crux_credential_store_get_info(&info);
    state_during_storage = info.state;
}

/* A status read during a save does not need the store lock, which the save holds. */
static void test_info_never_waits_on_storage(void)
{
    test_fresh_empty();
    info_during_storage = CRUX_ERR_IO;
    crux_file_fake_on_write(read_info_during_write);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    CHECK(info_during_storage == CRUX_OK);
    CHECK(state_during_storage == CRUX_CREDENTIAL_STATE_EMPTY);

    /* An event may read the state, and sees the state it reports. */
    test_fresh();
    info_during_storage = CRUX_ERR_IO;
    credential_fake_event_hook(record_info_during_event);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(info_during_storage == CRUX_OK);
    CHECK(state_during_storage == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    CHECK(state_during_storage == CRUX_CREDENTIAL_STATE_VALID);
    credential_fake_event_hook(NULL);
}

/* Runs first, before the store has any lock. Having no lock means not started, and start can retry.
 */
static void test_no_locks_available(void)
{
    credential_fake_reset();
    credential_medium_blank();
    creates_until_failure = 1;
    CHECK(crux_credential_start(TEST_ROOT_A, TEST_ROOT_B) == CRUX_CREDENTIAL_ERR_NOT_READY);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    creates_until_failure = 2;
    CHECK(crux_credential_start(TEST_ROOT_A, TEST_ROOT_B) == CRUX_CREDENTIAL_ERR_NOT_READY);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    crux_credential_store_info_t info;
    CHECK(crux_credential_store_get_info(&info) == CRUX_ERR_NOT_READY);
    CHECK(crux_file_fake_calls(CRUX_FILE_FAKE_STAT) == 0u);
    creates_until_failure = 0;
    CHECK(crux_credential_start(TEST_ROOT_A, TEST_ROOT_B) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
}

/* A lock that cannot be taken means misuse, and everything fails closed without touching storage.
 */
static void test_lost_lock(void)
{
    test_fresh_empty();
    CHECK(write_record(ACCOUNTS, value_old, sizeof(value_old)) == CRUX_CREDENTIAL_OK);
    unsigned stats = crux_file_fake_calls(CRUX_FILE_FAKE_STAT);
    unsigned opens = crux_file_fake_calls(CRUX_FILE_FAKE_OPEN);
    crux_credential_store_info_t info;
    locks_fail = true;
    CHECK(read_record(ACCOUNTS) == CRUX_CREDENTIAL_ERR_NOT_READY);
    CHECK(write_record(ACCOUNTS, value_new, sizeof(value_new)) == CRUX_CREDENTIAL_ERR_NOT_READY);
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) ==
          CRUX_CREDENTIAL_ERR_NOT_READY);
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_ERR_NOT_READY);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    CHECK(crux_credential_store_get_info(&info) == CRUX_ERR_NOT_READY);
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_NOT_READY);
    locks_fail = false;
    CHECK(crux_file_fake_calls(CRUX_FILE_FAKE_STAT) == stats);
    CHECK(crux_file_fake_calls(CRUX_FILE_FAKE_OPEN) == opens);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    CHECK(crux_credential_store_get_info(&info) == CRUX_ERR_NOT_READY);

    /* Start retries after a lost lock, because nothing was decided. */
    CHECK(crux_credential_start(TEST_ROOT_A, TEST_ROOT_B) == CRUX_CREDENTIAL_OK);
    CHECK(record_is(ACCOUNTS, value_old, sizeof(value_old)));
}

int main(void)
{
    test_no_locks_available();
    test_power_loss_walks();
    if (FAULTS_SMALL_SLOT) {
        test_flipped_header_bits();
        test_flipped_payload_and_padding();
    }
    test_read_errors_at_start();
    test_failed_save_and_invalidation();
    test_failed_second_write();
    test_failed_erase_steps();
    test_full_file_pool();
    test_full_store();
    test_medium_removed_while_running();
    test_info_never_waits_on_storage();
    test_lost_lock();
    return credential_test_finish("credential faults");
}
