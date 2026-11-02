# microSD physical gate — first card runs, 2 November 2026

Physical-board evidence for the RT1060-EVKB microSD composition ([composition](../../microsd-composition.md), issues [#14](https://github.com/wonny-labs/crux-pilot/issues/14) and [#15](https://github.com/wonny-labs/crux-pilot/issues/15)). It is one board, one card and one session. **It is not power-loss, removal, endurance or timing evidence, and it does not qualify the card or the board.**

## Setup
- Board: MIMXRT1060-EVKB Rev B per the owner (not independently verified by the agent; a build option cannot identify the fitted board), QSPI, SEGGER J-Link PLUS (serial 602003106) on J2, SWD 400 kHz (the `-speed 400` argument below), device `MIMXRT1062xxx6A`. Both images were built for `mimxrt1060_evk@B//qspi`; only Run 1's image contains the Rev B build guard of the open pull request #160. Target read over SWD: Cortex-M7 r1p1, CPUID 0x411FC271, VTref 3.32 V. Console: the on-board DAPLink virtual COM port at 115200 baud.
- Card: 15.9 GB, MBR, one FAT32 partition, formatted by the owner. The `files` and `settings` directories were created on the Mac before the run (the firmware never creates them).
- Owner approval: J-Link check, flash and the following steps for this task, and then the write/read/restart image and the host inspection (session chat, 2 November 2026; the chat does not carry a retained clock time).

## Commands

- Toolchain: Zephyr v4.4.0 (`684c9e8f`), Zephyr SDK 1.0.1, `ZEPHYR_TOOLCHAIN_VARIANT=zephyr`, in the Linux Dev Container.
- Run 1 image: `west build -p always -b "mimxrt1060_evk@B//qspi" crux-pilot/samples/storage_sd -d <build dir> -- -DCONFIG_SAMPLE_SD_START=y` (commit 050e995).
- Run 2 image: the same with `-DCONFIG_SAMPLE_SD_START=y -DCONFIG_SAMPLE_SD_FILE_CHECK=y` (commit 1c70658; the ELF was built from the working tree twelve seconds before the commit, and `samples/` is identical at the later head). The reported FLASH size is Zephyr's memory-report figure.
- J-Link, every step: `JLinkExe -device MIMXRT1062xxx6A -if SWD -speed 400 -autoconnect 1 -NoGui 1 -ExitOnError 1 -CommanderScript <file>` with the command files in this directory (`preflight.jlink`, `run1-flash.jlink`, `run2-flash.jlink`, `reset.jlink`).
- Console: `python3 capture.py <port> 115200 <seconds> <file>` on the DAPLink virtual COM port.

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

## Notes on the files

- Local paths in the J-Link logs and command files are replaced by `<local path>`; the logs are otherwise as produced. `host-verification.txt` is a hand-formatted transcript of the host commands, not raw tool output.
- Logs: [preflight](preflight-jlink.log), [run 1 flash](run1-flash-jlink.log) and [reset](run1-reset-jlink.log), [run 2 flash](run2-flash-jlink.log) and resets [A](run2-reset-a-jlink.log) and [B](run2-reset-b-jlink.log). Two unreadable early console captures (9600 baud) were not kept.
- The check reports a missing pattern file by creating it (`pattern_created=1`); on any boot after the first, `pattern_created=1` with `boot>1` means the file was lost. If the counter file cannot be rewritten the check stops with a counter error and later boots stop at an invalid counter (fail closed). Two cases (an extra trailing byte and a correct-but-short prefix) are covered by the native_sim suite.
- [capture.py](capture.py) opens the serial port at a fixed rate and keeps it open.
