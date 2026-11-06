# Portable File contract

Status: approved for software-only development by the human M4 Architecture and API Review on 23 October 2026. Production backend/layout adoption, upstream/native Flash changes, physical operations, PR merge and Issue closure remain separately gated. The [additions for roots, stat, size, rename and listing](#additions-for-roots-stat-size-rename-and-listing) come from decisions F1, F2, F3 and F6 of [the File extension proposal, PR #206](https://github.com/wonny-labs/crux-pilot/pull/206), with F4 keeping the name rule, accepted by [the owner on 3 November 2026](https://github.com/wonny-labs/crux-pilot/pull/209#issuecomment-6616400894) for Host and `native_sim` work. Target and physical work on them stays behind M7 and needs its own approval.

Baseline: main `225cd21b5fff37c31c5dff52570e9f26179682a1`.

## File alternatives and recommendation

1. **Bounded Crux C17 File contract (recommended):** explicit transferred-byte
   counts, close-failure lifetime and backend acknowledgment; no native types or
   global errno in the consumer. Costs a small maintained adapter and tests.
2. **Qualified POSIX file subset:** reuses open/read/write/lseek/fsync/close/unlink
   and existing Zephyr facilities. Valid alternative, but needs separate qualified
   headers/types/errno and close/partial-I/O semantics; Networking's approved
   POSIX profile does not grant this profile. No current File consumer requires
   POSIX source compatibility, so this does not yet offset its qualification cost.

This recommendation is specific to #23; it is not a rule to wrap all native APIs.
The justified operations follow #15 CRUD/sync/recovery and bounded file-position
access, and later the additions below. Directory trees, atomic replace, append,
sparse files, metadata other than size, permissions, descriptor multiplexing and
asynchronous I/O are out of scope.
Seek is a proposed representative runtime-consumer operation, not a discovered
existing firmware requirement: the qualification fixture will re-read an earlier
record in a two-record file. This operation is approved for software qualification.

## Public contract

Owner: `components/file/include/crux/file.h`; usage target `crux::file`.
C17 implementation, C++17 consumption through `extern "C"`.
Only ISO C headers/public Crux declarations are consumer dependencies.

```c
typedef struct crux_file crux_file_t; /* opaque provider-owned handle */
typedef enum { CRUX_FILE_READ, CRUX_FILE_READ_WRITE,
               CRUX_FILE_CREATE_REPLACE } crux_file_mode_t;
typedef enum {
    CRUX_FILE_OK = 0, CRUX_FILE_PARTIAL = 1,
    CRUX_FILE_ERR_INVALID_ARGUMENT = -1, CRUX_FILE_ERR_INVALID_STATE = -2,
    CRUX_FILE_ERR_NOT_READY = -3, CRUX_FILE_ERR_NOT_FOUND = -4,
    CRUX_FILE_ERR_NO_SPACE = -5, CRUX_FILE_ERR_NO_RESOURCES = -6,
    CRUX_FILE_ERR_ACCESS = -7, CRUX_FILE_ERR_IO = -8,
    CRUX_FILE_ERR_NOT_SUPPORTED = -9, CRUX_FILE_ERR_OUT_OF_RANGE = -10,
    CRUX_FILE_ERR_EXISTS = -11
} crux_file_result_t;
crux_file_result_t crux_file_open(const char *name, crux_file_mode_t mode,
                                crux_file_t **out);
crux_file_result_t crux_file_read(crux_file_t *file, void *buffer,
                                size_t length, size_t *transferred);
crux_file_result_t crux_file_write(crux_file_t *file, const void *buffer,
                                 size_t length, size_t *transferred);
crux_file_result_t crux_file_seek(crux_file_t *file, uint64_t offset);
crux_file_result_t crux_file_sync(crux_file_t *file);
crux_file_result_t crux_file_close(crux_file_t **file);
crux_file_result_t crux_file_remove(const char *name);
```

No ABI stability is promised. Existing base statuses
are left unchanged; File needs distinguishable missing-file, capacity and partial
results. Error mapping must be explicit and unknown native errors map to IO.

- One composition-selected volume, with provider-private native mount/root path.
  Initial qualification profile: flat ASCII names of 1–63 characters from
  `[A-Za-z0-9_.-]`, excluding `.` and `..`; no slash or traversal. This is a
  approved initial qualification choice, not a measured existing-consumer requirement or
  filesystem/security sandbox guarantee. Name and pool bounds are provider build
  configuration with documented supported minima, not a runtime framework.
- A file open for writing (READ_WRITE or CREATE_REPLACE) excludes every other handle on the
  same name, and a writer cannot open a name that is already open: such an open returns
  INVALID_STATE with the output left null. Several READ handles may share a file. Providers
  can hold per-handle state (for example FatFs with file locking disabled), so a second handle
  next to a writer is not supported. The check follows argument validation and precedes the
  pool check and any provider call, so a conflicting open returns INVALID_STATE even when the
  pool is full, and a rejected CREATE_REPLACE never touches the file. Other conflicting
  operations on a name remain the caller's to serialise.
- READ/READ_WRITE open an existing file; CREATE_REPLACE creates or truncates for
  read/write. No hidden retry or automatic directory creation/format/mount.
- Open requires a non-null output initially null; an already non-null output is
  rejected without changing it. A failed accepted open leaves the output null.
  Provider owns a bounded pool (four handles in the initial test profile).
  Buffer ownership remains with caller; buffers are not retained after return.
- Transferred output is mandatory and set to zero before valid dispatch. Zero
  length permits a null buffer. Read returns OK with a short/zero count at EOF.
  Write returns PARTIAL for every nonnegative result below a nonzero requested
  length, including zero progress, without inventing a cause;
  an explicit no-space error maps to NO_SPACE. No successful full-write claim
  follows from a short result. Accepted partial bytes are not rolled back.
  A provider count exceeding the requested length is inconsistent: the common
  layer returns IO and reports zero transferred bytes. This is not rollback or
  proof that the buffer or storage was unchanged. Valid accepted counts still
  survive provider errors.
- Seek is absolute and checked against representable backend offsets and EOF;
  beyond-EOF/sparse extension is unsupported. It does not imply durable changes.
  Unrepresentable offsets/request lengths return OUT_OF_RANGE before dispatch.
- Sync OK acknowledges the selected provider's flush operation. Write OK alone
  is not persistence. No atomic update or general power-loss safety is promised.
- Close consumes the public handle and clears the caller's pointer even when it
  returns an error; do not retry/use aliases afterwards. A close error means no
  successful flush acknowledgment. It does not undo an earlier successful sync.
  This avoids retrying a backend object already released during failed close.
- Remove success acknowledges logical removal, not independently qualified
  directory-metadata durability. Removing an open file is unsupported.
- Synchronous/task-context only; no bounded latency or ISR suitability. Caller
  serializes access to a handle and conflicting same-file operations. No shared
  seek position/concurrent modification guarantee. Providers protect pool state;
  composition owns mount/unmount and does not change it with live handles.

## Additions for roots, stat, size, rename and listing

```c
typedef uint8_t crux_file_root_t;
enum { CRUX_FILE_ROOT_DEFAULT = 0 };
typedef struct crux_file_dir crux_file_dir_t; /* opaque; only crux_file_dir_close retires it */
typedef struct { uint64_t size; } crux_file_info_t;

crux_file_result_t crux_file_open_at(crux_file_root_t root, const char *name,
                                     crux_file_mode_t mode, crux_file_t **out);
crux_file_result_t crux_file_remove_at(crux_file_root_t root, const char *name);
crux_file_result_t crux_file_stat(crux_file_root_t root, const char *name, crux_file_info_t *out);
crux_file_result_t crux_file_size(crux_file_t *file, uint64_t *size);
crux_file_result_t crux_file_rename(crux_file_root_t root, const char *from, const char *to);
crux_file_result_t crux_file_dir_open(crux_file_root_t root, crux_file_dir_t **out);
crux_file_result_t crux_file_dir_read(crux_file_dir_t *dir, char *name, size_t capacity,
                                      crux_file_info_t *info, bool *end);
crux_file_result_t crux_file_dir_close(crux_file_dir_t **dir);
```

The rules above apply: thread context, synchronous, no ISR use, buffers borrowed for the
call, and no retry. The existing functions keep their behaviour on root 0.

- **Roots.** A root is a directory that the composition binds, once per index, with
  `crux_file_linux_bind_root` or `crux_file_zephyr_bind_root`. `crux_file_linux_bind` and
  `crux_file_zephyr_bind` bind root 0. A path that is equal to, inside or a parent of another
  bound root returns INVALID_STATE, comparing path text only, so a FAT case alias or a Linux
  symbolic link in a composition's paths is not caught. An unbound or out-of-range root
  returns NOT_READY, after argument checks. `CRUX_FILE_ROOTS_MAX` is a build setting, default 8.
  The conflict rule compares the root and the name together. Roots separate names. They are
  not a security sandbox, as for the name rule above.
- **Stat.** Regular files only: a directory or a Linux symbolic link is NOT_FOUND. The size is
  what the provider last recorded, so a file with an open writer may report an older size.
- **Size.** A handle's current size, which includes unsynced writes and leaves the position
  where it was. It reads the end of the file on the same path as seek's check. It implies no
  durability.
- **Rename.** Inside one root. It refuses an existing destination of any type. Checks run in
  this order: arguments, INVALID_STATE for a live handle on either name, NOT_FOUND for a missing
  or non-regular source, and EXISTS for an existing destination, including `from` equal to `to`.
  No other Crux File call sees a partial state, because it runs under the File lock. **It is not
  atomic across power loss**: on FAT, a cut between the two directory writes of `f_rename` can
  leave two names on one cluster chain. This comes from reading `ff.c` and has not been
  reproduced here. Native clients that bypass File can race the destination check.
- **Listing.** A bounded pool of `CRUX_FILE_DIR_POOL_SIZE` handles, default 2, gives NO_RESOURCES
  when full. `crux_file_dir_read` returns entries in provider order and sets `end` at the finish,
  which then repeats without a provider call. It yields only regular files whose names pass the
  name rule, so subdirectories, Linux symbolic links and foreign files are skipped without an
  error. A skipped entry still costs a provider read under the File lock, so a directory full of
  foreign files delays other File callers. A capacity below 64 returns OUT_OF_RANGE and uses up no
  entry. After a failed read the listing can go on, and whether the failed entry comes back
  depends on the provider. An entry created or removed during a listing may be missed or repeated,
  because a listing is not a snapshot and it holds no file open.
- **Pool size.** `CRUX_FILE_POOL_SIZE` is also a public compile definition of `crux::file`, so
  that a consumer can size what it leaves free at build time.

The `crux_file_fake` test library is an in-memory provider behind the same private port, for
tests of code built on File. It injects faults and cuts power at a chosen mutation, under a
simple model stated in `tests/file/fake_file.h`. Results from it say nothing about FatFs or a card.

### Native mapping of the additions

The FatFs provider stores a name as `f` followed by the hex of its bytes. Listing decodes this and
skips anything else, including upper-case hex and an encoded NUL, so one file has one identity.
`fatfs_rename` in Zephyr's `subsys/fs/fat_fs.c` calls `f_unlink` on an existing destination before
`f_rename`, so the provider calls `fs_stat` on the destination first under the File lock and never
reaches that path. The Linux provider uses `fstatat` and then `renameat` for the same reason.
`CONFIG_FS_FATFS_NUM_DIRS` must be at least the listing pool, and the build checks this.

`struct fs_dirent` has no modification time, which is why decision F6 leaves time out. On
`native_sim` these additions have run on FatFs and on Reliance Edge, over a RAM disk. The LittleFS
simulator composition compiles them and has not run them. Earlier physical runs do not extend to
them.

### Reliance Edge mapping

`CONFIG_CRUX_FILE_RELIANCE_EDGE` selects the same provider over the Reliance Edge file system of
the Zephyr VFS (`CONFIG_CRUX_FS_RELIANCE_EDGE`). Every statement of the contract above is unchanged.
What differs is the mapping:

- A name is stored as it is, with no encoding, so a file can be seen on the medium by its own name.
  A name that fails the File name rule, or a directory, is not listed.
- The destination of a rename is checked first under the File lock, as on FatFs, because Reliance
  Edge's own rename replaces an existing file.
- A full volume gives `CRUX_FILE_ERR_NO_SPACE`, including for an empty file when no inode is left
  although blocks are free.
- After a media error the file system is read-only until it is mounted again, and every change then
  gives `CRUX_FILE_ERR_ACCESS`. A close that has unsaved data on such a volume gives it too.
- A thread that uses the file system takes one of `CONFIG_CRUX_FS_RELIANCE_EDGE_TASK_COUNT` slots for
  good. A thread beyond them gets `CRUX_FILE_ERR_NO_RESOURCES` from every call, and a call from it
  never reaches the file system, so it cannot take a slot from another thread. That includes closing
  or reading a handle that another thread opened. The File provider retires a handle whenever its
  close returns, so a refused close leaves the file system's handle open until the volume is
  mounted again. A composition keeps its threads within the limit.
- The File pools hold open files and listings together, so the file system's handle count must
  cover both, and `CONFIG_FILE_SYSTEM_MAX_FILE_NAME` must hold the longest name. The build checks
  both.

The selection works on any board that has the file system and excludes the other File selections.
The run on `native_sim` is a software result, and no physical-board result exists for it.

## Verified native mapping and limits

Pinned Zephyr is `684c9e8f32e4373a21098559f748f06915f950c9`.
`include/zephyr/fs/fs.h` and `subsys/fs/fs.c` supply open/read/write/seek/sync/
close/unlink, with native object/errno translation kept private. Absolute seek
needs checked offset conversion and size inspection; it must not silently narrow.

Important source finding: `fs_close()` retains `mp` after backend error
(`subsys/fs/fs.c:206–226`), but `littlefs_close()` releases its file data even on
an error (`subsys/fs/littlefs_fs.c:330–342`). Therefore failed-close retry is not
a safe generic contract. The adapter must retire/reset its own slot after close,
preserve the error, and test that path without dereferencing freed backend state.
This is source evidence, not a reproduced runtime defect or physical test.

A second pinned mapping hazard exists in `fs_open(...FS_O_TRUNC)`: after backend
open succeeds, a truncate failure clears `mp` without calling backend close
(`subsys/fs/fs.c:182–200`). A subsequent `fs_close()` is then a no-op. The future
adapter must not use that combined path: open without TRUNC, call `fs_truncate(0)`
while the handle is valid, then close exactly once on truncation failure. Preserve
the primary failure and retain any cleanup failure in provider diagnostics/tests.
Failed creation/truncation is not rollback: a file may have been created/changed.
Controlled failure tests must prove cleanup and error behavior before adopting
the mapping. This requires no upstream or production Flash-driver change.

#23 implements a Linux Host provider and a private deterministic fault provider.
The [software validation and handoff](file-validation.md) specify the Zephyr mapping.
A future change may compile a provider against a controlled
RAM-backed software fixture; this implementation does not enable native QSPI/LittleFS writes.
Media/partition/format/startup policy remains #14/#15 composition work.


## Result convention

The capability-specific result enum is a separate domain from `crux_status_t`.
As with the base convention, zero denotes full success and negative values are
errors; positive File partial/Configuration default outcomes require explicit
handling. Compare named values from the declaring API, never cast or compare
against base enum values, and do not treat every nonzero value as an error.
Any necessary composition-level translation uses an explicit switch; no generic
conversion service or change to base status values is introduced.
