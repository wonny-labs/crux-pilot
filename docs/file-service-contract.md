# File service contract

Status: implemented on Host with fakes, Issue #179. The owner [accepted decisions F1 to F6 on 3 November 2026](https://github.com/wonny-labs/crux-pilot/pull/209#issuecomment-6616400894), with F4 for now, and approved the headers, fakes and Host tests. Target and physical work stays behind M7 and needs its own approval. The reasoning, the threat table and the WinSCP evidence are in [the proposal, PR #206](https://github.com/wonny-labs/crux-pilot/pull/206).

Owner: `components/file_service/include/crux/file_service.h`; usage target `crux::file_service`. C17 implementation, C++17 consumption through `extern "C"`.

## What it does

The service stops a remote client with a valid session from reaching anything but declared areas, and from acting without the area's permission. SFTP and web adapters translate protocols into these calls, and enforcement stays here, in one place. It does not isolate code in the image, and File names are not a security sandbox.

It runs on the [File additions](file-contract.md) and the [access contract](access-contract.md). A private event port, `crux_file_service_port_event`, carries the `file.*` identifiers of Issue #182 until that component exists. No product composition supplies it yet, so an image cannot link this service today.

## Areas

A composition declares up to `CRUX_FILE_SERVICE_AREAS_MAX` areas, default 8, in a table that stays borrowed for the program's life. `crux_file_service_start` validates the whole table and refuses a duplicate name or root, a reserved root, an empty reserved list unless `no_credential_store` is set, zero limits, a `max_file_bytes` above `INT32_MAX`, a missing permission, a writable area with no mode, and a suffix that is empty or begins with `.crux-`. After a refused table every operation returns `ERR_NOT_READY`, and a corrected table can still start. A second start returns `ERR_EXISTS`.

An area whose root is not ready returns `ERR_NOT_READY` until a reset. An area whose scan or recovery failed returns `ERR_IO` until a reset. Neither stops the others.

## Permissions

Every operation that takes a session checks the session and the permission, including each chunk of a transfer.

| Session | Result |
| --- | --- |
| Ended or never valid | `ERR_SESSION_INVALID`, even for an area that does not exist |
| Holds neither of the area's permissions | `ERR_NOT_FOUND`, the same as a missing area, with one denial from `crux_access_check` |
| Holds the other permission only | `ERR_DENIED`, with one denial |
| Another session's listing or transfer | `ERR_DENIED`, including a session that has ended |

`crux_file_service_area_next` uses the quiet probe, so hiding an area leaves no denial event. A read-only area refuses any write with `ERR_DENIED` to a session that can see it. A write mode the area does not offer is also `ERR_DENIED`. These two, a write through a read transfer and use of another session's handle record no denial from `crux_access_check`, because the session holds what was checked or nothing was checked. Timing is not equalised, so a client may still tell a hidden area from a missing one that way.

## Paths and names

`crux_file_service_resolve` is the only path parser, and it touches no storage. It accepts `/area`, `/area/` and `/area/name`, up to 80 bytes. It refuses an empty component, `.` or `..`, more than two components, a backslash, and any name that fails the File name rule. Names beginning `.crux-` are the service's own: a client cannot open, stat, remove or rename one, and a listing hides it.

A name that a rule refused gives `ERR_INVALID_ARGUMENT` and `file.name_rejected`, which records only the area, because a client's text can hold anything. `resolve` has no session and emits nothing, so an adapter must not show its `NOT_FOUND` differently from a hidden area's.

## Capacity

Each area keeps `used_bytes` and `entries` in RAM, computed at start by scanning the area, so they do not survive a reset and start time grows with the number of files. Before a write, under the service lock, the service reserves the bytes that would grow the file, so that two transfers cannot both pass the same check. A write that would cross `max_file_bytes` or `max_area_bytes` returns `ERR_LIMIT` with nothing written. A create reserves an entry the same way. The accepted count then settles the reservation, including after an error.

