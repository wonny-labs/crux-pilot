# RT1060-EVKB microSD composition

Issue [#14](https://github.com/wonny-labs/crux-pilot/issues/14), acceptance criterion 2.
This records the approved composition and its evidence. The software evidence is below; the
physical results (one board, one card: mount, reboot persistence, power cuts) are in their own
sections and under `docs/evidence`. The one card-removal run is under "Physical results, 3 November". Nothing here claims hardware
qualification or product power-loss safety.

## Decisions in force

- Medium: the onboard microSD on uSDHC1 of the RT1060-EVKB Rev B. Zephyr VFS with FatFs.
  Firmware stays on QSPI NOR XIP. No LittleFS or writable QSPI revival.
- `crux/file.h` and `crux/configuration.h` are unchanged. There is no new filesystem API,
  storage manager or HAL wrapper.
- The composition owns mount order, readiness, root binding and recovery. The File
  component never mounts, formats or selects media.
- No automatic format. A card is formatted FAT32 and given its two directories on a host
  with a card reader, outside the firmware, as a separately authorised step.
- No platform retry loop; no unbind hook (a card swap needs a reboot).

## Composition

Project code in [`samples/storage_sd`](../samples/storage_sd), not in `components/file`:

1. `disk_access_init("SD")`: failure is `SD_STORAGE_NO_MEDIA`.
2. `fs_mount("/SD:")` with `FS_MOUNT_FLAG_NO_FORMAT` and `CONFIG_FS_FATFS_MOUNT_MKFS=n`:
   failure is `SD_STORAGE_MOUNT_FAILED`. `mkfs` is never called.
3. `/SD:/files` and `/SD:/settings` must both exist. If one is missing the volume is
   unmounted and the result is `SD_STORAGE_LAYOUT_MISSING`. A directory is never created.
4. `crux_file_zephyr_bind("/SD:/files")`. Until it succeeds File calls return
   `CRUX_FILE_ERR_NOT_READY`.

A failure at any step stops the sequence, unmounts the volume (the unmount result is not
checked; FatFs fails an unmount only if the volume was not mounted) and leaves the File
component unbound, so a later `sd_storage_start()` can succeed. The Settings File backend
is not part of this sequence; its target provider is a separate decision.

Selection: `CONFIG_CRUX_FILE_FATFS_RT1060` links the File provider into an EVKB Rev B image.
It is off by default and requires the Rev B QSPI board (`BOARD_REVISION_B` and
`BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI`, enforced in Kconfig, the File port CMake and a C `#error`;
the testcase and sample CMake turn an unsatisfied dependency into an error because Kconfig only
warns) and the guarded FatFs/SDHC configuration. Rev C and the hyperflash variant are refused. The older
`CONFIG_CRUX_FILE_FATFS_RT1060_COMPILE_CHECK` object-only check remains.
`CONFIG_SAMPLE_SD_START` (off by default) makes the sample image run the mount sequence at
boot.

## Software evidence

All of it is software-only: `native_sim` with a RAM disk named `SD`, and an arm-zephyr-eabi
cross-build that is linked but never flashed.

| Check | Result |
| --- | --- |
| `tests/native/file_sd_composition`, 8 cases, 64-bit `native_sim` | pass (32-bit runs in CI only) |
| Cases | blank medium fails closed and the medium digest is unchanged; missing directories are not created; one missing directory is not ready; a provisioned medium starts after earlier failures; a second start is rejected without side effects; unmount and remount keep the provider usable, and out-of-order stop and remount are refused; the medium digest itself is shown to see a one-byte change |
| Mutation check | a start that formats on a failed mount fails the blank-medium case; a start that creates missing directories fails the directory cases |
| `scripts/test-file-rt1060-compile.sh` | the compile check and both link scenarios (`crux.sample.storage_sd`, `.start`) build on `mimxrt1060_evk@B/mimxrt1062/qspi` |
| Image symbols | `f_mkfs` is not linked in either image. Only the mount-at-boot image links `sd_storage_start`, `disk_access_init` and `crux_file_zephyr_bind`; the base image links `f_mount` and `f_mkdir` (through the VFS table) but not the provider, so "never creates directories" is a property of the composition code, not of the image |

Observation: Zephyr's FatFs `fs_unmount` does not refuse while a File handle is open (the
case was run and the assertion that it would refuse failed). The application must close
every handle before `sd_storage_stop()`. While the volume is unmounted a File call returns
`CRUX_FILE_ERR_NOT_READY`; the provider cannot tell "unmounted" from "root missing". `stop`
is not a power-down: each start or remount calls `disk_access_init`, so the disk stays
initialised. After a stop, `start` reports `ALREADY_STARTED` and `remount` is the way back;
stop and remount out of order return `-EINVAL`, and remounting a mounted volume `-EALREADY`.

## Resource measurements (static, from linked images)

