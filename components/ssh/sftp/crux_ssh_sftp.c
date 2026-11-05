/*
 * SFTP over the file service. wolfSSH parses the protocol and calls these operations through the
 * macros in port/myFilesystem.h, on the worker thread of the connection, with the handle below as
 * its `fs` argument. So a session reaches a file only through the service, and no rule about a
 * session, a permission, a limit or a name is decided here. See docs/sftp-design.md.
 */
#include "crux_ssh_attachment.h"
#include "crux_ssh_callbacks.h"
#include "crux_ssh_sftp_ops.h"

#include <crux/file_service.h>
#include <crux_access_adapter.h>

#include <limits.h>
#include <stdatomic.h>
#include <string.h>

/* Sessions at once, across all connections. A session past this ends its connection. */
#ifndef CRUX_SSH_SFTP_SESSIONS_MAX
#define CRUX_SSH_SFTP_SESSIONS_MAX 4
#endif
/* Open files for one session. Below CRUX_FILE_SERVICE_TRANSFER_MAX, so that no session takes them
 * all. Each client this design tests holds one at a time. */
#ifndef CRUX_SSH_SFTP_FILES_MAX
#define CRUX_SSH_SFTP_FILES_MAX 2
#endif
/* Open listings for one session. A listing of an area also holds a listing of the service until it
 * has been read to its end. */
#ifndef CRUX_SSH_SFTP_DIRS_MAX
#define CRUX_SSH_SFTP_DIRS_MAX 2
#endif

#define NAME_CAPACITY CRUX_SSH_SFTP_NAME_CAPACITY
#define AREA_NAME_MAX 15u

/* A descriptor holds the slot and how many times the session slot has been opened, so that a
 * descriptor left from an earlier connection never matches. */
#define SLOT_BITS 8u
#define SLOT_MASK ((1u << SLOT_BITS) - 1u)
#define GENERATION_MASK 0x7fffu

_Static_assert(CRUX_SSH_SFTP_FILES_MAX > 0 && CRUX_SSH_SFTP_FILES_MAX <= (int)SLOT_MASK,
               "A descriptor holds the slot of a file");
_Static_assert(CRUX_SSH_SFTP_DIRS_MAX > 0 && CRUX_SSH_SFTP_DIRS_MAX <= (int)SLOT_MASK,
               "A descriptor holds the slot of a listing");
_Static_assert(CRUX_SSH_SFTP_NAME_CAPACITY >= 64u, "The file service needs 64 bytes for a name");
_Static_assert(sizeof(int) >= 3u, "A descriptor holds a slot and a generation");

typedef struct {
    crux_file_service_transfer_t *transfer;
    const crux_file_area_t *area;
} open_file_t;

typedef struct {
    bool used;
    bool finished;
    const crux_file_area_t *area; /* NULL for the virtual root. */
    crux_file_service_dir_t *listing;
    size_t cursor;
    uint32_t entry_mode;
} open_dir_t;

struct crux_ssh_file_handle {
    atomic_bool live;
    uint16_t generation;
    crux_access_session_t session;
    open_file_t files[CRUX_SSH_SFTP_FILES_MAX];
    open_dir_t dirs[CRUX_SSH_SFTP_DIRS_MAX];
};

static struct crux_ssh_file_handle sessions[CRUX_SSH_SFTP_SESSIONS_MAX];

/* ---- The session ---- */

static bool session_usable(crux_access_session_t session, crux_status_t *status)
{
    size_t cursor = 0;
    const crux_file_area_t *area = NULL;
    bool writable = false;
    switch (crux_file_service_area_next(session, &cursor, &area, &writable)) {
    case CRUX_FILE_SERVICE_OK:
    case CRUX_FILE_SERVICE_END:
        return true;
    case CRUX_FILE_SERVICE_ERR_NOT_READY:
        *status = CRUX_ERR_NOT_READY;
        return false;
    default:
        *status = CRUX_ERR_INVALID_STATE;
        return false;
    }
}

