# eMMC composition on Reliance Edge (link only)

Issue [#254](https://github.com/wonny-labs/crux-pilot/issues/254), the second part: the RT1060 link, the
measurements and this record. It follows the structure of the [microSD composition](microsd-composition.md) and
describes the reference composition in [`samples/storage_emmc`](../samples/storage_emmc) of the
[eMMC design](plans/emmc-reliance-design.md). **No result here comes from an eMMC device or from any board.** The
evidence is host and `native_sim` tests against a test-only simulator, and an arm-zephyr-eabi image that is linked
and never flashed. The RT1060-EVKB has no eMMC, so the link says what the composition costs and that its pieces
fit together, and nothing about how it behaves.

## Decisions in force

- Medium: one eMMC user-area volume, as a Zephyr `zephyr,mmc-disk`. File system: Reliance Edge behind the Zephyr
  VFS, through the adapter in `zephyr/fs/reliance_edge`. The boot path is out of scope.
- `crux/file.h` and `crux/configuration.h` are unchanged. There is no new file system API, storage manager or HAL
  wrapper. FatFs and the [microSD composition](microsd-composition.md) are unchanged and remain the EVKB profile.
- The composition owns mount order, readiness, root binding and recovery, as on microSD. The File component never
  mounts, formats or selects media.
- No automatic format and no directory creation. The medium is formatted and given its two directories outside the
  firmware, as a separately authorised step. The formatter (`CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT`) is not compiled into
  the sample, and the link check requires that `red_format` is absent from the image.
- No platform retry loop, and the file system adds no hidden retry (`bBlockIoRetries` is 0). The stack's own transfer
  retries (`CONFIG_SD_DATA_RETRIES`) stay as they are.
- **Owner's link-only exception (4 November 2026, session).** The RT1060 link may compile the File
  additions of the eMMC design. It covers linking only. Nothing runs on a target, and the exception does not cover a
  flash, a reset or any other hardware step. The design records the wording.
- Any use of an MMC disk needs `CONFIG_CRUX_MMC_CACHE_FLUSH`, see "Selection". Reliance Edge's commit relies on a
  flush as a barrier, and the pinned MMC stack does not send one.

## Composition

Project code in [`samples/storage_emmc`](../samples/storage_emmc), not in `components/file`, with the same shape as
`sd_storage`:

1. `disk_access_init("MMC")`: failure is `EMMC_STORAGE_NO_MEDIA`.
2. `fs_mount("/MMC:")` with Reliance Edge: failure is `EMMC_STORAGE_MOUNT_FAILED`. A blank medium fails here and is
   never formatted. The adapter never calls `red_format` from a mount.
3. `/MMC:/files` and `/MMC:/settings` must both exist. If one is missing the volume is unmounted and the result is
   `EMMC_STORAGE_LAYOUT_MISSING`. A directory is never created.
4. `crux_file_zephyr_bind("/MMC:/files")`: failure is `EMMC_STORAGE_BIND_FAILED`. Until it succeeds File calls return
   `CRUX_FILE_ERR_NOT_READY`.

A failure at any step stops the sequence, unmounts the volume (the unmount result is not checked) and leaves the File
component unbound, so a later `emmc_storage_start()` can succeed. `emmc_storage_stop()` and
`emmc_storage_remount()` are for the application: Reliance Edge refuses an unmount while a handle is open, so every
handle must be closed first. `stop` is not a power-down. A second start reports `ALREADY_STARTED`. The disk name is
`CONFIG_SAMPLE_EMMC_DISK_NAME` (default `MMC`) and the mount point stays `/MMC:`.

With `CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE` the sample calls `emmc_configuration_start()` instead. It runs the four
steps above and, only if they returned `EMMC_STORAGE_READY`, calls `crux_config_zephyr_init()`. The order matters: the
Settings File backend creates the directories of its file path when it initialises, and this composition's rule is that
the firmware creates none. So on a medium without `/MMC:/settings` the settings are never initialised, and a test
confirms that nothing is written. If the storage is ready and the settings initialisation fails, the status is
`EMMC_STORAGE_CONFIGURATION_FAILED` (added to the enumeration for this); the volume stays mounted and the File root stays
bound, so a second `emmc_configuration_start()` reports `ALREADY_STARTED` and retries nothing. The application retries
with `crux_config_zephyr_init()`, which is what the test does.

`CONFIG_SAMPLE_EMMC_START` (off by default) makes the sample image run the sequence at boot. The default image
touches no medium. Do not flash an image with it set until the physical gate is approved for the current task.

## Selection

- `CONFIG_CRUX_FILE_RELIANCE_EDGE` links the File provider over the Reliance Edge file system, on any board that has
  the file system. It is mutually exclusive with the other File selections, and names are stored unencoded. It sets
  `CONFIG_SDHC_BUFFER_ALIGNMENT` to 32, as the two RT1060 FatFs selections do, and the adapter refuses a smaller value
  at compile time, because the USDHC HAL invalidates a read target by whole D-cache lines.
- Reliance Edge source is not in this repository. It comes from the optional west group `crux-reliance`, which is off by
  default: `west config manifest.group-filter -- +crux-reliance`, then `west update reliance-edge`. A configuration
  without the tree fails and names the group. The sample's CMake fails if the File selection did not take effect, because
  Kconfig only warns.
- **The flush option.** `CONFIG_CRUX_MMC_CACHE_FLUSH` replaces `subsys/sd/mmc.c` in the SD library with a copy
  generated from `patches/zephyr/mmc-cache-flush.patch`, so that `DISK_IOCTL_CTRL_SYNC` sends `FLUSH_CACHE` when the
  stack has turned the eMMC cache on. The Zephyr checkout is never edited, and a source whose hash differs fails the
  configuration. The adapter's CMake refuses Reliance Edge on an MMC disk without it, and the
  fixture-only `CRUX_EMMC_FLUSH_CONTROL` that bypasses the guard is honoured on `native_sim` only. **Any use of an MMC
  disk needs this option, including by code that is not Crux.** Without it a successful sync can leave data in the
  card's cache.
- **FatFs on an MMC disk is not a Crux File selection.** `CONFIG_CRUX_FILE_FATFS_RT1060` requires the SDMMC disk, and
  nothing selects FatFs for an MMC disk. A project that mounts FatFs on one still needs the flush option, and its
  behaviour on the simulator is covered by the FatFs power-cut replay only, not by the composition.
- The sample's `prj.conf` names `CONFIG_DISK_DRIVER_MMC=y` and `CONFIG_IMX_USDHC=y`. The flush option depends on the MMC
  stack that the driver selects, so without them Kconfig would drop the option with only a warning. The gate is
  `scripts/test-file-emmc-compile.sh`, which reads the generated `.config` and fails when an option did not take.
- **Configuration on the Settings File backend.** `CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE` (`depends on
  CRUX_FS_RELIANCE_EDGE && SETTINGS_FILE`) links `components/configuration` with the private provider
  `configuration_settings.c` over the Zephyr Settings File backend, on Reliance Edge only. The pinned backend hides I/O
  faults (a failed write or read can be reported as success or as a missing key), so the selection stages the corrected
  build-local copies of `settings_file.c`, `settings_line.c` and `settings_store.c` that
  `tests/native/configuration_settings_file` already uses, and **an image that selects it reaches a file under
  `tests/`**: the module's `zephyr/CMakeLists.txt` includes `tests/native/configuration_settings_file/candidate.cmake`.
  The corrections were not moved, because moving them would touch the hash record of `tests/native/settings_error`,
  `.gitattributes` and frozen evidence for no change in behaviour. A product build that selects the provider needs that
  directory. Set `CONFIG_SETTINGS_FILE_PATH` under the volume's mount point (the sample uses `/MMC:/settings/cfg`; the
  link check requires exactly that). `CRUX_SETTINGS_FILE_CONTROL` keeps the unpatched backend for the native_sim
  negative control, and is refused on any other board at CMake. The corrections' limits are those of the
  [Configuration notes in the test guide](../tests/README.md): no checksum or resynchronisation, and a torn tail can lose
  the latest save. The Kconfig dependencies only warn when unmet, so the sample's link check requires the symbol in
  `.config`.
