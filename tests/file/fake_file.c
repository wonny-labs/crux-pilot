#include "fake_file.h"
#include "file_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define FAKE_ENTRIES 64
#define FAKE_NAME 128
#define FAKE_PATH 64

typedef struct {
    bool used;
    bool directory;
    bool on_medium; /* False until the first sync or close of a created file. */
    crux_file_root_t root;
    char name[FAKE_NAME];
    size_t size;
    size_t medium_size;
    uint8_t data[CRUX_FILE_FAKE_FILE_BYTES];
    uint8_t medium[CRUX_FILE_FAKE_FILE_BYTES];
} entry_t;

typedef struct {
    bool live;
    bool writable;
    bool dirty;
    size_t entry;
    size_t position;
} handle_t;

typedef struct {
    crux_file_root_t root;
    size_t next;
} listing_t;

typedef struct {
    entry_t entries[FAKE_ENTRIES];
    handle_t handles[CRUX_FILE_POOL_SIZE];
    listing_t listings[CRUX_FILE_DIR_POOL_SIZE];
    bool bound[CRUX_FILE_ROOTS_MAX];
    char paths[CRUX_FILE_ROOTS_MAX][FAKE_PATH];
    bool fail_armed[CRUX_FILE_FAKE_OP_COUNT];
    unsigned fail_skip[CRUX_FILE_FAKE_OP_COUNT];
    crux_file_result_t fail_result[CRUX_FILE_FAKE_OP_COUNT];
    unsigned calls[CRUX_FILE_FAKE_OP_COUNT];
    size_t write_limit;
    size_t write_through;
    bool drop_writes;
    uint64_t capacity;
    void (*write_hook)(void);
    void (*call_hook[CRUX_FILE_FAKE_OP_COUNT])(void);
    unsigned call_hook_skip[CRUX_FILE_FAKE_OP_COUNT];
    crux_file_result_t remove_then_fail;
    void (*dir_read_hook)(void);
    bool cut_armed;
    bool dead;
    uint32_t cut_remaining;
    void (*cut)(void);
    uint32_t mutations;
} state_t;

static state_t *state;
static crux_file_result_t cleanup_error;

static state_t *fake(void)
{
    if (!state) {
        /* Anonymous pages cost nothing until they are touched, and a forked child shares them. */
        void *mapping =
            mmap(NULL, sizeof(*state), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) {
            abort();
        }
        state = mapping;
        state->write_limit = SIZE_MAX;
        state->capacity = UINT64_MAX;
    }
    return state;
}

void crux_file_fake_reset(void)
{
    state_t *s = fake();
    for (size_t i = 0; i < FAKE_ENTRIES; i++) {
        s->entries[i].used = false;
    }
    memset(s->handles, 0, sizeof(s->handles));
    memset(s->listings, 0, sizeof(s->listings));
    memset(s->bound, 0, sizeof(s->bound));
    memset(s->fail_armed, 0, sizeof(s->fail_armed));
    memset(s->calls, 0, sizeof(s->calls));
    s->write_limit = SIZE_MAX;
    s->write_through = 0;
    s->drop_writes = false;
    s->capacity = UINT64_MAX;
    s->write_hook = NULL;
    memset(s->call_hook, 0, sizeof(s->call_hook));
    s->remove_then_fail = CRUX_FILE_OK;
    s->dir_read_hook = NULL;
    s->cut_armed = false;
    s->dead = false;
    s->cut = NULL;
    s->mutations = 0;
}

crux_file_result_t crux_file_fake_bind_root(crux_file_root_t root, const char *path)
{
    state_t *s = fake();
    if (!path || root >= CRUX_FILE_ROOTS_MAX || strlen(path) >= FAKE_PATH) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    if (s->bound[root]) {
        return CRUX_FILE_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < CRUX_FILE_ROOTS_MAX; i++) {
        if (s->bound[i] && crux_file_paths_overlap(s->paths[i], path)) {
            return CRUX_FILE_ERR_INVALID_STATE;
        }
    }
    strcpy(s->paths[root], path);
    s->bound[root] = true;
    return CRUX_FILE_OK;
}

static size_t find(crux_file_root_t root, const char *name)
{
    entry_t *entries = fake()->entries;
    for (size_t i = 0; i < FAKE_ENTRIES; i++) {
        if (entries[i].used && entries[i].root == root && strcmp(entries[i].name, name) == 0) {
            return i;
        }
    }
    return FAKE_ENTRIES;
}

