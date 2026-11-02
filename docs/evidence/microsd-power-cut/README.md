# microSD power-cut test — 2 November 2026

Physical-board evidence for the microSD composition ([composition](../../microsd-composition.md), issues [#14](https://github.com/wonny-labs/crux-pilot/issues/14), [#15](https://github.com/wonny-labs/crux-pilot/issues/15), [#16](https://github.com/wonny-labs/crux-pilot/issues/16)). Same board and card as the [first card runs](../microsd-physical/README.md). **One board, one card, 33 judged power cuts of continuous small synced writes. It does not qualify the card, the board or a product's power-loss behaviour.**

## Setup
- RT1060-EVKB Rev B per the owner (not independently verified by the agent), SEGGER J-Link PLUS (flashing and a supply-voltage reading only), one 15.9 GB FAT32 (MBR) microSD card.
- Power: after the owner removed the J48 supply, the board is powered only through J1 USB (the on-board DAPLink). The board's port on a VIA Labs hub (hub `0-1.4`, port 3) is switched with `uhubctl` 2.6.0 from macOS. The console is the DAPLink serial port, 115200 baud; the port vanishes and returns with each cut.
- Does the hub really cut power? With J48 still connected, switching the port off left the target supply at 3.323 V (the board stayed powered); with only J1, the J-Link-read target supply went from 3.322 V to **0.000 V** on `off` and back to 3.322 V on `on`, with the DAPLink device disappearing and returning under the same port name. J-Link remained attached to SWD at 0.000 V, so no back-feeding through the debug lines was seen.
- Image: `samples/storage_sd_interrupt` with `CONFIG_SAMPLE_SD_INTERRUPT_TEST=y`, built for `mimxrt1060_evk@B//qspi`, ELF SHA-256 `ce8037da32e44bdac87c7ff1cb69eb389b841d27508566ceed7593c9273454c7`, FLASH 60,708 B, Zephyr v4.4.0 (`684c9e8f`), SDK 1.0.1. It was built from the working tree at the author's local commit `da65388`; later local commits changed only the host script. Flashed with J-Link Commander ([command file](flash-interrupt-image.jlink), [log](flash-interrupt-image.log)).

## What the image and the host script do
Each boot advances a counter file (`intcount`), recovers the previous boot's run file `run<boot-1>` (last valid record, bytes after it, whether a full-size record is damaged), then appends fixed 32-byte records (magic, sequence, payload, CRC-32) to `run<boot>`, calling `sync` after every record and printing `ACK <seq>` only after `sync` returned. `BOOT`, `RECOVER` and `ACK` lines end with a one-byte checksum, and BOOT and RECOVER repeat every 16 records, because the console shares the USB that powers the board. The host script pairs the last ACK seen before each cut with the next boot's recovery report ([`interrupt_capture.py`](../../../samples/storage_sd_interrupt/host/interrupt_capture.py), tests in the same directory). Outcomes are recorded, not asserted. Driver scripts: [run-power-test.sh](run-power-test.sh), [run-phase2.sh](run-phase2.sh).

## Results
| Phase | Cuts | Run length before the cut | Records recovered | Acknowledged records lost | Torn tails | Damaged records | Mount failures |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 30 | 2-6 s after the report (400-1,610 records), except cycle 1: not timed, 6,013 records, the board had been running before the capture attached | 33,770 | 0 | 0 | 0 | 0 |
| 2 | 3 | cycles 2-3: 60-90 s after the report; cycle 1: not timed, the run began at phase 1's last restart and the capture attached about 66 s later (14,853 records) | 50,763 | 0 | 0 | 0 | 0 |

Per-cycle data: [p1-results.json](p1-results.json), [p2-results.json](p2-results.json); trimmed console logs (every BOOT and RECOVER once per boot, first and last three ACK lines of each boot; the trimming moved the repeated reports, so these files are not in firmware order and the `*** Booting` and `sd_storage_start` lines are not next to their BOOT line): [p1](p1-console-trimmed.log), [p2](p2-console-trimmed.log). The recovered records exceed the last ACK seen by a median of 29 (phase 1) and 25 (phase 2), at most 31: the console output lags the writes by about 30 lines, so the ACK count is a lower bound of what the board acknowledged and "acknowledged records lost" is a **minimum**, 0 here.

Card check afterwards ([host-verification.txt](host-verification.txt); the run-file lines are reproducible with [verify_card.py](verify_card.py), the other lines were checked by hand): `fsck_msdos -n` clean (exit 0); 44 run files, each scanned with its CRC-32 and sequence checks independently of the board; the 33 board-side recovery reports match the card exactly; no run has bytes after its last valid record; every run ends on a valid record, including run 43 (stopped by switching the hub port off after phase 2) and run 44 (77,548 valid records); the counter reads 44, matching the 44 boots. Boots 39, 43 and 44 were not observed by the script: boot 39 is the restart after phase 1, boot 43 the restart after phase 2's last cycle (stopped by hand through the hub), and boot 44 a later boot that nothing in the scripts or logs explains. The board was powered again after that hub power-off, probably while the card was being moved, ran for a few minutes writing, and was stopped by an unobserved removal of power or of the card. Its run is intact and its stop was another interruption nobody timed, but it is not part of the 33 judged cuts.

## Superseded first attempt
A five-cycle shake-down ran with an image whose console lines had no checksum. One ACK line was garbled while power collapsed and read as `ACK 73166` for a run whose last real ACK was about 7,675, which made the script report 65,276 lost records (the raw shake-down console log was not retained; the card then held 7,890 valid records for that run, and the last real ACK seen before the garbled line was about 7,675, a figure from the live console, not re-checkable from the retained files). That was a console artefact, not a card result. Lines now carry a checksum and the host drops any line with a wrong or missing one ([superseded-shakedown-results.json](superseded-shakedown-results.json), kept for the record). A second attempt to start phase 2 failed because the port had not returned yet; the script now waits for it.

## What this shows
On this card, 33 power cuts during continuous per-record syncs, including runs of 15,000 to 21,000 records, left no acknowledged record missing, no partial record, no damaged record and a volume that always mounted and passed a read-only check. For small synced appends this is better than the owner's tolerance for settings (the latest save may be lost): not even the latest acknowledged record was. Settings saves also involve compaction and renames, which were not cut.

## What it does not show
- Any other card, a card under wear, other cut timing statistics (the cuts landed between random run lengths, not at chosen points of a FAT update), or temperature and supply droop effects.
- Larger writes, directory changes, file creation or renames, or compaction during a cut. The writes here are 32-byte appends to one file with a sync each.
- Removal of the card while mounted.
- How the hub's power-off ramps compare with a product's supply collapse.
- Unacknowledged console data: the ACK count is a lower bound (see above).

`bootcount` reads 6 on the card because the earlier check image rebooted three more times when the board was power-cycled during the hub tests.
