#include "file_port.h"
#include <stdbool.h>
#include <string.h>

_Static_assert(CRUX_FILE_POOL_SIZE >= 4, "File profile requires at least four handles");
_Static_assert(CRUX_FILE_NAME_MAX >= 63, "File profile requires at least 63 name bytes");
_Static_assert(CRUX_FILE_DIR_POOL_SIZE >= 1, "File listing requires a listing handle");
_Static_assert(CRUX_FILE_ROOTS_MAX >= 1 && CRUX_FILE_ROOTS_MAX <= 255,
               "File roots use a uint8_t index, with one value left over as never bound");
struct crux_file {
    bool live;
    crux_file_mode_t mode;
    crux_file_root_t root;
    char name[CRUX_FILE_NAME_MAX + 1];
};

struct crux_file_dir {
    bool live;
    bool ended;
};

static struct crux_file files[CRUX_FILE_POOL_SIZE];
static struct crux_file_dir dirs[CRUX_FILE_DIR_POOL_SIZE];

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

static size_t dir_slot_of(const crux_file_dir_t *d)
{
    for (size_t i = 0; i < CRUX_FILE_DIR_POOL_SIZE; i++) {
        if (d == &dirs[i] && dirs[i].live) {
            return i;
        }
    }
    return CRUX_FILE_DIR_POOL_SIZE;
}

/* Any live handle on this file; the caller holds the File lock. */
static bool name_is_open(crux_file_root_t r, const char *n)
{
    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; i++) {
        if (files[i].live && files[i].root == r && strcmp(files[i].name, n) == 0) {
            return true;
        }
    }
    return false;
}