static size_t add(crux_file_root_t root, const char *name, bool directory, bool on_medium)
{
    entry_t *entries = fake()->entries;
    if (strlen(name) >= FAKE_NAME) {
        abort();
    }
    for (size_t i = 0; i < FAKE_ENTRIES; i++) {
        if (!entries[i].used) {
            entries[i].used = true;
            entries[i].directory = directory;
            entries[i].on_medium = on_medium;
            entries[i].root = root;
            strcpy(entries[i].name, name);
            entries[i].size = 0;
            entries[i].medium_size = 0;
            return i;
        }
    }
    return FAKE_ENTRIES;
}

void crux_file_fake_add_directory(crux_file_root_t root, const char *name)
{
    if (add(root, name, true, true) == FAKE_ENTRIES) {
        abort();
    }
}

void crux_file_fake_add_raw(crux_file_root_t root, const char *raw_name)
{
    if (add(root, raw_name, false, true) == FAKE_ENTRIES) {
        abort();
    }
}

void crux_file_fake_fail(crux_file_fake_op_t op, unsigned skip, crux_file_result_t result)
{
    state_t *s = fake();
    s->fail_armed[op] = true;
    s->fail_skip[op] = skip;
    s->fail_result[op] = result;
}

unsigned crux_file_fake_calls(crux_file_fake_op_t op)
{
    return fake()->calls[op];
}

void crux_file_fake_limit_write(size_t bytes)
{
    fake()->write_limit = bytes;
}

void crux_file_fake_write_through(size_t unit)
{
    fake()->write_through = unit;
}

void crux_file_fake_drop_writes(bool drop)
{
    fake()->drop_writes = drop;
}

void crux_file_fake_remove_medium(void)
{
    fake()->dead = true;
}

void crux_file_fake_on_write(void (*hook)(void))
{
    fake()->write_hook = hook;
}

void crux_file_fake_on_call(crux_file_fake_op_t op, unsigned skip, void (*hook)(void))
{
    fake()->call_hook[op] = hook;
    fake()->call_hook_skip[op] = skip;
}

void crux_file_fake_after_dir_read(void (*hook)(void))
{
    fake()->dir_read_hook = hook;
}

void crux_file_fake_remove_then_fail(crux_file_result_t result)
{
    fake()->remove_then_fail = result;
}

void crux_file_fake_set_capacity(uint64_t bytes)
{
    fake()->capacity = bytes;
}

void crux_file_fake_cut_after(uint32_t mutations, void (*cut)(void))
{
    state_t *s = fake();
    s->cut_armed = true;
    s->cut_remaining = mutations;
    s->cut = cut;
}

uint32_t crux_file_fake_mutations(void)
{
    return fake()->mutations;
}

static void store(entry_t *entry)
{
    memcpy(entry->medium, entry->data, entry->size);
    entry->medium_size = entry->size;
    entry->on_medium = true;
}

void crux_file_fake_power_on(void)
{
    state_t *s = fake();
    for (size_t i = 0; i < FAKE_ENTRIES; i++) {
        entry_t *entry = &s->entries[i];
        if (!entry->used) {
            continue;
        }
        if (!entry->on_medium) {
            entry->used = false;
            continue;
        }
        memcpy(entry->data, entry->medium, entry->medium_size);
        entry->size = entry->medium_size;
    }
    memset(s->handles, 0, sizeof(s->handles));
    memset(s->listings, 0, sizeof(s->listings));
    s->cut_armed = false;
    s->dead = false;
    s->cut = NULL;
}

bool crux_file_fake_exists(crux_file_root_t root, const char *name)
{
    return find(root, name) != FAKE_ENTRIES;
}

/* Every provider call starts here. A nonzero result is what the call must return. */
static crux_file_result_t enter(crux_file_fake_op_t op)
{
    state_t *s = fake();
    s->calls[op]++;
    if (s->call_hook[op]) {
        if (s->call_hook_skip[op] == 0) {
            void (*hook)(void) = s->call_hook[op];
            s->call_hook[op] = NULL;
            hook();
        } else {
            s->call_hook_skip[op]--;
        }
    }
    if (s->dead) {
        return CRUX_FILE_ERR_IO;
    }
    if (s->fail_armed[op]) {
        if (s->fail_skip[op] == 0) {
            s->fail_armed[op] = false;
            return s->fail_result[op];
        }
        s->fail_skip[op]--;
    }
    return CRUX_FILE_OK;
}

