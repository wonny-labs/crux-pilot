# Portable File contract

Status: approved for software-only development by the human M4 Architecture and API Review on 23 October 2026. Production backend/layout adoption, upstream/native Flash changes, physical operations, PR merge and Issue closure remain separately gated.

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
access. Directory trees, rename/atomic replace, append, sparse files, metadata,
permissions, descriptor multiplexing and asynchronous I/O are out of scope.
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
    CRUX_FILE_ERR_NOT_SUPPORTED = -9, CRUX_FILE_ERR_OUT_OF_RANGE = -10
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