crux_file_result_t crux_file_open_at(crux_file_root_t r, const char *n, crux_file_mode_t m,
                                     crux_file_t **out)
{
    if (!out || *out || !name_valid(n) ||
        (m != CRUX_FILE_READ && m != CRUX_FILE_READ_WRITE && m != CRUX_FILE_CREATE_REPLACE)) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    if (r >= CRUX_FILE_ROOTS_MAX) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    crux_file_lock();
    /* A file open for writing excludes every other handle; several readers may share it. A second
     * handle can hold stale provider state (for example a cached cluster chain) after a write. */
    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; i++) {
        if (files[i].live && files[i].root == r && strcmp(files[i].name, n) == 0 &&
            (files[i].mode != CRUX_FILE_READ || m != CRUX_FILE_READ)) {
            crux_file_unlock();
            return CRUX_FILE_ERR_INVALID_STATE;
        }
    }
    size_t s = 0;
    while (s < CRUX_FILE_POOL_SIZE && files[s].live) {
        s++;
    }
    if (s == CRUX_FILE_POOL_SIZE) {
        crux_file_unlock();
        return CRUX_FILE_ERR_NO_RESOURCES;
    }

    crux_file_result_t result = crux_file_impl_open(r, s, n, m);
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
        files[s].root = r;
        memcpy(files[s].name, n, strlen(n) + 1);
        *out = &files[s];
    }
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_open(const char *n, crux_file_mode_t m, crux_file_t **out)
{
    return crux_file_open_at(CRUX_FILE_ROOT_DEFAULT, n, m, out);
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

crux_file_result_t crux_file_remove_at(crux_file_root_t r, const char *n)
{
    if (!name_valid(n)) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    if (r >= CRUX_FILE_ROOTS_MAX) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    crux_file_lock();
    if (name_is_open(r, n)) {
        crux_file_unlock();
        return CRUX_FILE_ERR_INVALID_STATE;
    }

    crux_file_result_t result = crux_file_impl_remove(r, n);
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_remove(const char *n)
{
    return crux_file_remove_at(CRUX_FILE_ROOT_DEFAULT, n);
}

crux_file_result_t crux_file_stat(crux_file_root_t r, const char *n, crux_file_info_t *out)
{
    if (!out || !name_valid(n)) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    if (r >= CRUX_FILE_ROOTS_MAX) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    crux_file_lock();
    crux_file_result_t result = crux_file_impl_stat(r, n, out);
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_size(crux_file_t *f, uint64_t *size)
{
    crux_file_lock();
    size_t s = slot_of(f);
    crux_file_result_t result = s == CRUX_FILE_POOL_SIZE ? CRUX_FILE_ERR_INVALID_STATE
                                : !size                  ? CRUX_FILE_ERR_INVALID_ARGUMENT
                                                         : crux_file_impl_size(s, size);
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_rename(crux_file_root_t r, const char *from, const char *to)
{
    if (!name_valid(from) || !name_valid(to)) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    if (r >= CRUX_FILE_ROOTS_MAX) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    crux_file_lock();
    crux_file_result_t result = name_is_open(r, from) || name_is_open(r, to)
                                    ? CRUX_FILE_ERR_INVALID_STATE
                                    : crux_file_impl_rename(r, from, to);
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_dir_open(crux_file_root_t r, crux_file_dir_t **out)
{
    if (!out || *out) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    if (r >= CRUX_FILE_ROOTS_MAX) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    crux_file_lock();
    size_t s = 0;
    while (s < CRUX_FILE_DIR_POOL_SIZE && dirs[s].live) {
        s++;
    }
    crux_file_result_t result =
        s == CRUX_FILE_DIR_POOL_SIZE ? CRUX_FILE_ERR_NO_RESOURCES : crux_file_impl_dir_open(s, r);
    if (result == CRUX_FILE_OK) {
        dirs[s].live = true;
        dirs[s].ended = false;
        *out = &dirs[s];
    }
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_dir_read(crux_file_dir_t *d, char *name, size_t capacity,
                                      crux_file_info_t *info, bool *end)
{
    if (!name || !capacity || !info || !end) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }

    name[0] = '\0';
    *end = false;
    crux_file_lock();
    size_t s = dir_slot_of(d);
    crux_file_result_t result = CRUX_FILE_OK;
    if (s == CRUX_FILE_DIR_POOL_SIZE) {
        result = CRUX_FILE_ERR_INVALID_STATE;
    } else if (capacity < CRUX_FILE_NAME_MAX + 1) {
        result = CRUX_FILE_ERR_OUT_OF_RANGE;
    } else if (dirs[s].ended) {
        *end = true;
    } else {
        /* Foreign names are skipped here, so that the name rule has one definition. */
        do {
            name[0] = '\0';
            result = crux_file_impl_dir_read(s, name, capacity, info, end);
        } while (result == CRUX_FILE_OK && !*end && !name_valid(name));
        if (result != CRUX_FILE_OK || *end) {
            name[0] = '\0';
            *end = result == CRUX_FILE_OK;
            dirs[s].ended = *end;
        }
    }
    crux_file_unlock();
    return result;
}

crux_file_result_t crux_file_dir_close(crux_file_dir_t **d)
{
    if (!d) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }

    crux_file_lock();
    size_t s = dir_slot_of(*d);
    if (s == CRUX_FILE_DIR_POOL_SIZE) {
        crux_file_unlock();
        return CRUX_FILE_ERR_INVALID_STATE;
    }
    *d = NULL;
    crux_file_result_t result = crux_file_impl_dir_close(s);
    dirs[s].live = false;
    crux_file_unlock();
    return result;
}

static size_t path_length(const char *p)
{
    size_t length = strlen(p);
    while (length > 1 && p[length - 1] == '/') {
        length--;
    }
    return length;
}

bool crux_file_paths_overlap(const char *a, const char *b)
{
    size_t la = path_length(a);
    size_t lb = path_length(b);
    const char *longer = la > lb ? a : b;
    size_t shorter = la > lb ? lb : la;
    /* A shorter path that keeps its final separator is the top of the tree, which holds any path.
     */
    return strncmp(a, b, shorter) == 0 &&
           (la == lb || longer[shorter] == '/' || (shorter > 0 && longer[shorter - 1] == '/'));
}