/* False means the power went at this mutation, so it must not happen. */
static bool mutate(void)
{
    state_t *s = fake();
    if (s->cut_armed && s->cut_remaining == 0) {
        s->cut_armed = false;
        if (s->cut) {
            s->cut();
        }
        s->dead = true;
        return false;
    }
    if (s->cut_armed) {
        s->cut_remaining--;
    }
    s->mutations++;
    return true;
}

crux_file_result_t crux_file_impl_open(crux_file_root_t root, size_t slot, const char *name,
                                       crux_file_mode_t mode)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_OPEN);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    if (!s->bound[root]) {
        return CRUX_FILE_ERR_NOT_READY;
    }
    size_t index = find(root, name);
    if (index != FAKE_ENTRIES && s->entries[index].directory) {
        return CRUX_FILE_ERR_NOT_SUPPORTED;
    }
    if (index == FAKE_ENTRIES) {
        if (mode != CRUX_FILE_CREATE_REPLACE) {
            return CRUX_FILE_ERR_NOT_FOUND;
        }
        if (!mutate()) {
            return CRUX_FILE_ERR_IO;
        }
        index = add(root, name, false, s->write_through != 0);
        if (index == FAKE_ENTRIES) {
            return CRUX_FILE_ERR_NO_SPACE;
        }
    }
    s->handles[slot] = (handle_t){
        .live = true,
        .writable = mode != CRUX_FILE_READ,
        .dirty = !s->entries[index].on_medium,
        .entry = index,
        .position = 0,
    };
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_truncate(size_t slot)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_TRUNCATE);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    if (!mutate()) {
        return CRUX_FILE_ERR_IO;
    }
    s->entries[s->handles[slot].entry].size = 0;
    if (s->write_through != 0) {
        s->entries[s->handles[slot].entry].medium_size = 0;
    }
    s->handles[slot].position = 0;
    s->handles[slot].dirty = true;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_read(size_t slot, void *buffer, size_t length, size_t *count)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_READ);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    handle_t *handle = &s->handles[slot];
    entry_t *entry = &s->entries[handle->entry];
    size_t available = entry->size - handle->position;
    size_t taken = length < available ? length : available;
    memcpy(buffer, &entry->data[handle->position], taken);
    handle->position += taken;
    *count = taken;
    return CRUX_FILE_OK;
}

static uint64_t bytes_stored(void)
{
    uint64_t total = 0;
    for (size_t i = 0; i < FAKE_ENTRIES; i++) {
        if (fake()->entries[i].used) {
            total += fake()->entries[i].size;
        }
    }
    return total;
}

crux_file_result_t crux_file_impl_write(size_t slot, const void *buffer, size_t length,
                                        size_t *count)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_WRITE);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    /* With write through, each unit below is its own mutation. */
    if (s->write_through == 0 && !mutate()) {
        return CRUX_FILE_ERR_IO;
    }
    if (s->write_hook) {
        void (*hook)(void) = s->write_hook;
        s->write_hook = NULL;
        hook();
    }
    handle_t *handle = &s->handles[slot];
    entry_t *entry = &s->entries[handle->entry];
    if (s->drop_writes) {
        *count = length;
        return CRUX_FILE_OK;
    }
    size_t room = CRUX_FILE_FAKE_FILE_BYTES - handle->position;
    size_t taken = length < room ? length : room;
    taken = taken < s->write_limit ? taken : s->write_limit;

    /* Only bytes past the end take new room. */
    size_t overlap = entry->size - handle->position;
    uint64_t free_bytes = s->capacity > bytes_stored() ? s->capacity - bytes_stored() : 0;
    if (taken > overlap && taken - overlap > free_bytes) {
        taken = overlap + (size_t)free_bytes;
    }
    if (taken == 0) {
        return CRUX_FILE_ERR_NO_SPACE;
    }

    if (s->write_through != 0) {
        const uint8_t *source = buffer;
        for (size_t done = 0; done < taken;) {
            size_t offset = handle->position + done;
            size_t chunk = s->write_through - offset % s->write_through;
            chunk = chunk < taken - done ? chunk : taken - done;
            if (!mutate()) {
                *count = done;
                return CRUX_FILE_ERR_IO;
            }
            memcpy(&entry->data[offset], &source[done], chunk);
            memcpy(&entry->medium[offset], &source[done], chunk);
            done += chunk;
        }
    } else {
        memcpy(&entry->data[handle->position], buffer, taken);
    }
    handle->position += taken;
    if (handle->position > entry->size) {
        entry->size = handle->position;
    }
    handle->dirty = true;
    *count = taken;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_seek(size_t slot, uint64_t offset)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_SEEK);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    handle_t *handle = &s->handles[slot];
    if (offset > s->entries[handle->entry].size) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }
    handle->position = (size_t)offset;
    return CRUX_FILE_OK;
}

