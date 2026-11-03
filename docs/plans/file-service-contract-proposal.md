# File contract extension and file service proposal

Status: proposal for review, Issue #179. It refines the file sections of
[the secure management design](secure-management-design.md) and [the CLI and SFTP design](secure-management-cli-sftp-design.md).
Nothing here authorises implementation, dependency adoption or hardware operations. Baseline: main `4444d70`, Zephyr v4.4.2
`dccb095`, FatFs `f4ead3b`, wolfSSH v1.6.0 `8221e4e`, and WinSCP source `246bbd4`, which is the current upstream tip and not
a released version.

## Decisions requested

| ID | Decision | Recommendation | Impact if rejected |
| --- | --- | --- | --- |
| F1 | Add File *roots*, so that one volume can serve several directories, through new functions. The existing functions keep working on root 0. | Accept | Prefixed flat names in one directory cost name length and give the credential store no separate location |
| F2 | `crux_file_rename` refuses an existing destination. The service composes replacement from remove and rename. | Accept | A replacing rename inherits `fatfs_rename`'s removal of the destination while appearing to be one operation |
| F3 | Directory listing uses a bounded handle, not a callback or an index | Accept | A callback cannot serve SFTP's separate `READDIR` requests, and an index is quadratic and unstable |
| F4 | Keep the File name rule: `[A-Za-z0-9_.-]`, 1 to 63 characters | Accept for now. A later widening is additive. | Widening needs a new conformance run. Under this rule WinSCP cannot upload a name with a space, and names over 54 characters fail above its resume threshold. |
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

/** Opaque directory listing handle; only crux_file_dir_close() retires it. */
typedef struct crux_file_dir crux_file_dir_t;

typedef struct {
    uint64_t size; /**< Bytes. */
} crux_file_info_t;

/* crux_file_result_t gains CRUX_FILE_ERR_EXISTS = -11. Existing values are unchanged. */

crux_file_result_t crux_file_open_at(crux_file_root_t root, const char *name, crux_file_mode_t mode, crux_file_t **out);
crux_file_result_t crux_file_remove_at(crux_file_root_t root, const char *name);
crux_file_result_t crux_file_stat(crux_file_root_t root, const char *name, crux_file_info_t *out);
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
  recorded, and a file with an open writer may report an older size, so a caller wanting a current size uses the handle's own
  count. It does not imply durability.
- **Rename.** Atomic only in the sense that no other Crux File call sees a partial state, because it runs under the File lock. It
  is not atomic across power loss. Checks run in this order: argument validation, then `INVALID_STATE` if either name has a live
  handle, then `NOT_FOUND` for a missing `from`, then `CRUX_FILE_ERR_EXISTS` for an existing `to`, including `from` equal to `to`.
  Renames are within one root, because the service has no cross-area rename.
- **Listing.** `crux_file_dir_read` returns entries in provider order and sets `end` at the finish. It yields only regular files
  whose names pass the name rule, so foreign files and subdirectories are skipped without an error. On Linux, symbolic links are
  skipped too, matching the `O_NOFOLLOW` in the provider's open. `CRUX_FILE_ERR_OUT_OF_RANGE` means the name buffer was too small,
  and a capacity of 64 is always enough for the minimum profile. An entry created or removed during a listing may be missed or
  repeated. A bounded pool of `CRUX_FILE_DIR_POOL_SIZE`, default 2, holds handles, and `CONFIG_FS_FATFS_NUM_DIRS` (default 4) must
  be at least that.

### Native mapping

The FatFs provider stores a File name as `f` followed by the hex of its bytes (`make_path` in `file_native.c`). Listing decodes
this and skips anything that does not decode to a valid name, including upper-case hex, so one file has one identity. Files
written by a host under other names are invisible, as they are today.

`fatfs_rename` in Zephyr's `subsys/fs/fat_fs.c` calls `f_unlink` on an existing destination before `f_rename`. The provider
therefore calls `fs_stat` on the destination first, under the File lock, and never reaches that path. The Linux provider uses
`fstatat` then `renameat`, also under the lock, so both providers have the same no-replace behaviour. Native clients that bypass
File remain outside the composition, as the contract says.

