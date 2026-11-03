# microSD physical gate

Issue [#14](https://github.com/wonny-labs/crux-pilot/issues/14), acceptance criterion 5. This fixes the procedure that the physical microSD runs of 2 November 2026 followed, so that a later run starts from the same preconditions, observable outcomes and stop conditions. **This document authorises nothing.** Every hardware step needs explicit approval for the current task; a past approval does not carry over. The composition and its software evidence are in [microsd-composition.md](microsd-composition.md).

## Scope and rules

- Target: RT1060-EVKB **Rev B, QSPI** (`mimxrt1060_evk@B//qspi`). Rev C and the hyperflash variant are refused by the build.
- The card is **test-owned and may be erased or left needing a reformat**. Firmware never formats a card or creates a directory (`FS_FATFS_MOUNT_MKFS` is off). Formatting and creating the `files` and `settings` directories happen on a host with a card reader, as a separately approved step.
- Each image that writes to the card is **default-off** in its Kconfig and must be flashed only for an approved run: `samples/storage_sd` (`CONFIG_SAMPLE_SD_START`), `storage_sd_interrupt`, `storage_sd_replace` and `storage_sd_probe` (stack and removal probes).
- Physical results are recorded apart from `native_sim`, cross-build and CI results. An operator-reported setup fact is marked as such unless a tool read it.

## Preconditions (record all of them before the first flash)

1. Board revision and variant, card (size, partition scheme, file system), reader, firmware commit and image checksum.
2. Probe: SEGGER J-Link PLUS on J2, device `MIMXRT1062xxx6A`, SWD at 400 kHz. Read-only check first: the probe is listed, the target connects over SWD (CPUID, VTref).
3. Power: for scripted cuts the board is powered **only through J1 USB** (the J48 supply removed; with J48 connected the hub switch did not cut the target supply: 3.323 V stayed). Find the board's port with `uhubctl`, confirm the on-board DAPLink is the device on it, and measure the supply (VTref) with the port on and off: it must read about 3.3 V and **0.000 V**. Do not cut before that is shown.
4. Back up the first 128 KiB of QSPI flash at `0x60000000` before the first flash and record its SHA-256. Keep the file outside the repository.
5. Card prepared on the host (see "Card states"), and its host-side state recorded (`diskutil verifyVolume`, which runs `fsck_msdos -n` unmounted; exit 0 clean, 206 orphaned clusters).
6. Console: the DAPLink serial port at 115200 baud, opened with the speed set on the open handle (a speed set by `stty` is lost when the port closes). Lines that carry a checksum are accepted only when it matches.

## Steps

1. **Flash** the approved image with J-Link Commander (`loadfile`, then reset and go), record the command file and its log. A flash fails if the board's USB hub port is unplugged.
2. **Observe** the console. A normal boot prints `sd_storage_start: 0`.
3. **Cut power** only in an approved test: `uhubctl -p <port> -a off -f`, a pause, `-a on`; the serial port disappears and returns each time. Judge each cut by pairing the last acknowledgement the host saw before the cut with the next boot's own report. The console lags the writes, so an acknowledgement count is a lower bound.
4. **Verify the card on the host** after a batch: read-only `fsck_msdos -n` through `diskutil verifyVolume`, then read the files independently of the board (CRC, sequence). Record orphaned clusters and any repair. Do not repair a card without approval, and keep the unrepaired card as evidence until the results are reviewed.
5. **Restore** the board: flash the inert default image (no card access), switch the port off and leave the card seated. Record that state.

## Card states

| State | How it is prepared (host) | Expected outcome |
| --- | --- | --- |
| Provisioned | FAT32 (MBR), directories `files` and `settings` | `sd_storage_start: 0` |
| Blank (no FAT volume) | erased or never formatted | mount failure, **card unchanged**; firmware does not format |
| Wrong layout | FAT32 without one or both directories | layout missing, card unchanged, no directory created |

Required by the owner decision of 3 November 2026: the blank and wrong-layout cases. For the unchanged check, take a host-side image of the card before and after where the host allows reading the raw device, and otherwise record that limit. Card removal while mounted is optional: the policy is "reboot on swap", and the removal probe tests only the application remount path. It does not show whether the disk driver can recover, because the remount does not force a new card initialisation.

## Stop and recovery conditions

Stop and do not continue when: the supply does not read 0.000 V on a cut; the DAPLink is not on the port being switched; the card does not mount for a reason other than the state being tested; a result is damaged or missing in a way the test does not expect; the host reports corruption beyond orphaned clusters; any step needs an action that was not approved. Then: flash the inert image, switch the port off, record what happened and ask. A card that stops mounting is a result, not something to repair silently.

## What every record must contain

Tested revision and image checksum, probe and board facts with their source (read by a tool or reported by the operator), commands and their logs, the approval for the task, raw results including failures and unobserved boots, host verification output, what the run shows and does not show, and the cleanup state. Put it under `docs/evidence/` with the raw files, and have it independently reviewed. Do not close #14, #15 or #16 from a record alone.

## Prior runs (2 November 2026)

[First card runs](evidence/microsd-physical/README.md), [append power cuts](evidence/microsd-power-cut/README.md), [replace-pattern power cuts](evidence/microsd-replace-cut/README.md). The setup facts above come from them. Still not shown on hardware: blank and wrong-layout cards, the stack high-water mark, the M.2 slot state and uSDHC1 ownership, card removal, SD errors and timing, another card, and the Settings File backend itself.
