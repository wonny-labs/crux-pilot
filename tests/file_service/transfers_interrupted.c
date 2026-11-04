#include "service_test.h"
#include <stdio.h>
#include <unistd.h>

static crux_file_service_transfer_t *open_as(crux_access_session_t session, const char *area,
                                             const char *name, crux_file_service_open_t mode)
{
    crux_file_service_transfer_t *transfer = NULL;
    assert(crux_file_service_open(session, area, name, mode, 0, &transfer) == CRUX_FILE_SERVICE_OK);
    return transfer;
}

static void put_text(crux_file_service_transfer_t *t, uint64_t offset, const char *text)
{
    size_t count = 0;
    assert(crux_file_service_write(SESSION_ALL, t, offset, text, strlen(text), &count) ==
           CRUX_FILE_SERVICE_OK);
    assert(count == strlen(text));
}

static void a_listing_shows_only_what_a_client_may_name(void)
{
    put_raw(ROOT_DATA, "one", "abc");
    put_raw(ROOT_DATA, "two.filepart", "ab");
    test_start();
    crux_file_service_transfer_t *up =
        open_as(SESSION_ALL, "data", "new", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(up, 0, "pending");

    crux_file_service_dir_t *dir = NULL;
    char name[64];
    crux_file_info_t info;
    bool end = false;
    assert(crux_file_service_dir_open(SESSION_ALL, "data", &dir) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_dir_read(SESSION_ALL, dir, name, 63, &info, &end) ==
           CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE);
    int seen = 0;
    for (;;) {
        assert(crux_file_service_dir_read(SESSION_ALL, dir, name, sizeof(name), &info, &end) ==
               CRUX_FILE_SERVICE_OK);
        if (end) {
            break;
        }
        assert(strncmp(name, ".crux-", 6) != 0);
        seen += strcmp(name, "one") == 0 && info.size == 3;
        seen += strcmp(name, "two.filepart") == 0 && info.size == 2;
        assert(strcmp(name, "new") != 0);
    }
    assert(seen == 2);

    /* The listing belongs to its session, which can end under it. */
    assert(crux_file_service_dir_read(SESSION_OTHER, dir, name, sizeof(name), &info, &end) ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    assert(crux_file_service_dir_read(SESSION_ENDED, dir, name, sizeof(name), &info, &end) ==
           CRUX_FILE_SERVICE_ERR_DENIED);

    /* The pool is bounded. */
    crux_file_service_dir_t *second = NULL;
    assert(crux_file_service_dir_open(SESSION_ALL, "data", &second) ==
           CRUX_FILE_SERVICE_ERR_NO_RESOURCES);
    assert(second == NULL);
    crux_file_service_dir_t *stale = dir;
    assert(crux_file_service_dir_close(&dir) == CRUX_FILE_SERVICE_OK && dir == NULL);
    assert(crux_file_service_dir_close(&stale) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    assert(crux_file_service_dir_close(NULL) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    assert(crux_file_service_close(&up) == CRUX_FILE_SERVICE_OK);
}

static void reads_follow_their_offset(void)
{
    put_raw(ROOT_DATA, "f", "abcdefgh");
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_READ);
    char value[8];
    size_t count = 0;
    uint64_t size = 0;
    assert(crux_file_service_size(SESSION_ALL, t, &size) == CRUX_FILE_SERVICE_OK && size == 8);

    /* A sequential read costs no seek, because the provider's seek reads the end each time. */
    unsigned seeks = crux_file_fake_calls(CRUX_FILE_FAKE_SEEK);
    assert(crux_file_service_read(SESSION_ALL, t, 0, value, 3, &count) == CRUX_FILE_SERVICE_OK);
    assert(count == 3 && memcmp(value, "abc", 3) == 0);
    assert(crux_file_service_read(SESSION_ALL, t, 3, value, 2, &count) == CRUX_FILE_SERVICE_OK);
    assert(count == 2 && memcmp(value, "de", 2) == 0);
    assert(crux_file_fake_calls(CRUX_FILE_FAKE_SEEK) == seeks);

    assert(crux_file_service_read(SESSION_ALL, t, 1, value, 2, &count) == CRUX_FILE_SERVICE_OK);
    assert(count == 2 && memcmp(value, "bc", 2) == 0);
    assert(crux_file_fake_calls(CRUX_FILE_FAKE_SEEK) == seeks + 1);

    /* The end is a short read, and past it is a named refusal. */
    assert(crux_file_service_read(SESSION_ALL, t, 6, value, sizeof(value), &count) ==
           CRUX_FILE_SERVICE_OK);
    assert(count == 2);
    assert(crux_file_service_read(SESSION_ALL, t, 8, value, sizeof(value), &count) ==
           CRUX_FILE_SERVICE_OK);
    assert(count == 0);
    assert(crux_file_service_read(SESSION_ALL, t, 9, value, sizeof(value), &count) ==
           CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE);
    assert(count == 0);

    /* After a failed seek the position is unknown, so the next read seeks again. */
    crux_file_fake_fail(CRUX_FILE_FAKE_SEEK, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_read(SESSION_ALL, t, 2, value, 1, &count) == CRUX_FILE_SERVICE_ERR_IO);
    assert(crux_file_service_read(SESSION_ALL, t, 2, value, 1, &count) == CRUX_FILE_SERVICE_OK);
    assert(count == 1 && value[0] == 'c');

    /* A read transfer writes nothing. */
    assert(crux_file_service_write(SESSION_ALL, t, 0, "x", 1, &count) ==
           CRUX_FILE_SERVICE_ERR_DENIED);

    /* Argument checks. */
    assert(crux_file_service_read(SESSION_ALL, t, 0, NULL, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    assert(crux_file_service_read(SESSION_ALL, t, 0, value, 1, NULL) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    assert(crux_file_service_size(SESSION_ALL, t, NULL) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    crux_file_service_transfer_t *stale = t;
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK && t == NULL);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    assert(crux_file_service_close(NULL) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    assert(crux_file_service_read(SESSION_ALL, stale, 0, value, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
}

static void direct_modes_match_the_sftp_flags(void)
{
    test_start();
    crux_file_service_transfer_t *t = NULL;

    /* WRITE: not found is an error. */
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT, 0, &t) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(t == NULL && !crux_file_fake_exists(ROOT_DATA, "f"));

    /* WRITE,CREAT: creates, and it keeps what exists. */
    t = open_as(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    put_text(t, 0, "abcdef");
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
    assert(events_of(CRUX_FILE_SERVICE_EVENT_CREATED) == 1);
    t = open_as(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    uint64_t size = 0;
    assert(crux_file_service_size(SESSION_ALL, t, &size) == CRUX_FILE_SERVICE_OK && size == 6);

    /* Overwriting and appending are at or below the size, and a hole is refused. */
    put_text(t, 2, "XY");
    put_text(t, 6, "gh");
    size_t count = 99;
    assert(crux_file_service_write(SESSION_ALL, t, 9, "z", 1, &count) ==
           CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE);
    assert(count == 0);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
    expect_raw(ROOT_DATA, "f", "abXYefgh");
    assert(events_of(CRUX_FILE_SERVICE_EVENT_CREATED) == 1);

    /* WRITE,CREAT,EXCL. */
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_EXCLUSIVE,
                                  0, &t) == CRUX_FILE_SERVICE_ERR_EXISTS);
    assert(t == NULL);
    t = open_as(SESSION_ALL, "data", "g", CRUX_FILE_SERVICE_OPEN_DIRECT_EXCLUSIVE);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);

    /* WRITE,CREAT,TRUNC. */
    t = open_as(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE);
    assert(crux_file_service_size(SESSION_ALL, t, &size) == CRUX_FILE_SERVICE_OK && size == 0);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);

    /* Output checks. */
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_READ, 0, NULL) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    crux_file_service_transfer_t *held =
        open_as(SESSION_ALL, "data", "h", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    crux_file_service_transfer_t *not_null = held;
    assert(crux_file_service_open(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_READ, 0,
                                  &not_null) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    assert(crux_file_service_close(&held) == CRUX_FILE_SERVICE_OK);
}

static void a_rename_and_a_remove_report_what_they_did(void)
{
    put_raw(ROOT_DATA, "from", "abc");
    put_raw(ROOT_DATA, "to", "xyz");
    test_start();
    assert(crux_file_service_rename(SESSION_ALL, "data", "from", "to") ==
           CRUX_FILE_SERVICE_ERR_EXISTS);
    expect_raw(ROOT_DATA, "to", "xyz");
    assert(crux_file_service_rename(SESSION_ALL, "data", "missing", "new") ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(crux_file_service_remove(SESSION_ALL, "data", "missing") ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(events_of(CRUX_FILE_SERVICE_EVENT_RENAMED) == 0);
    assert(events_of(CRUX_FILE_SERVICE_EVENT_REMOVED) == 0);

    /* WinSCP's order: delete the target, then rename its partial file. */
    assert(crux_file_service_remove(SESSION_ALL, "data", "to") == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_rename(SESSION_ALL, "data", "from", "to") == CRUX_FILE_SERVICE_OK);
    expect_raw(ROOT_DATA, "to", "abc");
    assert(events_of(CRUX_FILE_SERVICE_EVENT_REMOVED) == 1);
    const recorded_event_t *event = events_get(events_count() - 1);
    assert(event->event == CRUX_FILE_SERVICE_EVENT_RENAMED);
    assert(strcmp(event->area, "data") == 0 && strcmp(event->name, "from") == 0);
    assert(strcmp(event->new_name, "to") == 0);
}

static void a_transfer_belongs_to_its_session(void)
{
    put_raw(ROOT_DATA, "f", "abc");
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_OTHER, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT);
    char value[4];
    size_t count = 99;
    uint64_t size = 0;
    assert(crux_file_service_read(SESSION_ALL, t, 0, value, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    assert(crux_file_service_write(SESSION_ALL, t, 0, "x", 1, &count) ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    assert(crux_file_service_size(SESSION_ALL, t, &size) == CRUX_FILE_SERVICE_ERR_DENIED);
    crux_file_service_transfer_t *alias = t;
    assert(crux_file_service_commit(SESSION_ALL, &alias) == CRUX_FILE_SERVICE_ERR_DENIED);
    assert(alias == t);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
    expect_raw(ROOT_DATA, "f", "abc");
}

/* A logout during a transfer fails the next chunk, and close still returns the handle. */
static void a_session_that_ends_mid_transfer(void)
{
    test_start();
    crux_file_service_transfer_t *direct =
        open_as(SESSION_OTHER, "data", "direct", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    crux_file_service_transfer_t *managed =
        open_as(SESSION_OTHER, "data", "managed", CRUX_FILE_SERVICE_OPEN_MANAGED);
    size_t count = 0;
    assert(crux_file_service_write(SESSION_OTHER, direct, 0, "abc", 3, &count) ==
           CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_write(SESSION_OTHER, managed, 0, "abc", 3, &count) ==
           CRUX_FILE_SERVICE_OK);
    crux_file_service_dir_t *listing = NULL;
    assert(crux_file_service_dir_open(SESSION_OTHER, "data", &listing) == CRUX_FILE_SERVICE_OK);
    fake_access_end_other();
    char name[64];
    crux_file_info_t info;
    bool end = false;
    uint64_t size = 0;
    assert(crux_file_service_dir_read(SESSION_OTHER, listing, name, sizeof(name), &info, &end) ==
           CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
    assert(crux_file_service_dir_close(&listing) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_size(SESSION_OTHER, direct, &size) ==
           CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
    assert(crux_file_service_read(SESSION_OTHER, direct, 0, name, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
    assert(crux_file_service_write(SESSION_OTHER, direct, 3, "d", 1, &count) ==
           CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
    assert(crux_file_service_commit(SESSION_OTHER, &managed) ==
           CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
    assert(managed != NULL);
    assert(crux_file_service_close(&direct) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_close(&managed) == CRUX_FILE_SERVICE_OK);

    /* A direct partial stays where it is, and a managed temporary is removed. */
    expect_raw(ROOT_DATA, "direct", "abc");
    assert(!crux_file_fake_exists(ROOT_DATA, ".crux-up-0"));
    assert(!crux_file_fake_exists(ROOT_DATA, "managed"));
}

static void a_managed_upload_appears_only_when_committed(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    size_t count = 99;
    put_text(t, 0, "new");

    /* It writes in order. */
    assert(crux_file_service_write(SESSION_ALL, t, 1, "x", 1, &count) ==
           CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE);
    assert(crux_file_service_write(SESSION_ALL, t, 9, "x", 1, &count) ==
           CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE);
    put_text(t, 3, "er");
    uint64_t size = 0;
    assert(crux_file_service_size(SESSION_ALL, t, &size) == CRUX_FILE_SERVICE_OK && size == 5);
    char value[4];
    assert(crux_file_service_read(SESSION_ALL, t, 0, value, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_DENIED);

    /* Nothing is visible until commit. */
    expect_raw(ROOT_WEB, "doc", "old");
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
    assert(t == NULL);
    expect_raw(ROOT_WEB, "doc", "newer");
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
    assert(events_of(CRUX_FILE_SERVICE_EVENT_CREATED) == 1);

    /* The replaced file's room came back: one entry each, and only the new size. */
    for (int i = 0; i < 4; i++) {
        char name[8];
        (void)snprintf(name, sizeof(name), "n%d", i);
        t = open_as(SESSION_ALL, "web", name, CRUX_FILE_SERVICE_OPEN_MANAGED);
        assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
    }
    assert(crux_file_service_open(SESSION_ALL, "web", "n9", CRUX_FILE_SERVICE_OPEN_MANAGED, 0,
                                  &t) == CRUX_FILE_SERVICE_ERR_LIMIT);

    /* A commit is only for a managed upload, and the direct mode has none. */
    crux_file_service_transfer_t *d =
        open_as(SESSION_ALL, "data", "d", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    assert(crux_file_service_commit(SESSION_ALL, &d) == CRUX_FILE_SERVICE_ERR_DENIED);
    assert(d != NULL);
    assert(crux_file_service_close(&d) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_commit(SESSION_ALL, NULL) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
}

static void a_commit_with_a_reader_on_the_target_changes_nothing(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    test_start();
    crux_file_service_transfer_t *reader =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_READ);
    crux_file_t *project = NULL;
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(t != NULL);
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
    expect_raw(ROOT_WEB, "doc", "old");
    assert(crux_file_service_close(&reader) == CRUX_FILE_SERVICE_OK);

    /* A reader outside the service is found the same way, before the sidecar. */
    assert(crux_file_open_at(ROOT_WEB, "doc", CRUX_FILE_READ, &project) == CRUX_FILE_OK);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(t != NULL);
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
    assert(crux_file_close(&project) == CRUX_FILE_OK);

    /* The upload is intact, so a retry finishes it. */
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
    expect_raw(ROOT_WEB, "doc", "new");
}

static void a_failed_commit_keeps_the_old_target_until_its_removal(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    test_start();
    /* The sync before anything else fails: no success, and the target stands. */
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");
    crux_file_fake_fail(CRUX_FILE_FAKE_SYNC, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    expect_raw(ROOT_WEB, "doc", "old");
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));

    /* The sidecar's sync fails: the same. */
    t = open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");
    crux_file_fake_fail(CRUX_FILE_FAKE_SYNC, 1, CRUX_FILE_ERR_IO);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    expect_raw(ROOT_WEB, "doc", "old");
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));

    /* The rename fails after the target has gone: both files stay for the next start. */
    t = open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");
    crux_file_fake_fail(CRUX_FILE_FAKE_RENAME, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    assert(!crux_file_fake_exists(ROOT_WEB, "doc"));
    assert(crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    assert(crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
    assert(events_of(CRUX_FILE_SERVICE_EVENT_COMMIT_FAILED) == 1);

    /* The slot it kept is skipped, so a new upload cannot be confused with it. */
    t = open_as(SESSION_ALL, "web", "other", CRUX_FILE_SERVICE_OPEN_MANAGED);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
    expect_raw(ROOT_WEB, "other", "");
}

/* A sidecar that cannot be written, or is written short, leaves the target and no debris. */
static void a_failed_sidecar_or_close_keeps_the_target(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");
    crux_file_fake_limit_write(2);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    crux_file_fake_limit_write(SIZE_MAX);
    assert(t == NULL);
    expect_raw(ROOT_WEB, "doc", "old");
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));

    t = open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");
    crux_file_fake_fail(CRUX_FILE_FAKE_CLOSE, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    expect_raw(ROOT_WEB, "doc", "old");
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
}

static crux_file_t *late_reader;

static void open_late_reader(void)
{
    assert(crux_file_open_at(ROOT_WEB, "doc", CRUX_FILE_READ, &late_reader) == CRUX_FILE_OK);
}

/* A reader after the first check is found at the removal, and it still changes nothing. */
static void a_reader_during_the_commit_changes_nothing(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");

    /* The only write left is the sidecar's. */
    crux_file_fake_on_write(open_late_reader);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_BUSY);
    assert(t != NULL && late_reader != NULL);
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
    expect_raw(ROOT_WEB, "doc", "old");
    assert(crux_file_close(&late_reader) == CRUX_FILE_OK);

    /* The upload goes on from where it was, and a retry finishes it. */
    put_text(t, 3, "er");
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
    assert(t == NULL);
    expect_raw(ROOT_WEB, "doc", "newer");
}

/* A provider that reports no handle at either open leaves the upload for a retry. */
static void no_handle_for_a_commit_keeps_the_upload(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");
    for (unsigned skip = 0; skip < 2; skip++) {
        crux_file_fake_fail(CRUX_FILE_FAKE_OPEN, skip, CRUX_FILE_ERR_NO_RESOURCES);
        assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_NO_RESOURCES);
        assert(t != NULL);
        expect_raw(ROOT_WEB, "doc", "old");
    }
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
    expect_raw(ROOT_WEB, "doc", "new");
}

static void no_handle_at_the_next_open(void)
{
    crux_file_fake_fail(CRUX_FILE_FAKE_OPEN, 0, CRUX_FILE_ERR_NO_RESOURCES);
}

/* With no handle for the probe or for reopening the temporary, the upload is lost and it says so.
 * This takes something else filling the pool in the moment that the commit freed a handle. */
static void no_handle_at_all_loses_the_upload_and_keeps_the_target(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");
    crux_file_fake_fail(CRUX_FILE_FAKE_OPEN, 0, CRUX_FILE_ERR_NO_RESOURCES);
    crux_file_fake_on_call(CRUX_FILE_FAKE_OPEN, 1, no_handle_at_the_next_open);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    expect_raw(ROOT_WEB, "doc", "old");
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
}

/* Only the refusals before anything changed may keep the handle, so a sync cannot report one. */
static void a_failure_that_consumes_is_never_reported_as_a_refusal(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");
    crux_file_fake_fail(CRUX_FILE_FAKE_SYNC, 0, CRUX_FILE_ERR_NO_RESOURCES);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    expect_raw(ROOT_WEB, "doc", "old");
}

/* If the temporary cannot be opened again there is nothing left to retry. */
static void a_failed_reopen_consumes_the_upload(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    test_start();
    crux_file_t *reader = NULL;
    assert(crux_file_open_at(ROOT_WEB, "doc", CRUX_FILE_READ, &reader) == CRUX_FILE_OK);
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "new");
    crux_file_fake_fail(CRUX_FILE_FAKE_OPEN, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    assert(crux_file_close(&reader) == CRUX_FILE_OK);
    expect_raw(ROOT_WEB, "doc", "old");
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
}

/* FatFs can report an error after the entry went, so a failed removal leaves the choice to start.
 */
static void removal_failed_after_the_entry_went(void)
{
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "newer");
    crux_file_fake_remove_then_fail(CRUX_FILE_ERR_IO);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    assert(!crux_file_fake_exists(ROOT_WEB, "doc"));
    assert(crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    assert(crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
    assert(events_of(CRUX_FILE_SERVICE_EVENT_COMMIT_FAILED) == 1);
}

static void removal_failed_with_the_entry_kept(void)
{
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "newer");
    crux_file_fake_fail(CRUX_FILE_FAKE_REMOVE, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    expect_raw(ROOT_WEB, "doc", "old");
    assert(crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    assert(crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
    assert(events_of(CRUX_FILE_SERVICE_EVENT_COMMIT_FAILED) == 1);
}

static void next_start_finds_the_new_file(void)
{
    test_start();
    expect_raw(ROOT_WEB, "doc", "newer");
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
}

static void next_start_finds_the_old_file(void)
{
    test_start();
    expect_raw(ROOT_WEB, "doc", "old");
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
}

static void a_failed_removal_is_decided_at_the_next_start(void)
{
    test_medium_reset();
    put_raw(ROOT_WEB, "doc", "old");
    assert(run_child(removal_failed_after_the_entry_went) == 0);
    crux_file_fake_power_on();
    assert(run_child(next_start_finds_the_new_file) == 0);

    test_medium_reset();
    put_raw(ROOT_WEB, "doc", "old");
    assert(run_child(removal_failed_with_the_entry_kept) == 0);
    crux_file_fake_power_on();
    assert(run_child(next_start_finds_the_old_file) == 0);
}

/* A close that cannot remove its temporary says so, and the slot stays out of use. */
static void a_failed_abort_is_reported_and_its_slot_skipped(void)
{
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "abc");
    crux_file_fake_fail(CRUX_FILE_FAKE_REMOVE, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    assert(crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    t = open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    assert(crux_file_fake_exists(ROOT_WEB, ".crux-up-1"));
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

static void an_abort_removes_the_temporary_and_gives_back_its_room(void)
{
    test_start();
    for (int i = 0; i < 20; i++) {
        crux_file_service_transfer_t *t =
            open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
        put_text(t, 0, "abcd");
        assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
    }
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    assert(!crux_file_fake_exists(ROOT_WEB, "doc"));
    /* Every entry is still free. */
    for (int i = 0; i < 5; i++) {
        char name[8];
        (void)snprintf(name, sizeof(name), "n%d", i);
        crux_file_service_transfer_t *t =
            open_as(SESSION_ALL, "web", name, CRUX_FILE_SERVICE_OPEN_MANAGED);
        assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
    }
}

/* A close error is passed on, and the handle is consumed all the same. */
static void a_close_error_is_passed_on(void)
{
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE);
    crux_file_fake_fail(CRUX_FILE_FAKE_CLOSE, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_ERR_IO);
    assert(t == NULL);
    t = open_as(SESSION_ALL, "data", "f", CRUX_FILE_SERVICE_OPEN_DIRECT);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);
}

void test_transfers(void)
{
    run_case("listing", a_listing_shows_only_what_a_client_may_name);
    run_case("reads", reads_follow_their_offset);
    run_case("direct_modes", direct_modes_match_the_sftp_flags);
    run_case("rename_remove", a_rename_and_a_remove_report_what_they_did);
    run_case("ownership", a_transfer_belongs_to_its_session);
    run_case("session_ends", a_session_that_ends_mid_transfer);
    run_case("managed", a_managed_upload_appears_only_when_committed);
    run_case("commit_reader", a_commit_with_a_reader_on_the_target_changes_nothing);
    run_case("failed_commit", a_failed_commit_keeps_the_old_target_until_its_removal);
    run_case("abort", an_abort_removes_the_temporary_and_gives_back_its_room);
    run_case("failed_sidecar", a_failed_sidecar_or_close_keeps_the_target);
    run_case("late_reader", a_reader_during_the_commit_changes_nothing);
    run_case("no_handle", no_handle_for_a_commit_keeps_the_upload);
    run_case("failed_reopen", a_failed_reopen_consumes_the_upload);
    run_case("no_handle_at_all", no_handle_at_all_loses_the_upload_and_keeps_the_target);
    run_case("consuming_failure", a_failure_that_consumes_is_never_reported_as_a_refusal);
    run_case("failed_abort", a_failed_abort_is_reported_and_its_slot_skipped);
    run_case("close_error", a_close_error_is_passed_on);
}

static const char *upload_content;
static uint32_t cut_point;

/* Arm the cut after start, so that it lands inside the upload and not in a recovery. */
static void upload_and_commit(void)
{
    test_start();
    crux_file_fake_cut_after(cut_point, cut_now);
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, upload_content);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
}

static bool temporaries_remain(void)
{
    for (int slot = 0; slot < CRUX_FILE_SERVICE_TEMP_SLOTS; slot++) {
        char name[16];
        (void)snprintf(name, sizeof(name), ".crux-up-%d", slot);
        if (crux_file_fake_exists(ROOT_WEB, name)) {
            return true;
        }
        (void)snprintf(name, sizeof(name), ".crux-co-%d", slot);
        if (crux_file_fake_exists(ROOT_WEB, name)) {
            return true;
        }
    }
    return false;
}

static const char *const old_content = "old";

/* A different size means start can tell the removal had not begun, so it decides every state. */
static void after_a_cut_with_a_different_size(void)
{
    test_start();
    /* It holds the whole old file or the whole new one, and nothing is left over. */
    char value[32];
    size_t count = get_raw(ROOT_WEB, "doc", value, sizeof(value));
    value[count] = '\0';
    assert(strcmp(value, old_content) == 0 || strcmp(value, upload_content) == 0);
    assert(!temporaries_remain());
    assert(events_of(CRUX_FILE_SERVICE_EVENT_RECOVERY_NEEDED) == 0);

    /* The counters match what is there: four files, so one more fits and a sixth does not. */
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "more", CRUX_FILE_SERVICE_OPEN_MANAGED);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_open(SESSION_ALL, "web", "last", CRUX_FILE_SERVICE_OPEN_MANAGED, 0,
                                  &t) == CRUX_FILE_SERVICE_ERR_LIMIT);
}

/*
 * The same walk with an upload of the old file's size. Removing such a temporary could free
 * clusters it shares with the target, so a state start cannot tell apart stays for a host repair.
 * This is a model of the sequence, not of FAT.
 */
static bool saw_kept;

static void after_a_cut_with_the_same_size(void)
{
    test_start();
    char value[32];
    size_t count = get_raw(ROOT_WEB, "doc", value, sizeof(value));
    value[count] = '\0';
    assert(strcmp(value, old_content) == 0 || strcmp(value, upload_content) == 0);
    unsigned kept = events_of(CRUX_FILE_SERVICE_EVENT_RECOVERY_NEEDED);
    assert(kept <= 1);
    if (kept == 1) {
        assert(strcmp(events_get(events_count() - 1)->area, "web") == 0);
        expect_raw(ROOT_WEB, ".crux-up-0", upload_content);

        /* The kept temporary and its sidecar count against the limit: four files and these two. */
        crux_file_service_transfer_t *t = NULL;
        assert(crux_file_service_open(SESSION_ALL, "web", "more", CRUX_FILE_SERVICE_OPEN_MANAGED, 0,
                                      &t) == CRUX_FILE_SERVICE_ERR_LIMIT);
        assert(crux_file_service_remove(SESSION_ALL, "web", "f1") == CRUX_FILE_SERVICE_OK);
        assert(crux_file_service_remove(SESSION_ALL, "web", "f2") == CRUX_FILE_SERVICE_OK);

        /* Its slot is skipped, so the new upload takes the next one and leaves the kept files. */
        t = open_as(SESSION_ALL, "web", "more", CRUX_FILE_SERVICE_OPEN_MANAGED);
        assert(crux_file_fake_exists(ROOT_WEB, ".crux-up-1"));
        put_text(t, 0, "abcd");
        assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_OK);
        expect_raw(ROOT_WEB, "more", "abcd");
        expect_raw(ROOT_WEB, ".crux-up-0", upload_content);
        assert(crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
        _exit(CUT_EXIT + 1);
    }
    assert(!temporaries_remain());
}

static void walk_cuts(const char *content, void (*check)(void), bool *any_kept)
{
    upload_content = content;
    for (cut_point = 0;; cut_point++) {
        test_medium_reset();
        put_raw(ROOT_WEB, "doc", old_content);
        put_raw(ROOT_WEB, "f1", "");
        put_raw(ROOT_WEB, "f2", "");
        put_raw(ROOT_WEB, "f3", "");
        int code = run_child(upload_and_commit);
        crux_file_fake_power_on();
        int checked = run_child(check);
        assert(checked == 0 || (any_kept && checked == CUT_EXIT + 1));
        if (any_kept && checked == CUT_EXIT + 1) {
            *any_kept = true;
        }
        if (code == 0) {
            break;
        }
        assert(code == CUT_EXIT);
    }
    /* Create, truncate, write, sync, sidecar create, truncate, write, sync, remove, rename, remove.
     */
    assert(cut_point == 11);
}

/* Cut the recovery itself: the next start must still reach a consistent state. */
static uint32_t recovery_cut;

static void leave_a_half_commit(void)
{
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    put_text(t, 0, "newer");
    /* Sync, the sidecar's create, truncate, write and sync, then the removal of the target. */
    crux_file_fake_cut_after(6, cut_now);
    (void)crux_file_service_commit(SESSION_ALL, &t);
    _exit(1);
}

static void start_with_a_cut(void)
{
    crux_file_fake_cut_after(recovery_cut, cut_now);
    test_start();
}

static void after_recovery_cut(void)
{
    test_start();
    expect_raw(ROOT_WEB, "doc", "newer");
    assert(!temporaries_remain());
}

static void recovery_survives_its_own_cut(void)
{
    for (recovery_cut = 0;; recovery_cut++) {
        test_medium_reset();
        put_raw(ROOT_WEB, "doc", old_content);
        assert(run_child(leave_a_half_commit) == CUT_EXIT);
        crux_file_fake_power_on();
        assert(!crux_file_fake_exists(ROOT_WEB, "doc"));
        int code = run_child(start_with_a_cut);
        crux_file_fake_power_on();
        assert(run_child(after_recovery_cut) == 0);
        if (code == 0) {
            break;
        }
        assert(code == CUT_EXIT);
    }
    assert(recovery_cut == 2); /* The rename and the sidecar's removal. */
}

static void start_with_an_unreadable_sidecar(void)
{
    crux_file_fake_fail(CRUX_FILE_FAKE_READ, 0, CRUX_FILE_ERR_IO);
    test_start();
    crux_file_info_t info;
    assert(crux_file_service_stat(SESSION_ALL, "web", "doc", &info) == CRUX_FILE_SERVICE_ERR_IO);
    assert(crux_file_fake_exists(ROOT_WEB, ".crux-up-0"));
    assert(crux_file_fake_exists(ROOT_WEB, ".crux-co-0"));
}

/* A read error is not a torn record. Here the target has gone, so the temporary is the only copy.
 */
static void an_unreadable_sidecar_keeps_the_upload(void)
{
    test_medium_reset();
    put_raw(ROOT_WEB, "doc", old_content);
    assert(run_child(leave_a_half_commit) == CUT_EXIT);
    crux_file_fake_power_on();
    assert(run_child(start_with_an_unreadable_sidecar) == 0);
    assert(run_child(after_recovery_cut) == 0);
}

/* A sidecar that holds no valid record means the removal never began. */
static void a_torn_sidecar_is_only_debris(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    put_raw(ROOT_WEB, ".crux-up-0", "new");
    put_raw(ROOT_WEB, ".crux-co-0", "");
    put_raw(ROOT_WEB, ".crux-up-1", "new");
    put_raw(ROOT_WEB, ".crux-co-1", "torn data");
    put_raw(ROOT_WEB, ".crux-co-2", "no temporary with this one");
    test_start();
    assert(!temporaries_remain());
    expect_raw(ROOT_WEB, "doc", "old");
    assert(events_of(CRUX_FILE_SERVICE_EVENT_RECOVERY_NEEDED) == 0);
}

static void put_bytes(crux_file_root_t root, const char *name, const void *bytes, size_t length)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    assert(crux_file_open_at(root, name, CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    assert(crux_file_write(file, bytes, length, &count) == CRUX_FILE_OK && count == length);
    assert(crux_file_sync(file) == CRUX_FILE_OK);
    assert(crux_file_close(&file) == CRUX_FILE_OK);
}

/* Read back what a commit wrote, as the medium holds it. */
static uint8_t real_sidecar[80];
static size_t real_sidecar_length;

static void capture_a_sidecar(void)
{
    test_start();
    crux_file_service_transfer_t *t =
        open_as(SESSION_ALL, "web", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED);
    /* Stop before the rename, so that the sidecar is still there. */
    crux_file_fake_fail(CRUX_FILE_FAKE_RENAME, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_service_commit(SESSION_ALL, &t) == CRUX_FILE_SERVICE_ERR_IO);
}

/* A sidecar that looks right but fails its check, or is cut short, must not rename anything. */
static void damaged_sidecar_start(void)
{
    test_start();
    assert(!crux_file_fake_exists(ROOT_WEB, "doc"));
    assert(!temporaries_remain());
}

static void a_sidecar_with_its_magic_but_no_valid_check_is_torn(void)
{
    test_medium_reset();
    assert(run_child(capture_a_sidecar) == 0);
    char value[sizeof(real_sidecar)];
    real_sidecar_length = get_raw(ROOT_WEB, ".crux-co-0", value, sizeof(value));
    assert(real_sidecar_length > 5 && memcmp(value, "CXCO", 4) == 0);
    memcpy(real_sidecar, value, real_sidecar_length);

    /* The undamaged record completes the rename, so the test below is about the damage alone. */
    assert(run_child(test_start) == 0);
    assert(crux_file_fake_exists(ROOT_WEB, "doc"));

    uint8_t damaged[sizeof(real_sidecar)];
    memcpy(damaged, real_sidecar, real_sidecar_length);
    damaged[real_sidecar_length - 1] ^= 0xff;
    test_medium_reset();
    put_raw(ROOT_WEB, ".crux-up-0", "new");
    put_bytes(ROOT_WEB, ".crux-co-0", damaged, real_sidecar_length);
    assert(run_child(damaged_sidecar_start) == 0);

    test_medium_reset();
    put_raw(ROOT_WEB, ".crux-up-0", "new");
    put_bytes(ROOT_WEB, ".crux-co-0", real_sidecar, real_sidecar_length - 1);
    assert(run_child(damaged_sidecar_start) == 0);

    /* A target name the client rules refuse would reach outside what a client may name. */
    test_medium_reset();
    uint8_t reserved[] = {'C', 'X', 'C', 'O', 6, '.', 'c', 'r', 'u', 'x', '-', 0};
    uint8_t sum = 0x5a;
    for (size_t i = 0; i < sizeof(reserved) - 1; i++) {
        sum = (uint8_t)(sum + reserved[i]);
    }
    reserved[sizeof(reserved) - 1] = sum;
    put_raw(ROOT_WEB, ".crux-up-0", "new");
    put_bytes(ROOT_WEB, ".crux-co-0", reserved, sizeof(reserved));
    assert(run_child(damaged_sidecar_start) == 0);
}

/* A temporary with no sidecar: a cut during the upload, or before the sidecar reached the medium.
 */
static void a_temporary_with_no_sidecar_is_removed(void)
{
    put_raw(ROOT_WEB, "doc", "old");
    put_raw(ROOT_WEB, ".crux-up-3", "partial");
    test_start();
    assert(!crux_file_fake_exists(ROOT_WEB, ".crux-up-3"));
    expect_raw(ROOT_WEB, "doc", "old");
}

/* F5: a client may resume a partial file, so a restart keeps it. */
static void partials_survive_a_restart(void)
{
    put_raw(ROOT_DATA, "up.filepart", "partial");
    test_start();
    expect_raw(ROOT_DATA, "up.filepart", "partial");
    assert(events_of(CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED) == 0);
}

/* A recovery that cannot finish stops only its own area, and the next start tries again. */
static void a_failed_recovery_marks_the_area(void)
{
    put_raw(ROOT_WEB, ".crux-up-0", "new");
    put_raw(ROOT_WEB, ".crux-co-0", "garbage");
    crux_file_fake_fail(CRUX_FILE_FAKE_REMOVE, 0, CRUX_FILE_ERR_IO);
    test_start();
    crux_file_info_t info;
    assert(crux_file_service_stat(SESSION_ALL, "web", "doc", &info) == CRUX_FILE_SERVICE_ERR_IO);
    assert(crux_file_service_stat(SESSION_ALL, "data", "doc", &info) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
}

void test_interrupted(void)
{
    walk_cuts("newer", after_a_cut_with_a_different_size, NULL);
    saw_kept = false;
    walk_cuts("new", after_a_cut_with_the_same_size, &saw_kept);
    assert(saw_kept);
    puts("Interrupted commits: replacement is not atomic across power loss. On the fake's model a "
         "different size ends with the whole old or new file, and the same size can keep a "
         "temporary for a host repair. This says nothing about FAT or a card.");
    recovery_survives_its_own_cut();
    an_unreadable_sidecar_keeps_the_upload();
    a_failed_removal_is_decided_at_the_next_start();
    a_sidecar_with_its_magic_but_no_valid_check_is_torn();
    run_case("torn_sidecar", a_torn_sidecar_is_only_debris);
    run_case("no_sidecar", a_temporary_with_no_sidecar_is_removed);
    run_case("partials", partials_survive_a_restart);
    run_case("failed_recovery", a_failed_recovery_marks_the_area);
}
