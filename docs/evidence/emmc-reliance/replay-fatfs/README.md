# Power-cut replay on FatFs — 5 November 2026

Software evidence for issue 4 of the [eMMC design](../../../plans/emmc-reliance-design.md#power-cut-replay):
power-cut replay on the eMMC simulator, run on FatFs. **Every result here is an observation of the
simulator's model on `native_sim`. None of it is evidence about power loss on an eMMC device.** The
simulator is a model written from the needs of the Zephyr MMC stack, by the same author as the tests. A
rebuilt image shows what the model allows at a cut, and it leaves out damage outside the write being made.
FatFs does not claim the invariants, so nothing about FatFs is asserted: only the two controls must fail.

## What ran

- Revision: the commit that adds this record, on top of `d0c8ebe`. Zephyr `dccb09599635bdff17633fa7e9dab014b91dce90`,
  FatFs module `f4ead3bf4a6dab3a07d7b5f5315795c073db568d`, `native_sim/native/64`, Python 3.12.3.
- Environment: the Dev Container image, run with Docker as `linux/amd64` under emulation on an arm64 Mac,
  limited to 4 CPUs and 6 GB. No CI runner was used, so the times below are emulated times.
- Images, built by `scripts/test-storage-emmc.sh` under `build/zephyr/emmc-powercut/`:
  - `fat-fix`: `-DEXTRA_CONF_FILE=fatfs.conf -DCONFIG_CRUX_MMC_CACHE_FLUSH=y`.
  - `fat-nofix`: the same with `-DCONFIG_CRUX_MMC_CACHE_FLUSH=n`, so the unmodified stack.
  - `CRUX_EMMC_FLUSH_CONTROL` does not exist yet. The design uses it for `red-nofix` only, and the
    adapter guard that it bypasses does not cover FatFs.
- Commands, from the repository in the container:

  ```sh
  scripts/test-storage-emmc.sh
  # which runs, after the existing suites:
  python3 tests/native/emmc_powercut/test_replay_model.py -v
  python3 tests/native/emmc_powercut/run_replay.py \
      --exe-fix build/zephyr/emmc-powercut/fat-fix/zephyr/zephyr.exe \
      --exe-nofix build/zephyr/emmc-powercut/fat-nofix/zephyr/zephyr.exe \
      --fs fatfs --seed 1 --samples 64 --max-seconds 300 --out build/zephyr/emmc-powercut/replay
  ```

- The baseline is made once by `fat-fix` on the no-cache profile. Every trace and control starts from it.
- Each workload runs twice with `fat-fix`, on the cached profile (the simulator's default of 64 KiB)
  and on `--emmc-cache-kb=0`. The verifier runs without a cache, so it reads each rebuilt image as it is.
- [`fatfs.json`](fatfs.json) is the run's output: counts, seeds, positions, every observed failure and
  both controls. No medium image is kept here.

## Workloads

| Workload | Steps | What a step does, each followed by a mark |
| --- | --- | --- |
| W1 | 16 | append a 200-byte record to `files/w1`, `fs_sync` |
| W2 | 8 | write `settings/w2.tmp` (generation, length, CRC-32), `fs_sync`, close, `fs_rename` onto `settings/w2` |
| W3 | 12 | over five files and a directory: create, overwrite, remove, `mkdir`, `rmdir`; a file is synced and closed |

The VFS has no directory sync. FatFs syncs the volume inside `f_unlink`, `f_mkdir` and `f_rename`, so
a directory step is its own sync. `files/keep` is written at provision and never again.

| Trace (`fat-fix`) | Events | Sector writes | Flushes | Marks |
| --- | --- | --- | --- | --- |
| W1 cached / no cache | 74 / 56 | 40 / 40 | 17 / 0 | 16 / 16 |
| W2 cached / no cache | 106 / 80 | 72 / 72 | 25 / 0 | 8 / 8 |
| W3 cached / no cache | 70 / 56 | 44 / 44 | 13 / 0 | 12 / 12 |

## Controls

| Control | Image and model | Required | Result |
| --- | --- | --- | --- |
| Flush | `fat-nofix`, cached, `lost`, W1, at the position after each of the 16 marks | `mount=1` and `data=0` | **Failed as required** at all 16 positions. The trace holds no flush. |
| Damaged image | `fat-fix`, the baseline with sector 0 (the FAT boot sector) overwritten with `0xa5` | `mount=0` | **Failed as required**: `mount=0`. |

With `fat-fix`, `lost` passed in all 66 cached W1 cases, which include both sides of every flush,
because its sync flushes.
Before the checks existed, a verifier whose W1 check always passed made the flush control report
"DID NOT FAIL", and the driver exited 1. That run was the red step of the test-first work, and it shows
that the control can fail.

## Observations

1240 cases ran in 410 verifier processes, in 23.5 to 24.6 s emulated: two direct runs and one through
the script. A rebuilt image that was already verified for the same workload and acknowledged count is
not verified again. The count includes 12 checks that the verifier passes on the baseline and on each
complete run, and 17 control cases. The same seed gave the same JSON apart from timing, in all three runs
compared field by field. Earlier runs with `--samples 8`, before a position drawn twice was counted
once, took 12.0 to 12.5 s and showed the same kinds of failure.

| Workload | Profile | Model | Cases | Passed | Failed |
| --- | --- | --- | --- | --- | --- |
| W1 | cached | lost | 66 | 66 | — |
| W1 | cached | prefix | 68 | 68 | — |
| W1 | cached | reordered | 68 | 65 | I2 3 |
| W1 | cached | torn | 64 | 59 | I2 5, read error 1 |
| W1 | no cache | prefix | 57 | 57 | — |
| W1 | no cache | torn | 57 | 56 | I2 1, read error 1 |
| W2 | cached | lost | 89 | 73 | I2 and I3 16 |
| W2 | cached | prefix | 88 | 66 | I2 and I3 22 |
| W2 | cached | reordered | 85 | 69 | I2 and I3 16 |
| W2 | cached | torn | 64 | 44 | I2 and I3 20 |
| W2 | no cache | prefix | 64 | 52 | I2 and I3 12 |
| W2 | no cache | torn | 64 | 52 | I2 and I3 12 |
| W3 | cached | lost | 66 | 66 | — |
| W3 | cached | prefix | 65 | 53 | I2 12 |
| W3 | cached | reordered | 68 | 60 | I2 8 |
| W3 | cached | torn | 64 | 48 | I2 16 |
| W3 | no cache | prefix | 57 | 44 | I2 13 |
| W3 | no cache | torn | 57 | 41 | I2 16 |

The volume mounted (I1), `files/keep` was byte-exact (I4), and a new write, sync and remount succeeded
(I5) in every case. The failures are in I2, I3 and reads:

- **W2, every model and both profiles.** `settings/w2` was missing at cuts between the flush that ends
  the removal of the destination and the end of the rename, as with `lost` at positions 10 and 11.
  This is the order in Zephyr's FatFs rename, which removes the destination first (finding F9). The
  [board measurement](../../microsd-replace-cut/README.md) found the same window on a card.
- **W1 with `reordered`.** The directory entry with the new size survived without the data sector,
  so the last record read back stale.
- **W1 with `torn`.** In every failing case the torn sector was the directory sector, which holds the
  file's size. Later in the run the last record read back stale. At the first step, on both profiles,
  it gave a read error on `files/w1`.
- **W3 with `prefix`, `reordered` and `torn`.** A cut inside a step left a state that matches no step,
  for example a FAT update and new data without the directory entry that points to them. `lost`
  never failed, because each W3 step in this trace has one flush, at its end.

None of these failures is a defect in this work. They show what FatFs does not guarantee under this
model, which is why the design asserts the invariants only on Reliance Edge.

## Limits

- The 5-minute bound applies to one CI runner. This run had no CI runner. The emulated time is about 8 % of the
  bound, and 64 samples were chosen from it. The script is not in the Zephyr CI workflow.
- FatFs has no metaroot, so only the flush positions are mandatory here. The commit pattern check and
  the barrier and early-acknowledgement controls need Reliance Edge, which later tasks add.
- One medium size (4 MiB), one cache size, 512-byte sectors, no timing and no wear.