crux_status_t crux_ssh_sftp_open(void *context, crux_ssh_attachment_t *attachment,
                                 crux_ssh_file_handle_t **handle)
{
    (void)context;
    if (handle == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    *handle = NULL;
    crux_access_session_t session = crux_ssh_attachment_session(attachment);
    crux_status_t status = CRUX_OK;
    if (!session_usable(session, &status)) {
        return status;
    }
    for (size_t i = 0; i < CRUX_SSH_SFTP_SESSIONS_MAX; i++) {
        bool expected = false;
        if (atomic_compare_exchange_strong(&sessions[i].live, &expected, true)) {
            struct crux_ssh_file_handle *taken = &sessions[i];
            taken->generation = (uint16_t)((taken->generation + 1u) & GENERATION_MASK);
            taken->session = session;
            memset(taken->files, 0, sizeof(taken->files));
            memset(taken->dirs, 0, sizeof(taken->dirs));
            *handle = taken;
            return CRUX_OK;
        }
    }
    return CRUX_ERR_NO_RESOURCES;
}

static void dir_give_back(open_dir_t *dir)
{
    if (dir->listing != NULL) {
        (void)crux_file_service_dir_close(&dir->listing);
    }
}

void crux_ssh_sftp_close(void *context, crux_ssh_attachment_t *attachment,
                         crux_ssh_file_handle_t *handle)
{
    (void)context;
    (void)attachment;
    if (handle == NULL) {
        return;
    }
    /* A direct write stays where it is, as the file service states, and counts against the area
     * until the client resumes it or the stale rule removes it. */
    for (size_t i = 0; i < CRUX_SSH_SFTP_FILES_MAX; i++) {
        if (handle->files[i].transfer != NULL) {
            (void)crux_file_service_close(&handle->files[i].transfer);
        }
    }
    for (size_t i = 0; i < CRUX_SSH_SFTP_DIRS_MAX; i++) {
        dir_give_back(&handle->dirs[i]);
    }
    handle->session.value = 0;
    atomic_store(&handle->live, false);
}

/* The handle behind `fs`, for an operation that a client asked for. Every request is activity, so a
 * transfer keeps its session and an idle one ends at session_alive. The service still decides
 * whether the session is good, so the result of the touch does not matter here. */
static struct crux_ssh_file_handle *session_enter(void *fs)
{
    struct crux_ssh_file_handle *handle = fs;
    if (handle == NULL || !atomic_load(&handle->live)) {
        return NULL;
    }
    (void)crux_access_touch(handle->session);
    return handle;
}

static int slot_descriptor(const struct crux_ssh_file_handle *handle, size_t slot)
{
    return (int)(((unsigned)handle->generation << SLOT_BITS) | (unsigned)slot);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a descriptor and a count, used only below
static bool slot_from_descriptor(const struct crux_ssh_file_handle *handle, int descriptor,
                                 size_t capacity, size_t *slot)
{
    if (descriptor < 0 || ((unsigned)descriptor >> SLOT_BITS) != handle->generation) {
        return false;
    }
    *slot = (unsigned)descriptor & SLOT_MASK;
    return *slot < capacity;
}

static open_file_t *file_from_descriptor(struct crux_ssh_file_handle *handle, int fd)
{
    size_t slot = 0;
    if (!slot_from_descriptor(handle, fd, CRUX_SSH_SFTP_FILES_MAX, &slot) ||
        handle->files[slot].transfer == NULL) {
        return NULL;
    }
    return &handle->files[slot];
}

static open_dir_t *dir_from_descriptor(struct crux_ssh_file_handle *handle, int dir)
{
    size_t slot = 0;
    if (!slot_from_descriptor(handle, dir, CRUX_SSH_SFTP_DIRS_MAX, &slot) ||
        !handle->dirs[slot].used) {
        return NULL;
    }
    return &handle->dirs[slot];
}

/* ---- Paths ---- */

typedef enum { PATH_ROOT, PATH_AREA, PATH_FILE, PATH_REFUSED } path_kind_t;

typedef struct {
    bool readable;
    bool writable;
} area_rights_t;

/* What the session may do in an area, as the service says, with no denial event. It only decides
 * what a client is shown: each operation still goes to the service. False if the area is hidden. */
static bool area_rights(const struct crux_ssh_file_handle *handle, const crux_file_area_t *area,
                        area_rights_t *rights)
{
    size_t cursor = 0;
    const crux_file_area_t *found = NULL;
    while (crux_file_service_area_next(handle->session, &cursor, &found, &rights->writable) ==
           CRUX_FILE_SERVICE_OK) {
        if (found == area) {
            return crux_access_permitted(handle->session, area->read_permission,
                                         &rights->readable) == CRUX_ACCESS_OK;
        }
    }
    return false;
}

/*
 * Direct use of a hidden area gives what a missing one gives, and a denial event: gate #183,
 * "Hiding". The service records it, and it stops at the permission, so the name plays no part. It
 * is for the requests that the port refuses with no other call to the service.
 */
static void report_hidden_area(const struct crux_ssh_file_handle *handle,
                               const crux_file_area_t *area)
{
    crux_file_info_t ignored;
    (void)crux_file_service_stat(handle->session, area->name, "x", &ignored);
}

static void report_if_hidden(const struct crux_ssh_file_handle *handle,
                             const crux_file_area_t *area)
{
    area_rights_t rights = {false, false};
    if (!area_rights(handle, area, &rights)) {
        report_hidden_area(handle, area);
    }
}

/*
 * crux_file_service_resolve is the only parser of a path. A name that its rules refuse stops here,
 * but the event for it belongs to the service, which alone knows the session may see the area. So
 * this cuts off the first component, only to ask the service about the rest, and the path is
 * refused whatever the answer. The text goes no further than the rule that refuses it.
 *
 * A stat needs read permission, so a session that can only write asks with a rename of the text to
 * itself, which needs write permission. That changes nothing whatever the text is: File refuses a
 * rename onto its own name.
 */
static void report_refused_name(const struct crux_ssh_file_handle *handle, const char *path)
{
    const char *slash = path[0] == '/' ? strchr(path + 1, '/') : NULL;
    if (slash == NULL || (size_t)(slash - path) > AREA_NAME_MAX + 1u) {
        return;
    }
    char area_path[AREA_NAME_MAX + 2u];
    size_t length = (size_t)(slash - path);
    memcpy(area_path, path, length);
    area_path[length] = '\0';
    const crux_file_area_t *area = NULL;
    char name[NAME_CAPACITY];
    if (crux_file_service_resolve(area_path, &area, name, sizeof(name)) != CRUX_FILE_SERVICE_OK) {
        return;
    }
    area_rights_t rights = {false, false};
    crux_file_info_t ignored;
    if (!area_rights(handle, area, &rights)) {
        report_hidden_area(handle, area);
    } else if (rights.readable) {
        (void)crux_file_service_stat(handle->session, area->name, slash + 1, &ignored);
    } else {
        (void)crux_file_service_rename(handle->session, area->name, slash + 1, slash + 1);
    }
}

static path_kind_t path_resolve(const struct crux_ssh_file_handle *handle, const char *path,
                                const crux_file_area_t **area, char *name)
{
    if (path == NULL) {
        return PATH_REFUSED;
    }
    if (strcmp(path, "/") == 0) {
        return PATH_ROOT;
    }
    switch (crux_file_service_resolve(path, area, name, NAME_CAPACITY)) {
    case CRUX_FILE_SERVICE_OK:
        return name[0] == '\0' ? PATH_AREA : PATH_FILE;
    case CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT:
        report_refused_name(handle, path);
        return PATH_REFUSED;
    default:
        return PATH_REFUSED;
    }
}

static uint32_t directory_mode(area_rights_t rights)
{
    return CRUX_SSH_SFTP_MODE_DIRECTORY | (rights.readable ? 0555u : 0u) |
           (rights.writable ? 0300u : 0u);
}

static uint32_t file_mode(area_rights_t rights)
{
    return CRUX_SSH_SFTP_MODE_FILE | (rights.readable ? 0444u : 0u) |
           (rights.writable ? 0200u : 0u);
}

static void attributes_of_root(crux_ssh_sftp_attributes_t *attributes)
{
    attributes->size = 0;
    attributes->mode = CRUX_SSH_SFTP_MODE_DIRECTORY | 0555u;
}

/* ---- Files ---- */

/* A flag set that no file service mode states gets no meaning here. */
static bool open_mode(int flags, crux_file_service_open_t *mode)
{
    bool reading = (flags & (CRUX_SSH_SFTP_O_RDONLY | CRUX_SSH_SFTP_O_RDWR)) != 0;
    bool writing = (flags & (CRUX_SSH_SFTP_O_WRONLY | CRUX_SSH_SFTP_O_RDWR)) != 0;
    bool create = (flags & CRUX_SSH_SFTP_O_CREAT) != 0;
    bool truncate = (flags & CRUX_SSH_SFTP_O_TRUNC) != 0;
    bool exclusive = (flags & CRUX_SSH_SFTP_O_EXCL) != 0;
    bool append = (flags & CRUX_SSH_SFTP_O_APPEND) != 0;

    if (!writing) {
        *mode = CRUX_FILE_SERVICE_OPEN_READ;
        return reading && !create && !truncate && !exclusive && !append;
    }
    if ((truncate || exclusive) && !create) {
        return false;
    }
    if (exclusive) {
        *mode = CRUX_FILE_SERVICE_OPEN_DIRECT_EXCLUSIVE;
    } else if (truncate) {
        *mode = CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE;
    } else if (create) {
        *mode = CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE;
    } else {
        *mode = CRUX_FILE_SERVICE_OPEN_DIRECT;
    }
    return true;
}

int crux_ssh_sftp_file_open(void *fs, const char *path, int flags)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    if (handle == NULL) {
        return -1;
    }
    const crux_file_area_t *area = NULL;
    char name[NAME_CAPACITY];
    path_kind_t kind = path_resolve(handle, path, &area, name);
    if (kind != PATH_FILE) {
        if (kind == PATH_AREA) {
            report_if_hidden(handle, area);
        }
        return -1;
    }
    crux_file_service_open_t mode = CRUX_FILE_SERVICE_OPEN_READ;
    if (!open_mode(flags, &mode)) {
        report_if_hidden(handle, area);
        return -1;
    }
    for (size_t i = 0; i < CRUX_SSH_SFTP_FILES_MAX; i++) {
        open_file_t *file = &handle->files[i];
        if (file->transfer == NULL) {
            /* wolfSSH hands the size attribute of OPEN to no port, so the size is unknown. */
            if (crux_file_service_open(handle->session, area->name, name, mode, 0,
                                       &file->transfer) != CRUX_FILE_SERVICE_OK) {
                return -1;
            }
            file->area = area;
            return slot_descriptor(handle, i);
        }
    }
    report_if_hidden(handle, area);
    return -1;
}

int crux_ssh_sftp_file_close(void *fs, int fd)
{
    /* A close needs no session, and it does not count as activity for one that has gone. */
    struct crux_ssh_file_handle *handle = fs;
    if (handle == NULL || !atomic_load(&handle->live)) {
        return -1;
    }
    open_file_t *file = file_from_descriptor(handle, fd);
    if (file == NULL) {
        return -1;
    }
    return crux_file_service_close(&file->transfer) == CRUX_FILE_SERVICE_OK ? 0 : -1;
}

static uint64_t offset_from_words(const uint32_t offset[2])
{
    return ((uint64_t)offset[1] << 32) | offset[0];
}

int crux_ssh_sftp_file_pread(void *fs, int fd, void *buffer, uint32_t size,
                             const uint32_t offset[2])
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    open_file_t *file = handle != NULL ? file_from_descriptor(handle, fd) : NULL;
    if (file == NULL || size > (uint32_t)INT_MAX) {
        return -1;
    }
    size_t count = 0;
    crux_file_service_result_t result = crux_file_service_read(
        handle->session, file->transfer, offset_from_words(offset), buffer, size, &count);
    if (result == CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE) {
        /* Past the end is the end to an SFTP client, which reads ahead of it as a matter of course.
         */
        return 0;
    }
    return result == CRUX_FILE_SERVICE_OK ? (int)count : -1;
}