- **The link-only overlay.** `samples/storage_emmc/boards/mimxrt1060_evk_mimxrt1062_qspi_B.overlay` removes the
  card-detect GPIO and the `sdmmc` child of `usdhc1` and adds a four-bit `zephyr,mmc-disk` named `MMC`. It exists to
  link only, and it does not describe any board. Zephyr applies it to every build of this sample for this board, so an
  image for the EVKB microSD with the same code needs its own overlay selection and is not built or checked here.
- **Commercial kit selection point.** The public source is GPLv2 only. The CMake cache variables
  `CRUX_RELIANCE_EDGE_DIR` (the source tree), `CRUX_RELIANCE_EDGE_CONFIG_DIR` (the configuration headers and
  `redconf.c`) and `CRUX_RELIANCE_EDGE_SOURCES` (the source list) are the only place a kit is named. They are set on the
  build command line and are never committed. No kit source, text or product information belongs in this repository,
  its issues or its pull requests. The adapter stays on the base API, but nothing here was checked against a kit.
  An image that links the public source is a test image and is not distributed or uploaded, and `docs/evidence` keeps no
  image.

## Failure behaviour

| Event | Result of the composition |
| --- | --- |
| The disk does not initialise | `EMMC_STORAGE_NO_MEDIA`; nothing is mounted. A failed init takes no reference, so a later start may succeed. |
| Blank or foreign medium | `EMMC_STORAGE_MOUNT_FAILED`; the medium is not written. |
| Missing `files` or `settings` | The volume is unmounted and `EMMC_STORAGE_LAYOUT_MISSING` is reported; nothing is created. |
| A second start | `EMMC_STORAGE_ALREADY_STARTED`, with no side effects. |
| `stop` or `remount` out of order | `-EINVAL`; a remount of a mounted volume is `-EALREADY`. |
| The storage is ready and the settings initialisation fails | `EMMC_STORAGE_CONFIGURATION_FAILED` from `emmc_configuration_start()`. The volume stays mounted and the File root bound; nothing retries. Call `crux_config_zephyr_init()` again. |
| Block write, read, commit or flush error on a mounted volume | The call that reaches it gets an error and the volume becomes read-only until a remount (the adapter, F14 of the design). Writes and syncs then report `ACCESS`, and a close with unsaved writes reports `ACCESS` too. |
| The device rejects the flush | `DISK_IOCTL_CTRL_SYNC` returns an error, and so does the sync; tested with a status bit that clears on read. |
| Card busy past `CONFIG_SD_DATA_TIMEOUT` | An error, not a hang. Linux allows 30 s for the flush and Zephyr's default is 10 s, so a composition that needs longer raises the value. |
| Power cut | The replay invariants of the design, on the simulator only. |

