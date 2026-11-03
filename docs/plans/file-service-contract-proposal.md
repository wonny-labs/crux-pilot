# File contract extension and file service proposal

Status: accepted by [the owner's decision of 3 November 2026](https://github.com/wonny-labs/crux-pilot/pull/209#issuecomment-6616400894), Issue #179: F1 to F6, with F4 for now. Implementing its headers, fakes and Host tests is approved. Target and physical work stays behind M7 and needs its own approval, and no merge is approved. It refines the file sections of
[the secure management design](secure-management-design.md) and [the CLI and SFTP design](secure-management-cli-sftp-design.md).
Baseline: main `4444d70`, Zephyr v4.4.2
`dccb095`, FatFs `f4ead3b`, wolfSSH v1.6.0 `8221e4e`, and WinSCP source `246bbd4`, which is the current upstream tip and not
a released version.

## Decisions

The owner accepted the recommendation of every row below.

| ID | Decision | Recommendation | Impact if rejected |
| --- | --- | --- | --- |
| F1 | Add File *roots*, so that one volume can serve several directories, through new functions. The existing functions keep working on root 0. | Accept | Prefixed flat names in one directory cost name length and give the credential store no separate location |
| F2 | `crux_file_rename` refuses an existing destination. The service composes replacement from remove and rename. | Accept | A replacing rename inherits `fatfs_rename`'s removal of the destination while appearing to be one operation |
| F3 | Directory listing uses a bounded handle, not a callback or an index | Accept | A callback cannot serve SFTP's separate `READDIR` requests, and an index is quadratic and unstable |
| F4 | Keep the File name rule: `[A-Za-z0-9_.-]`, 1 to 63 characters | Accept for now, unless product users need spaces. A later widening is additive. | Widening needs a new conformance run. Under this rule WinSCP cannot upload a name with a space, and a name of 55 to 63 characters gets no `.filepart` above its resume threshold, as stated below. |
| F5 | Stale partial rule: partial files survive a reset and are removed only when a new write needs the room | Accept | Removing at each boot loses resume across resets. Keeping them for ever can fill an area. |
| F6 | No modification time in the File contract | Accept | See the WinSCP table |

## Trust boundary

The service stops a remote client with a valid session from reaching anything but declared areas, and from acting without the
area's permission. It does not isolate code in the image, and File names are not a security sandbox: the contract says so, and
that stays true. Enforcement lives in the service, in one place, and the SFTP and web adapters only translate.

| Threat | Response | Limit |
| --- | --- | --- |
| Traversal through a client path | One bounded parser, then the File name rule, which refuses `/`, `..` and anything outside the character set | The FatFs provider also hex-encodes names, as a second layer. |
| Reaching credential records | Store roots cannot be declared as areas | Code in the image, and a host with a card reader, can still reach them |
| Filling the medium | Per-file, per-area and entry limits, checked before writing | Limits are declared by the composition. The medium can still fill for other reasons, and that returns no-space. |
| Discovering hidden areas | An unpermitted area looks like a missing one to the client, and the check produces an event | Timing is not equalised |
| Handle theft between sessions | A transfer belongs to the session that opened it | |
| Executing uploads from the device origin | Web adapter sends attachment downloads, stated in the web design | Outside this contract |

## Component layout

| Item | Location | Users |
| --- | --- | --- |
| File contract additions | `components/file/include/crux/file.h`, target `crux::file` | File service, credential store, projects |
| File service | `components/file_service/include/crux/file_service.h`, target `crux::file_service` | SFTP and web adapters, projects |
| Root binding | `crux_file_zephyr_bind_root` in `file_native.h`, `crux_file_linux_bind_root` in `file_linux.h` | Composition only |

## File contract additions

```c
/** Index of a directory that the composition bound. Root 0 is the one the existing functions use. */
typedef uint8_t crux_file_root_t;
enum { CRUX_FILE_ROOT_DEFAULT = 0 };

/* <crux/file.h> gains <stdbool.h>, for crux_file_dir_read's end output. */

/** Opaque directory listing handle; only crux_file_dir_close() retires it. */
typedef struct crux_file_dir crux_file_dir_t;

typedef struct {
    uint64_t size; /**< Bytes. */
} crux_file_info_t;

/* crux_file_result_t gains CRUX_FILE_ERR_EXISTS = -11. Existing values are unchanged. */

crux_file_result_t crux_file_open_at(crux_file_root_t root, const char *name, crux_file_mode_t mode, crux_file_t **out);
crux_file_result_t crux_file_remove_at(crux_file_root_t root, const char *name);
crux_file_result_t crux_file_stat(crux_file_root_t root, const char *name, crux_file_info_t *out);
crux_file_result_t crux_file_size(crux_file_t *file, uint64_t *size);
crux_file_result_t crux_file_rename(crux_file_root_t root, const char *from, const char *to);
crux_file_result_t crux_file_dir_open(crux_file_root_t root, crux_file_dir_t **out);
crux_file_result_t crux_file_dir_read(crux_file_dir_t *dir, char *name, size_t capacity, crux_file_info_t *info,
                                      bool *end);
crux_file_result_t crux_file_dir_close(crux_file_dir_t **dir);
```

The rules of the existing contract apply: thread context, synchronous, no ISR use, buffers borrowed for the call, and no retry.

- **Roots.** `crux_file_open(n, m, o)` is exactly `crux_file_open_at(CRUX_FILE_ROOT_DEFAULT, n, m, o)`, and likewise for remove.
  The conflict rule compares root and name together. `CRUX_FILE_ROOTS_MAX` is a build setting, default 8. An unbound root returns
  `CRUX_FILE_ERR_NOT_READY`, as an unbound provider does today. Binding is composition-only and once per index, and the
  existing `crux_file_zephyr_bind` binds root 0 with its existing behaviour. A bind whose path is equal to, inside or a parent of
  another bound root returns `CRUX_FILE_ERR_INVALID_STATE`.
- **Stat.** Regular files only. A name that fails the name rule is `INVALID_ARGUMENT`, as for open, and a directory is `NOT_FOUND`. `size` is what the provider last
  recorded, and a file with an open writer may report an older size. `crux_file_size` gives a handle's current size, using the same mechanism as seek's end check, and it preserves the position. Neither implies durability.
- **Rename.** Atomic only in the sense that no other Crux File call sees a partial state, because it runs under the File lock. It
  is not atomic across power loss. Checks run in this order: argument validation, then `INVALID_STATE` if either name has a live
  handle, then `NOT_FOUND` for a missing `from`, then `CRUX_FILE_ERR_EXISTS` for an existing `to`, including `from` equal to `to`.
  Renames are within one root, because the service has no cross-area rename.
- **Listing.** `crux_file_dir_read` returns entries in provider order and sets `end` at the finish. It yields only regular files
  whose names pass the name rule, so foreign files and subdirectories are skipped without an error. On Linux, symbolic links are
  skipped too, matching the `O_NOFOLLOW` in the provider's open. `CRUX_FILE_ERR_OUT_OF_RANGE` means the name buffer was too small,
  and a capacity of 64 is always enough for the minimum profile. An entry created or removed during a listing may be missed or
  repeated. A bounded pool of `CRUX_FILE_DIR_POOL_SIZE`, default 2, holds handles, and `CONFIG_FS_FATFS_NUM_DIRS` (default 4) must
  be at least that. A full pool gives `NO_RESOURCES`.

These are the private additions, on `file_port.h` and the bindings:

```c
/* crux_file_impl_open and crux_file_impl_remove gain crux_file_root_t root as their first parameter. */
crux_file_result_t crux_file_impl_stat(crux_file_root_t root, const char *name, crux_file_info_t *out);
crux_file_result_t crux_file_impl_size(size_t slot, uint64_t *size);
crux_file_result_t crux_file_impl_rename(crux_file_root_t root, const char *from, const char *to);
crux_file_result_t crux_file_impl_dir_open(size_t dir_slot, crux_file_root_t root);
crux_file_result_t crux_file_impl_dir_read(size_t dir_slot, char *name, size_t capacity, crux_file_info_t *info,
                                           bool *end);
crux_file_result_t crux_file_impl_dir_close(size_t dir_slot);

crux_file_result_t crux_file_zephyr_bind_root(crux_file_root_t root, const char *path);
crux_file_result_t crux_file_linux_bind_root(crux_file_root_t root, const char *path);
```

`CRUX_FILE_POOL_SIZE` moves from `file_port.h` to a public compile definition of `crux::file`, with the same value, so that the file service's build check below can read it.

### Native mapping

The FatFs provider stores a File name as `f` followed by the hex of its bytes (`make_path` in `file_native.c`). Listing decodes
this and skips anything that does not decode to a valid name, including upper-case hex, so one file has one identity. Files
written by a host under other names are invisible, as they are today.

`fatfs_rename` in Zephyr's `subsys/fs/fat_fs.c` calls `f_unlink` on an existing destination before `f_rename`. The provider
therefore calls `fs_stat` on the destination first, under the File lock, and never reaches that path. The Linux provider uses
`fstatat` then `renameat`, also under the lock, so both providers have the same no-replace behaviour. Native clients that bypass
File remain outside the composition, as the contract says.

`struct fs_dirent` in Zephyr carries type, name and size, with `size` a `size_t`. It has no modification time, and
`CONFIG_FS_FATFS_HAS_RTC` is off, so FatFs uses a hardcoded date. This is why decision F6 omits time. `crux_file_info_t` is only an output, so a field can be added later without breaking a consumer's source.

## Backward compatibility

The qualified contract stays true for existing consumers, and the proposal changes no existing assertion.

| Item | Change | Effect on qualified evidence |
| --- | --- | --- |
| `crux_file_open`, read, write, seek, sync, close, remove | No signature or behaviour change on root 0 | Their tests in `tests/file` and `tests/file_consumer` must pass unchanged |
| `crux_file_result_t` | One new negative value | Compare by name, as the result convention requires. Existing consumers never receive it from existing functions. |
| `crux_file_zephyr_bind`, `crux_file_linux_bind` | Unchanged | |
| Name rule, pool size, handle rules | Unchanged | |
| Private port in `file_port.h` | `crux_file_impl_open` and `_remove` gain a root, and new functions appear | `tests/file/fault_test.c` implements this port, so its fixture changes, while its assertions do not |
| `file_native.c` | One root becomes an array | Rerun File's Host and `native_sim` suites and `scripts/test-file-rt1060-compile.sh`, and record a new software result. |
| `docs/file-contract.md` | "Directory trees, rename/atomic replace, append, sparse files, metadata, permissions" is out of scope today. On adoption, listing, stat, size metadata and rename move in scope. Atomic replace, append, sparse files and permissions stay out. | The M4 approval covered the old text, so adoption needs its own review |

Accepted evidence stays as recorded and is not edited. The physical runs under `docs/evidence` remain results for the revisions
they name. They do not extend to the new operations, and the new code path does not re-qualify them. The 100-cut replace test
used the same `fs_rename`, so it is relevant evidence for rename on that one card, but the new sequences have not been cut.

Scripts under `scripts/` are protected. The AGENTS rule allows editing the test runner of a component a work package changes
if no suite, board, build option or warning flag is removed. A new runner for `file_service` is a new check script.

## File service

### Area declaration

```c
typedef struct {
    const char *name;            /* Client-visible, 1 to 15 characters of [a-z0-9_-]. */
    crux_file_root_t root;
    const char *read_permission; /* Required. */
    const char *write_permission; /* Required for a writable area, otherwise NULL. */
    uint64_t max_file_bytes;
    uint64_t max_area_bytes;     /* Sum of file sizes, partial files included. */
    uint16_t max_entries;
    const char *partial_suffix;  /* Files the stale rule may remove, for example ".filepart". NULL means none are. */
    uint8_t write_modes;         /* CRUX_FILE_SERVICE_MANAGED, CRUX_FILE_SERVICE_DIRECT, or both. */
} crux_file_area_t;

enum { CRUX_FILE_SERVICE_MANAGED = 1u << 0, CRUX_FILE_SERVICE_DIRECT = 1u << 1 };

typedef struct {
    const crux_file_area_t *areas;
    size_t area_count;
    const crux_file_root_t *reserved_roots; /* Roots no area may use, from crux_credential_roots. */
    size_t reserved_root_count;
    bool no_credential_store;               /* Required to allow an empty reserved list. */
} crux_file_service_config_t;

crux_file_service_result_t crux_file_service_start(const crux_file_service_config_t *config);
```

`crux/file_service.h` includes `<crux/access.h>`, `<crux/file.h>`, `<stdbool.h>`, `<stddef.h>` and `<stdint.h>`.

`crux_file_service_start` takes the config once, validates the whole table and scans each area. It refuses a duplicate name or
root, a reserved root, an empty reserved list unless `no_credential_store` is set, zero limits, a `max_file_bytes` above `INT32_MAX` (the provider's seek limit), a missing permission, and a
writable area with no mode. On failure every operation returns `CRUX_FILE_SERVICE_ERR_NOT_READY`, which fails closed. The table
stays borrowed for the program's life. An area whose root is not ready at start returns `ERR_NOT_READY` from its operations until a reset, and it does not stop the others.

### Operations

```c
typedef enum {
    CRUX_FILE_SERVICE_OK = 0,
    CRUX_FILE_SERVICE_END = 1,                   /* A listing of areas finished. */
    CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT = -1, /* Includes a name or path that the rules refuse. */
    CRUX_FILE_SERVICE_ERR_NOT_READY = -2,
    CRUX_FILE_SERVICE_ERR_SESSION_INVALID = -3,
    CRUX_FILE_SERVICE_ERR_DENIED = -4,
    CRUX_FILE_SERVICE_ERR_NOT_FOUND = -5,        /* Missing, or an area the session may not see. */
    CRUX_FILE_SERVICE_ERR_EXISTS = -6,
    CRUX_FILE_SERVICE_ERR_BUSY = -7,             /* A conflicting open or reservation. */
    CRUX_FILE_SERVICE_ERR_NO_SPACE = -8,         /* The medium is full. */
    CRUX_FILE_SERVICE_ERR_LIMIT = -9,            /* A declared limit would be exceeded. */
    CRUX_FILE_SERVICE_ERR_NO_RESOURCES = -10,
    CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE = -11,    /* A sparse write, or an offset past the end. */
    CRUX_FILE_SERVICE_ERR_IO = -12
} crux_file_service_result_t;

typedef struct crux_file_service_dir crux_file_service_dir_t;           /* Opaque, from a bounded pool. */
typedef struct crux_file_service_transfer crux_file_service_transfer_t; /* Opaque, from a bounded pool. */

crux_file_service_result_t crux_file_service_resolve(const char *path, const crux_file_area_t **area, char *name,
                                                     size_t capacity);
crux_file_service_result_t crux_file_service_area_next(crux_access_session_t session, size_t *cursor,
                                                       const crux_file_area_t **area, bool *writable);
crux_file_service_result_t crux_file_service_dir_open(crux_access_session_t session, const char *area,
                                                      crux_file_service_dir_t **out);
crux_file_service_result_t crux_file_service_dir_read(crux_access_session_t session, crux_file_service_dir_t *dir,
                                                      char *name, size_t capacity, crux_file_info_t *info, bool *end);
crux_file_service_result_t crux_file_service_dir_close(crux_file_service_dir_t **dir);
crux_file_service_result_t crux_file_service_stat(crux_access_session_t session, const char *area, const char *name,
                                                  crux_file_info_t *out);
crux_file_service_result_t crux_file_service_remove(crux_access_session_t session, const char *area, const char *name);
crux_file_service_result_t crux_file_service_rename(crux_access_session_t session, const char *area, const char *from,
                                                    const char *to);

typedef enum {
    CRUX_FILE_SERVICE_OPEN_READ = 1,
    CRUX_FILE_SERVICE_OPEN_MANAGED = 2,      /* Write a temporary file, published by commit. */
    CRUX_FILE_SERVICE_OPEN_DIRECT = 3,       /* The named file must exist. */
    CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE = 4, /* Open, or create if missing. */
    CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE = 5,
    CRUX_FILE_SERVICE_OPEN_DIRECT_EXCLUSIVE = 6
} crux_file_service_open_t;

crux_file_service_result_t crux_file_service_open(crux_access_session_t session, const char *area, const char *name,
                                                  crux_file_service_open_t mode, uint64_t declared_size,
                                                  crux_file_service_transfer_t **out);
crux_file_service_result_t crux_file_service_read(crux_access_session_t session, crux_file_service_transfer_t *transfer,
                                                  uint64_t offset, void *buffer, size_t length, size_t *transferred);
crux_file_service_result_t crux_file_service_write(crux_access_session_t session, crux_file_service_transfer_t *transfer,
                                                   uint64_t offset, const void *buffer, size_t length,
                                                   size_t *transferred);
crux_file_service_result_t crux_file_service_size(crux_access_session_t session, crux_file_service_transfer_t *transfer,
                                                  uint64_t *size);
crux_file_service_result_t crux_file_service_commit(crux_access_session_t session,
                                                    crux_file_service_transfer_t **transfer);
crux_file_service_result_t crux_file_service_close(crux_file_service_transfer_t **transfer);
```

- Every operation that takes a session checks it and the area's permission. A session that ends mid-transfer fails its next
  chunk with `SESSION_INVALID`, and the adapter then calls `crux_file_service_close`, which needs no session, so a handle is
  always returned.
- `crux_file_service_area_next` uses the quiet permission probe, request AC1 below, so hiding an area leaves no denial event. Any
  other operation calls `crux_access_check`, which produces the event. An area the session has neither permission on returns
  `NOT_FOUND`, a write on a read-only session's area returns `DENIED`, and a read on a write-only session's area returns `DENIED`. `crux_file_service_area_next` starts with `*cursor` at zero and returns `CRUX_FILE_SERVICE_END` when finished.
- All operations can wait on storage, so a web adapter runs them on its worker, and SSH runs them on the session thread.
  `crux_file_service_resolve` touches no storage. Only one service lock is used, and it is never held across storage: it guards area
  counters, the transfer pool and the name reservations below. `crux_file_lock` can wait without limit behind another File call, so no service operation promises a time bound.
- Reserved temporary names begin with `.crux-`. A client cannot open, stat, remove or rename such a name, and listing hides it.

### Path resolution

`crux_file_service_resolve` is the only path parser. It accepts `/area` and `/area/name`, with one optional trailing slash on an
area, and nothing else: no empty component, `.` or `..`, more than two components, or a backslash. The total length is bounded by 80
bytes. Protocols that carry length-delimited strings reject an embedded NUL first, as the access proposal requires for user names.
wolfSSH normalises paths with `wolfSSH_RealPath` before its file macros, and the adapter resolves the result again, so this parser
and not the normalisation is the check. `wolfSSH_SFTP_SetConfinePath` is a second defence only.

### Capacity

Each area keeps `used_bytes` and `entries` in RAM. `crux_file_service_start` computes them by scanning the area. Before a write, under the service lock, the service checks `offset + length` for overflow, which gives `ERR_OUT_OF_RANGE`, then reserves the bytes that would grow the file, so two transfers cannot both pass the same check. A write that would cross `max_file_bytes`
or `max_area_bytes` returns `ERR_LIMIT` with nothing written. A create reserves an entry the same way. Afterwards it settles the reservation against the accepted count. `declared_size` lets a managed upload fail before any data arrives, and it
is zero when unknown. A failed partial write updates the counters from the accepted count. If the medium is full first, the result is
`ERR_NO_SPACE` from File.

A scan that meets a read error marks the area not ready, and the area returns `ERR_IO` until a reset. Counters do not survive a reset.

### Managed upload

For the web interface, or any caller that wants one file to appear only when it is complete.

1. `open` with `OPEN_MANAGED` checks permission, the name and `declared_size`, and takes the temporary name `.crux-up-<slot>`.
2. `write` appends. Offset must equal the current position.
3. `commit` syncs and closes the temporary file, then writes and syncs a small sidecar, `.crux-co-<slot>`, holding the target name. It removes an existing target, renames, and removes the sidecar. The transfer handle is consumed. A reader holding the target gives `ERR_BUSY`, and nothing changes.
4. `close` without commit is the abort: it closes and removes the temporary file.

A slot whose temporary file or sidecar still exists is skipped, so a kept file is never reused and an old sidecar can never name a new upload. If no slot is free, the result is `ERR_NO_RESOURCES`.

### Direct write

For SFTP. The client owns its temporary names, resume and rename, so an abort or disconnect leaves the partial file where it is. The
adapter maps SFTP open flags as follows. Before any storage call, the service records the area and name in a bounded table of `CRUX_FILE_SERVICE_RESERVATIONS` (default 8) under its lock, and a second writer for that name gets `ERR_BUSY`, so a stat and the open that follows cannot be split by another client. Remove reserves one name for the call, rename two, and a managed upload reserves its target until commit or close. A full table, or a full transfer pool, gives `ERR_NO_RESOURCES`.

| SFTP flags | Mode | File operations |
| --- | --- | --- |
| `READ` | `OPEN_READ` | `CRUX_FILE_READ` |
| `WRITE` | `OPEN_DIRECT` | `CRUX_FILE_READ_WRITE`. Not found is an error. |
| `WRITE`, `CREAT` | `OPEN_DIRECT_CREATE` | `CRUX_FILE_READ_WRITE`, and on not found `CRUX_FILE_CREATE_REPLACE` |
| `WRITE`, `CREAT`, `TRUNC` | `OPEN_DIRECT_TRUNCATE` | `CRUX_FILE_CREATE_REPLACE`, with the counters adjusted first |
| `WRITE`, `CREAT`, `EXCL` | `OPEN_DIRECT_EXCLUSIVE` | Stat, then `CRUX_FILE_ERR_EXISTS` or create |
| `APPEND`, with the above | As above | The adapter records the flag and passes the offset from `crux_file_service_size` on each write. wolfSSH calls `WWRITE` for this case when the port defines it. |

A write's offset may be at or below the current size. An offset past it returns `ERR_OUT_OF_RANGE`, because File does not create sparse
files, so a client that writes out of order fails rather than leaving a hole. wolfSSH passes each write's offset through `WPWRITE`, and
the service only calls `crux_file_seek` when the offset differs from the tracked position, because the provider's seek reads the end of the
file each time.

### Replacement and power loss

**Neither mode provides atomic replacement across power loss, and results must say so.** The windows are real:

- Commit removes the target and then renames. A cut between them leaves no target and a complete temporary file, which the sidecar lets the service finish at start. The earlier
  replace-pattern run on one card reached that state in 29 of 100 cuts, in a loop that did nothing else.
- FatFs marks part of its rename as a critical section, where an interruption can cause a cross-link. `f_rename` registers the new
  entry and then removes the old one, so if they are in different sectors, a cut between the writes can leave two names on one
  cluster chain. This comes from reading `ff.c` at the pinned revision. It has not been reproduced here.
- Orphaned clusters remain after such cuts. The same run found 40 orphaned clusters, about 320 KiB.
- SFTP clients have the same windows, and WinSCP's own sequence is delete then rename.

### Stale partial files

The rule depends on the file's owner.

| File | At start | Later |
| --- | --- | --- |
| `.crux-up-*` with no valid sidecar | Removed. No removal or rename began, because the sidecar is written and synced first. | Removed on abort |
| Sidecar, temporary, no target | The rename is completed, as `sd_settings_recover` does for Settings, and the sidecar removed | |
| Sidecar, temporary, a target of a different size | Temporary and sidecar removed. The removal had not begun, so the old target stands. | |
| Sidecar, temporary, a target of the same size | Kept, with event `file.recovery_needed`. It may be an unfinished removal, or it may share clusters with the target after a cut during rename, and removing it would free them. | Counts against the limit, and its slot is skipped. Removed only by a host repair, or by the composition when it declares that the risk is accepted. |
| Sidecar with no temporary, valid or torn | Sidecar removed. The commit had finished, or had not reached removal. | |
| A file ending in the area's `partial_suffix` | Kept, because a client may resume it | When a new create or write would hit a limit, the service removes such files that have no live handle, in listing order, with event `file.partial_removed`, then retries once |

No clock is needed, and a client whose partial was removed starts again. A direct write always leaves its partial file in place. The rule cannot tell a client's interrupted upload from a file that
merely has that suffix, so the suffix belongs to a declaration by the composition and is `NULL` for an area that wants none removed.

### Conflicts

File's single-writer rule stays. A conflicting open, or a remove or rename of an open file, gives `ERR_BUSY`, which the adapter maps to a
protocol failure. `CRUX_FILE_SERVICE_TRANSFER_MAX`, default 3, bounds open transfers, and it must stay below `CRUX_FILE_POOL_SIZE`, default 4,
so that one handle stays free from the service for the credential store and the project, although project code could still take it. The build fails if it does not. The web design's rule of one bulk
transfer at a time remains the adapter's. The File lock is global and held across each storage call, so one slow card write delays other
File callers, including a project's. This already holds, and M10 measures it.

### Events

`file.created`, `file.removed`, `file.renamed`, `file.partial_removed`, `file.recovery_needed` and `file.name_rejected` under Issue #182. A
denial comes from `crux_access_check`. A rejected name is not logged, because a client's text can hold anything. Only the area is recorded.

## What WinSCP needs

Sources are the WinSCP documentation and `source/core/SftpFileSystem.cpp`. A real client trace is still required, under M12.

| Behaviour | Evidence | This contract | Open risk |
| --- | --- | --- | --- |
| Upload above the threshold (100 KB initially) goes to `<name>.filepart`, then is renamed | Documentation and source | Direct write, rename | None known |
| With an existing target, WinSCP deletes the target and then sends rename, unless the session opts into `posix-rename@openssh.com` (off by default in `SessionData.cpp`) | Source | No-replace rename is enough | `wolfsftp.c` has no handler for that extension. A client trace confirms the sequence. |
| Name plus `.filepart` must fit | Source: `PartialExt`, and `SftpFileSystem.cpp` retries on the target name when creating the partial fails, under the default smart resume setting | Up to 54 characters gets a `.filepart`. 55 to 63 characters falls back to writing the target directly, so an interruption leaves a partial under the real name. | Client trace confirms the fallback. Decision F4. |
| Resume after an interruption keeps the `.filepart` and continues it | Documentation. The request sequence is not confirmed. | Stat, write at the current size | Trace needed |
| Timestamp is preserved after upload, with a properties request. `PreserveTime` defaults to true in `CopyParam.cpp`. | Source | No time in File, so the adapter must accept and ignore it | See below |
| Lists from the virtual root | Design | `area_next`, then a directory listing | wolfSSH's `SFTP_GetAttributes` for the user file system must be written. |
| File names with spaces or non-ASCII | File rule | Refused | Decision F4. WinSCP's filename option does not cover this. |
| `MKDIR` | Design | Refused | |

**Finding for M12, `SETSTAT`.** The CLI and SFTP design says `SETSTAT` and `FSETSTAT` are accepted without effect. In `wolfsftp.c` these
two request cases are compiled only when `WOLFSSH_ZEPHYR` and `WOLFSSH_FATFS` are both undefined. Otherwise the request falls to the default case, which
returns `WOLFSSH_FTP_FAILURE`, "Unknown/Unsupported packet type". WinSCP treats that as an error after a successful upload: `IgnorePermErrors`
covers only a permission-denied status. With the macros compiled in, `SFTP_SetFileAttributes` also returns `WS_UNIMPLEMENTED_E` unless the port
defines `WSETTIME`, and it calls `WCHMOD` for permissions. M12 must prove one of these: a build without `WOLFSSH_ZEPHYR` that has no-op
`WSETTIME` and `WCHMOD`, a patch, or a documented client setting. This does not change the service contract. It is recorded here because the
owner requires WinSCP support and the current design statement does not yet hold for the Zephyr port as written. It is a source reading, with nothing run.

## Failure handling

| Failure | Result |
| --- | --- |
| Medium missing at start | Affected areas `NOT_READY`. Other areas work if their root is ready. |
| Medium removed while running | `ERR_IO` or `NOT_READY`. Reinsertion needs a reset, per the microSD composition. |
| Medium full | `ERR_NO_SPACE`, with the accepted count reported |
| Write shorter than asked (`CRUX_FILE_PARTIAL`) | The accepted count is returned with `OK` and a short `transferred`, and the adapter continues or fails it |
| Limit reached | `ERR_LIMIT`, after the stale partial rule |
| Conflicting open | `ERR_BUSY` |
| Session ended during a transfer | Next chunk `SESSION_INVALID`, and the adapter closes the transfer. A direct partial stays, and a managed temporary is removed. |
| Close reports an error | Passed on. The handle is consumed, as File requires. No success is reported for a commit. |
| Commit fails after removing the target | `ERR_IO`, with `file.commit_failed`. The temporary file and sidecar stay, and the next start completes the rename under the rule above. |

## Testing

| Level | Cases |
| --- | --- |
| File conformance, Host and `native_sim` FatFs | Listing, including foreign names, directories and a name buffer too small. Stat. Rename's order of checks, including destination existing, `from` equal to `to`, open handles on either name and a missing source. Roots: separation, the same name in two roots, an unbound root, overlapping paths. Existing suites pass unchanged on root 0. |
| Provider faults | Failed stat, rename and directory read, on the fault port. Rename when the destination appears to be removable never removes it. |
| Service permissions | Each operation with no session, an ended session, read-only, write-only and neither permission. Hidden areas in listings produce no event, and direct access does. A transfer used by a second session is refused. |
| Traversal | `..`, `.`, empty components, backslash, an overlong path, three components, embedded NUL at the adapter, control characters and reserved names |
| Capacity | Per-file, per-area and entry limits at and over the bound, recount at start, partial accounting with a short write, two concurrent transfers near the area limit, and an overflowing offset |
| Conflict | Second writer, reader during a writer, remove and rename of an open file, commit with a reader on the target, transfer pool and reservation table exhaustion, and a handle left free for the store |
| Interrupted writes | Abort and disconnect in each mode, restart with each stale state in the table above, a torn sidecar, a kept temporary whose slot is skipped, and a cut at each step of commit on the fake. Results assert which states appear, and they state that replacement is not atomic. |
| Secret and name absence | Rejected names never appear in events |
| SFTP adapter, under M12 | A file port against a fake service, OpenSSH scripted, and a recorded WinSCP run that becomes conformance cases |
| Physical, separately approved | Listing, rename and commit on the card, cuts during a rename, and stack use. None is implied by earlier runs. |

## Limits

- Replacement is not atomic across power loss, and a rename on FAT has a cross-link window.
- Listing is not a snapshot.
- Names are ASCII, flat and at most 63 characters.
- No modification time, permissions, directories or sparse writes. Files over 2 GiB are outside the profile, as the microSD composition states.
- Counters are recomputed at each start, and start time grows with the number of files, bounded by `max_entries`.
- A declared limit does not reserve room on the medium.
- Overlap between root paths is checked as text, so a FAT case alias or a Linux symbolic link in a composition's paths is not caught. Roots are composition data.
- A kept temporary needs a host repair.

## Changes other contracts need

- **AC1, access control:** `crux_access_permitted(session, permission, bool *permitted)` in the public `crux/access.h`, with no event, no touch and no
  storage. Services call it, so it cannot be adapter-only under decision A1. Hiding areas, parameters and commands would otherwise produce a denial event for every item hidden. This is an addition to the
  access proposal, which is not changed here. It also affects `crux_cli` and the configuration service.
- **CLI and SFTP design:** the `SETSTAT` statement above, and "Modification time applied only if the medium supports it", which no Zephyr VFS call
  can do. The rename table row holds. `FSTAT` and `SFTP_GetAttributes_Handle` use `crux_file_service_size`.
- **Web design:** a `Content-Length` check maps to `declared_size`, and a missing or wrong area maps to `404` under the hiding rule, which refines "`403` means a valid session without permission".
