#include "fake_file.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CUT_EXIT 77

static void put(const char *name, const char *text)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    assert(crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    assert(crux_file_write(file, text, strlen(text), &count) == CRUX_FILE_OK);
    assert(crux_file_sync(file) == CRUX_FILE_OK);
    assert(crux_file_close(&file) == CRUX_FILE_OK);
}

static void expect(const char *name, const char *text)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[32] = {0};
    assert(crux_file_open(name, CRUX_FILE_READ, &file) == CRUX_FILE_OK);
    assert(crux_file_read(file, value, sizeof(value), &count) == CRUX_FILE_OK);
    assert(count == strlen(text) && memcmp(value, text, count) == 0);
    assert(crux_file_close(&file) == CRUX_FILE_OK);
}

static void start(void)
{
    crux_file_fake_reset();
    assert(crux_file_fake_bind_root(0, "/volume/a") == CRUX_FILE_OK);
}

/* Failed operations pass their result on and leave the core's pools usable. */
static void test_provider_failures_pass_through(void)
{
    start();
    crux_file_info_t info;
    crux_file_dir_t *dir = NULL;
    crux_file_t *file = NULL;
    char name[64];
    bool end = false;
    uint64_t size = 0;

    put("record", "abc");
    crux_file_fake_fail(CRUX_FILE_FAKE_STAT, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_stat(0, "record", &info) == CRUX_FILE_ERR_IO);
    assert(crux_file_stat(0, "record", &info) == CRUX_FILE_OK && info.size == 3);

    crux_file_fake_fail(CRUX_FILE_FAKE_RENAME, 0, CRUX_FILE_ERR_ACCESS);
    assert(crux_file_rename(0, "record", "moved") == CRUX_FILE_ERR_ACCESS);
    expect("record", "abc");

    assert(crux_file_open("record", CRUX_FILE_READ, &file) == CRUX_FILE_OK);
    crux_file_fake_fail(CRUX_FILE_FAKE_SIZE, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_size(file, &size) == CRUX_FILE_ERR_IO);
    assert(crux_file_size(file, &size) == CRUX_FILE_OK && size == 3);
    assert(crux_file_close(&file) == CRUX_FILE_OK);

    crux_file_fake_fail(CRUX_FILE_FAKE_DIR_OPEN, 0, CRUX_FILE_ERR_IO);
    assert(crux_file_dir_open(0, &dir) == CRUX_FILE_ERR_IO && dir == NULL);
    assert(crux_file_dir_open(0, &dir) == CRUX_FILE_OK);

    /* A failed read reports neither an entry nor the end, and the listing can go on. */
    crux_file_fake_fail(CRUX_FILE_FAKE_DIR_READ, 0, CRUX_FILE_ERR_IO);
    name[0] = 'x';
    end = true;
    assert(crux_file_dir_read(dir, name, sizeof(name), &info, &end) == CRUX_FILE_ERR_IO);
    assert(!end && name[0] == '\0');
    assert(crux_file_dir_read(dir, name, sizeof(name), &info, &end) == CRUX_FILE_OK);
    assert(!end && strcmp(name, "record") == 0);

    /* A close consumes the handle even when it fails. */
    crux_file_fake_fail(CRUX_FILE_FAKE_DIR_CLOSE, 0, CRUX_FILE_ERR_IO);
    crux_file_dir_t *stale = dir;
    assert(crux_file_dir_close(&dir) == CRUX_FILE_ERR_IO && dir == NULL);
    assert(crux_file_dir_read(stale, name, sizeof(name), &info, &end) ==
           CRUX_FILE_ERR_INVALID_STATE);
    assert(crux_file_dir_open(0, &dir) == CRUX_FILE_OK);
    assert(crux_file_dir_close(&dir) == CRUX_FILE_OK);
}

/* Nothing that the destination check decides ever removes the destination. */
static void test_rename_never_removes_a_destination(void)
{
    start();
    put("from", "new");
    put("to", "old");

    unsigned removes = crux_file_fake_calls(CRUX_FILE_FAKE_REMOVE);
    unsigned renames = crux_file_fake_calls(CRUX_FILE_FAKE_RENAME);
    assert(crux_file_rename(0, "from", "to") == CRUX_FILE_ERR_EXISTS);
    assert(crux_file_fake_calls(CRUX_FILE_FAKE_REMOVE) == removes);
    assert(crux_file_fake_calls(CRUX_FILE_FAKE_RENAME) == renames + 1);
    expect("to", "old");
    expect("from", "new");
}