Measured on the code of commit `9a03c5d` (`samples/storage_sd`, the File provider and the
configuration unchanged since). Linked with `CONFIG_MAIN_STACK_SIZE=4096`, `CONFIG_FS_FATFS_NUM_FILES=4`,
`CONFIG_FS_FATFS_MAX_LFN=127` (stack mode), `CRUX_FILE_POOL_SIZE=4`.

| Image | FLASH | RAM (linker RAM region, including the main stack) |
| --- | --- | --- |
| mount sequence compiled out | 56,724 B | 9,184 B |
| mount sequence compiled in | 57,876 B | 9,824 B |

RAM here is the 32 MB SDRAM selected by the board devicetree (`zephyr,sram`), not on-chip
RAM. Whether USDHC buffers and DMA are correctly placed relative to cache is not verified.

Static stack frames (`-fstack-usage`, bytes), from a separate build of the same sample at the same commit with
`CONFIG_STACK_USAGE=y` and the default 1,024 B main stack: `f_open` 360,
`f_stat` 336, `fatfs_stat` 296, `mount_and_check` 144, `dir_register` 72, `f_read` 64,
`dir_find` 64, `f_write` 56, `follow_path` 56, `f_mount` 40. The `fs_stat` chain used by the
layout check and by every File call alone is about 780 B, and an open about 650 B, before the
SD driver, disk layers and Crux frames. These frames are a lower bound, not a call-chain
maximum. The 4,096 B main stack is a choice; its first measured high-water mark is
2,264 B used with 1,832 B unused in one run on the board (see "Physical results, 3 November").

## Physical results (first card runs, 2 November 2026)

On one RT1060-EVKB Rev B and one 16 GB FAT32 (MBR) card the composition mounted without formatting
(`sd_storage_start: 0`), and the write/read/restart check kept a boot counter and a 4 KiB pattern file
across two normal CPU resets; the card was clean under `fsck_msdos -n` and held the expected contents on
the host afterwards. This is a single run on one card. It is not power-loss, removal, timing or endurance
evidence. Details and limits: [docs/evidence/microsd-physical](evidence/microsd-physical/README.md).

## Physical results, 3 November 2026 (card states and stack)