`declared_size` lets a managed upload fail before any data, and zero means unknown. It is only a check: the writes reserve what they use. A direct mode ignores it, because such a write may replace what is there. Only what grows is checked, so an area over a bound after a table changed still overwrites. A declared limit does not reserve room on the medium, and a full medium gives `ERR_NO_SPACE` with `file.storage_failed`.

**Stale partial files (F5).** A file ending in an area's `partial_suffix` survives a reset, because a client may resume it. When a create or write would hit a limit, the service removes such files that have no handle or reservation, in listing order, each with `file.partial_removed`, until the reservation fits or none are left. It cannot tell a client's interrupted upload from a file that merely has that suffix, so a composition declares the suffix, and `NULL` means none are removed.

## Transfers

`CRUX_FILE_SERVICE_TRANSFER_MAX`, default 3, bounds open transfers, and the build fails unless it stays below `CRUX_FILE_POOL_SIZE`, so that one File handle stays free from the service for the credential store and the project. Project code could still take that handle. `CRUX_FILE_SERVICE_DIR_MAX` defaults to 1 and leaves a listing handle for the stale rule. A full pool, name table or set of temporary names gives `ERR_NO_RESOURCES`.

| Mode | SFTP flags | File operations |
| --- | --- | --- |
| `OPEN_READ` | `READ` | `CRUX_FILE_READ` |
| `OPEN_DIRECT` | `WRITE` | `CRUX_FILE_READ_WRITE`. Not found is an error. |
| `OPEN_DIRECT_CREATE` | `WRITE`, `CREAT` | `CRUX_FILE_READ_WRITE`, and on not found `CRUX_FILE_CREATE_REPLACE` |
| `OPEN_DIRECT_TRUNCATE` | `WRITE`, `CREAT`, `TRUNC` | `CRUX_FILE_CREATE_REPLACE`, with the counters adjusted first |
| `OPEN_DIRECT_EXCLUSIVE` | `WRITE`, `CREAT`, `EXCL` | Stat, then `ERR_EXISTS` or create |
| `OPEN_MANAGED` | None; for the web interface | A temporary file, published by commit |

Before any storage call, a write mode records the area and name in a table of `CRUX_FILE_SERVICE_RESERVATIONS`, default 8, so that a stat and the open after it cannot be split by another client. A second writer gets `ERR_BUSY`. Remove reserves one name for the call, rename two, and a write holds its name until commit or close. A reader takes no entry, and File's own rule refuses it next to a direct writer. A managed upload's target stays readable until the commit.

A write's offset may be at or below the current size. An offset past it returns `ERR_OUT_OF_RANGE`, because File does not create sparse files. A managed upload must write at its current position. The service seeks only when the offset differs from the tracked position, because the provider's seek reads the end of the file, and it seeks again after any failure.

A session that ends mid-transfer fails its next chunk with `ERR_SESSION_INVALID`. `crux_file_service_close` and `crux_file_service_dir_close` need no session, so a handle always comes back. Closing a direct write leaves its file where it is, and closing an uncommitted managed upload removes the temporary file.

## Commit and power loss

**Neither write mode provides atomic replacement across power loss, and results must say so.** Commit runs in this order:

1. Sync and close the temporary file `.crux-up-<slot>`, so that the service stays within its share of the File handles during the next steps.
2. Check the target by opening it for writing, which File refuses next to any other handle, so that a reader inside or outside the service gives `ERR_BUSY` before anything changes.
3. Write and sync a sidecar, `.crux-co-<slot>`, holding the target name and a check byte.
4. Remove an existing target, rename the temporary, and remove the sidecar.

`ERR_BUSY`, `ERR_NO_RESOURCES` and a refusal of the session or the permission change nothing and keep the handle, so that the caller can write on, retry or close. That includes a reader that arrives after step 2, which File refuses at the removal, before it reaches the target. The service then opens the temporary again, and if that fails the result is `ERR_IO` and the upload is gone. Every other result consumes the handle.

