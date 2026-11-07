# RT1060 link of the eMMC composition: size and stack frames, 7 November 2026

Software evidence for issue [#254](https://github.com/wonny-labs/crux-pilot/issues/254) of the
[eMMC design](../../../plans/emmc-reliance-design.md): the reference composition `samples/storage_emmc`, with Reliance
Edge and the flush-corrected MMC stack, linked for the RT1060-EVKB Rev B.

**This is a link, not a run.** The EVKB has no eMMC. The overlay `samples/storage_emmc/boards/mimxrt1060_evk_mimxrt1062_qspi_B.overlay`
replaces the microSD disk of `usdhc1` with a four-bit `zephyr,mmc-disk` so that the pieces link, and it describes no
board. No image was flashed, no board was touched, and nothing here shows how the composition behaves on a device,
how long anything takes, or how much stack a run uses. The images link GPLv2 Reliance Edge source: they are test
images, are not kept here, and are not distributed.

## What ran

- Revision: the Task 10 change on top of `24d9737`, built from the working tree and then committed unchanged with
  this record. The figures below were built before two comment-only edits (`emmc_storage.h`, and the test's `Kconfig`
  and `app.overlay`) and do not depend on them. Zephyr `dccb09599635bdff17633fa7e9dab014b91dce90` (v4.4.2),
  Reliance Edge `6706984258c5bb61c778fe8ca2edd04b559efb78`, Zephyr SDK 1.0.1 (`arm-zephyr-eabi`, GNU toolchain),
  board `mimxrt1060_evk@B/mimxrt1062/qspi`.
- Environment: the Dev Container image, run with Docker as `linux/amd64` under emulation on an arm64 Mac, limited to 4
  CPUs and 6 GB. No CI runner was used.
- Link check, from the repository in the container. It builds both scenarios of the sample with Twister
  (`--build-only`, which links) and runs the checks of `scripts/test-file-emmc-compile.sh`:

  ```sh
  scripts/test-file-emmc-compile.sh
  ```

  Exit status 0. The Twister run took about 190 s for the two images. Both scenarios (`crux.sample.storage_emmc` and
  `crux.sample.storage_emmc.start`) built, and the script's checks passed on each: the generated `.config` has
  `CONFIG_DISK_DRIVER_MMC=y`, `CONFIG_MMC_STACK=y`, `CONFIG_CRUX_MMC_CACHE_FLUSH=y`, `CONFIG_SDHC_BUFFER_ALIGNMENT=32`,
  and neither `CONFIG_DISK_DRIVER_SDMMC`, `CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT`, `CONFIG_FAT_FILESYSTEM_ELM` nor
  `CONFIG_CRUX_FILE_FATFS_RT1060`; `nm` of `zephyr.elf` has no `red_format` and no `f_mount`; `compile_commands.json`
  lists one `crux-mmc/mmc.c` and not `subsys/sd/mmc.c`; `mmc.json` carries the hashes that
  `scripts/prepare-mmc-cache-flush.py` pins; and two further configurations failed at CMake for the stated reason
  (`-DCONFIG_CRUX_MMC_CACHE_FLUSH=n`, and `-DCRUX_EMMC_FLUSH_CONTROL=ON`).
- Measurement builds, separate from the check, with the static stack frames on. From `/workspaces/crux`, for the base
  image and for the mount-at-boot image:

  ```sh
  west build -p always -b 'mimxrt1060_evk@B/mimxrt1062/qspi' -d crux-pilot/build/zephyr/emmc-measure/base \
      crux-pilot/samples/storage_emmc -- -DCONFIG_STACK_USAGE=y
  west build -p always -b 'mimxrt1060_evk@B/mimxrt1062/qspi' -d crux-pilot/build/zephyr/emmc-measure/start \
      crux-pilot/samples/storage_emmc -- -DCONFIG_STACK_USAGE=y -DCONFIG_SAMPLE_EMMC_START=y
  ```

  The frames are the `.su` files that GCC writes next to each object (158 files for the mount-at-boot image, 1,040
  entries), read with `find <build> -name '*.su' -exec cat {} +` and `awk` on the tab-separated fields. The build
  log's memory report of each image is equal to that of the Twister build without the stack option, so the option does not
  change the image.

## Image size

| Image | FLASH | RAM |
| --- | --- | --- |
| `crux.sample.storage_emmc` (mount sequence compiled out) | 68,252 B | 71,808 B |
| `crux.sample.storage_emmc.start` (mount sequence compiled in) | 69,584 B | 72,320 B |

Both against 8 MB of FLASH (0.81 % and 0.83 %) and 32 MB of RAM (0.21 % and 0.22 %); RAM is the SDRAM of the board
devicetree, and no on-chip region is used. `CONFIG_MAIN_STACK_SIZE` is 8,192. The mount-at-boot image adds 1,332 B of
FLASH and 512 B of RAM (the File provider's root table). The base image keeps Reliance Edge and the MMC stack through the
VFS table and the disk driver table, and it does not link the composition or the File binding.

Buffer and state memory, from the link maps (RAM objects of 256 B or more):

| Object | Size | What it is |
| --- | --- | --- |
| `gBufCtx` (`buffer.c`) | 49,300 B | 12 block buffers of 4,096 B and their context |
| `gaRedCoreVol` (`core.c`) | 8,240 B | the volume state |
| main stack (`kernel/init.c`, `.noinit`) | 8,256 B | `CONFIG_MAIN_STACK_SIZE` of 8,192 B and its guard |
| `gaHandle` (`posix.c`) | 960 B | 8 handles |
| `mmc_data_0` (`mmc_subsys.c`) | 704 B | the MMC disk's state |
| `roots` (`file_native.c`, mount-at-boot image only) | 512 B | the File provider's root table |
| `usdhc_0_data` (`imx_usdhc.c`) | 320 B | the host controller's state |

`CONFIG_CRUX_FS_RELIANCE_EDGE_BUFFER_COUNT` sets the first line. These are configuration values of the sample, not a
tuned or recommended profile.

For comparison, the [microSD composition](../../../microsd-composition.md) recorded 56,724 and 57,876 B of FLASH and
9,184 and 9,824 B of RAM, with a 4,096 B main stack, FatFs and an older revision (`9a03c5d`). The two sets differ in
revision, stack size and file system configuration, so the comparison is indicative: the Reliance Edge images are about
11.5 KB larger in FLASH, and their RAM is dominated by the block buffers.

## Static stack frames

`-fstack-usage` frames in bytes, from the mount-at-boot image. The qualifier is `static` for all of those listed. These
are single frames, not the maximum of a call chain, so they are a lower bound for any path, and no run measured a stack
high-water mark.

| Group | Frames |
| --- | --- |
| Largest Reliance Edge frames | `CoreRename` 248, `CoreUnlink` 136, `CoreCreate` 128, `RedDirEntryRename` 120, `RedCoreFileRead` 88, `RedCoreDirRead` 80 |
| `red_*` entry points, deepest first | `red_unlinkat` 88, `red_renameat` 72, `red_lseek` 64, `red_getdirpath` 64, `red_mkdirat` 48, `red_fstatat` 48, `red_openat` 40, `red_opendir` 32, `red_ftruncate` 32; `red_write`, `red_read`, `red_fsync`, `red_close`, `red_umount2`, `red_sync` 24; `red_mount2`, `red_init`, `red_readdir`, `red_closedir`, `red_statvfs` 16 |
| Adapter | `crux_red_statvfs` 72, `crux_red_stat` 56, `crux_red_open` 40; the rest 24 or less |
| Flush path (generated `mmc.c`) | `mmc_ioctl` 96, `mmc_card_init` 160. `mmc_flush_cache` and its locked helper have no frame of their own in the listing, so they were inlined into `mmc_ioctl` |
| SD layer and host driver | `imx_usdhc_request` 224, `card_write` 144, `imx_usdhc_execute_tuning` 128, `card_read` 80 |
| File provider (the largest of the image) | `crux_file_impl_rename` 352, `crux_file_impl_stat` 224, `crux_file_impl_remove` 144 |

Two `dynamic` frames exist in the whole image (`rb_insert` 48, `rb_remove` 64, both in the kernel), and none in Reliance
Edge, the adapter, the SD layer or the File provider. The microSD record has `f_open` at 360 and `f_stat` at 336 for
the FatFs side, from a different build, so those do not compare directly.

## What the link cannot show

- Anything about an eMMC device: initialisation, speed modes, the flush command's timing or effect, the device cache,
  power-loss behaviour. The EVKB has no eMMC, and the overlay does not describe a board.
- Whether the USDHC and its DMA descriptors behave with alignment 32 on silicon, and what the bounce path costs. No
  board has run that value, on FatFs or here.
- Stack use at run time, call-chain depth, or timing. The 8,192 B main stack is a choice.
- That a build with another Zephyr, another Reliance Edge revision or a commercial kit links, or has the same size.
- That the CI runs this check. It is a separate, proposed change to `.github/workflows/zephyr.yml`.

## Checks shown to fail

`CRUX_FILE_EMMC_SKIP_BUILD=1` runs the script's checks on an existing output directory. Each row changed one thing, on a
copy of the good output (the copy's `compile_commands.json` was repointed at the copy) or in a guard, ran the script, and
required a non-zero exit and the stated message. Every mutant was removed or reverted afterwards. The harness is not kept.
All 22 were caught (the three `mmc.json` hash mutants are one table row).

| Mutant | Message that the script gave |
| --- | --- |
| `.config` without `CONFIG_DISK_DRIVER_MMC=y` | `CONFIG_DISK_DRIVER_MMC=y is missing` |
| `.config` without `CONFIG_MMC_STACK=y` | `CONFIG_MMC_STACK=y is missing` |
| `.config` with the flush option off (base image) | `CONFIG_CRUX_MMC_CACHE_FLUSH=y is missing` |
| `.config` with `CONFIG_SDHC_BUFFER_ALIGNMENT=4` | `CONFIG_SDHC_BUFFER_ALIGNMENT=32 is missing` |
| `.config` without `CONFIG_CRUX_FILE_RELIANCE_EDGE=y` | `CONFIG_CRUX_FILE_RELIANCE_EDGE=y is missing` |
| `.config` with `CONFIG_DISK_DRIVER_SDMMC=y` | `CONFIG_DISK_DRIVER_SDMMC is set` |
| `.config` with the formatter on (base image) | `CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT is set` |
| `.config` with `CONFIG_FAT_FILESYSTEM_ELM=y` | `CONFIG_FAT_FILESYSTEM_ELM is set` |
| `CONFIG_SAMPLE_EMMC_START=y` in the base image | `CONFIG_SAMPLE_EMMC_START enabled=True, expected False` |
| `CONFIG_SAMPLE_EMMC_START=y` missing from the start image | `CONFIG_SAMPLE_EMMC_START enabled=False, expected True` |
| `compile_commands.json` names `subsys/sd/mmc.c` instead of the generated file | `found 0 generated and 1 original` |
| `compile_commands.json` names both | `found 1 generated and 1 original` |
| `mmc.json` with another `generated_sha256`, `patch_sha256`, `upstream_sha256` (three mutants) | `has generated_sha256 ...`, `has patch_sha256 ...`, `has upstream_sha256 ...` |
| generated `mmc.c` edited | `the generated or the pinned MMC source does not match its hash` |
| the base image's `zephyr.elf` in place of the start image's | `emmc_storage_start is not linked` |
| a real image built with the formatter (`CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT=y`), good `.config` kept | `red_format is linked` |
| a real image built with FatFs (`CONFIG_FAT_FILESYSTEM_ELM=y` and long names), good `.config` kept | `FatFs is linked` |
| CMake guard of Reliance Edge on MMC without the flush removed | `configured with -DCONFIG_CRUX_MMC_CACHE_FLUSH=n` |
| the guard's message changed | `failed for another reason` |
| the fixture-only control allowed on every board | `configured with -DCRUX_EMMC_FLUSH_CONTROL=ON` |

The first run of the harness reported three `mmc.json` mutants as missed. They had failed with exit 1, but the harness
cut the message before the key name, which was a fault in the harness. A second run with the match on the whole message
caught them, and the same run built the FatFs image, whose first build had failed for lack of long-name settings.

## CI evidence collection

`scripts/test-file-emmc-compile.sh` writes `build/zephyr/file-emmc-link`, not a `twister-*` directory, so the link-only
image's map and `compile_commands.json` do not match the CI evidence step's `build/zephyr/twister-*` collection. That
step is unchanged. `scripts/test-storage-emmc.sh` already writes `twister-*` directories for `native_sim` builds that link
the same Reliance Edge source, so the existing step would upload their `zephyr.map` and `compile_commands.json` once the
script runs in CI. It uploads no `zephyr.elf` or `zephyr.bin` from those directories: the named image paths of the upload
step do not cover them. The step's direct globs (`build/zephyr/*/compile_commands.json`, `*/CMakeCache.txt`,
`*/zephyr/.config`) also match the direct build directories of `test-storage-emmc.sh` (`emmc-restart*`,
`reliance-*`) and of `test-file-emmc-compile.sh` (`file-emmc-noflush`, `file-emmc-control`). They collect build
commands, CMake caches and `.config` files, and no map and no image.
A reviewer decides whether maps of images that link GPLv2 source should be collected.
