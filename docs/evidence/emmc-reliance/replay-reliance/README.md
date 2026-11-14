# Power-cut replay on Reliance Edge — 7 November 2026

Software evidence for issue 5 of the [eMMC design](../../../plans/emmc-reliance-design.md#power-cut-replay):
the power-cut replay on the eMMC simulator, run on Reliance Edge through the Crux VFS adapter.

**Every result here is an observation of the simulator's model on `native_sim`. None of it is evidence about
power loss on an eMMC device.** The simulator and the replay are models, written by the same author as the
tests. A rebuilt image shows what the model allows at a cut. It leaves out damage outside the write being
made, and it says nothing about a device's flash translation layer.

On `red-fix`, the invariants I1 to I5 and `read_error=0` are asserted for W1 to W3 on both profiles. The
controls show that the checks can fail. They do not show that the model matches a device.

## What ran

- Revision: the Task 8 change on top of `162ef06`, run from the working tree and then committed unchanged with
  this record. The run's log, which is not kept, starts with that `HEAD` and the changed files.
- The Reliance Edge steps were run as an extract of `scripts/test-storage-emmc.sh`: its environment checks,
  then everything from the FatFs replay build to the Reliance Edge JSON check. That extract exited 0 after
  400 s of wall time. The whole script was not run, because its other steps did not change. Zephyr `dccb09599635bdff17633fa7e9dab014b91dce90`, Reliance Edge
  `6706984258c5bb61c778fe8ca2edd04b559efb78`, `native_sim/native/64`.
- Environment:
  - The Dev Container image, run with Docker as `linux/amd64` under emulation on an arm64 Mac, limited to 4 CPUs
    and 6 GB.
  - No CI runner was used, so all times are emulated times.
  - The images link the GPLv2 Reliance Edge source. They are test images and are not kept here.
- Images, built under `build/zephyr/emmc-powercut/`:
  - `red-fix`: `-DEXTRA_CONF_FILE=reliance.conf -DCONFIG_CRUX_MMC_CACHE_FLUSH=y`.
  - `red-nofix`: `-DEXTRA_CONF_FILE=reliance.conf -DCONFIG_CRUX_MMC_CACHE_FLUSH=n -DCRUX_EMMC_FLUSH_CONTROL=ON`,
    which is the unmodified stack. The adapter's guard allows it only on `native_sim`.
  - Both images include the formatter, because the same program writes the baseline.
- Commands, from the repository in the container. They are the Reliance Edge steps of
  `scripts/test-storage-emmc.sh`:

  ```sh
  python3 tests/native/emmc_powercut/test_replay_model.py -v
  python3 tests/native/emmc_powercut/run_replay.py \
      --exe-fix build/zephyr/emmc-powercut/red-fix/zephyr/zephyr.exe \
      --exe-nofix build/zephyr/emmc-powercut/red-nofix/zephyr/zephyr.exe \
      --fs reliance --seed 1 --samples 64 --max-seconds 300 --out build/zephyr/emmc-powercut/replay
  ```

- The baseline is formatted by firmware (`fs_mkfs` with `CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT`) and provisioned
  by `red-fix` on the no-cache profile. Every trace and control starts from it.
- Each workload runs with `red-fix` on the cached profile (64 KiB) and on `--emmc-cache-kb=0`. The verifier runs
  without a cache.
- [`reliance.json`](reliance.json) is the run's output. No medium image is kept.
- In the same run the FatFs replay, rebuilt from this change, gave the same JSON as the
  [FatFs record](../replay-fatfs/fatfs.json) apart from timing: 1182 cases, with both controls failing as
  required.

## Metaroot recognition

- Metaroot writes are recognised by their sectors: blocks 1 and 2 of 4 KiB, which are sectors 8 to 23.
  - The first metaroot block number (`BLOCK_NUM_FIRST_METAROOT`, 1) and its documentation comment were read
    from one line of the Reliance Edge `core/include/redcoremacs.h`.
  - The block size is the profile's own value.
- On each cached trace the driver confirms that every commit is a flush, one whole metaroot in order, then a
  flush. It stops if the pattern is not found, because the barrier control depends on it.
- In every trace the commits alternate between the two metaroots.
- A metaroot carries its content in its first sector. The other seven sectors were zero in every write.

| Trace (`red-fix`) | Events | Sector writes | Flushes | Marks | Commits | Metaroot positions |
| --- | --- | --- | --- | --- | --- | --- |
| W1 cached / no cache | 433 / 400 | 384 / 384 | 32 / 0 | 16 / 16 | 16 | 144 / 144 |
| W2 cached / no cache | 761 / 712 | 704 / 704 | 48 / 0 | 8 / 8 | 24 | 216 / 216 |
| W3 cached / no cache | 493 / 460 | 448 / 448 | 32 / 0 | 12 / 12 | 16 | 144 / 144 |

W1 commits once per step, at `fs_sync`. W2 commits three times per generation: the create of `w2.tmp`, the
sync and the rename. W3 commits once per step, or twice in a step that creates a file (see below).

## Cut positions

- On the cached profile: both sides of every flush, with `lost`, `prefix` and one `reordered` draw.
- On both profiles: both sides of every metaroot sector, with `prefix`, `torn` and `reordered` (cached), or
  `prefix` and `torn` (no cache). So every metaroot sector is torn.
- On the cached profile: the position after every mark, with `lost`.
- Then 64 positions per workload, profile and model, drawn by seed. `torn` is drawn from write positions only.

## Asserted results on `red-fix`

| Workload | Profile | lost | prefix | reordered | torn |
| --- | --- | --- | --- | --- | --- |
| W1 | cached | 130 / 130 | 213 / 213 | 210 / 210 | 189 / 189 |
| W1 | no cache | — | 192 / 192 | — | 185 / 185 |
| W2 | cached | 156 / 156 | 307 / 307 | 302 / 302 | 267 / 267 |
| W2 | no cache | — | 259 / 259 | — | 260 / 260 |
| W3 | cached | 133 / 133 | 220 / 220 | 216 / 216 | 192 / 192 |
| W3 | no cache | — | 190 / 190 | — | 188 / 188 |

Each cell gives the cases that passed out of the cases run. Every case held I1 to I5 with `read_error=0`. The W3
counts include the created-empty cases described next, which pass only under the narrowed W3 check.

In total, 4947 cases ran in 892 verifier processes, in 85.9 s emulated. An earlier run of the same code took
97.3 s, and its JSON was the same apart from timing. This count includes:

- the sanity checks;
- the controls;
- the barrier draws recorded on W1 and W3;
- the checks of the narrowed W3 check.

A rebuilt image that was already verified for the same workload and acknowledged count is not verified again.

### A new file can be present and empty (W3)

The Crux profile commits on create (`RED_TRANSACT_CREAT`). A W3 step that writes a new file therefore makes two
Reliance Edge commits: one when `fs_open` creates the file, and one at `fs_sync`. A cut between them leaves the
new file present with size 0. Every other path is as after the acknowledged step, so acknowledged data is
intact.

The four steps are 2 (`files/w3b`), 4 (`files/w3dir/e`), 6 (`files/w3c`) and 11 (`files/w3d`).

Before the check modelled this state, these were the only W3 failures: 253 at 64 samples, all I2. A debug
build of the verifier, not kept, printed the files of five such images and showed exactly this state.

The verifier's W3 check now accepts this one state, with `--create-may-be-empty`, given only on Reliance Edge.
It accepts it only in the step after the acknowledged one, only when that step creates the file, and only with
every other path as after the acknowledged step. Such cases are counted apart:

| Profile | Model | Step 2 | Step 4 | Step 6 | Step 11 |
| --- | --- | --- | --- | --- | --- |
| cached | lost | 7 | 6 | 8 | 4 |
| cached | prefix | 13 | 15 | 12 | 12 |
| cached | reordered | 13 | 9 | 11 | 13 |
| cached | torn | 11 | 13 | 9 | 10 |
| no cache | prefix | 10 | 9 | 12 | 11 |
| no cache | torn | 12 | 11 | 12 | 10 |

That is 253 cases in all, and acknowledged data was intact in every one. Under `lost` they lie between the create
commit and the end of the sync commit's metaroot write, because `lost` drops a metaroot written after the last
flush. Under `reordered` they include positions where the draw dropped the sync commit's metaroot.

Two kinds of check show that the narrowed check still refuses everything else:

- **Images from the cut.** For each create step, the first such image is verified again in two ways:
  - claiming that step as acknowledged, where the empty file is a lost acknowledged file;
  - claiming each other create step as in progress, where the empty file is not the one that step creates.

  All 16 reported `data=0`, as required.
- **Staged states.** `red-fix --phase=stage` makes the state after n steps, both as it is and with each W3 file
  left empty, for n = 0 to 11. There are 67 states, after 5 were skipped where `w3dir` does not exist.
  - Every unchanged state was accepted.
  - Exactly the 4 empty files of the step in progress were accepted.
  - The other 51 empty files were refused with `data=0`.

Mutants of the check, run before this record:

| Mutant | Caught by |
| --- | --- |
| Accept an empty file of any later create step | the image checks |
| Accept whenever the next step creates a file, without comparing the state | the image checks |
| Accept an empty file for whatever path the next step touches, of any kind | the staged states |

The image checks alone did not catch the third mutant, which is why the staged states were added.

Dropping `RED_TRANSACT_CREAT` from the profile would make each W3 step a single commit, but it departs from the
design's transaction list. It was not done, and the profile is unchanged. The design's Failure behaviour and Risks
sections now state the effect for File consumers.

### W3 directory steps are durable at their marks

The VFS has no directory sync, which is the requirement carried from the
[FatFs record](../replay-fatfs/README.md#requirement-carried-to-the-reliance-edge-task).

The profile's transaction mask includes `RED_TRANSACT_MKDIR` and `RED_TRANSACT_UNLINK`. The public header
`redtransact.h` documents the latter as covering `red_unlink` and `red_rmdir`. The adapter maps `fs_unlink` of a
directory to `red_unlink`.

In the traces, each directory step has exactly one commit before its mark:

- step 3, `mkdir`;
- step 7, remove `w3a`;
- step 9, remove `w3dir/e`;
- step 10, `rmdir`.

`lost` at the position after every mark passed on W1 (16), W2 (8) and W3 (12). No configuration change was
needed.

## Controls

| Control | Run | Required | Result |
| --- | --- | --- | --- |
| Reliance Edge flush | `red-nofix`, cached, `lost`, W1, after each of the 16 marks | `mount=1` and `data=0` | **Failed as required** at all 16. The trace holds no flush. |
| Barrier | `red-fix` cached trace of **W2**, `reordered`. The flush before each of the 24 metaroot writes is not a barrier, every metaroot sector is kept and each other sector of the window is kept with even odds. 216 positions, from each ignored flush to the flush after the metaroot. | `mount=0`, `untouched=0` or `read_error=1` somewhere. `data=0` alone does not count | **Failed as required**: `read_error=1` at 71 positions in 14 of the 24 windows (32 of them also with `recommit=0`). The same draws with every barrier honoured passed at all 216. |
| Early acknowledgement | `red-fix`, `run-early`, cached, `lost`, W1, after each of the 16 marks | `mount=1` and `data=0` | **Failed as required** at all 16. |
| Damaged image | `red-fix`, the baseline with sectors 8 to 23 (both metaroots) overwritten with `0xa5` | `mount=0` | **Failed as required**: `mount=0`. With either metaroot overwritten alone, the volume mounted and passed every check, so the damage hit the metaroots and the file system fell back to the other one. |

**Why the barrier control runs on W2, not W1.** The brief named W1. On W1 the draw cannot give the required
result for two reasons:

- Each commit window holds two meaningful sectors. One is the first sector of the file's inode block, whose
  other copy, at the alternate location, is a valid inode from two commits earlier. The other is the first
  sector of a new data block, and file data has no check value.
- A kept metaroot over stale blocks therefore gives a consistent older file or stale content. Both appear only
  as `data=0`, which an older state explains.

The same draw is recorded on both other workloads:

| Workload | Positions | Unexplained results | Detail |
| --- | --- | --- | --- |
| W1 | 144 | 0 | `data=0` at 115 positions |
| W3 | 144 | 31 | all with `read_error=1`, 13 of them also `untouched=0` |

W2's and W3's windows hold directory blocks and new inode blocks, whose stale copies are blank or belong to
another object. The design's control table was corrected to name W2 and give this reason.

A note on what W1 shows: a new inode over a stale data block is not an older state, but the verifier's single
data bit cannot tell it apart from one.

What the controls show and do not show:

- The flush control shows that a sync does not reach the medium without the correction, on this model. Any file
  system would fail it, so it says nothing about the replay's power.
- The barrier control shows that the replay catches the window in finding F3 when the flush before a metaroot
  write is not a barrier, on W2.
- The early acknowledgement control shows that the marks and I2 work.
- The damaged image control shows that the verifier reports an unmountable volume.

## Time

- The run took 85.9 s and 97.3 s emulated in two runs with 64 samples, at most about a third of the 300 s bound.
- The mandatory positions dominate. Earlier runs without the barrier observations and the staged states gave:

  | Samples | Cases | Time |
  | --- | --- | --- |
  | 8 | 3442 | 53.2 s |
  | 32 | 3732 | 56.0 s |
  | 64 | 4144 | 60.4 s |

- No CI runner was used, so the bound on one CI runner is not measured. The script is not in the Zephyr CI
  workflow.

## Limits

- The case count in `scripts/test-storage-emmc.sh` (4947) depends on three things:
  - the traces, and so the pinned Zephyr and Reliance Edge;
  - the Crux profile;
  - the algorithms of Python's `random` module.
- One medium size (4 MiB), one cache size, 512-byte sectors, no timing and no wear.
- Torn tears only the sector being written at the cut. Damage outside the write is not modelled.
- The narrowed W3 check is specific to a file system that commits on create. FatFs is not run with it, and its
  record is unchanged.