`struct fs_dirent` in Zephyr carries type, name and size, with `size` a `size_t`. It has no modification time, and
`CONFIG_FS_FATFS_HAS_RTC` is off, so FatFs uses a hardcoded date. This is why decision F6 omits time. Adding a field later is
source-compatible for designated initialisers.

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
    const char *partial_suffix;  /* For example ".filepart". NULL when direct partials are not kept. */
    uint8_t write_modes;         /* CRUX_FILE_SERVICE_MANAGED, CRUX_FILE_SERVICE_DIRECT, or both. */
} crux_file_area_t;

typedef struct {
    const crux_file_area_t *areas;
    size_t area_count;
    const crux_file_root_t *reserved_roots; /* Roots no area may use. The credential store supplies its own. */
    size_t reserved_root_count;
} crux_file_service_config_t;
```

`crux_file_service_start` takes the config once, validates the whole table and scans each area. It refuses a duplicate name or
root, a reserved root, zero limits, a `max_file_bytes` above `INT32_MAX` (the provider's seek limit), a missing permission, and a
writable area with no mode. On failure every operation returns `CRUX_FILE_SERVICE_ERR_NOT_READY`, which fails closed. The table
stays borrowed for the program's life. An area whose root is not ready at start is reported per area and does not stop the others.

### Operations

```c
typedef enum {
    CRUX_FILE_SERVICE_OK = 0,
    CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT = -1, /* Includes a name or path that the rules refuse. */
    CRUX_FILE_SERVICE_ERR_NOT_READY = -2,
    CRUX_FILE_SERVICE_ERR_SESSION_INVALID = -3,
    CRUX_FILE_SERVICE_ERR_DENIED = -4,
    CRUX_FILE_SERVICE_ERR_NOT_FOUND = -5,        /* Missing, or an area the session may not see. */
    CRUX_FILE_SERVICE_ERR_EXISTS = -6,
    CRUX_FILE_SERVICE_ERR_BUSY = -7,             /* A conflicting open, or a lock wait that expired. */
    CRUX_FILE_SERVICE_ERR_NO_SPACE = -8,         /* The medium is full. */
    CRUX_FILE_SERVICE_ERR_LIMIT = -9,            /* A declared limit would be exceeded. */
    CRUX_FILE_SERVICE_ERR_NO_RESOURCES = -10,
    CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE = -11,    /* A sparse write, or an offset past the end. */
    CRUX_FILE_SERVICE_ERR_IO = -12
} crux_file_service_result_t;

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
    CRUX_FILE_SERVICE_OPEN_DIRECT = 3,       /* Open or create the named file. Truncate only on request. */
    CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE = 4,
    CRUX_FILE_SERVICE_OPEN_DIRECT_EXCLUSIVE = 5
} crux_file_service_open_t;

crux_file_service_result_t crux_file_service_open(crux_access_session_t session, const char *area, const char *name,
                                                  crux_file_service_open_t mode, uint64_t declared_size,
                                                  crux_file_service_transfer_t **out);
crux_file_service_result_t crux_file_service_read(crux_access_session_t session, crux_file_service_transfer_t *transfer,
                                                  uint64_t offset, void *buffer, size_t length, size_t *transferred);
crux_file_service_result_t crux_file_service_write(crux_access_session_t session, crux_file_service_transfer_t *transfer,
                                                   uint64_t offset, const void *buffer, size_t length,
                                                   size_t *transferred);
crux_file_service_result_t crux_file_service_commit(crux_access_session_t session,
                                                    crux_file_service_transfer_t **transfer);