static crux_file_result_t flush(handle_t *handle)
{
    state_t *s = fake();
    if (!handle->dirty) {
        return CRUX_FILE_OK;
    }
    if (!mutate()) {
        return CRUX_FILE_ERR_IO;
    }
    store(&s->entries[handle->entry]);
    handle->dirty = false;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_sync(size_t slot)
{
    crux_file_result_t result = enter(CRUX_FILE_FAKE_SYNC);
    return result != CRUX_FILE_OK ? result : flush(&fake()->handles[slot]);
}

crux_file_result_t crux_file_impl_close(size_t slot)
{
    handle_t *handle = &fake()->handles[slot];
    crux_file_result_t result = enter(CRUX_FILE_FAKE_CLOSE);
    if (result == CRUX_FILE_OK) {
        result = flush(handle);
    }
    handle->live = false;
    return result;
}

crux_file_result_t crux_file_impl_remove(crux_file_root_t root, const char *name)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_REMOVE);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    if (!s->bound[root]) {
        return CRUX_FILE_ERR_NOT_READY;
    }
    size_t index = find(root, name);
    if (index == FAKE_ENTRIES) {
        return CRUX_FILE_ERR_NOT_FOUND;
    }
    if (!mutate()) {
        return CRUX_FILE_ERR_IO;
    }
    s->entries[index].used = false;
    result = s->remove_then_fail;
    s->remove_then_fail = CRUX_FILE_OK;
    return result;
}

crux_file_result_t crux_file_impl_stat(crux_file_root_t root, const char *name,
                                       crux_file_info_t *out)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_STAT);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    if (!s->bound[root]) {
        return CRUX_FILE_ERR_NOT_READY;
    }
    size_t index = find(root, name);
    if (index == FAKE_ENTRIES || s->entries[index].directory) {
        return CRUX_FILE_ERR_NOT_FOUND;
    }
    out->size = s->entries[index].size;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_size(size_t slot, uint64_t *size)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_SIZE);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    *size = s->entries[s->handles[slot].entry].size;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_rename(crux_file_root_t root, const char *from, const char *to)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_RENAME);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    if (!s->bound[root]) {
        return CRUX_FILE_ERR_NOT_READY;
    }
    size_t source = find(root, from);
    if (source == FAKE_ENTRIES || s->entries[source].directory) {
        return CRUX_FILE_ERR_NOT_FOUND;
    }
    if (find(root, to) != FAKE_ENTRIES) {
        return CRUX_FILE_ERR_EXISTS;
    }
    if (!mutate()) {
        return CRUX_FILE_ERR_IO;
    }
    strcpy(s->entries[source].name, to);
    s->entries[source].on_medium = true;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_dir_open(size_t slot, crux_file_root_t root)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_DIR_OPEN);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    if (!s->bound[root]) {
        return CRUX_FILE_ERR_NOT_READY;
    }
    s->listings[slot] = (listing_t){.root = root, .next = 0};
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_dir_read(size_t slot, char *name, size_t capacity,
                                           crux_file_info_t *info, bool *end)
{
    state_t *s = fake();
    crux_file_result_t result = enter(CRUX_FILE_FAKE_DIR_READ);
    if (result != CRUX_FILE_OK) {
        return result;
    }
    listing_t *listing = &s->listings[slot];
    while (listing->next < FAKE_ENTRIES) {
        entry_t *entry = &s->entries[listing->next++];
        if (entry->used && !entry->directory && entry->root == listing->root &&
            strlen(entry->name) < capacity) {
            strcpy(name, entry->name);
            info->size = entry->size;
            if (s->dir_read_hook) {
                void (*hook)(void) = s->dir_read_hook;
                s->dir_read_hook = NULL;
                hook();
            }
            return CRUX_FILE_OK;
        }
    }
    *end = true;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_dir_close(size_t slot)
{
    (void)slot;
    return enter(CRUX_FILE_FAKE_DIR_CLOSE);
}

void crux_file_lock(void)
{
}

void crux_file_unlock(void)
{
}

void crux_file_record_cleanup_error(crux_file_result_t error)
{
    cleanup_error = error;
}