An upload claims its temporary name under the service lock before it checks that the name is free, so that two uploads cannot share one. A slot whose temporary file or sidecar still exists is skipped, so a kept file is never reused and an old sidecar can never name a new upload. If a failure comes before the removal in step 4, the temporary file and sidecar go and the target stands. A failed removal or rename returns `ERR_IO` with `file.commit_failed`, and both files stay, because FatFs can report an error after the entry has gone. The temporary counts against the limit from then on, and the sidecar from the next start, which decides the outcome by the table below. The target's name stays held until that start, so remove, rename and every write mode give `ERR_BUSY`. A client's change to it would change what the start decides, and could undo an acknowledged removal. The cost is that a client cannot retry that name until a reset.

At start, before any client can call:

| Found | Action |
| --- | --- |
| Temporary, with no sidecar or a torn one | Removed, with the sidecar. The removal had not begun. |
| Temporary, with a sidecar that cannot be read | Both stay, and the area returns `ERR_IO` until a reset, when start tries again. The target may be gone, so the temporary may be the only copy. A sidecar that stays unreadable keeps the area down until a host repair. |
| Sidecar, temporary, no target | The rename is completed and the sidecar removed |
| Sidecar, temporary, a target of a different size | Temporary and sidecar removed. The old target stands. |
| Sidecar, temporary, a target of the same size | Kept, with `file.recovery_needed`. It may be an unfinished removal, or it may share clusters with the target after a cut during rename, and removing it would free them. It counts against the limit and its slot is skipped. The target's name is held, and each start takes it again, so that no client can change what this choice depends on: remove, rename and every write mode give `ERR_BUSY`, and readers are unaffected. Only a host repair clears this. |
| Sidecar with no temporary, valid or torn | Removed |
| A file with the area's partial suffix | Kept |
| `.crux-up-` or `.crux-co-` with a name no build can write: not canonical digits, or 32 and above | Removed |

## Limits

- Replacement is not atomic across power loss. FatFs marks part of its rename as a critical section, and a cut between its two directory writes can leave two names on one cluster chain. This comes from reading `ff.c`, and it has not been reproduced here.
- The fake's power model is simpler than FatFs, as `tests/file/fake_file.h` states, so the cut walks here prove the order of this sequence and nothing about a card. Physical cuts during a rename and commit need their own approval.
- A listing is not a snapshot. Names are ASCII, flat and at most 63 characters (F4), so WinSCP cannot upload a name with a space. There is no modification time (F6), permissions, directories or sparse writes.
- Foreign files are invisible, so they are not counted against an area's limits.
- A kept temporary needs a host repair.
- `CRUX_FILE_SERVICE_TEMP_SLOTS` limits allocation only, and it cannot exceed 32. Recovery covers all 32 names, so that lowering the setting strands no file. Any other name beginning `.crux-` stays hidden and counted, and start leaves it alone.
- Each kept temporary and each failed commit holds a name in the reservation table until a reset or a host repair. The table is shared by every area, so eight held names refuse every writer with `ERR_NO_RESOURCES`, in spite of the rule that one area's trouble does not stop the others. If the table is full at start, the area that needed it returns `ERR_IO`.
- The service keeps its own copy of the File name rule, so that `resolve` touches no storage. A test holds the two together on every byte value and on the length bound.
- The File lock is global and held across each storage call, so one slow card write delays other File callers. No operation promises a time bound, and M10 measures this.
- The SFTP adapter, its `SETSTAT` handling and a recorded WinSCP run belong to M12, and none is part of this work.

## Validation

`FileService.default` and `FileService.small_table` run the cases in `tests/file_service` on `crux_file_fake`, with a test double for the three access functions that the service calls. `FileService.with_access` runs the service on the real Access core with its fake ports. `scripts/test-file-service.sh` runs them with and without sanitizers and builds a C and C++ consumer from the public headers alone. No part of the service has run on Zephyr or on a target.
