#include "service_test.h"
#include <stdio.h>

static void start_refuses_bad_tables(void)
{
    crux_file_area_t good = {"good", 0, "r", "w", 10, 10, 1, NULL, CRUX_FILE_SERVICE_DIRECT};
    crux_file_root_t store = 7;
    crux_file_service_config_t config = {&good, 1, &store, 1, false};

    assert(crux_file_service_start(NULL) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);

#define REFUSED(field, value)                                                                      \
    do {                                                                                           \
        crux_file_area_t bad = good;                                                               \
        bad.field = value;                                                                         \
        config.areas = &bad;                                                                       \
        assert(crux_file_service_start(&config) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);        \
        config.areas = &good;                                                                      \
    } while (0)

    REFUSED(name, NULL);
    REFUSED(name, "");
    REFUSED(name, "Upper");
    REFUSED(name, "has/slash");
    REFUSED(name, "sixteen-letters-");
    REFUSED(read_permission, NULL);
    REFUSED(read_permission, "");
    REFUSED(write_permission, "");
    REFUSED(root, 7); /* A store root. */
    REFUSED(max_file_bytes, 0);
    REFUSED(max_file_bytes, (uint64_t)INT32_MAX + 1);
    REFUSED(max_area_bytes, 0);
    REFUSED(max_entries, 0);
    REFUSED(write_modes, 0);              /* Writable with no mode. */
    REFUSED(write_modes, 4);              /* An unknown mode. */
    REFUSED(partial_suffix, "");          /* Would match every file. */
    REFUSED(partial_suffix, ".crux-up-"); /* The service's own files. */
#undef REFUSED

    /* A read-only area offers no write mode. */
    crux_file_area_t read_only = good;
    read_only.write_permission = NULL;
    config.areas = &read_only;
    assert(crux_file_service_start(&config) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    read_only.write_modes = 0;
    config.areas = &good;

    /* Duplicates. */
    crux_file_area_t twins[2] = {good, good};
    twins[1].root = 1;
    config.areas = twins;
    config.area_count = 2;
    assert(crux_file_service_start(&config) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    twins[1].name = "other";
    twins[1].root = 0;
    assert(crux_file_service_start(&config) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    config.areas = &good;
    config.area_count = 1;

    /* An empty table, and an empty reserved list that nothing vouches for. */
    config.area_count = 0;
    assert(crux_file_service_start(&config) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    config.area_count = 1;
    config.reserved_root_count = 0;
    assert(crux_file_service_start(&config) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);

    /* Fails closed: nothing works after a refused table. */
    const crux_file_area_t *area = NULL;
    size_t cursor = 0;
    bool writable = false;
    crux_file_info_t info;
    char name[64];
    assert(crux_file_service_resolve("/good", &area, name, sizeof(name)) ==
           CRUX_FILE_SERVICE_ERR_NOT_READY);
    assert(crux_file_service_area_next(SESSION_ALL, &cursor, &area, &writable) ==
           CRUX_FILE_SERVICE_ERR_NOT_READY);
    assert(crux_file_service_stat(SESSION_ALL, "good", "x", &info) ==
           CRUX_FILE_SERVICE_ERR_NOT_READY);

    /* A table that vouches for having no store starts, and only once. */
    config.no_credential_store = true;
    assert(crux_file_service_start(&config) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_start(&config) == CRUX_FILE_SERVICE_ERR_EXISTS);
}

static void resolve_accepts_only_the_two_shapes(void)
{
    test_start();
    const crux_file_area_t *area = NULL;
    char name[64];

    assert(crux_file_service_resolve("/data", &area, name, sizeof(name)) == CRUX_FILE_SERVICE_OK);
    assert(strcmp(area->name, "data") == 0 && name[0] == '\0');
    assert(crux_file_service_resolve("/data/", &area, name, sizeof(name)) == CRUX_FILE_SERVICE_OK);
    assert(strcmp(area->name, "data") == 0 && name[0] == '\0');
    assert(crux_file_service_resolve("/web/a.txt", &area, name, sizeof(name)) ==
           CRUX_FILE_SERVICE_OK);
    assert(strcmp(area->name, "web") == 0 && strcmp(name, "a.txt") == 0);

    assert(crux_file_service_resolve("/missing", &area, name, sizeof(name)) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(crux_file_service_resolve("/missing/file", &area, name, sizeof(name)) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);

    const char *refused[] = {
        NULL,
        "",
        "/",
        "data",
        "data/file",
        "//data",
        "/data//",
        "/data//f",
        "/data/f/",
        "/data/f/g",
        "/./data",
        "/../data",
        "/data/.",
        "/data/..",
        "/data/../x",
        "/data\\f",
        "\\data\\f",
        "/data/a b",
        "/data/a\tb",
        "/data/a\nb",
        "/data/\x7f",
        "/data/\xc3\xa9",
        "/data/.crux-up-0",
        "/data/.crux-co-0",
        "/data/.crux-anything",
    };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        area = NULL;
        name[0] = 'x';
        assert(crux_file_service_resolve(refused[i], &area, name, sizeof(name)) ==
               CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
        assert(area == NULL);
    }

    /* The longest area and name make 80 bytes, which is the bound on a whole path. */
    char path[128];
    memset(path, 'a', sizeof(path));
    memcpy(path, "/data/", 6);
    path[80] = '\0';
    assert(crux_file_service_resolve(path, &area, name, sizeof(name)) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT); /* Its name is over 63 bytes. */
    path[69] = '\0';
    assert(crux_file_service_resolve(path, &area, name, sizeof(name)) == CRUX_FILE_SERVICE_OK);
    assert(strlen(name) == 63);
    path[69] = 'a';
    path[80] = 'a';
    path[81] = '\0';
    assert(crux_file_service_resolve(path, &area, name, sizeof(name)) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);

    /* Output checks. */
    assert(crux_file_service_resolve("/data/f", NULL, name, sizeof(name)) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    assert(crux_file_service_resolve("/data/f", &area, NULL, sizeof(name)) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    assert(crux_file_service_resolve("/data/f", &area, name, 63) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);

    /* It touches no storage. */
    unsigned stats = crux_file_fake_calls(CRUX_FILE_FAKE_STAT);
    unsigned reads = crux_file_fake_calls(CRUX_FILE_FAKE_DIR_READ);
    assert(crux_file_service_resolve("/data/f", &area, name, sizeof(name)) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_fake_calls(CRUX_FILE_FAKE_STAT) == stats);
    assert(crux_file_fake_calls(CRUX_FILE_FAKE_DIR_READ) == reads);
}

/* A refused name is a named result and an event that holds only the area. */
static void rejected_names_are_never_recorded(void)
{
    test_start();
    crux_file_info_t info;
    crux_file_service_transfer_t *transfer = NULL;
    const char *names[] = {"s3cret word", "../s3cret", ".crux-up-0", ".crux-s3cret", "s3cret/x"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        assert(crux_file_service_stat(SESSION_ALL, "data", names[i], &info) ==
               CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
        assert(crux_file_service_remove(SESSION_ALL, "data", names[i]) ==
               CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
        assert(crux_file_service_rename(SESSION_ALL, "data", names[i], "ok") ==
               CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
        assert(crux_file_service_rename(SESSION_ALL, "data", "ok", names[i]) ==
               CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
        assert(crux_file_service_open(SESSION_ALL, "data", names[i],
                                      CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE, 0,
                                      &transfer) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
        assert(transfer == NULL);
    }
    assert(events_of(CRUX_FILE_SERVICE_EVENT_NAME_REJECTED) == 25);
    for (unsigned i = 0; i < events_count(); i++) {
        assert(strcmp(events_get(i)->area, "data") == 0);
    }
    assert(!events_mention("s3cret"));
    assert(!events_mention("crux"));
}

/* What a session may see comes from a quiet probe, and any real attempt is what gets recorded. */
static void hidden_areas_look_missing_and_only_attempts_are_recorded(void)
{
    test_start();
    const crux_file_area_t *area = NULL;
    size_t cursor = 0;
    bool writable = false;

    /* Neither permission on anything: nothing listed, and nothing denied. */
    assert(crux_file_service_area_next(SESSION_NONE, &cursor, &area, &writable) ==
           CRUX_FILE_SERVICE_END);
    assert(fake_access_denials() == 0 && fake_access_probes() > 0);

    cursor = 0;
    assert(crux_file_service_area_next(SESSION_READ, &cursor, &area, &writable) ==
           CRUX_FILE_SERVICE_OK);
    assert(strcmp(area->name, "data") == 0 && !writable);
    assert(crux_file_service_area_next(SESSION_READ, &cursor, &area, &writable) ==
           CRUX_FILE_SERVICE_OK);
    assert(strcmp(area->name, "logs") == 0 && !writable);
    assert(crux_file_service_area_next(SESSION_READ, &cursor, &area, &writable) ==
           CRUX_FILE_SERVICE_OK);
    assert(strcmp(area->name, "web") == 0 && !writable);
    /* An area whose root is not ready is still one this session may see. */
    assert(crux_file_service_area_next(SESSION_READ, &cursor, &area, &writable) ==
           CRUX_FILE_SERVICE_OK);
    assert(strcmp(area->name, "gone") == 0 && !writable);
    assert(crux_file_service_area_next(SESSION_READ, &cursor, &area, &writable) ==
           CRUX_FILE_SERVICE_END);
    assert(crux_file_service_area_next(SESSION_READ, &cursor, &area, &writable) ==
           CRUX_FILE_SERVICE_END);

    /* A write-only session sees the area and is told it may write. */
    cursor = 0;
    assert(crux_file_service_area_next(SESSION_WRITE, &cursor, &area, &writable) ==
           CRUX_FILE_SERVICE_OK);
    assert(strcmp(area->name, "data") == 0 && writable);
    assert(fake_access_denials() == 0);

    cursor = 0;
    assert(crux_file_service_area_next(SESSION_ENDED, &cursor, &area, &writable) ==
           CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
    assert(crux_file_service_area_next(SESSION_ALL, NULL, &area, &writable) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);

    /* A real attempt on a hidden area is NOT_FOUND, like a missing one, and it is recorded. */
    crux_file_info_t info;
    assert(crux_file_service_stat(SESSION_NONE, "data", "x", &info) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(fake_access_denials() == 1);
    assert(crux_file_service_stat(SESSION_NONE, "missing", "x", &info) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(fake_access_denials() == 1);
}

static crux_file_service_result_t try_list(crux_access_session_t session, const char *area)
{
    crux_file_service_dir_t *dir = NULL;
    crux_file_service_result_t result = crux_file_service_dir_open(session, area, &dir);
    if (dir) {
        assert(crux_file_service_dir_close(&dir) == CRUX_FILE_SERVICE_OK);
    }
    return result;
}

static crux_file_service_result_t try_open(crux_access_session_t session, const char *area,
                                           const char *name, crux_file_service_open_t mode)
{
    crux_file_service_transfer_t *transfer = NULL;
    crux_file_service_result_t result =
        crux_file_service_open(session, area, name, mode, 0, &transfer);
    if (transfer) {
        assert(crux_file_service_close(&transfer) == CRUX_FILE_SERVICE_OK);
    }
    return result;
}

static void every_operation_checks_the_session_and_the_permission(void)
{
    put_raw(ROOT_DATA, "file", "abc");
    put_raw(ROOT_LOGS, "log", "abc");
    test_start();
    crux_file_info_t info;

    /* No session and an ended one. */
    const crux_access_session_t invalid[] = {{0}, SESSION_ENDED};
    for (size_t i = 0; i < 2; i++) {
        crux_access_session_t s = invalid[i];
        assert(try_list(s, "data") == CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
        assert(crux_file_service_stat(s, "data", "file", &info) ==
               CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
        assert(crux_file_service_remove(s, "data", "file") ==
               CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
        assert(crux_file_service_rename(s, "data", "file", "to") ==
               CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
        assert(try_open(s, "data", "file", CRUX_FILE_SERVICE_OPEN_READ) ==
               CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
        assert(try_open(s, "data", "file", CRUX_FILE_SERVICE_OPEN_DIRECT) ==
               CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
        /* Even for an area that does not exist. */
        assert(crux_file_service_stat(s, "missing", "file", &info) ==
               CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
    }

    /* Read-only: reading works, and writing is refused without hiding the area. */
    assert(try_list(SESSION_READ, "data") == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_stat(SESSION_READ, "data", "file", &info) == CRUX_FILE_SERVICE_OK);
    assert(try_open(SESSION_READ, "data", "file", CRUX_FILE_SERVICE_OPEN_READ) ==
           CRUX_FILE_SERVICE_OK);
    assert(fake_access_denials() == 0);
    assert(crux_file_service_remove(SESSION_READ, "data", "file") == CRUX_FILE_SERVICE_ERR_DENIED);
    assert(crux_file_service_rename(SESSION_READ, "data", "file", "to") ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    assert(try_open(SESSION_READ, "data", "file", CRUX_FILE_SERVICE_OPEN_DIRECT) ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    assert(try_open(SESSION_READ, "data", "new", CRUX_FILE_SERVICE_OPEN_MANAGED) ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    assert(fake_access_denials() == 4);

    /* Write-only: the opposite. */
    assert(crux_file_service_stat(SESSION_WRITE, "data", "file", &info) ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    assert(try_list(SESSION_WRITE, "data") == CRUX_FILE_SERVICE_ERR_DENIED);
    assert(try_open(SESSION_WRITE, "data", "file", CRUX_FILE_SERVICE_OPEN_READ) ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    assert(try_open(SESSION_WRITE, "data", "file", CRUX_FILE_SERVICE_OPEN_DIRECT) ==
           CRUX_FILE_SERVICE_OK);
    assert(fake_access_denials() == 7);

    /* Neither: every operation sees nothing there. */
    assert(try_list(SESSION_NONE, "data") == CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(crux_file_service_stat(SESSION_NONE, "data", "file", &info) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(crux_file_service_remove(SESSION_NONE, "data", "file") ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(crux_file_service_rename(SESSION_NONE, "data", "file", "to") ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(try_open(SESSION_NONE, "data", "file", CRUX_FILE_SERVICE_OPEN_READ) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);

    /* A read-only area: writing is refused for someone who can read it. */
    assert(crux_file_service_remove(SESSION_ALL, "logs", "log") == CRUX_FILE_SERVICE_ERR_DENIED);
    assert(try_open(SESSION_ALL, "logs", "log", CRUX_FILE_SERVICE_OPEN_DIRECT) ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    assert(try_open(SESSION_NONE, "logs", "log", CRUX_FILE_SERVICE_OPEN_DIRECT) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    expect_raw(ROOT_LOGS, "log", "abc");

    /* A write-only session's direct transfer writes and reports its size, and it cannot read. */
    crux_file_service_transfer_t *t = NULL;
    size_t count = 0;
    uint64_t size = 0;
    char value[4];
    assert(crux_file_service_open(SESSION_WRITE, "data", "file", CRUX_FILE_SERVICE_OPEN_DIRECT, 0,
                                  &t) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_write(SESSION_WRITE, t, 0, "x", 1, &count) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_size(SESSION_WRITE, t, &size) == CRUX_FILE_SERVICE_OK && size == 3);
    assert(crux_file_service_read(SESSION_WRITE, t, 0, value, 1, &count) ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);

    /* A read-only session reports the size of what it reads. */
    t = NULL;
    assert(crux_file_service_open(SESSION_READ, "data", "file", CRUX_FILE_SERVICE_OPEN_READ, 0,
                                  &t) == CRUX_FILE_SERVICE_OK);
    assert(crux_file_service_size(SESSION_READ, t, &size) == CRUX_FILE_SERVICE_OK && size == 3);
    assert(crux_file_service_close(&t) == CRUX_FILE_SERVICE_OK);

    /* A mode the area does not offer. */
    assert(try_open(SESSION_ALL, "web", "x", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE) ==
           CRUX_FILE_SERVICE_ERR_DENIED);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): invalid-input probe
    crux_file_service_open_t unknown_mode = (crux_file_service_open_t)99;
    assert(try_open(SESSION_ALL, "data", "x", unknown_mode) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
}

/* The session may see the area before it learns that the root is missing. */
static void an_unbound_root_only_stops_its_own_area(void)
{
    put_raw(ROOT_DATA, "file", "abc");
    test_start();
    crux_file_info_t info;
    assert(crux_file_service_stat(SESSION_ALL, "gone", "x", &info) ==
           CRUX_FILE_SERVICE_ERR_NOT_READY);
    assert(try_list(SESSION_ALL, "gone") == CRUX_FILE_SERVICE_ERR_NOT_READY);
    assert(try_open(SESSION_ALL, "gone", "x", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE) ==
           CRUX_FILE_SERVICE_ERR_NOT_READY);
    assert(crux_file_service_stat(SESSION_NONE, "gone", "x", &info) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    assert(crux_file_service_stat(SESSION_ALL, "data", "file", &info) == CRUX_FILE_SERVICE_OK);
}

/* A scan that fails marks its area until a reset, whatever the medium does afterwards. */
static void a_failed_scan_stops_only_its_area_until_a_reset(void)
{
    put_raw(ROOT_DATA, "file", "abc");
    put_raw(ROOT_LOGS, "log", "abc");
    crux_file_fake_fail(CRUX_FILE_FAKE_DIR_READ, 0, CRUX_FILE_ERR_IO);
    test_start();
    crux_file_info_t info;
    assert(crux_file_service_stat(SESSION_ALL, "data", "file", &info) == CRUX_FILE_SERVICE_ERR_IO);
    assert(crux_file_service_stat(SESSION_ALL, "data", "file", &info) == CRUX_FILE_SERVICE_ERR_IO);
    assert(try_open(SESSION_ALL, "data", "new", CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE) ==
           CRUX_FILE_SERVICE_ERR_IO);
    assert(!crux_file_fake_exists(ROOT_DATA, "new"));
    assert(crux_file_service_stat(SESSION_ALL, "logs", "log", &info) == CRUX_FILE_SERVICE_OK);
}

/* Service names and File names must not drift apart, because the service keeps its own copy of the
 * rule so that resolve touches no storage. */
static void the_name_rule_matches_files(void)
{
    test_start();
    for (int c = 1; c < 256; c++) {
        char name[3] = {'a', (char)c, '\0'};
        char path[16];
        char out[64];
        const crux_file_area_t *area = NULL;
        crux_file_info_t info;
        (void)snprintf(path, sizeof(path), "/data/%s", name);
        bool service =
            crux_file_service_resolve(path, &area, out, sizeof(out)) == CRUX_FILE_SERVICE_OK;
        bool file = crux_file_stat(ROOT_DATA, name, &info) != CRUX_FILE_ERR_INVALID_ARGUMENT;
        assert(service == file);
    }
    char longest[66];
    memset(longest, 'a', sizeof(longest));
    longest[63] = '\0';
    crux_file_info_t info;
    assert(crux_file_stat(ROOT_DATA, longest, &info) == CRUX_FILE_ERR_NOT_FOUND);
    assert(crux_file_service_stat(SESSION_ALL, "data", longest, &info) ==
           CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    longest[63] = 'a';
    longest[64] = '\0';
    assert(crux_file_stat(ROOT_DATA, longest, &info) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    assert(crux_file_service_stat(SESSION_ALL, "data", longest, &info) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
}

/* The longest area and the longest name make 80 bytes, the bound on a whole path. */
static void the_longest_legal_path_resolves(void)
{
    static const crux_file_area_t area = {
        "fifteen-letters", ROOT_DATA, "data.read", NULL, 10, 10, 1, NULL, 0};
    static const crux_file_service_config_t table = {&area, 1, NULL, 0, true};
    assert(crux_file_service_start(&table) == CRUX_FILE_SERVICE_OK);
    char path[82];
    memset(path, 'n', sizeof(path));
    memcpy(path, "/fifteen-letters/", 17);
    path[80] = '\0';
    const crux_file_area_t *found = NULL;
    char name[64];
    assert(crux_file_service_resolve(path, &found, name, sizeof(name)) == CRUX_FILE_SERVICE_OK);
    assert(found == &area && strlen(name) == 63);
    path[80] = 'n';
    path[81] = '\0';
    assert(crux_file_service_resolve(path, &found, name, sizeof(name)) ==
           CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
}

void test_paths(void)
{
    run_case("start_refuses_bad_tables", start_refuses_bad_tables);
    run_case("resolve_accepts_only_the_two_shapes", resolve_accepts_only_the_two_shapes);
    run_case("rejected_names_are_never_recorded", rejected_names_are_never_recorded);
    run_case("name_rule", the_name_rule_matches_files);
    run_case("longest_path", the_longest_legal_path_resolves);
}

void test_permissions(void)
{
    run_case("hidden_areas", hidden_areas_look_missing_and_only_attempts_are_recorded);
    run_case("every_operation", every_operation_checks_the_session_and_the_permission);
    run_case("unbound_root", an_unbound_root_only_stops_its_own_area);
    run_case("failed_scan", a_failed_scan_stops_only_its_area_until_a_reset);
}