On the same board and card, a blank card (no partition table, no FAT volume) gave `sd_storage_start: 2`
(`SD_STORAGE_MOUNT_FAILED`) and a FAT32 card without `files` and `settings` gave `3` (`SD_STORAGE_LAYOUT_MISSING`);
nothing was formatted and no directory was created. With a provisioned card the stack probe left 1,832 B of the
4,096 B main stack unused (2,264 B used; the probe's own analyzer frames may be included), and `fsck_msdos -n`
was clean afterwards. Removing the card while mounted gave `CRUX_FILE_ERR_IO` on the next open, no hang, and the
application unmount and remount did not bring the volume back after the card was re-seated (`remount=-5`, no
recovery; flashing the `sd-start` image and resetting mounted the same card), which matches "a card swap needs a reboot". One run per state; the M.2 slot was
empty (operator-reported) and uSDHC1 ownership was not checked against a schematic. Details and limits:
[docs/evidence/microsd-card-states-stack](evidence/microsd-card-states-stack/README.md).

## Profile limits and decisions

- **Settings and power loss (owner decision, 2 November 2026):** the controller may lose its
  latest saved setting after a power cut and fall back to a previous value or to defaults, then
  be configured again. FatFs is not required to do better. If it cannot meet the product need
  after physical evidence, a different file system (for example Reliance Edge) is the intended route, so no FatFs
  effort goes into checksummed or atomic Settings storage. No Settings File target provider is
  part of this composition.
- **File size:** FAT32 (the card format here; exFAT is not enabled) files end at 4 GiB minus
  one byte. A write that would cross the limit is cut to the remaining bytes and returns
  `CRUX_FILE_PARTIAL`; a write made when no bytes remain is reported as `NO_SPACE`, not
  `OUT_OF_RANGE`, and `crux_file_seek` rejects offsets above `INT32_MAX`
  (2 GiB minus one). Files larger than 2 GiB are outside this profile; this is a documented
  limit, not a tested behaviour (the simulator media is 1 MiB).
- **Persistence script:** `scripts/test-file-persistence.sh` stays a manual gate and is not in
  CI (about a minute and dependent on host timing).

## Power-cut results (2 November 2026)

33 power cuts during continuous per-record syncs on the same board and card, made by switching the
board's USB hub port: no acknowledged record was missing, no partial or damaged record was found, the
card always mounted and a read-only `fsck_msdos -n` was clean. The writes were 32-byte appends with a sync
each; larger writes, directory changes and renames were not cut, and it is one card. See
[docs/evidence/microsd-power-cut](evidence/microsd-power-cut/README.md).

## Replace-pattern power-cut results (2 November 2026)

The Settings File backend compacts by writing a temporary file and calling `fs_rename`, and FatFs's rename
removes the destination first. Repeating that sequence on the same board and card with 100 power cuts:
**29 left no settings file but a complete newer temporary** (the cut fell between the removal and the
rename), 71 left a valid file, and none left a stale or damaged settings file (stale detection is a lower bound: two cuts could not be checked); the card always mounted.
The rate belongs to a loop that does nothing but replace the file: a real controller compacts rarely, so it
is not a deployment rate. With the corrections staged in the test fixture (#144, not a product backend) a missing settings file
next to a `.cmp` leftover is an I/O error until the next save, not defaults; on 3 November 2026 the owner chose to complete the rename with the start-up recovery step described below (software only, not yet called by any image). The read-only `fsck_msdos -n` afterwards reported 40 orphaned clusters (about
320 KiB), where the earlier append test had been clean. One card, 256-byte files, the backend's
write-temporary, close, rename order and the identical `fs_rename`, not the backend itself. See
[docs/evidence/microsd-replace-cut](evidence/microsd-replace-cut/README.md).

## Probes for the remaining physical items

`samples/storage_sd_probe` holds two default-off images, both writing to the card (physical approval
needed for each run): `CONFIG_SAMPLE_SD_STACK_PROBE=y` mounts, runs the file check and a FatFs workload
(create, 64 KiB write, sync, read back, rename, stat, list, remove) twice and prints the
Zephyr thread analyzer report (stack size and unused bytes per thread; the interrupt stack appears as an
`ISR0` line whose figure includes early boot, so it overstates interrupt use),
and `CONFIG_SAMPLE_SD_REMOVAL_PROBE=y` writes one small file every 200 ms and prints the result codes of every
failing step (successful steps print every 25th time); after the first failure it unmounts and retries a
remount every second, so removing and reinserting the card shows whether the application remount path
recovers without a reboot. It does not show whether the disk driver can recover: the remount does not force a
new card initialisation (the source suggests the disk reference count never returns to zero), so a failure
after a real swap is consistent with the documented reboot-on-swap policy. If the unmount fails the probe
stays LOST (`remount=-120`, `-EALREADY` on the RT1060 picolibc image, on the console). The stack probe leaves `bootcount` and `pattern` in the File root,
the removal probe leaves `probe`. The blank-card and wrong-layout cases need a card prepared on a
host, the M.2 slot and uSDHC1 ownership need the board in hand. The stack probe, the removal probe and the
blank and wrong-layout cards were run on the board on 3 November 2026
([evidence](evidence/microsd-card-states-stack/README.md)). uSDHC1 ownership was checked on 3 November 2026 against
the vendor schematic (Rev B1, not stored in this repository) and the operator's photos: the SD slot reaches the uSDHC1 pads
through populated zero-ohm resistors, and the M.2 SDIO lines reach the same pads only through unpopulated ones. That is a
document and photo check, not a measurement on this board; the SD power-enable pin was not traced, and the M.2 slot was
reported empty by the operator.

## Recovery step for an interrupted compaction (3 November 2026, software only)

Owner decision of 3 November 2026: complete the rename at start-up in the one state the replace-pattern test
found. `sd_settings_recover(path)` in [`samples/storage_sd`](../samples/storage_sd) renames `<path>.cmp` to
`<path>` when the settings file is missing and the temporary is a non-empty regular file, and does nothing in
any other state. It is safe because the backend closes the temporary (a synced close) before it starts the
rename, so a missing settings file next to a `.cmp` means the compaction had finished writing. A stat error is
never read as "missing", a zero-length temporary is left alone (`-ENODATA`), a directory is never renamed, and a
path of 28 bytes or more is refused: `<path>.cmp` then does not fit the backend's 32-byte name buffer, the backend
shortens the temporary's name, and this step does not handle that.
Call it after the volume is mounted (on an unmounted volume the file system reports "not found" and the step
returns 0) and before the first settings load. It is not called by any image yet: the
2 November decision keeps a Settings File target provider out of this composition, so the call site belongs to
whichever provider is added. It does not make Settings atomic: a cut during an append can still leave a torn
tail, orphaned clusters remain, and a cut inside the FAT metadata update has no guarantee. It is covered by
`tests/native/file_sd_settings_recover` on the unpatched backend; it has not been run on the board.

## Not covered (needs the card and board)

Card detect and power sequencing (the card was present at every boot); real SD timing and error behaviour; removal while mounted beyond the one run above (no forced driver re-initialisation was tried);
whether the M.2 Wi-Fi SDIO path shares uSDHC1 on this board (the board devicetree labels the
same controller `m2_wifi_sdio`; nothing in this repository confirms pin or controller
ownership); stack use beyond the one probe run (no Settings backend, networking or other threads); cache and DMA placement; and power-loss behaviour beyond the 32-byte append test and the replace-pattern test above (larger
writes, directory changes other than that replace, other cards, the Settings File backend itself); data
across normal CPU restarts and those two power-cut tests are covered in the physical results above.

## Physical gate procedure

The procedure, preconditions, card states, stop conditions and record requirements are in
[microsd-physical-gate.md](microsd-physical-gate.md). It authorises nothing: every hardware step needs
explicit approval for the current task.