static int write_at(struct crux_ssh_file_handle *handle, open_file_t *file, uint64_t offset,
                    const void *buffer, uint32_t size)
{
    if (size > (uint32_t)INT_MAX) {
        return -1;
    }
    size_t count = 0;
    return crux_file_service_write(handle->session, file->transfer, offset, buffer, size, &count) ==
                   CRUX_FILE_SERVICE_OK
               ? (int)count
               : -1;
}

int crux_ssh_sftp_file_pwrite(void *fs, int fd, const void *buffer, uint32_t size,
                              const uint32_t offset[2])
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    open_file_t *file = handle != NULL ? file_from_descriptor(handle, fd) : NULL;
    if (file == NULL) {
        return -1;
    }
    return write_at(handle, file, offset_from_words(offset), buffer, size);
}

int crux_ssh_sftp_file_append(void *fs, int fd, const void *buffer, uint32_t size)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    open_file_t *file = handle != NULL ? file_from_descriptor(handle, fd) : NULL;
    uint64_t end = 0;
    if (file == NULL ||
        crux_file_service_size(handle->session, file->transfer, &end) != CRUX_FILE_SERVICE_OK) {
        return -1;
    }
    return write_at(handle, file, end, buffer, size);
}

int crux_ssh_sftp_file_attributes(void *fs, int fd, crux_ssh_sftp_attributes_t *attributes)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    open_file_t *file = handle != NULL ? file_from_descriptor(handle, fd) : NULL;
    area_rights_t rights = {false, false};
    if (file == NULL || attributes == NULL ||
        crux_file_service_size(handle->session, file->transfer, &attributes->size) !=
            CRUX_FILE_SERVICE_OK ||
        !area_rights(handle, file->area, &rights)) {
        return -1;
    }
    attributes->mode = file_mode(rights);
    return 0;
}

