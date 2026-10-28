#include "file_port.h"
#include <assert.h>
#include <stdio.h>
static crux_file_result_t opened = CRUX_FILE_OK, truncated = CRUX_FILE_OK, closed = CRUX_FILE_OK;
static crux_file_result_t io = CRUX_FILE_OK, sync_result = CRUX_FILE_OK, cleanup = CRUX_FILE_OK;
static size_t count = 3, closes, opens, calls;
static int live[CRUX_FILE_POOL_SIZE];
static int changed;
static unsigned locks, unlocks;
void crux_file_port_lock(void)
{
    locks++;
}
void crux_file_port_unlock(void)
{
    assert(locks > unlocks);
    unlocks++;
}
crux_file_result_t crux_file_impl_open(size_t s, const char *n, crux_file_mode_t m)
{
    (void)s;
    (void)n;
    (void)m;
    opens++;
    assert(!live[s]);
    if (opened == CRUX_FILE_OK)
        live[s] = 1;
    return opened;
}
crux_file_result_t crux_file_impl_truncate(size_t s)
{
    assert(live[s]);
    changed = 1;
    return truncated;
}
crux_file_result_t crux_file_impl_close(size_t s)
{
    assert(live[s]);
    live[s] = 0;
    closes++;
    return closed;
}
crux_file_result_t crux_file_impl_read(size_t s, void *b, size_t l, size_t *c)
{
    assert(live[s]);
    (void)b;
    (void)l;
    calls++;
    *c = count;
    return io;
}
crux_file_result_t crux_file_impl_write(size_t s, const void *b, size_t l, size_t *c)
{
    return crux_file_impl_read(s, (void *)b, l, c);
}
crux_file_result_t crux_file_impl_seek(size_t s, uint64_t o)
{
    assert(live[s] && o <= INT64_MAX);
    calls++;
    return o > 3 ? CRUX_FILE_ERR_OUT_OF_RANGE : CRUX_FILE_OK;
}
crux_file_result_t crux_file_impl_sync(size_t s)
{
    assert(live[s]);
    return sync_result;
}
crux_file_result_t crux_file_impl_remove(const char *n)
{
    (void)n;
    calls++;
    return io;
}
void crux_file_record_cleanup_error(crux_file_result_t e)
{
    cleanup = e;
}
int main(void)
{
    crux_file_t *f = NULL;
    size_t n = 99;
    char b[8] = {0};
    assert(crux_file_open("record", CRUX_FILE_CREATE_REPLACE, &f) == CRUX_FILE_OK);
    assert(f != NULL);
    assert(crux_file_open("x", CRUX_FILE_READ, &f) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    /* A faulty provider must not publish an impossible accepted-byte count,
     * even when it also returns an error or PARTIAL. No buffer access is made. */
    const crux_file_result_t outcomes[] = {CRUX_FILE_OK, CRUX_FILE_PARTIAL, CRUX_FILE_ERR_NO_SPACE,
                                           CRUX_FILE_ERR_IO};
    const size_t overcounts[] = {sizeof(b) + 1, SIZE_MAX};
    for (size_t i = 0; i < sizeof(outcomes) / sizeof(outcomes[0]); ++i) {
        io = outcomes[i];
        for (size_t j = 0; j < sizeof(overcounts) / sizeof(overcounts[0]); ++j) {
            count = overcounts[j];
            assert(crux_file_read(f, b, sizeof(b), &n) == CRUX_FILE_ERR_IO && n == 0);
            assert(crux_file_write(f, b, sizeof(b), &n) == CRUX_FILE_ERR_IO && n == 0);
        }
    }
    io = CRUX_FILE_OK;
    count = 3;
    assert(crux_file_read(f, b, 8, &n) == CRUX_FILE_OK && n == 3);
    assert(crux_file_write(f, b, 8, &n) == CRUX_FILE_PARTIAL && n == 3);
    count = 0;
    assert(crux_file_read(f, b, 8, &n) == CRUX_FILE_OK && n == 0);
    assert(crux_file_write(f, b, 8, &n) == CRUX_FILE_PARTIAL && n == 0);
    count = 8;
    assert(crux_file_write(f, b, 8, &n) == CRUX_FILE_OK && n == 8);
    io = CRUX_FILE_ERR_NO_SPACE;
    count = 2;
    assert(crux_file_write(f, b, 8, &n) == CRUX_FILE_ERR_NO_SPACE && n == 2);
    io = CRUX_FILE_ERR_IO;
    count = 0;
    assert(crux_file_read(f, b, 8, &n) == CRUX_FILE_ERR_IO && n == 0);
    io = CRUX_FILE_OK;
    size_t before = calls;
    assert(crux_file_write(f, NULL, 0, &n) == CRUX_FILE_OK && n == 0 && calls == before);
    assert(crux_file_read(f, NULL, 1, &n) == CRUX_FILE_ERR_INVALID_ARGUMENT && n == 0);
    assert(crux_file_read(f, b, 1, NULL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    assert(crux_file_read(NULL, b, 1, &n) == CRUX_FILE_ERR_INVALID_STATE && n == 0);
    assert(crux_file_seek(f, UINT64_MAX) == CRUX_FILE_ERR_OUT_OF_RANGE && calls == before);
    assert(crux_file_write(f, b, SIZE_MAX, &n) == CRUX_FILE_ERR_OUT_OF_RANGE && n == 0);
    assert(crux_file_seek(f, 4) == CRUX_FILE_ERR_OUT_OF_RANGE);
    assert(crux_file_seek(f, 1) == CRUX_FILE_OK);
    sync_result = CRUX_FILE_ERR_IO;
    assert(crux_file_sync(f) == CRUX_FILE_ERR_IO);
    sync_result = CRUX_FILE_OK;
    assert(crux_file_sync(f) == CRUX_FILE_OK);
    closed = CRUX_FILE_ERR_IO;
    before = closes;
    assert(crux_file_close(&f) == CRUX_FILE_ERR_IO && f == NULL && closes == before + 1);
    assert(crux_file_close(&f) == CRUX_FILE_ERR_INVALID_STATE && closes == before + 1);
    assert(crux_file_close(NULL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    closed = CRUX_FILE_OK;
    assert(crux_file_open("record", CRUX_FILE_READ, &f) == CRUX_FILE_OK);
    assert(crux_file_write(f, b, 1, &n) == CRUX_FILE_ERR_ACCESS && n == 0);
    assert(crux_file_remove("record") == CRUX_FILE_ERR_INVALID_STATE);
    assert(crux_file_close(&f) == CRUX_FILE_OK);
    const char *bad[] = {"", ".", "..", "../a", "a/b", "a b", "a\\b", "\x80"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert(crux_file_open(bad[i], CRUX_FILE_READ, &f) == CRUX_FILE_ERR_INVALID_ARGUMENT);
        assert(crux_file_remove(bad[i]) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    }
    char long_name[65];
    for (size_t i = 0; i < 64; i++)
        long_name[i] = 'a';
    long_name[64] = 0;
    assert(crux_file_open(long_name, CRUX_FILE_READ, &f) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    long_name[63] = 0;
    assert(crux_file_open(long_name, CRUX_FILE_READ, &f) == CRUX_FILE_OK);
    assert(crux_file_close(&f) == CRUX_FILE_OK);
    assert(crux_file_open(NULL, CRUX_FILE_READ, &f) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    assert(crux_file_open("x", (crux_file_mode_t)99, &f) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    assert(crux_file_open("x", CRUX_FILE_READ, NULL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    crux_file_t *pool[CRUX_FILE_POOL_SIZE] = {0};
    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; i++)
        assert(crux_file_open("x", CRUX_FILE_READ, &pool[i]) == CRUX_FILE_OK);
    before = opens;
    assert(crux_file_open("x", CRUX_FILE_READ, &f) == CRUX_FILE_ERR_NO_RESOURCES && f == NULL &&
           opens == before);
    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; i++)
        assert(crux_file_close(&pool[i]) == CRUX_FILE_OK);
    opened = CRUX_FILE_ERR_NOT_FOUND;
    assert(crux_file_open("missing", CRUX_FILE_READ, &f) == CRUX_FILE_ERR_NOT_FOUND && f == NULL);
    opened = CRUX_FILE_OK;
    changed = 0;
    truncated = CRUX_FILE_ERR_NO_SPACE;
    closed = CRUX_FILE_ERR_IO;
    before = closes;
    assert(crux_file_open("new", CRUX_FILE_CREATE_REPLACE, &f) == CRUX_FILE_ERR_NO_SPACE &&
           f == NULL);
    assert(closes == before + 1 && cleanup == CRUX_FILE_ERR_IO && changed);
    closed = CRUX_FILE_OK;
    before = closes;
    changed = 0;
    assert(crux_file_open("new", CRUX_FILE_CREATE_REPLACE, &f) == CRUX_FILE_ERR_NO_SPACE && !f);
    assert(closes == before + 1 && changed);
    truncated = CRUX_FILE_OK;
    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; i++)
        assert(crux_file_open("x", CRUX_FILE_READ, &pool[i]) == CRUX_FILE_OK);
    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; i++)
        assert(crux_file_close(&pool[i]) == CRUX_FILE_OK);
    assert(crux_file_remove("x") == CRUX_FILE_OK);
    io = CRUX_FILE_ERR_IO;
    assert(crux_file_remove("x") == CRUX_FILE_ERR_IO);
    assert(locks == unlocks && locks > 0);
    puts("File deterministic contract checks passed");
    return 0;
}
