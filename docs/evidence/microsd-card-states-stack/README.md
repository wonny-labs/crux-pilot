# microSD card states and stack high-water mark — 3 November 2026

Physical-board evidence for three items of [#14](https://github.com/wonny-labs/crux-pilot/issues/14) acceptance criterion 2 that earlier runs left open: how the composition behaves on a **blank** card and on a **wrong-layout** card, and the **stack high-water mark**. Gate: [microsd-physical-gate.md](../../microsd-physical-gate.md). Same board as the [first card runs](../microsd-physical/README.md). **One board, one card, one run each. This does not qualify the card, the board or the stack for any other configuration.**

## Setup
- RT1060-EVKB Rev B per the owner (not independently verified), SEGGER J-Link PLUS (S/N matches `docs/baseline.md`), board powered through J1 USB only. Read by the tool before each run ([preflight](blank-preflight.log), [layout](layout-preflight.log), [stack](stack-preflight.log)): VTref 3.322 V, Cortex-M7 CPUID 0x411FC271, QSPI head `FCFB`.
- **Changed since 2 November:** the DAPLink was on hub `0-1.4` port **4** (it was port 3), and the console port had another name. The port was switched on and off with `uhubctl -l 0-1.4 -p 4`; no power cut was part of these tests, and the supply read 0.000 V after each final off.
- Card: the same 15.9 GB card, erased and re-made by the host for each state, after a full host backup (SHA-256 and details in [host-verification.txt](host-verification.txt)). The firmware never formats a card or creates a directory. The card's earlier content, including the 40 orphaned clusters of the replace test, is only in that backup.
- Images (`mimxrt1060_evk@B//qspi`, Zephyr v4.4.0, SDK 1.0.1), flashed with J-Link Commander (`h`, `loadfile`, `r`, `g`; erase, program and verify reported Done in each [log](blank-flash.log)):
  - blank and wrong layout: `samples/storage_sd` with `CONFIG_SAMPLE_SD_START=y`, ELF SHA-256 `1ca99a99b075fd7effb3b9614d75631f5ebaa0918ff59d4439aa1f9d4116c0fb`, built at commit `050e995` (the image of the 2 November first run, not a rebuild of current main). `sd_storage.c` and `sd_storage.h`, the code these two states exercise, are unchanged since that commit (`git log 050e995..origin/main` on both is empty); later changes to `samples/storage_sd` only add the file check, the recovery step and their build entries.
  - stack: `samples/storage_sd_probe` with `CONFIG_SAMPLE_SD_STACK_PROBE=y`, ELF SHA-256 `b9fb7c4fbb0be53aad20c4c9beaa381098f67c2cdaa2f6be81bb029078c6f414`, built at commit `fe4cb3d` (before the review wording fixes, which changed comments, Kconfig help and documents only).
  - restore: `samples/storage_sd` with no option set (touches no card), SHA-256 `a5b11970b0fdbd12d67c2f154db99ba0be2821d9e4198ac6249b7407e9e4b456`, flashed after every run; each reboot printed only the boot banner ([restore console](restore-console.log)).
- Console: DAPLink serial at 115200 baud, speed set on the open handle ([capture.py](../microsd-physical/capture.py)). The first bytes after a reset are garbled by the board's USB; non-printable bytes in the saved logs are shown as `?` and carriage returns are removed.

## Results
| Card state (prepared on the host) | Console | Meaning |
| --- | --- | --- |
| **Blank**: no partition table, no FAT volume | `sd_storage_start: 2` | `SD_STORAGE_MOUNT_FAILED`: the disk initialised, there was no volume to mount, nothing was formatted |
| **Wrong layout**: FAT32 (MBR), no `files`, no `settings` | `sd_storage_start: 3` | `SD_STORAGE_LAYOUT_MISSING`: the volume mounted, the missing directories were detected, none was created |
| **Provisioned**: FAT32 with `files` and `settings` | `sd_storage_start: 0` | `SD_STORAGE_READY`, then the stack probe ran |

[blank console](blank-console.log), [wrong-layout console](layout-console.log) (the line appears twice because the capture caught the boot that follows the flash as well as the reset).

After each run the card went back to the host ([host-verification.txt](host-verification.txt)): the blank card still showed no partition map and no file system; the wrong-layout card had no `files` or `settings` and `fsck_msdos -n` exited 0. Both images only mount and look at the directories.

### Stack probe ([console](stack-console.log))
The probe mounted the card, ran the file check and the FatFs workload (create, 64 KiB write, sync, read back, rename, stat, list, remove) twice, both with result 0, then printed the thread analyzer report once:

| Thread | Stack | Used (high-water) | Unused |
| --- | --- | --- | --- |
| main (4096 B, `CONFIG_MAIN_STACK_SIZE`) | 4096 B | 2264 B (55 %) | **1832 B** |
| ISR0 (interrupt stack) | 2048 B | 240 B (11 %) | 1808 B |
| idle | 320 B | 64 B (20 %) | 256 B |

The workload and file check ran in `main`, which is what the 4096 B choice has to cover. Afterwards `fsck_msdos -n` exited 0, `files` held `bootcount` (value 2, the file check ran twice in the boot) and `pattern` (identical to the earlier pattern file), and `settings` was empty (the workload's temporary files were gone).

## What this shows
- On this board and card, a blank card and a card with the wrong layout both fail closed with the documented codes, and the firmware neither formats nor creates a directory.
- The main stack of this composition used 2264 B at the deepest FatFs path the probe exercises, leaving 1832 B of the 4096 B, so the earlier "no measured margin" now has one measurement.

## What it does not show
- One run per state. The blank check is a `diskutil` layout check, not a sector-level comparison (reading the raw device needs root); the card was made blank by a `zeroDisk` that was interrupted, so bytes beyond the first sectors may be old data, and macOS touched `.fseventsd` when it auto-mounted the wrong-layout card, so the "unchanged" evidence for that card is the absent directories and the clean fsck.
- The images for the first two states are the 2 November build, not a rebuild of current main (the exercised source is unchanged, see above), and the stack image is the build before the wording fixes.
- The stack figure is for this composition with the file check and the workload only: no Settings backend, no networking, no other threads' use. ISR0 includes early boot on the same stack, so it overstates interrupt use. The analyzer listed only `idle`, `main` and `ISR0`.
- Not run: card removal while mounted (optional), the M.2 slot state and uSDHC1 ownership (needs the board and its schematic), another card, SD errors and timing, the recovery step (`sd_settings_recover`) on the board (no image calls it).

## Cleanup state
Inert restore image flashed, hub `0-1.4` port 4 off (0.000 V), card back on the host with `bootcount` and `pattern` in `files`, the full backup kept outside the repository.