crux_file_service_result_t crux_file_service_close(crux_file_service_transfer_t **transfer);
```

- Every operation that takes a session checks it and the area's permission. A session that ends mid-transfer fails its next
  chunk with `SESSION_INVALID`, and the adapter then calls `crux_file_service_close`, which needs no session, so a handle is
  always returned.
- `crux_file_service_area_next` uses the quiet permission probe, request AC1 below, so hiding an area leaves no denial event. Any
  other operation calls `crux_access_check`, which produces the event. An area the session has neither permission on returns
  `NOT_FOUND`, and write on a readable area returns `DENIED`.
- All operations can wait on storage, so a web adapter runs them on its worker, and SSH runs them on the session thread.
  `crux_file_service_resolve` touches no storage. Only one service lock is used, and it is never held across storage: it guards area
  counters, the transfer pool and the name reservations below.
- Reserved temporary names begin with `.crux-`. A client cannot open, stat, remove or rename such a name, and listing hides it.

### Path resolution

`crux_file_service_resolve` is the only path parser. It accepts `/area` and `/area/name`, with one optional trailing slash on an
area, and nothing else: no empty component, `.` or `..`, more than two components, or a backslash. The total length is bounded by 80
bytes. Protocols that carry length-delimited strings reject an embedded NUL first, as the access proposal requires for user names.
wolfSSH normalises paths with `wolfSSH_RealPath` before its file macros, and the adapter resolves the result again, so this parser
and not the normalisation is the check. `wolfSSH_SFTP_SetConfinePath` is a second defence only.

### Capacity

Each area keeps `used_bytes` and `entries` in RAM. `crux_file_service_start` computes them by scanning the area, and operations update
them under the service lock. The service checks before the write, using `offset + length`, so a write that would cross `max_file_bytes`
or `max_area_bytes` returns `ERR_LIMIT` with nothing written. `declared_size` lets a managed upload fail before any data arrives, and it
is zero when unknown. A failed partial write updates the counters from the accepted count. If the medium is full first, the result is
`ERR_NO_SPACE` from File.

A scan that meets a read error marks the area not ready, and the area returns `ERR_IO` until a reset. Counters do not survive a reset.

### Managed upload

For the web interface, or any caller that wants one file to appear only when it is complete.

1. `open` with `OPEN_MANAGED` checks permission, the name and `declared_size`, and takes the temporary name `.crux-up-<slot>`.
2. `write` appends. Offset must equal the current position.
3. `commit` syncs and closes the temporary file, removes an existing target, and renames. The transfer handle is consumed.
4. `close` without commit is the abort: it closes and removes the temporary file.

### Direct write

For SFTP. The client owns its temporary names, resume and rename, so an abort or disconnect leaves the partial file where it is. The
adapter maps SFTP open flags as follows. Before any storage call, the service records the area and name in the transfer slot under its lock, and a second writer for that name gets `ERR_BUSY`, so a stat and the open that follows cannot be split by another client. Remove, rename and a managed upload's target reserve their names the same way.

| SFTP flags | Mode | File operations |
| --- | --- | --- |
| `READ` | `OPEN_READ` | `CRUX_FILE_READ` |
| `WRITE` | `OPEN_DIRECT` | `CRUX_FILE_READ_WRITE`. Not found is an error unless `CREAT` is set. |
| `WRITE`, `CREAT` | `OPEN_DIRECT` | `CRUX_FILE_READ_WRITE`, and on not found `CRUX_FILE_CREATE_REPLACE` |
| `WRITE`, `CREAT`, `TRUNC` | `OPEN_DIRECT_TRUNCATE` | `CRUX_FILE_CREATE_REPLACE`, with the counters adjusted first |
| `WRITE`, `CREAT`, `EXCL` | `OPEN_DIRECT_EXCLUSIVE` | Stat, then `CRUX_FILE_ERR_EXISTS` or create |
| `APPEND` | `OPEN_DIRECT` | Each write goes to the current size |

A write's offset may be at or below the current size. An offset past it returns `ERR_OUT_OF_RANGE`, because File does not create sparse
files, so a client that writes out of order fails rather than leaving a hole. wolfSSH passes each write's offset through `WPWRITE`, and
the service only calls `crux_file_seek` when the offset differs from the tracked position, because the provider's seek reads the end of the
file each time.

### Replacement and power loss

**Neither mode provides atomic replacement across power loss, and results must say so.** The windows are real:

- Commit removes the target and then renames. A cut between them leaves no target and a complete temporary file. The earlier
  replace-pattern run on one card reached that state in 29 of 100 cuts, in a loop that did nothing else.
- FatFs marks the rename itself as a critical section, where an interruption can cause a cross-link. `f_rename` registers the new
  entry and then removes the old one, so if they are in different sectors, a cut between the writes can leave two names on one
  cluster chain. This comes from reading `ff.c`. It has not been reproduced here.
- Orphaned clusters remain after such cuts. The same run found 40 orphaned clusters, about 320 KiB.
- SFTP clients have the same windows, and WinSCP's own sequence is delete then rename.

### Stale partial files

The rule depends on the file's owner.

| File | At start | Later |
| --- | --- | --- |
| `.crux-up-*` with no target of the same size | Removed | Removed on abort |
| `.crux-up-*` with a target of the same size | Kept, with event `file.recovery_needed`. It may share clusters with the target after a cut during rename, and removing it would free them. | Counts against the limit. Removed only by a host repair, or by the composition when it declares that the risk is accepted. |
| A file ending in the area's `partial_suffix` | Kept, because a client may resume it | When a new create or write would hit a limit, the service removes such files that have no live handle, in listing order, with event `file.partial_removed`, then retries once |

No clock is needed, and a client whose partial was removed starts again. The rule cannot tell a client's interrupted upload from a file that
merely has that suffix, so the suffix belongs to a declaration by the composition and is `NULL` for an area that wants none removed.

### Conflicts

File's single-writer rule stays. A conflicting open, or a remove or rename of an open file, gives `ERR_BUSY`, which the adapter maps to a
protocol failure. `CRUX_FILE_SERVICE_TRANSFER_MAX`, default 3, bounds open transfers, and it must stay below `CRUX_FILE_POOL_SIZE`, default 4,
so that one handle is always free for the credential store and the project. The build fails if it does not. The web design's rule of one bulk
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
| Name plus `.filepart` must fit | Source (`PartialExt`), File rule | Names up to 54 characters above the threshold | Longer names fail. Decision F4. |
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
| Commit fails after removing the target | `ERR_IO`, with an event. The temporary file stays for inspection and is cleared at the next start under the rule above. |

## Testing

| Level | Cases |
| --- | --- |
| File conformance, Host and `native_sim` FatFs | Listing, including foreign names, directories and a name buffer too small. Stat. Rename's order of checks, including destination existing, `from` equal to `to`, open handles on either name and a missing source. Roots: separation, the same name in two roots, an unbound root, overlapping paths. Existing suites pass unchanged on root 0. |
| Provider faults | Failed stat, rename and directory read, on the fault port. Rename when the destination appears to be removable never removes it. |
| Service permissions | Each operation with no session, an ended session, read-only, write-only and neither permission. Hidden areas in listings produce no event, and direct access does. A transfer used by a second session is refused. |
| Traversal | `..`, `.`, empty components, backslash, an overlong path, three components, embedded NUL at the adapter, control characters and reserved names |
| Capacity | Per-file, per-area and entry limits at and over the bound, recount at start, and partial accounting with a short write |
| Conflict | Second writer, reader during a writer, remove and rename of an open file, transfer pool exhaustion, and a handle left free for the store |
| Interrupted writes | Abort and disconnect in each mode, restart with each stale state in the table above, and a cut at each step of commit on the fake. Results assert which states appear, and they state that replacement is not atomic. |
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

## Changes other contracts need

- **AC1, access control:** an adapter-only `crux_access_permitted(session, permission, bool *permitted)` with no event, no touch and no
  storage. Hiding areas, parameters and commands would otherwise produce a denial event for every item hidden. This is an addition to the
  access proposal, which is not changed here. It also affects `crux_cli` and the configuration service.
- **CLI and SFTP design:** the `SETSTAT` statement above, and "Modification time applied only if the medium supports it", which no Zephyr VFS call
  can do. The rename table row holds. The design should also name the 54-character limit.
- **Web design:** a `Content-Length` check maps to `declared_size`, and a missing or wrong area maps to `404` under the hiding rule, which refines "`403` means a valid session without permission".