int crux_ssh_sftp_file_set_attributes(void *fs, int fd)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    open_file_t *file = handle != NULL ? file_from_descriptor(handle, fd) : NULL;
    uint64_t ignored = 0;
    /* Asking for the size has the service check the session and the permission, as for any chunk.
     */
    return file != NULL && crux_file_service_size(handle->session, file->transfer, &ignored) ==
                               CRUX_FILE_SERVICE_OK
               ? 0
               : -1;
}

/* ---- Names ---- */

int crux_ssh_sftp_remove(void *fs, const char *path)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    if (handle == NULL) {
        return -1;
    }
    const crux_file_area_t *area = NULL;
    char name[NAME_CAPACITY];
    path_kind_t kind = path_resolve(handle, path, &area, name);
    if (kind != PATH_FILE && kind != PATH_AREA) {
        return -1;
    }
    /* For an area the name is empty, which the service refuses and records, after the permission.
     */
    crux_file_service_result_t result = crux_file_service_remove(handle->session, area->name, name);
    return result == CRUX_FILE_SERVICE_OK && kind == PATH_FILE ? 0 : -1;
}

int crux_ssh_sftp_rename(void *fs, const char *from, const char *to)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    if (handle == NULL) {
        return -1;
    }
    const crux_file_area_t *from_area = NULL;
    const crux_file_area_t *to_area = NULL;
    char from_name[NAME_CAPACITY];
    char to_name[NAME_CAPACITY];
    path_kind_t from_kind = path_resolve(handle, from, &from_area, from_name);
    if (from_kind == PATH_REFUSED) {
        return -1;
    }
    path_kind_t to_kind = path_resolve(handle, to, &to_area, to_name);
    bool from_named = from_kind == PATH_AREA || from_kind == PATH_FILE;
    bool to_named = to_kind == PATH_AREA || to_kind == PATH_FILE;

    if (from_named && to_named && from_area == to_area) {
        /* An area's name is empty, which the service refuses and records, after the permission. */
        crux_file_service_result_t result =
            crux_file_service_rename(handle->session, from_area->name, from_name, to_name);
        return result == CRUX_FILE_SERVICE_OK && from_kind == PATH_FILE && to_kind == PATH_FILE
                   ? 0
                   : -1;
    }
    /* Rename stays within one area, and the root is not a file. The service has no call for what
     * the port refuses here, but a hidden area named in it is still direct use of that area. */
    if (from_named) {
        report_if_hidden(handle, from_area);
    }
    if (to_named) {
        report_if_hidden(handle, to_area);
    }
    return -1;
}