/* The checks that the common layer owns never reach the provider. */
static void test_common_checks_do_not_reach_the_provider(void)
{
    start();
    crux_file_t *file = NULL;
    put("held", "x");
    assert(crux_file_open("held", CRUX_FILE_READ, &file) == CRUX_FILE_OK);
    unsigned renames = crux_file_fake_calls(CRUX_FILE_FAKE_RENAME);
    assert(crux_file_rename(0, "held", "to") == CRUX_FILE_ERR_INVALID_STATE);
    assert(crux_file_rename(0, "a/b", "to") == CRUX_FILE_ERR_INVALID_ARGUMENT);
    assert(crux_file_rename(9, "held", "to") == CRUX_FILE_ERR_NOT_READY);
    assert(crux_file_fake_calls(CRUX_FILE_FAKE_RENAME) == renames);
    assert(crux_file_close(&file) == CRUX_FILE_OK);

    /* The end of a listing is remembered rather than asked for again. */
    crux_file_dir_t *dir = NULL;
    crux_file_info_t info;
    char name[64];
    bool end = false;
    assert(crux_file_dir_open(0, &dir) == CRUX_FILE_OK);
    while (!end) {
        assert(crux_file_dir_read(dir, name, sizeof(name), &info, &end) == CRUX_FILE_OK);
    }
    unsigned reads = crux_file_fake_calls(CRUX_FILE_FAKE_DIR_READ);
    assert(crux_file_dir_read(dir, name, sizeof(name), &info, &end) == CRUX_FILE_OK && end);
    assert(crux_file_fake_calls(CRUX_FILE_FAKE_DIR_READ) == reads);
    /* A buffer that is too small costs no provider call either. */
    assert(crux_file_dir_read(dir, name, 8, &info, &end) == CRUX_FILE_ERR_OUT_OF_RANGE);
    assert(crux_file_fake_calls(CRUX_FILE_FAKE_DIR_READ) == reads);
    assert(crux_file_dir_close(&dir) == CRUX_FILE_OK);
}