The rows about the adapter and the flush are tested below the composition, in the suites listed next. The
composition's own test covers the first five.

## Software evidence

Software only, as it stands at the revision recorded in the
[link record](evidence/emmc-reliance/rt1060-link/README.md). **There is no physical eMMC evidence.** The simulator
and the replay are models written by the author of the code that they test, and the negative controls show that the
checks can fail, not that the model matches a device.

| Check | Result |
| --- | --- |
| `tests/native/file_emmc_composition`, 64-bit `native_sim`, on the eMMC simulator with the flush option | pass: 9 cases in `crux.file.emmc_composition` and 1 in `.init_failure` (32-bit runs in CI only) |
| Its cases | the digest sees one changed byte; stop and remount before start are refused; a blank medium fails closed and is not formatted; missing directories are not created; one missing directory is not ready; a provisioned medium starts after the earlier failures; a second start is rejected; the provider survives an application unmount and remount; `crux_file_sync` reaches the medium; a failed card init reports no media |
| Mutation checks | a start that formats on a failed mount, one that creates directories, one that accepts a second start, a stop that skips the unmount, a layout check that never fails, a start that ignores a failed disk init, and a sync without the flush (by build and by source) each fail the case that names them |
| `tests/native/emmc_stack`, `file_reliance`, `emmc_powercut`, `emmc_restart` | the stack against the simulator with and without a cache, the adapter and File contract on Reliance Edge, the power-cut replay on both file systems with controls, and data across process restarts; see the [test guide](../tests/README.md) and the [replay record](evidence/emmc-reliance/replay-reliance/README.md) |
| `tests/native/configuration_settings_reliance`, 64-bit `native_sim`, on a RAM disk named `MMC` | pass: 28 executed cases and 1 skipped in `crux.configuration.settings_reliance`, 1 executed and 3 skipped in `.init_failure`. The 25 shared cases of the FatFs Settings File suite (all but the read-back probe) pass against the real provider with the staged corrections, and three cases are new: a compaction leaves no `.cmp`, a start on a medium without `/MMC:/settings` initialises nothing and writes nothing, and a start on a provisioned medium initialises the settings once. The fourth, a failing settings initialisation, has the second scenario |
| Its control | the same suite on the unpatched backend (`CRUX_SETTINGS_FILE_CONTROL`) fails exactly 12 cases, passes 16 and skips 1. FatFs fails 13; `test_full_volume_never_acknowledges_a_lost_save` passes here because Reliance Edge reports a full volume as `-ENOSPC`, which the unpatched write handler returns, where FatFs gives a short or zero count that it turns into success |
| Mutation checks of the selection and `emmc_configuration_start` | settings initialised before the layout check, initialised twice, the ready check skipped, a failed initialisation ignored, the corrections not staged, and an init before the mount each fail the case that names them (the first, third and last also fail later cases that depend on the volume) |
| W4 of the power-cut replay, `native_sim`, `--workload W4` | 2174 cut cases on both profiles (2232 with the sanity cases, controls and staged states), I1 to I5 held on `red-fix-settings` in every cut case, the active settings file never missing; 864 cuts left a `.cmp` temporary behind, which is not a failure; three controls and three staged states as required. See the [settings replay record](evidence/emmc-reliance/replay-settings/README.md) |
| `scripts/test-file-emmc-compile.sh` | the three scenarios (`crux.sample.storage_emmc`, `.start` and `.settings`) build and link on `mimxrt1060_evk@B/mimxrt1062/qspi`; the Settings image has the provider, `CONFIG_SETTINGS_FILE_PATH="/MMC:/settings/cfg"` and the three staged Settings File sources in its build and none of the pinned ones, and the other two have none of this; the Settings scenario with the fixture-only control fails at CMake; in every image the generated `.config` has `CONFIG_DISK_DRIVER_MMC=y`, `CONFIG_CRUX_MMC_CACHE_FLUSH=y` and `CONFIG_SDHC_BUFFER_ALIGNMENT=32`, and has neither `CONFIG_DISK_DRIVER_SDMMC` nor the formatter; `red_format` and FatFs are not in the image; the build used the generated `mmc.c` with the expected hashes and not `subsys/sd/mmc.c`; the same sample without the flush option fails at CMake, and so does one with the fixture-only control |
| Symbols | only the two mount-at-boot images (`.start` and `.settings`) call the composition, so only they link `emmc_storage_start`, `disk_access_init` and `crux_file_zephyr_bind`, and only `.settings` links the Settings provider. The base image keeps the file system and the stack through the VFS table, so "never formats" and "never creates directories" are properties of the composition code, and the image check shows only the absence of the formatter |
| CI | the check is not yet run by CI. The change that adds it to `.github/workflows/zephyr.yml` is proposed in its own commit and needs review. Until it runs there, the 32-bit runs and the property that CI uploads no such image are not shown |