int crux_ssh_sftp_attributes(void *fs, const char *path, crux_ssh_sftp_attributes_t *attributes)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    if (handle == NULL || attributes == NULL) {
        return -1;
    }
    const crux_file_area_t *area = NULL;
    char name[NAME_CAPACITY];
    area_rights_t rights = {false, false};
    crux_status_t ignored = CRUX_OK;
    crux_file_info_t info = {0};

    switch (path_resolve(handle, path, &area, name)) {
    case PATH_ROOT:
        if (!session_usable(handle->session, &ignored)) {
            return -1;
        }
        attributes_of_root(attributes);
        return 0;
    case PATH_AREA:
        if (!area_rights(handle, area, &rights)) {
            report_hidden_area(handle, area);
            return -1;
        }
        attributes->size = 0;
        attributes->mode = directory_mode(rights);
        return 0;
    case PATH_FILE:
        if (crux_file_service_stat(handle->session, area->name, name, &info) !=
                CRUX_FILE_SERVICE_OK ||
            !area_rights(handle, area, &rights)) {
            return -1;
        }
        attributes->size = info.size;
        attributes->mode = file_mode(rights);
        return 0;
    default:
        return -1;
    }
}

/*
 * There are no permission bits or times to set (F6), and a client that sends them must not see its
 * upload fail. It costs no storage, and it needs only an area the session can see, so that a client
 * with write permission and no read permission can still finish an upload.
 */
