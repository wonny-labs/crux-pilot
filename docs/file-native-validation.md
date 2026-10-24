# Simulator File provider candidate

The user approved D1–D4 for software-only implementation. This candidate preserves
`<crux/file.h>` and adds a private Zephyr VFS provider qualified only with
native_sim and LittleFS. It neither mounts/formats media nor selects a target
partition. `CONFIG_CRUX_FILE_SIMULATOR` cannot be selected for an EVKB build;
the provider and CMake independently reject non-simulator compositions.

## Composition and ownership

Composition explicitly mounts a volume and binds its root once through the private
`file_native.h`, before concurrent calls. Root binding is not a sandbox, media
manager or public API. The process-lifetime root accepts at most 63 bytes; file
names retain the existing contract. Unmount/rebind concurrent with live File
calls is unsupported. The public `crux::file` target exposes declarations only;
Zephyr registers the private implementation independently, following existing
component patterns. The test's C17 consumer compiles and links without native
headers or generated kernel usage requirements. The selected 32/64-bit compiler
ABI flag is still required and propagated as a toolchain requirement.

File pool/call serialization now uses a private sleepable mutex on Linux and
Zephyr, rather than an atomic spinlock held across I/O. It preserves serial I/O
and the caller's per-handle/same-file obligations. No bounded latency, fairness,
ISR support or concurrency performance guarantee is added. Internal lock/ownership
failure is fatal, since continuing could violate handle/pool ownership; this is
not converted into a recoverable File close result that would weaken its
consume-on-close guarantee. The Linux wrapper has concurrency and isolated fatal
acquisition-failure coverage. No public Sync pool or new synchronization framework
is introduced.

CREATE_REPLACE opens without native TRUNC, truncates separately and closes exactly
once on failure, preserving the primary failure and recording secondary cleanup
failure. Close consumes the public handle and retires/reinitializes native state,
including when native close reports an error after freeing backend data. No
rollback or close retry is implied. Short/zero writes preserve counts and PARTIAL;
explicit native no-space errors map to NO_SPACE, unknown errors to IO.

The provider limits transfer lengths and offsets to LittleFS's signed 32-bit
count/offset range, including on a 64-bit simulator. Checks occur before native
dispatch. Seek uses the live handle's EOF, not committed path metadata, so seeking
inside a buffered write works. It temporarily seeks to end and restores the
original position before rejecting beyond-EOF requests. Native errors remain
observable; no rollback of native effects after an I/O error is promised.

## Focused verification gate

The existing pinned Zephyr source remains unchanged. `west.yml` adds the pinned
Zephyr manifest's LittleFS module to its allowlist without upgrading Zephyr.
Use `west update littlefs` in the normal development workspace. The author used
that exact module revision's source archive when the GitHub clone/API transport
was unavailable; archive identity is recorded in the evidence. It is an external
dependency, not vendored source in this repository.

From a configured Linux west workspace with the pinned LittleFS dependency:

```sh
export ZEPHYR_BASE=/path/to/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=host
python3 "$ZEPHYR_BASE/scripts/twister" -T tests/native/file_littlefs \
  -p native_sim/native -p native_sim/native/64 -O /tmp/file-provider -j2 --inline-logs
```

The fixture automatically adds the current Crux module. If using explicit module
selection, include the LittleFS directory using `--extra-args=ZEPHYR_MODULES=/path/to/modules/fs/littlefs`.
This manual gate is not part of the standard CI workflows. Re-execute it and
obtain affected review after changing the provider, fixture, pin or profile;
standard CI success alone does not prove this gate ran.

The 64 KiB area at simulator offset `0x100000` is test-only. Tests explicitly
format fresh owned simulator storage. Production code never autoformats;
`FS_MOUNT_FLAG_NO_FORMAT` is used for mounts. Tests cover public CRUD/sync/remount,
full/reclamation with retained sentinel, native short/zero/error results,
truncate/close release semantics, corrupt mount without autoformat, live EOF and
32/64-bit request bounds. Native VFS link wrappers are explicit deterministic
boundary faults; Flash byte callbacks exercise actual LittleFS write failures.

The interruption case permits sixteen Flash byte callbacks during sync, then
returns EIO and checkpoints simulated bytes before close/unmount cleanup. After
retiring volatile objects, the test restores that checkpoint and remounts: the
prior acknowledged six-byte file must survive. Cleanup effects are deliberately
excluded from the replayed interruption image. This is one bounded simulated
interruption/reinitialization, not a process reset, physical power cut or proof
of arbitrary write atomicity. Remount results cannot satisfy physical reboot ACs.

See [retained evidence](evidence/file-native/) for source/configuration identities,
commands, RED/GREEN observations and review. Existing Host tests and independent
C17/C++17 consumers remain applicable. The old Host-spinlock description in
`file-validation.md` describes the accepted historical profile; the change here
supersedes only its serialization mechanism and live native mapping assumptions.

## Target gates retained

No EVKB writable provider or layout is selected. File and Configuration physical
integration still require #14 medium/layout/capacity decisions, truthful bounded
Flash operations and safe busy-state termination, final-image XIP/startup/IRQ/
exception/DMA closure, effective linker/runtime/programmer protection, backup and
exclusive board ownership. Target operation needs separate explicit approval.
The simulator's NO_FORMAT test is not a whole-system no-write guarantee: other
native mount/repair/Settings paths need their own review. No wear, timing,
endurance, long-load or general power-loss qualification is claimed.
