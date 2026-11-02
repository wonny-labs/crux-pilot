# microSD physical gate — first card runs, 2 November 2026

Physical-board evidence for the RT1060-EVKB microSD composition ([composition](../../microsd-composition.md), issues [#14](https://github.com/wonny-labs/crux-pilot/issues/14) and [#15](https://github.com/wonny-labs/crux-pilot/issues/15)). It is one board, one card and one session. **It is not power-loss, removal, endurance or timing evidence, and it does not qualify the card or the board.**

## Setup
- Board: MIMXRT1060-EVKB Rev B (owner-confirmed; the guard in the build also requires Rev B QSPI), QSPI, SEGGER J-Link PLUS (serial 602003106) on J2, SWD 400 kHz, device `MIMXRT1062xxx6A`. Target read over SWD: Cortex-M7 r1p1, CPUID 0x411FC271, VTref 3.32 V. Console: the on-board DAPLink virtual COM port at 115200 baud.
- Card: 15.9 GB, MBR, one FAT32 partition, formatted by the owner. The `files` and `settings` directories were created on the Mac before the run (the firmware never creates them).
- Owner approval: J-Link check, flash and the following steps for this task (session chat, 2 November 2026); the write/read/restart image and the host inspection were approved in the same chat.

## Run 1 — mount only (image from commit 050e995, branch rt1060-revision-guard)
ELF SHA-256 `1ca99a99b075fd7effb3b9614d75631f5ebaa0918ff59d4439aa1f9d4116c0fb`, `CONFIG_SAMPLE_SD_START=y`. Before flashing, the first 128 KiB of QSPI was backed up (SHA-256 `449c869bee730fe60ab368f4fbc4f2034ba6d060eefd216eaeffe9efbf813fc2`, kept outside the repository). Console after the J-Link reset ([run1-console.log](run1-console.log)): `sd_storage_start: 0` (READY). Two earlier captures were unreadable because the port was opened at 9600 baud; that was a capture error, not a firmware fault.

## Run 2 — write/read/restart (image from commit 1c70658)
ELF SHA-256 `5d16c62f051ba28bc9d4a7a9c3d1159408c82104c8338b3c5ad6b8a15e6efbd4`, `CONFIG_SAMPLE_SD_START=y`, `CONFIG_SAMPLE_SD_FILE_CHECK=y`, FLASH 60,316 B. Flash: erase, program and verify reported Done ([log](run2-flash-jlink.log)). The image advances a `bootcount` file and verifies a fixed 4096-byte `pattern` file through `crux_file_*`, with sync and close.

| Boot | Console ([run2-console.log](run2-console.log)) |
| --- | --- |
| after flash | `sd_storage_start: 0`, `sd_file_check: status=0 boot=1 pattern_created=1` |
| after J-Link reset A | `sd_storage_start: 0`, `sd_file_check: status=0 boot=2 pattern_created=0` |
| after J-Link reset B | `sd_storage_start: 0`, `sd_file_check: status=0 boot=3 pattern_created=0` |

Host inspection of the card afterwards ([host-verification.txt](host-verification.txt)): `fsck_msdos -n` clean, `bootcount` = `3`, `pattern` identical to the expected 4096 bytes, `settings` empty.

## What this shows
- On this board and card, the composition mounts without formatting and the layout check and File binding succeed at boot.
- Files written through the File API with sync were present after two normal restarts (CPU reset through the debug probe) and on the host afterwards, with a consistent FAT32 volume.

## What it does not show
- Power loss, a power cycle of the card, interrupted writes, card removal while mounted, behaviour with a blank or wrong-layout card, SD timing or error handling, endurance, other cards.
- The J-Link reset restarts the CPU but does not remove power from the card; the card's own state across a real power cycle is untested.
- Stack high-water mark, cache and DMA placement, and whether the M.2 Wi-Fi path shares uSDHC1 on this board (not checked; whether an M.2 module is fitted was not established).
- Run 1 used an image from the open pull request for the Rev B guard; Run 2 used the image at the commit above.

Files: [preflight](preflight-jlink.log), [capture.py](capture.py) (opens the serial port at a fixed rate and keeps it open), [J-Link command files](run2-flash.jlink) with local paths replaced by placeholders.
