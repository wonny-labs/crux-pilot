#include "file_port.h"
#include <stdbool.h>
#include <string.h>

_Static_assert(CRUX_FILE_POOL_SIZE >= 4, "File profile requires at least four handles");
_Static_assert(CRUX_FILE_NAME_MAX >= 63, "File profile requires at least 63 name bytes");
struct crux_file {
    bool live;
    crux_file_mode_t mode;
    char name[CRUX_FILE_NAME_MAX + 1];
};

static struct crux_file files[CRUX_FILE_POOL_SIZE];

static bool name_valid(const char *name)
{
    if (!name || !*name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
    for (size_t i = 0;; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == 0) {
            return true;
        }
        if (i >= CRUX_FILE_NAME_MAX ||
            !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
}

static size_t slot_of(const crux_file_t *f)
{
    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; i++) {
        if (f == &files[i] && files[i].live) {
            return i;
        }
    }
    return CRUX_FILE_POOL_SIZE;
}

crux_file_result_t crux_file_open(const char *n, crux_file_mode_t m, crux_file_t **out)
{
    if (!out || *out || !name_valid(n) ||
        (m != CRUX_FILE_READ && m != CRUX_FILE_READ_WRITE && m != CRUX_FILE_CREATE_REPLACE)) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }

    crux_file_lock();
    size_t s = 0;
    while (s < CRUX_FILE_POOL_SIZE && files[s].live) {
        s++;
    }
    if (s == CRUX_FILE_POOL_SIZE) {
        crux_file_unlock();
        return CRUX_FILE_ERR_NO_RESOURCES;
    }

    crux_file_result_t result = crux_file_impl_open(s, n, m);
    if (result == CRUX_FILE_OK && m == CRUX_FILE_CREATE_REPLACE) {
        result = crux_file_impl_truncate(s);
        if (result != CRUX_FILE_OK) {
            crux_file_result_t cleanup = crux_file_impl_close(s);
            if (cleanup != CRUX_FILE_OK) {
                crux_file_record_cleanup_error(cleanup);
            }
        }
    }
    if (result == CRUX_FILE_OK) {
        files[s].live = true;
        files[s].mode = m;
        memcpy(files[s].name, n, strlen(n) + 1);
        *out = &files[s];
    }
    crux_file_unlock();
    return result;
}

static crux_file_result_t transfer(crux_file_t *f, void *b, size_t l, size_t *c, bool write)
{
    if (!c) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }

    *c = 0;
    if (l && !b) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }

    crux_file_lock();
    size_t s = slot_of(f);
    crux_file_result_t result;
    if (s == CRUX_FILE_POOL_SIZE) {
        result = CRUX_FILE_ERR_INVALID_STATE;
    }
#if SIZE_MAX > INT64_MAX
    else if (l > INT64_MAX) {
        result = CRUX_FILE_ERR_OUT_OF_RANGE;
    }
#endif
    else if (write && files[s].mode == CRUX_FILE_READ) {
        result = CRUX_FILE_ERR_ACCESS;
    } else if (!l) {
        result = CRUX_FILE_OK;
    } else {
        result = write ? crux_file_impl_write(s, b, l, c) : crux_file_impl_read(s, b, l, c);
        if (*c > l) {
            /* A contradictory provider result cannot acknowledge any count.
             * This does not undo possible buffer or storage changes. */
            *c = 0;
            result = CRUX_FILE_ERR_IO;
        } else if (result == CRUX_FILE_OK && write && *c < l) {
            result = CRUX_FILE_PARTIAL;
        }
    }
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_read(crux_file_t *f, void *b, size_t l, size_t *c)
{
    return transfer(f, b, l, c, false);
}

crux_file_result_t crux_file_write(crux_file_t *f, const void *b, size_t l, size_t *c)
{
    return transfer(f, (void *)b, l, c, true);
}

crux_file_result_t crux_file_seek(crux_file_t *f, uint64_t o)
{
    crux_file_lock();
    size_t s = slot_of(f);
    crux_file_result_t result = s == CRUX_FILE_POOL_SIZE ? CRUX_FILE_ERR_INVALID_STATE
                                : o > INT64_MAX          ? CRUX_FILE_ERR_OUT_OF_RANGE
                                                         : crux_file_impl_seek(s, o);
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_sync(crux_file_t *f)
{
    crux_file_lock();
    size_t s = slot_of(f);
    crux_file_result_t result =
        s == CRUX_FILE_POOL_SIZE ? CRUX_FILE_ERR_INVALID_STATE : crux_file_impl_sync(s);
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_close(crux_file_t **f)
{
    if (!f) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }

    crux_file_lock();
    size_t s = slot_of(*f);
    if (s == CRUX_FILE_POOL_SIZE) {
        crux_file_unlock();
        return CRUX_FILE_ERR_INVALID_STATE;
    }
    *f = NULL;
    crux_file_result_t result = crux_file_impl_close(s);
    files[s].live = false;
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_remove(const char *n)
{
    if (!name_valid(n)) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }

    crux_file_lock();
    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; i++) {
        if (files[i].live && strcmp(files[i].name, n) == 0) {
            crux_file_unlock();
            return CRUX_FILE_ERR_INVALID_STATE;
        }
    }

    crux_file_result_t result = crux_file_impl_remove(n);
    crux_file_unlock();
    return result;
}