## Resource measurements (static, from linked images)

Linked on `mimxrt1060_evk@B/mimxrt1062/qspi` with the code of the revision in the
[link record](evidence/emmc-reliance/rt1060-link/README.md), `CONFIG_MAIN_STACK_SIZE=8192`, 12 Reliance Edge block
buffers of 4,096 B and 8 handles.

| Image | FLASH | RAM |
| --- | --- | --- |
| mount sequence compiled out | 68,252 B | 71,808 B |
| mount sequence compiled in | 69,584 B | 72,320 B |
| mount sequence and Configuration on the Settings File backend | 73,356 B | 72,448 B |

The third row is the Settings scenario of this record, from the Twister build report of the change that adds it, and
was not built with the stack option. Its settings file is `/MMC:/settings/cfg` and the other Settings File options are
Zephyr's defaults (`CONFIG_SETTINGS_FILE_MAX_LINES` is 32); the two images above are unchanged by it.

RAM is the 32 MB SDRAM of the board devicetree, cached and with 32-byte lines, as for the microSD composition. Most of
it is Reliance Edge's block buffers (49,300 B, which is 12 buffers of 4,096 B and their context), its volume state
(8,240 B), the 8,192 B main stack and the handle table (960 B). A larger buffer count raises RAM linearly; fewer
buffers trade RAM for more reads and writes. For comparison, the microSD images recorded in the
[microSD composition](microsd-composition.md) measured 56,724 and 57,876 B of FLASH and 9,184 and 9,824 B of RAM, with
a 4,096 B main stack, FatFs with a 4 KiB sector window and an older revision, so the two sets are indicative and not a
like-for-like comparison.

