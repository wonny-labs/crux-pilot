# Power-cut replay on FatFs — 5 November 2026

Software evidence for issue 4 of the [eMMC design](../../../plans/emmc-reliance-design.md#power-cut-replay):
power-cut replay on the eMMC simulator, run on FatFs. **Every result here is an observation of the
simulator's model on `native_sim`. None of it is evidence about power loss on an eMMC device.** The
simulator is a model written from the needs of the Zephyr MMC stack, by the same author as the tests. A
rebuilt image shows what the model allows at a cut, and it leaves out damage outside the write being made.
FatFs does not claim the invariants, so nothing about FatFs is asserted: only the two controls must fail.

## What ran

- Revisions:
  - First run: `88c388c`, whose parent is `d0c8ebe`. That commit was then rebased onto `1b909cc`, becoming
    `98246e0`. `1b909cc` changed documentation only: two sentences in `patches/zephyr/README.md`.
  - The run recorded here: the first review's fixes, uncommitted, on `98246e0`. The runner log shows
    that `HEAD` and the changed files. The fixes were then committed unchanged as the next commit, which
    also adds this text. This log sat in the untracked build directory and is not kept in the
    repository, so the claim that the fixes were committed unchanged as `b7e19d7` cannot be checked
    from the record. A re-run at `b7e19d7` would settle it. The figures below are from this run and
    replace the first run's.
- Zephyr `dccb09599635bdff17633fa7e9dab014b91dce90`, FatFs module
  `f4ead3bf4a6dab3a07d7b5f5315795c073db568d`, `native_sim/native/64`, Python 3.12.3.
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
- [`fatfs.json`](fatfs.json) is the run's output: counts, seeds, positions, every observed failure,
  both controls and the `run-early` observation. No medium image is kept here.
- A verifier process gets 60 s. A process that hangs is killed and recorded as a failure with its seed,
  position and image, and the run goes on. None hung in this run.

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

## Early acknowledgement, recorded

The `run-early` phase marks each W1 step before its `fs_sync`. On `fat-fix`, cached, with `lost` at the
position after each of the 16 marks, all 16 cases reported `data=0`: each step was marked while its
record was still only in the cache. This is an observation that the phase and the marks work. It is not
asserted here, because the design's early-acknowledgement control runs on Reliance Edge.

## Observations

1182 cases ran in 465 verifier processes, in 24.8 to 25.7 s emulated: two direct runs and one through
the script. The script's whole run took 489 s of wall time, mostly the earlier suites and the builds.
A rebuilt image that was already verified for the same workload, acknowledged count and check is
not verified again. The count includes 12 checks that the verifier passes on the baseline and on each
complete run, 17 control cases and the 16 `run-early` cases. The second `--acked-only` look at a W1
failure is not counted as a case. The same seed gave the same JSON apart from timing in all three runs,
compared field by field.

`torn` positions are drawn from write positions only, because only a write at the cut can tear.
Each `torn` case cut a write in progress and mixed its sector byte by byte. The count does not
check that the mixed sector differs from both the old and the new contents, so a small change such
as a directory entry's size can leave the old or the new sector whole. When a trace has fewer write
positions than `--samples`, all of them are used: 40 for W1 and 44 for W3 on each profile, and 64 of the 72 for W2.

`I2` is the brief's rule: every acknowledged step's result is present, and a later unacknowledged step
counts only as its complete result. For W1 the design's I2 is weaker: the acknowledged records are
intact. Each W1 case that failed the brief's rule was verified again with `--acked-only`, which asks
only the design's question. The table splits those cases.

| Workload | Profile | Model | Cases | Passed | Failed |
| --- | --- | --- | --- | --- | --- |
| W1 | cached | lost | 66 | 66 | — |
| W1 | cached | prefix | 68 | 68 | — |
| W1 | cached | reordered | 68 | 65 | I2 3, all with every acknowledged record intact |
| W1 | cached | torn | 40 | 34 | I2 6: 2 with every acknowledged record intact, 4 lost (1 of them a read error) |
| W1 | no cache | prefix | 57 | 57 | — |
| W1 | no cache | torn | 40 | 39 | I2 1, lost: a read error |
| W2 | cached | lost | 89 | 73 | I2 and I3 16 |
| W2 | cached | prefix | 88 | 66 | I2 and I3 22 |
| W2 | cached | reordered | 85 | 69 | I2 and I3 16 |
| W2 | cached | torn | 64 | 46 | I2 and I3 18 |
| W2 | no cache | prefix | 64 | 52 | I2 and I3 12 |
| W2 | no cache | torn | 64 | 49 | I2 and I3 15 |
| W3 | cached | lost | 66 | 66 | — |
| W3 | cached | prefix | 65 | 53 | I2 12 |
| W3 | cached | reordered | 68 | 60 | I2 8 |
| W3 | cached | torn | 44 | 27 | I2 17 |
| W3 | no cache | prefix | 57 | 44 | I2 13 |
| W3 | no cache | torn | 44 | 28 | I2 16 |

The volume mounted (I1), `files/keep` was byte-exact (I4), and a new write, sync and remount succeeded
(I5) in every case. The failures are in I2, I3 and reads:

- **W2, every model and both profiles.** `settings/w2` was missing at cuts between the flush that ends
  the removal of the destination and the end of the rename, as with `lost` at positions 10 and 11.
  This is the order in Zephyr's FatFs rename, which removes the destination first (finding F9). The
  [board measurement](../../microsd-replace-cut/README.md) found the same window on a card.
- **W1 with `reordered`, 3 cases.** Every acknowledged record was intact. The author's analysis,
  which the record cannot back because no image was kept: the directory entry with the new size
  survived without its data sector, so the next, unacknowledged record read back stale, as at
  position 39 with 8 acknowledged. These cases fail only the brief's stricter rule. They are not a loss
  of acknowledged data.
- **W1 with `torn`.** The author's analysis, which the record cannot back because no image was kept:
  in every failing case the torn sector was the directory sector, which holds the file's size and
  first cluster.
  - Every acknowledged record was intact in 2 cached cases, at positions 16 and 70, and only the next
    record was stale.
  - The other 3 cached cases, at positions 7, 38 and 48, lost an acknowledged record.
  - At the first step, on both profiles (cached position 3 and no-cache position 2), the torn entry
    made `files/w1` unreadable, which reports `read_error=1`. These are counted as losses. With nothing acknowledged, I2 holds vacuously, and what failed there
    is that the provisioned `files/w1` was unreadable.
- **W3 with `prefix`, `reordered` and `torn`.** A cut inside a step left a state that matches no step,
  for example a FAT update and new data without the directory entry that points to them. `lost`
  never failed, because each W3 step in this trace has one flush, at its end. The W3 check is the
  brief's rule and was not split.

None of these failures is a defect in this work. They show what FatFs does not guarantee under this
model, which is why the design asserts the invariants only on Reliance Edge.

## Requirement carried to the Reliance Edge task

The VFS has no directory sync, so a W3 directory step (`fs_unlink` of a file, `fs_mkdir`, the
`fs_unlink` of a directory) has no separate sync. It relies on the file system making the operation
durable before it returns. FatFs does this. When this replay runs on Reliance Edge, its configuration
must commit these operations, through its transaction settings. Otherwise W3 would fail there because
of the configuration, not because of the file system's guarantees.

## Limits

- The 5-minute bound applies to one CI runner. This run had no CI runner. The emulated time is about 9 % of the
  bound, and 64 samples were chosen from it. The script is not in the Zephyr CI workflow.
- The pinned case count in `scripts/test-storage-emmc.sh` depends on the traces, so on the pinned
  Zephyr and FatFs, and on the algorithms of Python's `random` module, which draw the samples.
- FatFs has no metaroot, so only the flush positions are mandatory here. The commit pattern check and
  the barrier and early-acknowledgement controls need Reliance Edge, which later tasks add.
- One medium size (4 MiB), one cache size, 512-byte sectors, no timing and no wear.
