# microSD replace-pattern power-cut test — 2 November 2026

Physical-board evidence for how a power cut interacts with the compaction sequence of the Zephyr Settings File backend on FatFs ([composition](../../microsd-composition.md), issues [#14](https://github.com/wonny-labs/crux-pilot/issues/14), [#15](https://github.com/wonny-labs/crux-pilot/issues/15), [#16](https://github.com/wonny-labs/crux-pilot/issues/16)). Same board and card, and the same setup, as the [append power-cut test](../microsd-power-cut/README.md). **One board, one card, 100 judged power cuts of a tight loop that does nothing but replace the file. The measured rates are properties of that loop, not of a product.**

## What the test does
The image (`samples/storage_sd_replace`, `CONFIG_SAMPLE_SD_REPLACE_TEST=y`) repeats the Settings File backend's compaction with the same `fs_*` calls: write generation g into `cfg.cmp` (eight 32-byte CRC-checked records), `fs_sync`, `fs_close`, then `fs_rename(cfg.cmp, cfg)`; FatFs's rename (`subsys/fs/fat_fs.c`) first stats the destination, removes it, then renames. After the rename returned it prints `GEN g`. Each boot advances a counter and classifies both files left by the previous run: `cfg` valid (complete, one generation), `bad`, or `none`, and the same for `cfg.cmp`. The host script ([`replace_capture.py`](../../../samples/storage_sd_replace/host/replace_capture.py), tests in the same directory) pairs the last printed generation with that classification. BOOT, CLASS and GEN lines carry a checksum, as in the append test.

| Outcome | Meaning |
| --- | --- |
| `ok` | `cfg` is valid at the last printed generation or later (the console lags the writes) |
| `stale` | `cfg` is valid but older than a generation the board had printed |
| `missing_cmp_valid` | `cfg` is gone and a complete `cfg.cmp` exists: the cut fell between FatFs removing the destination and finishing the rename |
| `missing_cmp_bad`, `missing_no_cmp` | `cfg` is gone and the temporary is incomplete or absent |
| `cfg_bad` | `cfg` exists but is incomplete or damaged |

Image: ELF SHA-256 `f571935871e75fd4d505d45aa6bc2ca9a05001005d09dab783c0dda38da0c281` (`mimxrt1060_evk@B//qspi`, FLASH 59,628 B, Zephyr v4.4.0, SDK 1.0.1), built from the working tree at the author's local commit `cce440a` (the sources are those of this PR), flashed with J-Link Commander ([command file](flash-replace-image.jlink), [log](flash-replace-image.log)). Cuts: `uhubctl -l 0-1.4 -p 3 -a off -f` / `-a on` on the board's hub port, 0.5-4 s after the report, 3 s off ([run-replace-test.sh](run-replace-test.sh)); the supply behaviour of this method is in the append test's README.

## Results (100 cuts, all judged)
| Outcome | Cuts |
| --- | --- |
| `ok` | 71 |
| `missing_cmp_valid` | **29** |
| `stale` | 0 |
| `cfg_bad` | 0 |
| `missing_cmp_bad`, `missing_no_cmp` | 0 |

- Every boot reported `sd_storage_start: 0`: the card always mounted. No report was missing or unobserved.
- In the 29 `missing_cmp_valid` cuts the temporary file always held a **complete newer generation**: its generation was 1 to 9 (median 8) ahead of the last printed one, which matches the console lag. The new data was on the card and only the rename had not finished.
- Two cuts (cycles 7 and 66) were judged against a previous run that had printed no `GEN` line to the host (`last_gen` 0), so `stale` could not be detected there; the other 98 cuts (70 `ok`, 28 `missing_cmp_valid`) could. In the `ok` cuts `cfg` was 0 to 8 generations ahead of the last printed one (median 7).
- At most 4 `missing_cmp_valid` cuts came in a row. The 29/100 rate has a 95% interval (Wilson) of about 21% to 39%.
- Per-cycle data: [replace-results.json](replace-results.json); trimmed console (each BOOT and CLASS once per boot, first and last three GEN lines of each boot): [console-trimmed.log](console-trimmed.log). The trimmed file is not in firmware order.

## The card afterwards
[host-verification.txt](host-verification.txt): a read-only `fsck_msdos -n` reported **40 orphaned clusters** (exit code 206; about 320 KiB at 8 KiB per cluster). The previous check, after the 33-cut append test, was clean (exit 0). No cross-linked or damaged structure was reported, and the card mounted and read normally. `cfg` was valid (generation 19250) and `cfg.cmp` was empty (0 bytes), as the last power-off hit the creation of the temporary file. The card was not repaired. The cause of the orphans was not isolated: they appeared over roughly 104 interruptions (the 100 judged cuts, the closing restart, the J-Link halt used to flash, the final power-off and two boots nobody observed), and a cut between a FAT update and the matching directory update is the usual way to leak a cluster chain.

## What this shows
- The window described in the Settings File review is real on this card: with continuous replaces, 29% of the cuts left no settings file but a complete newer temporary. Within this test no cut left a damaged settings file, a stale one, or an incomplete temporary with no settings file, and the volume always mounted.
- That outcome (settings file missing, defaults on the next start) is the one the owner accepted for settings (issue #16). Here the lost data is recoverable: the complete `cfg.cmp` could be renamed into place at initialisation. The backend's own source has an unimplemented note for that (`settings_file.c`: look for `.cmp` when the actual file does not exist). No such recovery was built or tested.
- FatFs does not clean up after cuts: orphaned clusters accumulated and a host-side check reports them. They leak free space slowly and were not otherwise harmful here.

## What it does not show
- **Not a product rate.** A real controller compacts rarely. The probability that a power loss falls in the window is the share of time spent there, here about a quarter because the loop does nothing else. The 29% must not be read as the risk of a deployment.
- The Settings File backend itself and Crux Configuration were not run: this is the same filesystem call sequence, not the same code. Torn appends inside the file and the backend's record format are covered only by the earlier simulator work.
- One card, one file size (256 bytes, one cluster), 100 cuts, the hub's power-off ramp, no wear, no temperature or supply effects, no card removal.
- Whether the orphaned clusters come from the rename sequence, the counter or temporary-file creation was not determined.
- The console lag makes `stale` detection a lower bound; two cuts could not be checked for it.

## Unobserved boots
The counter on the card reads 103: boot 102 was the restart after the last judged cut (stopped by switching the hub port off), and boot 103 is a later boot nothing in the scripts or logs explains (the board was probably powered again while the card was moved). It is not part of the 100 judged cuts.
