#include "service_test.h"
#include <stdio.h>

static const char zeros[AREA_MAX_AREA + 1];

static crux_file_service_transfer_t *open_new(const char *area, const char *name,
                                              crux_file_service_open_t mode)
{
    crux_file_service_transfer_t *transfer = NULL;
    assert(crux_file_service_open(SESSION_ALL, area, name, mode, 0, &transfer) ==
           CRUX_FILE_SERVICE_OK);
    assert(transfer != NULL);
    return transfer;
}

static void write_all(crux_file_service_transfer_t *transfer, uint64_t offset, size_t length)
{
    size_t count = 0;
    assert(crux_file_service_write(SESSION_ALL, transfer, offset, zeros, length, &count) ==
           CRUX_FILE_SERVICE_OK);
    assert(count == length);
}

static void put_sized(const char *area, const char *name, size_t length)
{
    crux_file_service_transfer_t *t = open_new(area, name, CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE);
    write_all(t, 0, length);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

static void file_limit_at_and_over_the_bound(void)
{
    test_start();
    crux_file_service_transfer_t *t = open_new("data", "big", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    size_t count = 99;
    write_all(t, 0, AREA_MAX_FILE);
    unsigned writes = crux_file_fake_calls(CRUX_FILE_FAKE_WRITE);
    assert(crux_file_service_write(SESSION_ALL, t, AREA_MAX_FILE, zeros, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(count == 0 && crux_file_fake_calls(CRUX_FILE_FAKE_WRITE) == writes);

    /* An overwrite inside the bound takes no new room. */
    write_all(t, 0, AREA_MAX_FILE);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);

    /* A declared size fails before any data. */
    t = NULL;
    assert(crux_file_service_open(SESSION_ALL, "data", "over", CRUX_FILE_SERVICE_OPEN_MANAGED,
                                  AREA_MAX_FILE + 1, &t) == CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(t == NULL);
    assert(!crux_file_fake_exists(ROOT_DATA, ".crux-up-0"));
}

static void an_overflowing_offset_is_out_of_range(void)
{
    test_start();
    crux_file_service_transfer_t *t = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, t, UINT64_MAX, zeros, 2, &count) ==
           CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE);
    assert(count == 0);

    /* An offset inside the file, with a length whose sum wraps past every limit. */
    write_all(t, 0, 4);
    unsigned writes = crux_file_fake_calls(CRUX_FILE_FAKE_WRITE);
    assert(crux_file_service_write(SESSION_ALL, t, 2, zeros, SIZE_MAX, &count) ==
           CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE);
    assert(count == 0 && crux_file_fake_calls(CRUX_FILE_FAKE_WRITE) == writes);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

static void area_limit_counts_every_file(void)
{
    put_raw(ROOT_DATA, "seed", "abcd");
    test_start();
    /* The recount at start saw the seed. */
    put_sized("data", "one", AREA_MAX_FILE);
    crux_file_service_transfer_t *t = open_new("data", "two", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    size_t count = 99;
    size_t room = AREA_MAX_AREA - AREA_MAX_FILE - 4;
    write_all(t, 0, room);
    assert(crux_file_service_write(SESSION_ALL, t, room, zeros, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(count == 0);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);

    /* A removal gives the room back. */
    assert(crux_file_service_remove(SESSION_ALL, "data", "seed") == CRUX_FILE_SERVICE_OK);
    t = open_new("data", "two", CRUX_FILE_SERVICE_OPEN_DIRECT);
    write_all(t, room, 4);
    assert(crux_file_service_write(SESSION_ALL, t, room + 4, zeros, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);

    /* A truncation gives its old size back first. */
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
    t = open_new("data", "two", CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE);
    write_all(t, 0, AREA_MAX_FILE);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

static void entry_limit_and_the_recount_at_start(void)
{
    put_raw(ROOT_DATA, "a", "");
    put_raw(ROOT_DATA, "b", "");
    test_start();
    put_sized("data", "c", 1);
    put_sized("data", "d", 1);
    put_sized("data", "e", 1);
    crux_file_service_transfer_t *t = NULL;
    unsigned opens = crux_file_fake_calls(CRUX_FILE_FAKE_OPEN);
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE, 0,
                                  &t) == CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_MANAGED, 0,
                                  &t) == CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(t == NULL && !crux_file_fake_exists(ROOT_DATA, "f"));
    assert(crux_file_fake_calls(CRUX_FILE_FAKE_OPEN) == opens);

    /* Opening what exists takes no entry. */
    t = open_new("data", "a", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
    t = open_new("data", "a", CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);

    assert(crux_file_service_remove(SESSION_ALL, "data", "a") == CRUX_FILE_SERVICE_OK);
    t = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_EXCLUSIVE);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

/* A failed create gives its entry back. */
static void a_failed_create_returns_its_entry(void)
{
    put_raw(ROOT_DATA, "a", "");
    put_raw(ROOT_DATA, "b", "");
    put_raw(ROOT_DATA, "c", "");
    put_raw(ROOT_DATA, "d", "");
    test_start();
    crux_file_service_transfer_t *t = NULL;
    for (int i = 0; i < 3; i++) {
        crux_file_fake_fail(CRUX_FILE_FAKE_OPEN, 0, CRUX_FILE_ERR_IO);
        assert(crux_file_service_open(SESSION_ALL, "data", "new",
                                      CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE, 0,
                                      &t) == CRUX_FILE_SERVICE_ERR_IO);
        assert(t == NULL);
    }
    assert(events_of(CRUX_FILE_SERVICE_EVENT_STORAGE_FAILED) == 3);
    t = open_new("data", "new", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

/* The accepted count settles the reservation, whatever else the provider says. */
static void partial_accounting_with_a_short_write(void)
{
    test_start();
    crux_file_service_transfer_t *t = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    size_t count = 0;

    crux_file_fake_limit_write(3);
    assert(crux_file_service_write(SESSION_ALL, t, 0, zeros, 10, &count) == CRUX_FILE_SERVICE_OK);
    assert(count == 3);
    crux_file_fake_limit_write(SIZE_MAX);

    /* The other 7 bytes came back, so that the rest of the area can still fill. */
    write_all(t, 3, AREA_MAX_FILE - 3);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
    put_sized("data", "g", AREA_MAX_AREA - AREA_MAX_FILE);

    /* An error gives back everything that was not accepted. */
    t = open_new("data", "g", CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE);
    crux_file_fake_fail(CRUX_FILE_FAKE_WRITE, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_write(SESSION_ALL, t, 0, zeros, 5, &count) ==
           CRUX_FILE_SERVICE_ERR_IO);
    assert(count == 0);
    write_all(t, 0, AREA_MAX_AREA - AREA_MAX_FILE);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

static crux_file_service_transfer_t *racer;
static crux_file_service_result_t racer_result;

static void race_write(void)
{
    size_t count = 0;
    racer_result = crux_file_service_write(SESSION_ALL, racer, 0, zeros, 600, &count);
}

/* B's check runs while A's write is on the medium, so A's reservation must already be counted. */
static void two_transfers_cannot_both_pass_the_same_check(void)
{
    test_start();
    put_sized("data", "fill", AREA_MAX_FILE);
    crux_file_service_transfer_t *a = open_new("data", "a", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    racer = open_new("data", "b", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    crux_file_fake_on_write(race_write);
    write_all(a, 0, 600);
    assert(racer_result == CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_service_close(&a) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_close(&racer) == CRUX_FILE_SERVICE_OK);
}

static void medium_full_reports_no_space_and_the_accepted_count(void)
{
    test_start();
    crux_file_service_transfer_t *t = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    size_t count = 0;
    crux_file_fake_set_capacity(4);
    assert(crux_file_service_write(SESSION_ALL, t, 0, zeros, 10, &count) == CRUX_FILE_SERVICE_OK);
    assert(count == 4);
    assert(crux_file_service_write(SESSION_ALL, t, 4, zeros, 10, &count) ==
           CRUX_FILE_SERVICE_ERR_NO_SPACE);
    assert(count == 0);
    assert(events_of(CRUX_FILE_SERVICE_EVENT_STORAGE_FAILED) == 1);
    const recorded_event_t *event = events_get(events_count() - 1);
    assert(event->event == CRUX_FILE_SERVICE_EVENT_STORAGE_FAILED && event->storage_limit);
    assert(strcmp(event->area, "data") == 0 && event->name[0] == '\0');
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

/* F5: partial files survive a reset, and only a real need for room removes them. */
static void stale_partials_go_only_when_room_is_needed(void)
{
    put_raw(ROOT_DATA, "one.filepart", "abcd");
    put_raw(ROOT_DATA, "two.filepart", "abcd");
    put_raw(ROOT_DATA, "kept", "abcd");
    test_start();
    assert(crux_file_fake_exists(ROOT_DATA, "one.filepart"));
    assert(events_of(CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED) == 0);

    /* Only as many as the write needs, and each with an event. */
    crux_file_service_transfer_t *t = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    write_all(t, 0, AREA_MAX_FILE);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
    t = open_new("data", "g", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    write_all(t, 0, AREA_MAX_AREA - AREA_MAX_FILE - 12);
    assert(events_of(CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED) == 0);
    write_all(t, AREA_MAX_AREA - AREA_MAX_FILE - 12, 2);
    assert(events_of(CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED) == 1);
    assert(crux_file_fake_exists(ROOT_DATA, "kept"));
    assert(crux_file_fake_exists(ROOT_DATA, "one.filepart") !=
           crux_file_fake_exists(ROOT_DATA, "two.filepart"));
    const recorded_event_t *event = events_get(events_count() - 1);
    assert(strstr(event->name, ".filepart") && strcmp(event->area, "data") == 0);

    /* The rest goes, and after that the limit is real. */
    write_all(t, AREA_MAX_AREA - AREA_MAX_FILE - 10, 6);
    assert(events_of(CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED) == 2);
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, t, AREA_MAX_AREA - AREA_MAX_FILE - 4, zeros, 1,
                                   &count) == CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(count == 0 && crux_file_fake_exists(ROOT_DATA, "kept"));
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

static void a_partial_in_use_is_never_removed(void)
{
    test_start();
    crux_file_service_transfer_t *t =
        open_new("data", "up.filepart", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    write_all(t, 0, AREA_MAX_FILE - 100);
    crux_file_service_transfer_t *u = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    write_all(u, 0, AREA_MAX_FILE);
    crux_file_service_transfer_t *v = open_new("data", "g", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    write_all(v, 0, 100);
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, v, 100, zeros, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_fake_exists(ROOT_DATA, "up.filepart"));
    assert(events_of(CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED) == 0);

    /* The writer of the partial reached the area limit itself, and it does not remove its own file.
     */
    assert(crux_file_service_write(SESSION_ALL, t, AREA_MAX_FILE - 100, zeros, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_fake_exists(ROOT_DATA, "up.filepart"));
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_close(&u) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_close(&v) == CRUX_FILE_SERVICE_OK);
}

/* An area with no declared suffix never loses a file to this rule. */
static void no_suffix_means_nothing_is_removed(void)
{
    put_raw(ROOT_WEB, "one.filepart", "abcd");
    crux_file_t *big = NULL;
    size_t count = 0;
    assert(crux_file_open_at(ROOT_WEB, "big", CRUX_FILE_CREATE_REPLACE, &big) == CRUX_FILE_OK);
    assert(crux_file_write(big, zeros, AREA_MAX_AREA - 4, &count) == CRUX_FILE_OK);
    assert(crux_file_close(&big) == CRUX_FILE_OK);
    test_start();

    /* Inside the file limit, and over the area limit by a little. */
    crux_file_service_transfer_t *t = NULL;
    assert(crux_file_service_open(SESSION_ALL, "web", "new", CRUX_FILE_SERVICE_OPEN_MANAGED, 10,
                                  &t) == CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(t == NULL && crux_file_fake_exists(ROOT_WEB, "one.filepart"));
    assert(events_of(CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED) == 0);
}

/* A partial file open only for reading has a handle and no reservation, so File's rule stops it. */
static void a_partial_with_a_reader_is_never_removed(void)
{
    put_raw(ROOT_DATA, "up.filepart", "abcd");
    test_start();
    crux_file_service_transfer_t *reader =
        open_new("data", "up.filepart", CRUX_FILE_SERVICE_OPEN_READ);
    put_sized("data", "a", AREA_MAX_FILE);
    crux_file_service_transfer_t *t = open_new("data", "b", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    write_all(t, 0, AREA_MAX_FILE - 4);
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, t, AREA_MAX_FILE - 4, zeros, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_fake_exists(ROOT_DATA, "up.filepart"));
    assert(events_of(CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED) == 0);
    assert(crux_file_service_close(&reader) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

/* An area over a bound, after a table changed, still overwrites. It only cannot grow past the
 * bound. */
static void an_area_over_its_entries_still_writes(void)
{
    for (int i = 0; i < 6; i++) {
        char name[8];
        (void)snprintf(name, sizeof(name), "f%d", i);
        put_raw(ROOT_DATA, name, "abcd");
    }
    test_start();
    crux_file_service_transfer_t *t = NULL;
    assert(crux_file_service_open(SESSION_ALL, "data", "new", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE,
                                  0, &t) == CRUX_FILE_SERVICE_ERR_LIMIT);
    t = open_new("data", "f0", CRUX_FILE_SERVICE_OPEN_DIRECT);
    write_all(t, 0, 4);
    write_all(t, 4, 4);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

static void an_area_over_its_bytes_still_overwrites(void)
{
    crux_file_t *big = NULL;
    size_t count = 0;
    assert(crux_file_open_at(ROOT_DATA, "big", CRUX_FILE_CREATE_REPLACE, &big) == CRUX_FILE_OK);
    assert(crux_file_write(big, zeros, sizeof(zeros), &count) == CRUX_FILE_OK);
    assert(crux_file_close(&big) == CRUX_FILE_OK);
    test_start();
    crux_file_service_transfer_t *t = open_new("data", "big", CRUX_FILE_SERVICE_OPEN_DIRECT);
    write_all(t, 0, 10);
    assert(crux_file_service_write(SESSION_ALL, t, sizeof(zeros), zeros, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

/* A hole is refused for what it is, before the limit and the stale rule can act on it. */
static void a_sparse_write_touches_no_partial(void)
{
    put_raw(ROOT_DATA, "one.filepart", "abcd");
    test_start();
    crux_file_service_transfer_t *t = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, t, AREA_MAX_AREA + 10, zeros, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE);
    assert(count == 0 && crux_file_fake_exists(ROOT_DATA, "one.filepart"));
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

static const crux_file_area_t tight_area = {"tight",
                                            ROOT_DATA,
                                            "data.read",
                                            "data.write",
                                            100,
                                            10,
                                            8,
                                            "-0",
                                            CRUX_FILE_SERVICE_MANAGED | CRUX_FILE_SERVICE_DIRECT};
static const crux_file_service_config_t tight_table = {&tight_area, 1, NULL, 0, true};

/* A suffix can match the service's own names, and those are never the stale rule's to remove. */
static void the_stale_rule_leaves_the_services_own_files(void)
{
    assert(crux_file_service_start(&tight_table) == CRUX_FILE_SERVICE_OK);
    crux_file_service_transfer_t *t = open_new("tight", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    write_all(t, 0, 3);
    crux_file_fake_fail(CRUX_FILE_FAKE_RENAME, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(crux_file_fake_exists(ROOT_DATA, ".crux-up-0"));

    t = open_new("tight", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, t, 0, zeros, 10, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_fake_exists(ROOT_DATA, ".crux-up-0"));
    assert(crux_file_fake_exists(ROOT_DATA, ".crux-co-0"));
    assert(events_of(CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED) == 0);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

/* A write bigger than the whole area can never fit, so it costs no partial file. */
static void an_impossible_write_removes_nothing(void)
{
    put_raw(ROOT_DATA, "one-0", "abcd");
    assert(crux_file_service_start(&tight_table) == CRUX_FILE_SERVICE_OK);
    crux_file_service_transfer_t *t = open_new("tight", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, t, 0, zeros, 11, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_fake_exists(ROOT_DATA, "one-0"));
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

/* A failed open of the temporary leaves neither a file nor a counted entry behind. */
static void a_failed_temporary_leaves_nothing_behind(void)
{
    test_start();
    crux_file_service_transfer_t *t = NULL;
    for (int i = 0; i < 8; i++) {
        crux_file_fake_fail(CRUX_FILE_FAKE_TRUNCATE, 0, CRUX_FILE_ERR_IO);
        assert(crux_file_service_open(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED, 0,
                                      &t) == CRUX_FILE_SERVICE_ERR_IO);
        assert(t == NULL && !crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    }
    crux_file_fake_fail(CRUX_FILE_FAKE_SIZE, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_open(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED, 0,
                                  &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL && !crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));

    /* Every entry is still free. */
    for (int i = 0; i < 5; i++) {
        char name[8];
        (void)snprintf(name, sizeof(name), "n%d", i);
        t = open_new("web", name, CRUX_FILE_SERVICE_OPEN_MANAGED);
        assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
    }
}

/* A truncation that fails may or may not have happened, so the counters follow what is there. */
static void a_failed_truncation_counts_what_is_left(void)
{
    put_raw(ROOT_DATA, "f", "abcd");
    test_start();
    crux_file_service_transfer_t *t = NULL;
    crux_file_fake_fail(CRUX_FILE_FAKE_TRUNCATE, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE,
                                  0, &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    expect_raw(ROOT_DATA, "f", "abcd");

    /* The four bytes are still counted, so the area fills exactly. */
    put_sized("data", "g", AREA_MAX_FILE);
    t = open_new("data", "h", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    size_t room = AREA_MAX_AREA - AREA_MAX_FILE - 4;
    write_all(t, 0, room);
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, t, room, zeros, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

/* If nothing can say what a failed truncation left, the old size stays counted, on the safe side.
 */
static void a_failed_truncation_with_a_failed_check_keeps_the_old_size(void)
{
    put_raw(ROOT_DATA, "f", "abcd");
    test_start();
    crux_file_service_transfer_t *t = NULL;
    crux_file_fake_fail(CRUX_FILE_FAKE_TRUNCATE, 0, CRUX_FILE_ERR_IO);
    crux_file_fake_fail(CRUX_FILE_FAKE_STAT, 1, CRUX_FILE_ERR_IO);
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE,
                                  0, &t) == CRUX_FILE_SERVICE_ERR_IO);
    put_sized("data", "g", AREA_MAX_FILE);
    t = open_new("data", "h", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    write_all(t, 0, AREA_MAX_AREA - AREA_MAX_FILE - 4);
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, t, AREA_MAX_AREA - AREA_MAX_FILE - 4, zeros, 1,
                                   &count) == CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

/* A direct write's final size says nothing about room, because it may replace what is there. */
static void a_direct_mode_ignores_the_declared_size(void)
{
    put_raw(ROOT_DATA, "f", "abcd");
    test_start();
    crux_file_service_transfer_t *t = NULL;
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE,
                                  AREA_MAX_AREA, &t) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

void test_capacity(void)
{
    run_case("file_limit", file_limit_at_and_over_the_bound);
    run_case("overflow", an_overflowing_offset_is_out_of_range);
    run_case("area_limit", area_limit_counts_every_file);
    run_case("entry_limit", entry_limit_and_the_recount_at_start);
    run_case("failed_create", a_failed_create_returns_its_entry);
    run_case("partial_accounting", partial_accounting_with_a_short_write);
    run_case("two_transfers", two_transfers_cannot_both_pass_the_same_check);
    run_case("medium_full", medium_full_reports_no_space_and_the_accepted_count);
    run_case("stale_partials", stale_partials_go_only_when_room_is_needed);
    run_case("partial_in_use", a_partial_in_use_is_never_removed);
    run_case("partial_with_reader", a_partial_with_a_reader_is_never_removed);
    run_case("no_suffix", no_suffix_means_nothing_is_removed);
    run_case("over_entries", an_area_over_its_entries_still_writes);
    run_case("over_bytes", an_area_over_its_bytes_still_overwrites);
    run_case("failed_temporary", a_failed_temporary_leaves_nothing_behind);
    run_case("direct_declared", a_direct_mode_ignores_the_declared_size);
    run_case("failed_truncation", a_failed_truncation_counts_what_is_left);
    run_case("failed_truncation_check", a_failed_truncation_with_a_failed_check_keeps_the_old_size);
    run_case("sparse", a_sparse_write_touches_no_partial);
    run_case("own_files", the_stale_rule_leaves_the_services_own_files);
    run_case("impossible_write", an_impossible_write_removes_nothing);
}

static void writers_and_readers_of_one_name(void)
{
    put_raw(ROOT_DATA, "f", "abc");
    test_start();
    crux_file_service_transfer_t *w = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT);
    crux_file_service_transfer_t *other = NULL;
    const crux_file_service_open_t writers[] = {
        CRUX_FILE_SERVICE_OPEN_DIRECT, CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE,
        CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE, CRUX_FILE_SERVICE_OPEN_MANAGED};
    for (size_t i = 0; i < sizeof(writers) / sizeof(writers[0]); i++) {
        assert(crux_file_service_open(SESSION_ALL, "data", "f", writers[i], 0, &other) ==
               CRUX_FILE_SERVICE_ERR_BUSY);
        assert(other == NULL);
    }
    /* File's rule: a reader next to a writer is refused. */
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_READ, 0,
                                  &other) == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_remove(SESSION_ALL, "data", "f") == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_rename(SESSION_ALL, "data", "f", "to") == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_rename(SESSION_ALL, "data", "to", "f") == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_close(&w) == CRUX_FILE_SERVICE_OK);

    /* Readers share a file, and each one stops a remove or rename. */
    crux_file_service_transfer_t *r1 = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_READ);
    crux_file_service_transfer_t *r2 = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_READ);
    assert(crux_file_service_remove(SESSION_ALL, "data", "f") == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_rename(SESSION_ALL, "data", "f", "to") == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT, 0,
                                  &other) == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_close(&r1) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_close(&r2) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_remove(SESSION_ALL, "data", "f") == CRUX_FILE_SERVICE_OK);
}

/* A managed upload's target stays readable, and another writer stays out until it ends. */
static void a_managed_upload_reserves_its_target(void)
{
    put_raw(ROOT_DATA, "f", "old");
    test_start();
    crux_file_service_transfer_t *up = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_MANAGED);
    crux_file_service_transfer_t *other = NULL;
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_MANAGED, 0,
                                  &other) == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT, 0,
                                  &other) == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_remove(SESSION_ALL, "data", "f") == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_rename(SESSION_ALL, "data", "to", "f") == CRUX_FILE_SERVICE_ERR_BUSY);
    crux_file_service_transfer_t *r = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_READ);
    assert(crux_file_service_close(&r) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_close(&up) == CRUX_FILE_SERVICE_OK);
    other = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT);
    assert(crux_file_service_close(&other) == CRUX_FILE_SERVICE_OK);
    expect_raw(ROOT_DATA, "f", "old");
}

static void the_pool_leaves_a_handle_for_the_store(void)
{
    test_start();
    crux_file_service_transfer_t *pool[3] = {0};
    pool[0] = open_new("data", "a", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    pool[1] = open_new("data", "b", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    pool[2] = open_new("data", "c", CRUX_FILE_SERVICE_OPEN_MANAGED);
    crux_file_service_transfer_t *extra = NULL;
    assert(crux_file_service_open(SESSION_ALL, "data", "d", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE, 0,
                                  &extra) == CRUX_FILE_SERVICE_ERR_NO_RESOURCES);
    assert(extra == NULL && !crux_file_fake_exists(ROOT_DATA, "d"));

    /* The handle the store needs is still there while every transfer is open. */
    crux_file_t *store = NULL;
    assert(crux_file_open_at(ROOT_STORE, "store.img", CRUX_FILE_CREATE_REPLACE, &store) ==
           CRUX_FILE_OK);
    assert(crux_file_close(&store) == CRUX_FILE_OK);

    for (int i = 0; i < 3; i++) {
        assert(crux_file_service_close(&pool[i]) == CRUX_FILE_SERVICE_OK);
    }
}

static void the_name_table_refuses_when_full(void)
{
    /* Each upload holds one name and a rename needs two. The default table has room for that, and
     * this suite also runs with a table of four, which does not. */
    test_start();
    crux_file_service_transfer_t *pool[3] = {0};
    pool[0] = open_new("data", "a", CRUX_FILE_SERVICE_OPEN_MANAGED);
    pool[1] = open_new("data", "b", CRUX_FILE_SERVICE_OPEN_MANAGED);
    pool[2] = open_new("data", "c", CRUX_FILE_SERVICE_OPEN_MANAGED);
#if CRUX_FILE_SERVICE_RESERVATIONS == 4
    assert(crux_file_service_rename(SESSION_ALL, "data", "x", "y") ==
           CRUX_FILE_SERVICE_ERR_NO_RESOURCES);
    assert(crux_file_service_remove(SESSION_ALL, "data", "x") == CRUX_FILE_SERVICE_ERR_NOT_FOUND);
#else
    assert(crux_file_service_rename(SESSION_ALL, "data", "x", "y") ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
#endif
    for (int i = 0; i < 3; i++) {
        assert(crux_file_service_close(&pool[i]) == CRUX_FILE_SERVICE_OK);
    }
}

static void a_project_handle_counts_as_a_conflict(void)
{
    put_raw(ROOT_DATA, "f", "abc");
    test_start();
    crux_file_t *project = NULL;
    assert(crux_file_open_at(ROOT_DATA, "f", CRUX_FILE_READ, &project) == CRUX_FILE_OK);
    crux_file_service_transfer_t *t = NULL;
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT, 0, &t) ==
           CRUX_FILE_SERVICE_ERR_BUSY);
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE,
                                  0, &t) == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(t == NULL);
    expect_raw(ROOT_DATA, "f", "abc");
    assert(crux_file_close(&project) == CRUX_FILE_OK);

    /* A busy truncate changed nothing, so the counters still hold the old size. */
    t = open_new("data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT);
    write_all(t, 3, AREA_MAX_FILE - 3);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
    put_sized("data", "g", AREA_MAX_AREA - AREA_MAX_FILE);
    t = open_new("data", "h", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, t, 0, zeros, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_LIMIT);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

static crux_file_service_transfer_t *second_upload;

static void open_second_upload(void)
{
    assert(crux_file_service_open(SESSION_ALL, "web", "b", CRUX_FILE_SERVICE_OPEN_MANAGED, 0,
                                  &second_upload) == CRUX_FILE_SERVICE_OK);
}

/* The second upload arrives after the first found its temporary name free and before it opened it.
 */
static void two_uploads_never_share_a_temporary(void)
{
    test_start();
    crux_file_fake_on_call(CRUX_FILE_FAKE_STAT, 1, open_second_upload);
    crux_file_service_transfer_t *first = open_new("web", "a", CRUX_FILE_SERVICE_OPEN_MANAGED);
    assert(second_upload != NULL);
    size_t count = 0;
    assert(crux_file_service_write(SESSION_ALL, first, 0, "aaa", 3, &count) ==
           CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_write(SESSION_ALL, second_upload, 0, "bbbb", 4, &count) ==
           CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_commit(SESSION_ALL, &first) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_commit(SESSION_ALL, &second_upload) == CRUX_FILE_SERVICE_OK);
    expect_raw(ROOT_WEB, "a", "aaa");
    expect_raw(ROOT_WEB, "b", "bbbb");
}

static unsigned store_opened;

static void store_opens(void)
{
    store_opened++;
    crux_file_t *store = NULL;
    assert(crux_file_open_at(ROOT_STORE, "store.img", CRUX_FILE_CREATE_REPLACE, &store) ==
           CRUX_FILE_OK);
    assert(crux_file_close(&store) == CRUX_FILE_OK);
}

/* The handle left for the store stays free during a commit too, with every transfer open. */
static void the_store_can_open_during_a_commit(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    test_start();
    crux_file_service_transfer_t *up = open_new("web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    crux_file_service_transfer_t *a = open_new("data", "a", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    crux_file_service_transfer_t *b = open_new("data", "b", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    crux_file_fake_on_call(CRUX_FILE_FAKE_SIZE, 0, store_opens);
    crux_file_fake_on_write(store_opens);
    assert(crux_file_service_commit(SESSION_ALL, &up) == CRUX_FILE_SERVICE_OK);
    assert(store_opened == 2);
    assert(crux_file_service_close(&a) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_close(&b) == CRUX_FILE_SERVICE_OK);
}

void test_conflicts(void)
{
    run_case("writers_and_readers", writers_and_readers_of_one_name);
    run_case("managed_reservation", a_managed_upload_reserves_its_target);
    run_case("pool", the_pool_leaves_a_handle_for_the_store);
    run_case("name_table", the_name_table_refuses_when_full);
    run_case("project_handle", a_project_handle_counts_as_a_conflict);
    run_case("shared_temporary", two_uploads_never_share_a_temporary);
    run_case("store_during_commit", the_store_can_open_during_a_commit);
}