Static stack frames (`-fstack-usage`, bytes, from a separate build with `CONFIG_STACK_USAGE=y` whose FLASH and RAM equal
the figures above): the largest Reliance Edge frames are `CoreRename` 248, `CoreUnlink` 136, `CoreCreate` 128 and
`RedDirEntryRename` 120. The deepest `red_*` entry frames are `red_unlinkat` 88, `red_renameat` 72, `red_lseek` 64 and
`red_getdirpath` 64, and `red_write`, `red_read`, `red_fsync` and `red_close` are 24 each. In the flush path the
generated `mmc.c` has `mmc_ioctl` at 96 and `mmc_card_init` at 160. `mmc_flush_cache` and its locked helper have no
frame of their own in the listing, so the compiler inlined them into `mmc_ioctl`. Below them, `imx_usdhc_request` is 224 and `card_write`
144. The adapter's own entry points are at most 72. The File provider's `crux_file_impl_rename` (352) is the
largest frame of the whole image, followed by `CoreRename` (248), and `crux_file_impl_stat` and `imx_usdhc_request` (224 each). These are single frames, not call-chain
maxima, (the microSD record has `f_open` at 360 and `f_stat` at 336 for comparison), and no run measured a high-water mark, so the 8,192 B main stack is a choice that no measurement supports yet.
The method and the full figures are in the link record.

## Not covered (needs a board with an eMMC)

Everything about a device: the eMMC's initialisation, speed modes and timing; whether the flush command does what the
simulator models; the device's cache and power-loss behaviour; the DMA and D-cache behaviour of the USDHC with
alignment 32, which no board has run on FatFs or here; the bounce path's effect on speed; the pin, voltage and clock
setup of a real board; stack use at run time; wear and endurance; the commercial kit; the File service and the
credential store on this profile (a separate assessment issue); and the Settings File target provider on a device,
which has run on a RAM disk and on the simulator's replay only. The boot partition, RPMB and reliable write are out of
scope.

## Physical gate procedure

The procedure, preconditions and stop conditions are a separate document of the eMMC design (issue
[#256](https://github.com/wonny-labs/crux-pilot/issues/256)). It authorises nothing: every hardware step needs
explicit approval for the current task.