int crux_ssh_sftp_set_attributes(void *fs, const char *path)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    if (handle == NULL) {
        return -1;
    }
    const crux_file_area_t *area = NULL;
    char name[NAME_CAPACITY];
    area_rights_t rights = {false, false};
    crux_status_t ignored = CRUX_OK;

    switch (path_resolve(handle, path, &area, name)) {
    case PATH_ROOT:
        return session_usable(handle->session, &ignored) ? 0 : -1;
    case PATH_AREA:
    case PATH_FILE:
        if (!area_rights(handle, area, &rights)) {
            report_hidden_area(handle, area);
            return -1;
        }
        return 0;
    default:
        return -1;
    }
}

/* ---- Listings ---- */

int crux_ssh_sftp_dir_open(void *fs, const char *path, int *dir)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    if (handle == NULL || dir == NULL) {
        return -1;
    }
    const crux_file_area_t *area = NULL;
    char name[NAME_CAPACITY];
    path_kind_t kind = path_resolve(handle, path, &area, name);
    if (kind != PATH_ROOT && kind != PATH_AREA) {
        if (kind == PATH_FILE) {
            report_if_hidden(handle, area);
        }
        return -1;
    }
    size_t slot = 0;
    while (slot < CRUX_SSH_SFTP_DIRS_MAX && handle->dirs[slot].used) {
        slot++;
    }
    if (slot == CRUX_SSH_SFTP_DIRS_MAX) {
        if (kind == PATH_AREA) {
            report_if_hidden(handle, area);
        }
        return -1;
    }
    open_dir_t opened = {.used = true, .area = area};
    if (kind == PATH_ROOT) {
        crux_status_t ignored = CRUX_OK;
        if (!session_usable(handle->session, &ignored)) {
            return -1;
        }
    } else {
        area_rights_t rights = {false, false};
        /* A listing is held from here, not from the first read, because wolfSSH turns a failure of
         * a read into the end of a listing: a client could not tell a busy service from an empty
         * area. A listing that is opened and never read holds it until the client closes it. */
        if (crux_file_service_dir_open(handle->session, area->name, &opened.listing) !=
            CRUX_FILE_SERVICE_OK) {
            return -1;
        }
        if (!area_rights(handle, area, &rights)) {
            (void)crux_file_service_dir_close(&opened.listing);
            return -1;
        }
        opened.entry_mode = file_mode(rights);
    }
    handle->dirs[slot] = opened;
    *dir = slot_descriptor(handle, slot);
    return 0;
}