static void test_short_write_and_capacity(void)
{
    start();
    crux_file_t *file = NULL;
    size_t count = 0;
    assert(crux_file_open("short", CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    crux_file_fake_limit_write(2);
    assert(crux_file_write(file, "abcd", 4, &count) == CRUX_FILE_PARTIAL && count == 2);
    crux_file_fake_limit_write(SIZE_MAX);
    crux_file_fake_set_capacity(3);
    assert(crux_file_write(file, "cd", 2, &count) == CRUX_FILE_PARTIAL && count == 1);
    assert(crux_file_write(file, "d", 1, &count) == CRUX_FILE_ERR_NO_SPACE && count == 0);
    assert(crux_file_close(&file) == CRUX_FILE_OK);
    crux_file_fake_set_capacity(UINT64_MAX);
    expect("short", "abc");
}

static void cut_now(void)
{
    _exit(CUT_EXIT);
}

/* Each kind of mutation, with bytes written after the only sync. */
static void unsynced_then_synced(void)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    assert(crux_file_open("synced", CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    assert(crux_file_write(file, "kept", 4, &count) == CRUX_FILE_OK);
    assert(crux_file_sync(file) == CRUX_FILE_OK);
    assert(crux_file_write(file, "lost", 4, &count) == CRUX_FILE_OK);
    assert(crux_file_seek(file, 0) == CRUX_FILE_OK);
    assert(crux_file_rename(0, "source", "moved") == CRUX_FILE_OK);
    assert(crux_file_remove("doomed") == CRUX_FILE_OK);
    assert(crux_file_close(&file) == CRUX_FILE_OK);
}

static int run_cut(uint32_t mutations)
{
    fflush(NULL);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        crux_file_fake_cut_after(mutations, cut_now);
        unsynced_then_synced();
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    return WEXITSTATUS(status);
}

/* A cut stops the program at each mutation, and only what reached storage is left afterwards. */
static void test_power_cut_at_each_mutation(void)
{
    uint32_t cut = 0;
    for (;; cut++) {
        start();
        put("source", "s");
        put("doomed", "d");
        uint32_t before = crux_file_fake_mutations();
        int code = run_cut(cut);
        uint32_t made = crux_file_fake_mutations() - before;
        crux_file_fake_power_on();

        if (code == 0) {
            assert(cut > 0);
            assert(made == cut);
            expect("synced", "keptlost");
            expect("moved", "s");
            assert(!crux_file_fake_exists(0, "doomed"));
            break;
        }
        assert(code == CUT_EXIT);
        assert(made == cut);

        crux_file_info_t info;
        /* Create, truncate and write have not reached storage until the first sync returns. */
        crux_file_result_t synced = crux_file_stat(0, "synced", &info);
        assert(synced == CRUX_FILE_OK || synced == CRUX_FILE_ERR_NOT_FOUND);
        if (synced == CRUX_FILE_OK) {
            expect("synced", "kept");
        }
        /* A rename reaches storage at once, and the later unsynced bytes need the close. */
        assert(crux_file_fake_exists(0, "source") != crux_file_fake_exists(0, "moved"));
    }
    assert(cut == 8); /* Create, truncate, write, sync, write, rename, remove and close. */
}

static void test_dead_medium_fails_every_call(void)
{
    start();
    crux_file_t *file = NULL;
    size_t count = 0;
    put("record", "abc");
    assert(crux_file_open("record", CRUX_FILE_READ_WRITE, &file) == CRUX_FILE_OK);
    crux_file_fake_cut_after(0, NULL);
    assert(crux_file_write(file, "z", 1, &count) == CRUX_FILE_ERR_IO && count == 0);
    assert(crux_file_rename(0, "spare", "moved") == CRUX_FILE_ERR_IO);
    assert(crux_file_remove("other") == CRUX_FILE_ERR_IO);
    assert(crux_file_close(&file) == CRUX_FILE_ERR_IO && file == NULL);
    crux_file_fake_power_on();
    expect("record", "abc");
}

/* Write through puts each unit on the medium as it is written, and the size only at a sync. */
static void test_write_through_cuts_between_units(void)
{
    static uint8_t data[1024];
    memset(data, 0xA5, sizeof(data));
    crux_file_t *file = NULL;
    size_t count = 0;
    crux_file_info_t info;

    /* In a new file, nothing is visible until the sync, though the file itself exists. */
    start();
    crux_file_fake_write_through(512);
    assert(crux_file_open("slot", CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    assert(crux_file_write(file, data, sizeof(data), &count) == CRUX_FILE_OK);
    assert(count == sizeof(data));
    crux_file_fake_cut_after(0, NULL);
    assert(crux_file_sync(file) == CRUX_FILE_ERR_IO);
    assert(crux_file_close(&file) == CRUX_FILE_ERR_IO);
    crux_file_fake_power_on();
    assert(crux_file_stat(0, "slot", &info) == CRUX_FILE_OK && info.size == 0);

    /* An existing file: one unit and only one reached the medium before the cut. */
    assert(crux_file_open("slot", CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    assert(crux_file_write(file, data, sizeof(data), &count) == CRUX_FILE_OK);
    assert(crux_file_close(&file) == CRUX_FILE_OK);
    memset(data, 0x5A, sizeof(data));
    assert(crux_file_open("slot", CRUX_FILE_READ_WRITE, &file) == CRUX_FILE_OK);
    crux_file_fake_cut_after(1, NULL);
    assert(crux_file_write(file, data, sizeof(data), &count) == CRUX_FILE_ERR_IO);
    assert(count == 512);
    assert(crux_file_close(&file) == CRUX_FILE_ERR_IO);
    crux_file_fake_power_on();
    static uint8_t read_back[1024];
    assert(crux_file_open("slot", CRUX_FILE_READ, &file) == CRUX_FILE_OK);
    assert(crux_file_read(file, read_back, sizeof(read_back), &count) == CRUX_FILE_OK);
    assert(count == sizeof(read_back));
    assert(crux_file_close(&file) == CRUX_FILE_OK);
    assert(read_back[0] == 0x5A && read_back[511] == 0x5A);
    assert(read_back[512] == 0xA5 && read_back[1023] == 0xA5);

    /* A write that straddles a unit boundary splits on it. */
    assert(crux_file_open("slot", CRUX_FILE_READ_WRITE, &file) == CRUX_FILE_OK);
    assert(crux_file_seek(file, 510) == CRUX_FILE_OK);
    uint32_t before = crux_file_fake_mutations();
    assert(crux_file_write(file, data, 4, &count) == CRUX_FILE_OK);
    assert(crux_file_fake_mutations() - before == 2);
    assert(crux_file_close(&file) == CRUX_FILE_OK);

    /* A medium that drops a write reports it as done, and the file does not change. */
    crux_file_fake_drop_writes(true);
    assert(crux_file_open("slot", CRUX_FILE_READ_WRITE, &file) == CRUX_FILE_OK);
    memset(data, 0x11, sizeof(data));
    assert(crux_file_write(file, data, 4, &count) == CRUX_FILE_OK && count == 4);
    assert(crux_file_close(&file) == CRUX_FILE_OK);
    crux_file_fake_drop_writes(false);
    assert(crux_file_open("slot", CRUX_FILE_READ, &file) == CRUX_FILE_OK);
    assert(crux_file_read(file, read_back, 1, &count) == CRUX_FILE_OK);
    assert(read_back[0] == 0x5A);
    assert(crux_file_close(&file) == CRUX_FILE_OK);

    /* A medium that leaves fails everything, and reset brings the default model back. */
    crux_file_fake_remove_medium();
    assert(crux_file_stat(0, "slot", &info) == CRUX_FILE_ERR_IO);
    crux_file_fake_write_through(0);
    crux_file_fake_power_on();
    assert(crux_file_stat(0, "slot", &info) == CRUX_FILE_OK);
}

int main(void)
{
    test_provider_failures_pass_through();
    test_write_through_cuts_between_units();
    test_rename_never_removes_a_destination();
    test_common_checks_do_not_reach_the_provider();
    test_short_write_and_capacity();
    test_power_cut_at_each_mutation();
    test_dead_medium_fails_every_call();
    puts("File fake provider checks passed");
    return 0;
}
