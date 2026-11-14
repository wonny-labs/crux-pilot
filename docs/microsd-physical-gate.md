# microSD physical gate

Issue [#14](https://github.com/wonny-labs/crux-pilot/issues/14), acceptance criterion 5. This fixes the procedure that the physical microSD runs of 2 November 2026 followed, so that a later run starts from the same preconditions, observable outcomes and stop conditions. **This document authorises nothing.** Every hardware step needs explicit approval for the current task; a past approval does not carry over. The composition and its software evidence are in [microsd-composition.md](microsd-composition.md).

## Scope and rules

- Target: RT1060-EVKB **Rev B, QSPI** (`mimxrt1060_evk@B//qspi`). Rev C and the hyperflash variant are refused by the build.
- The card is **test-owned and may be erased or left needing a reformat**. Firmware never formats a card or creates a directory (`FS_FATFS_MOUNT_MKFS` is off). Formatting and creating the `files` and `settings` directories happen on a host with a card reader, as a separately approved step.
- Images that touch the card are **default-off** in their Kconfig and are flashed only for an approved run. `samples/storage_sd`: `CONFIG_SAMPLE_SD_START` (mounts the card, writes nothing itself) and `CONFIG_SAMPLE_SD_FILE_CHECK` (boot counter and a 4 KiB pattern file). `samples/storage_sd_interrupt`: `CONFIG_SAMPLE_SD_INTERRUPT_TEST`. `samples/storage_sd_replace`: `CONFIG_SAMPLE_SD_REPLACE_TEST`. `samples/storage_sd_probe`: the choice `SAMPLE_SD_PROBE_MODE` with `CONFIG_SAMPLE_SD_STACK_PROBE` or `CONFIG_SAMPLE_SD_REMOVAL_PROBE` (default `SAMPLE_SD_PROBE_NONE`, which touches no card).
- The **restore image** is `samples/storage_sd` built with none of those options set: it touches no card. The inert image used on 2 November had ELF SHA-256 `a5b11970b0fdbd12d67c2f154db99ba0be2821d9e4198ac6249b7407e9e4b456`. Its flash on 2 November has no retained log; the three restore flashes of 3 November (same image) do ([evidence](evidence/microsd-card-states-stack/README.md)), so the restore step is shown by those logs and not by the 2 November records.
- Physical results are recorded apart from `native_sim`, cross-build and CI results. An operator-reported setup fact is marked as such unless a tool read it.

## Preconditions (record all of them before the first flash)

1. Board state: revision and variant (operator-reported: the build guard checks the build target, not the board that is fitted), the M.2 slot state, and uSDHC1, card-detect and power GPIO ownership against the schematic. Card (size, partition scheme, file system), reader, firmware commit and image checksum.
2. Probe: SEGGER J-Link PLUS on J2, device `MIMXRT1062xxx6A`, SWD at 400 kHz. Read-only check first: the probe is listed, the target connects over SWD (CPUID, VTref).
3. Power: for scripted cuts the board is powered **only through J1 USB** (the J48 supply removed; with J48 connected the hub switch did not cut the target supply: 3.323 V stayed). Find the board's hub and port with `uhubctl` (on 2 November: hub `0-1.4`, port 3) and confirm the on-board DAPLink is the device on it; always name the hub (`-l <hub>`), because a bare port number can switch that port on every smart hub attached. Then measure the supply (VTref) with the port on and off: it must read about 3.3 V and **0.000 V**. Do not cut before that is shown.
4. Back up the first 128 KiB of QSPI flash at `0x60000000` before the first flash (`savebin <file> 0x60000000 0x20000`) and record its SHA-256. Keep the file outside the repository. **Right after the backup, and before the first flash, check that 128 KiB covers the firmware the chip holds** (the chip starts with the FlexSPI configuration block, `FCFB`; the image size comes from the boot data). If it does not, stop and ask: the existing firmware would be lost for good. The existing firmware is **replaced** by the images flashed here, not preserved in place. The backup is a record of what was on the chip, not a tested restore: to put it back use `loadbin <file> 0x60000000` and verify. No restore from the backup was done on 2 November.
5. Card prepared on the host (see "Card states"), and its host-side state recorded (`diskutil verifyVolume`, which runs `fsck_msdos -n` unmounted; exit 0 clean, 206 orphaned clusters).
6. Console: the DAPLink serial port at 115200 baud, opened with the speed set on the open handle (as `capture.py` in `docs/evidence/microsd-physical` does; two captures at the wrong speed were unreadable). Lines that carry a checksum are accepted only when it matches.

## Steps

1. **Flash** the approved image with J-Link Commander (`h`, `loadfile`, `r`, `g`), record the command file and its log, and confirm erase, program and verify report Done. A flash cannot run while the board's hub port is switched off or unplugged.
2. **Observe** the console. A normal boot prints `sd_storage_start: 0`.
3. **Cut power** only in an approved test: `uhubctl -l <hub> -p <port> -a off -f`, a pause, `-a on` (on 2 November: `-l 0-1.4 -p 3`); the serial port disappears and returns each time. Judge each cut by pairing the last acknowledgement the host saw before the cut with the next boot's own report. The console lags the writes, so an acknowledgement count is a lower bound.
4. **Restore before the card is handled**: flash the restore image (no card access), switch the port off, and only then take the card out. Boots 44 (append test) and 103 (replace test) were unobserved boots of a card-writing image, probably when the board was powered again while the card was being moved (the evidence READMEs say so with that hedge); this order prevents that. Record the flash log and the resulting state.
5. **Verify the card on the host** after a batch: read-only `fsck_msdos -n` through `diskutil verifyVolume`, then read the files independently of the board (CRC, sequence). Record orphaned clusters and any repair. Do not repair a card without approval, and keep the unrepaired card as evidence until the results are reviewed. Put the card back in the board and leave the port off.

## Card states

| State | How it is prepared (host) | Expected outcome |
| --- | --- | --- |
| Provisioned | FAT32 (MBR), directories `files` and `settings` | `sd_storage_start: 0` (`SD_STORAGE_READY`) |
| Blank (no FAT volume) | erased or never formatted | `2` (`SD_STORAGE_MOUNT_FAILED`), **card unchanged**; firmware does not format. `1` (`SD_STORAGE_NO_MEDIA`) means the disk did not initialise at all, which is a different finding |
| Wrong layout | FAT32 without one or both directories | `3` (`SD_STORAGE_LAYOUT_MISSING`), card unchanged, no directory created |

Required by the owner decision of 3 November 2026: the blank and wrong-layout cases. For the unchanged check, take a host-side image of the card before and after where the host allows reading the raw device, and otherwise record that limit. Card removal while mounted is optional: the policy is "reboot on swap", and the removal probe tests only the application remount path. It does not show whether the disk driver can recover, because the remount does not force a new card initialisation.

## Stop and recovery conditions

Stop and do not continue when: the J-Link cannot connect, CPUID or VTref differ from the recorded values, or a flash does not report Done and verify; the backup does not cover the firmware on the chip; the supply does not read 0.000 V on a cut; the DAPLink is not on the port being switched; the card does not mount for a reason other than the state being tested; a result is damaged or missing in a way the test does not expect; the host reports corruption beyond orphaned clusters; any step needs an action that was not approved. Then: flash the restore image, switch the port off, record what happened and ask. If that flash itself fails, leave the port off, do not move the card, and ask. A card that stops mounting is a result, not something to repair silently.

## What every record must contain

Tested revision and image checksum, probe and board facts with their source (read by a tool or reported by the operator), commands and their logs, the approval for the task, raw results including failures and unobserved boots, host verification output, what the run shows and does not show, and the cleanup state. Put it under `docs/evidence/` with the raw files, and have it independently reviewed. Do not close #14, #15 or #16 from a record alone.

## Prior runs (2 and 3 November 2026)

[First card runs](evidence/microsd-physical/README.md), [append power cuts](evidence/microsd-power-cut/README.md), [replace-pattern power cuts](evidence/microsd-replace-cut/README.md), and on 3 November [blank and wrong-layout cards, the stack probe and card removal](evidence/microsd-card-states-stack/README.md) (on that day the DAPLink was on hub port 4, so confirm the port again each time). The setup facts above come from them. Precondition 1 (uSDHC1, card-detect and power GPIO ownership) is only partly met by the prior runs: the uSDHC1 pad ownership has a document and photo check recorded afterwards, on 3 November 2026 at 16:33 UTC, as a [comment on issue #14](https://github.com/wonny-labs/crux-pilot/issues/14#issuecomment-6609663276) by a Claude session from the owner's account (limits in [microsd-composition.md](microsd-composition.md#usdhc1-ownership-check-document-and-photo-only)); card-detect and `SD_PWREN` have no such check. Still not shown on hardware: uSDHC1 ownership (document check only, nothing measured), the M.2 slot (reported empty by the operator, the photo agrees, not otherwise verified; a fitted module was not tested), card-detect and power GPIO ownership, SD errors and timing, another card, and the Settings File backend itself.