static int dir_read_root(struct crux_ssh_file_handle *handle, open_dir_t *dir, char *name,
                         crux_ssh_sftp_attributes_t *attributes)
{
    const crux_file_area_t *area = NULL;
    area_rights_t rights = {false, false};
    crux_file_service_result_t result =
        crux_file_service_area_next(handle->session, &dir->cursor, &area, &rights.writable);
    if (result == CRUX_FILE_SERVICE_END) {
        dir->finished = true;
        return 1;
    }
    if (result != CRUX_FILE_SERVICE_OK ||
        crux_access_permitted(handle->session, area->read_permission, &rights.readable) !=
            CRUX_ACCESS_OK) {
        dir->finished = true;
        return -1;
    }
    /* The service holds an area's name to 15 characters, and capacity is at least 64. */
    memcpy(name, area->name, strlen(area->name) + 1u);
    attributes->size = 0;
    attributes->mode = directory_mode(rights);
    return 0;
}

static int dir_read_area(struct crux_ssh_file_handle *handle, open_dir_t *dir, char *name,
                         size_t capacity, crux_ssh_sftp_attributes_t *attributes)
{
    crux_file_info_t info = {0};
    bool end = false;
    crux_file_service_result_t result =
        crux_file_service_dir_read(handle->session, dir->listing, name, capacity, &info, &end);
    if (result != CRUX_FILE_SERVICE_OK || end) {
        /* The listing of the service comes back as soon as this one has nothing more to give. */
        dir->finished = true;
        (void)crux_file_service_dir_close(&dir->listing);
        return result == CRUX_FILE_SERVICE_OK ? 1 : -1;
    }
    attributes->size = info.size;
    attributes->mode = dir->entry_mode;
    return 0;
}

int crux_ssh_sftp_dir_read(void *fs, int dir, char *name, size_t capacity,
                           crux_ssh_sftp_attributes_t *attributes)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    open_dir_t *opened = handle != NULL ? dir_from_descriptor(handle, dir) : NULL;
    if (opened == NULL || name == NULL || attributes == NULL || capacity < NAME_CAPACITY) {
        return -1;
    }
    if (opened->finished) {
        return 1;
    }
    return opened->area == NULL ? dir_read_root(handle, opened, name, attributes)
                                : dir_read_area(handle, opened, name, capacity, attributes);
}

int crux_ssh_sftp_dir_close(void *fs, int dir)
{
    struct crux_ssh_file_handle *handle = fs;
    if (handle == NULL || !atomic_load(&handle->live)) {
        return -1;
    }
    open_dir_t *opened = dir_from_descriptor(handle, dir);
    if (opened == NULL) {
        return -1;
    }
    dir_give_back(opened);
    memset(opened, 0, sizeof(*opened));
    return 0;
}

int crux_ssh_sftp_directory_refused(void *fs, const char *path)
{
    struct crux_ssh_file_handle *handle = session_enter(fs);
    if (handle != NULL) {
        const crux_file_area_t *area = NULL;
        char name[NAME_CAPACITY];
        path_kind_t kind = path_resolve(handle, path, &area, name);
        if (kind == PATH_AREA || kind == PATH_FILE) {
            report_if_hidden(handle, area);
        }
    }
    return 0;
}

char *crux_ssh_sftp_start_directory(char *buffer, size_t size)
{
    if (buffer == NULL || size < 2u) {
        return NULL;
    }
    buffer[0] = '/';
    buffer[1] = '\0';
    return buffer;
}
