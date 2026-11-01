# RT1060-EVKB microSD composition (software only)

Issue [#14](https://github.com/wonny-labs/crux-pilot/issues/14), acceptance criterion 2.
This records the approved composition and what software evidence exists for it. No
physical board, microSD card, power-loss, reboot or removal result exists. Nothing here
claims persistence or hardware qualification.

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
It is off by default and requires the guarded FatFs/SDHC configuration. The older
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
maximum. The 4,096 B main stack is a choice with no measured margin; its high-water mark
needs a run on hardware.

## Not covered (needs the card and board)

Card detect and power sequencing; real SD timing and error behaviour; removal while mounted;
whether the M.2 Wi-Fi SDIO path shares uSDHC1 on this board (the board devicetree labels the
same controller `m2_wifi_sdio`; nothing in this repository confirms pin or controller
ownership); high-water stack use; cache and DMA placement; and persistence, power loss,
reboot and interrupted-save behaviour.

## Physical gate procedure (draft, nothing here is authorised)

Every step below is a hardware operation. Each needs explicit approval for the current task,
a verified board, probe and card, and a safe execution setup. A past approval does not
authorise a later run. Use a card that is test-owned and may be erased.

1. Record the board revision, card, reader and firmware commit. Confirm uSDHC1 and the
   card-detect and power GPIO against the schematic, and the M.2 slot state.
2. Prepare the card on a host: format FAT32, create `files` and `settings`. Keep the
   card-reader output. This is the only formatting step.
3. Flash the `crux.sample.storage_sd.start` image only after approval; read the status
   printed by `sd_storage_start`. Expected: `0`. A blank card must give a mount failure and
   leave the card unchanged (compare a host-side image before and after).
4. Write, sync and read files through `crux_file_*`, then read the card on the host. The
   sample image only calls `sd_storage_start`; an image with a write and read path is
   separate work that does not exist yet.
5. Only with separate approval, interrupt power and reboot during and between saves. Record
   which acknowledged files and Settings records survive. Torn-write evidence from
   simulators is not a substitute.
6. Record every result, including failures, with the tested revision. Do not close #14 or
   #16 from this record alone.
