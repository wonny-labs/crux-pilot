# File software qualification

See [the subsequent simulator candidate](file-native-validation.md) for the separately authorised sleepable-lock change and actual VFS/LittleFS software tests. The retained results below describe the original Host qualification.

Issue [#23](https://github.com/wonny-labs/crux-pilot/issues/23) implements the
[approved contract](file-contract.md) as a bounded portable C17 component with a
Linux Host provider. Production media, layout and backend adoption remain open.
It supports the CRUD/sync/recovery concerns from
[#15](https://github.com/wonny-labs/crux-pilot/issues/15), without granting physical
Flash access. The existing [Flash safety work](flash-safety-acceptance.md) and
[storage assessment](storage-configuration-assessment.md) remain separate evidence.
QSPI/LittleFS/NVS are candidates, not selected production providers.

## Implementation and configuration

Consumers include `<crux/file.h>` and link `crux::file`. Only the private trusted
Host composition includes `components/file/ports/linux/file_linux.h` to bind an existing
root directory once, before concurrent access. No root is implicit: open/remove
return NOT_READY until binding succeeds. The root descriptor stays open for the
process lifetime; process exit releases it. Rebinding/unmounting is unsupported.
Files are opened relative to that descriptor, symlinks are not followed, and only
regular files are accepted. This is a qualification profile, not a security sandbox.

The private `file_port.h` permits provider build overrides of
`CRUX_FILE_POOL_SIZE` (default/minimum 4) and `CRUX_FILE_NAME_MAX`
(default/minimum 63), applied consistently to shared/provider translation units.
These are build-time bounds, not runtime configuration. There is no heap allocation
in the component. A C17 atomic flag protects pool state and serializes complete
calls, including blocking provider I/O; contention can spin without a latency bound.
This initial Host profile does not promise parallel I/O on separate handles.
The caller still serializes aliases, per-handle use and conflicting same-file
operations. The composition-only root binding is outside concurrent operation.

Shared code reserves a slot, opens without truncate, truncates separately for
CREATE_REPLACE, and closes exactly once on truncation failure. The public output
stays null and the primary failure is preserved. A private Host diagnostic retains
the latest secondary cleanup error. Accepted writes and failed creation/truncation
are never rolled back. Close clears the public pointer and retires the slot even
on a backend failure; retrying or using any stale alias is outside the contract.
Slots may be reused for a later, separately opened handle.

The Host provider checks 64-bit signed offsets and `SSIZE_MAX` before dispatch.
No operation automatically retries, including interrupted I/O/close. Linux close
is invoked once and the descriptor is retired even when it reports an error.
Unknown native error numbers map to File IO. Host ENOSPC/EDQUOT map to NO_SPACE,
resource errors to NO_RESOURCES, access errors to ACCESS, and size/overflow errors
to OUT_OF_RANGE. Native errno never escapes the provider.

`crux_file_result_t` is a separate result domain from `crux_status_t`: both use
zero for success and negative errors, but individual numeric codes differ. File
PARTIAL is positive and needs explicit handling, including zero-progress writes.
Consumers compare `CRUX_FILE_*` names; no casts, base-enum comparisons or generic
status conversion are introduced. A future composition translation must use an
explicit switch and preserve distinctions such as PARTIAL and NO_SPACE.

## Zephyr provider handoff (plan only)

Pinned source: Zephyr `684c9e8f32e4373a21098559f748f06915f950c9`.
No Zephyr File provider, VFS shim, native runtime test or writable QSPI composition
is claimed here. Deterministic tests exercise the shared port algorithm, not the
actual VFS or LittleFS object lifecycle.

| Public operation | Planned private VFS mapping and checks |
| --- | --- |
| Open READ | `fs_file_t_init`, `fs_open(..., FS_O_READ)` on the composition-selected mount/root |
| Open READ_WRITE | `fs_open(..., FS_O_READ | FS_O_WRITE)` without CREATE or TRUNC |
| Open CREATE_REPLACE | `fs_open(..., FS_O_CREATE | FS_O_READ | FS_O_WRITE)` **without FS_O_TRUNC**, then `fs_truncate(file, 0)` |
| Failed truncate | Save primary failure, call `fs_close` exactly once while the native object remains valid, record secondary cleanup failure, reinitialize/retire private slot and leave output null; no rollback |
| Read/write | `fs_read`/`fs_write`; checked length conversion to supported `size_t`/`ssize_t` range; preserve nonnegative count, map negative native errno explicitly; short/zero write becomes PARTIAL |
| Seek | Validate `uint64_t` against actual signed `off_t` range before casting; `fs_stat` the same private file path for EOF bound, then `fs_seek(..., FS_SEEK_SET)`; caller excludes conflicting modification |
| Sync | `fs_sync`; OK acknowledges the selected backend flush, not general power-loss safety |
| Close | `fs_close` once; preserve failure, reinitialize/retire slot and clear public pointer regardless; never retry a released backend object |
| Remove | `fs_unlink` after shared open-name check; success acknowledges logical removal only |

The future provider must also bound private mount/root/name path construction
before side effects and test conversion against its actual VFS types. Error
mapping uses an explicit switch over native negative errno; unknown values map
IO. Controlled VFS integration tests must verify initialization, truncate cleanup,
released-state retirement and secondary diagnostics before provider adoption.

`subsys/fs/fs.c:182–200` clears `mp` after combined FS_O_TRUNC failure without
backend close; subsequent generic close cannot provide that cleanup. The separate
truncate sequence avoids this path. `fs.c:206–226` retains `mp` after close failure,
while `subsys/fs/littlefs_fs.c:330–342` releases file data even on close failure.
These are source findings at the pinned revision, not reproduced runtime defects.
Mount/unmount, partition/media selection, formatting, startup/recovery and
production readiness remain #14/#15 composition decisions. No native/upstream
Flash change is required or made by #23.

## Evidence and acceptance map

The final Linux software checks passed: 78/78 Host debug CTests, 78/78
ASan/UBSan CTests, 2/2 independent consumer CTests, 292/292 build-tool unittests,
and 13/13 focused native-interface unittests (including the added File consumer
inventory). Formatting, dependency guards and the full Host static-analysis script
passed. Static-analysis output reports suppressed non-user/system warnings; it
contains no remaining owned-code diagnostics. These are local runs, not a claim
of GitHub CI execution.

The software checks and limitations are recorded under
[evidence/file](evidence/file/). Debug and ASan/UBSan exercise real temporary
Host directories and a separate deterministic provider. The fault provider models
native slot ownership and rejects double-close; it is deliberately private.

| #23 acceptance criterion | Source and verification |
| --- | --- |
| 1. Reviewed contract covers operations/lifecycle/ownership/blocking/concurrency/errors/durability without native types | `file-contract.md`; `components/file/include/crux/file.h`; human software approval recorded in contract; independent Reviewer and Challenger PASS for this software scope (see retained review) |
| 2. Public header/target consumed independently in Host C/C++ without private consumer includes or Zephyr dependencies | `tests/file_consumer/CMakeLists.txt`, `consumer.c`, `consumer.cpp`; native root binding is a separate `fixture.c`; source inventory and CMake direct/public guards |
| 3. Deterministic success/invalid/short/full/storage-error/sync/close tests | `tests/file/fault_test.c`; `tests/file/host_test.c`; File CTests; close/truncate tests preserve primary errors, count close once, retire native state and permit later independent opens |
| 4. Exact Zephyr integration plan separates mount/media/format composition | VFS mapping above and approved native findings; no VFS runtime qualification claimed |
| 5. Unsupported operations and nonpromises explicit | Contract: no directory tree, rename/replace transaction, append, sparse extension, metadata, async I/O, interrupted-write atomicity or general power-loss safety |
| 6. Reviewed contract linked to #15 and tested without physical Flash access | Links above, Linux software container and private fault provider; no device/home/socket mounts or physical actions |

Reproduce Linux checks with `scripts/check-host.sh`,
`CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh`, `scripts/check-format.sh`,
`scripts/check-dependencies.sh`, `scripts/analyze-host.sh`, and
`python3 -m unittest discover -s tests/build -v`.
For separate consumers: configure `tests/file_consumer` into a fresh build folder,
build it and run CTest there. The Host CI includes this separate consumer build.
The deterministic contract executables explicitly keep assertions enabled even
when a composition selects a release build.

## Independent review

The separate [Reviewer report](evidence/file/independent-review.md) found no material
issues and independently reran fault, actual Linux provider, and C17/C++17 consumer
checks with strict warnings and ASan/UBSan. The separate Challenger also accepted
the File mechanism for this Host qualification scope, with an explicit target
integration condition: a spinlock held across blocking I/O can starve/deadlock a
priority-scheduled target. Review synchronization before adopting a target provider;
this Host implementation is not evidence that target concurrency is safe.
Root separately reran the complete debug and sanitizer suites: 78/78 each.
Human review, native provider adoption and physical safety gates remain intact.
