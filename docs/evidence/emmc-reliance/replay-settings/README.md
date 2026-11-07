# Power-cut replay of W4, Settings File saves on Reliance Edge — 7 November 2026

Software evidence for issue 7 of the [eMMC design](../../../plans/emmc-reliance-design.md#power-cut-replay): the
power-cut replay on the eMMC simulator, with a workload (W4) that saves through the Zephyr Settings File backend,
on Reliance Edge through the Crux VFS adapter.

**Every result here is an observation of the simulator's model on `native_sim`. None of it is evidence about
power loss on an eMMC device.** The simulator and the replay are models, written by the same author as the
tests. A rebuilt image shows what the model allows at a cut. It leaves out damage outside the write being made,
and it says nothing about a device's flash translation layer. W4 uses the corrected backend that the Settings
File selection stages (see the [composition document](../../../emmc-reliance-composition.md)), not the pinned one.

On `red-fix-settings`, the invariants I1 to I5 and `read_error=0` are asserted for W4 on both profiles. The
controls and the staged states show that the checks can fail.

## What ran

- Revision: the Task 11 change on top of `981f15f`, run from the working tree and then committed unchanged with
  this record. Zephyr `dccb09599635bdff17633fa7e9dab014b91dce90`, Reliance Edge
  `6706984258c5bb61c778fe8ca2edd04b559efb78`, `native_sim/native/64`.
- Environment: the Dev Container image, run with Docker as `linux/amd64` under emulation on an arm64 Mac, limited
  to 4 CPUs and 6 GB. No CI runner was used, so all times are emulated times. The images link the GPLv2 Reliance
  Edge source. They are test images and are not kept here.
- Images, built under `build/zephyr/emmc-powercut/`, both with `-DEXTRA_CONF_FILE="reliance.conf;settings.conf"`:
  - `red-fix-settings`: `-DCONFIG_CRUX_MMC_CACHE_FLUSH=y`.
  - `red-nofix-settings`: `-DCONFIG_CRUX_MMC_CACHE_FLUSH=n -DCRUX_EMMC_FLUSH_CONTROL=ON`, the unmodified stack,
    which the adapter's guard allows only on `native_sim`.
- Command, from the repository in the container. It is the W4 step of `scripts/test-storage-emmc.sh`:

  ```sh
  python3 tests/native/emmc_powercut/run_replay.py \
      --exe-fix build/zephyr/emmc-powercut/red-fix-settings/zephyr/zephyr.exe \
      --exe-nofix build/zephyr/emmc-powercut/red-nofix-settings/zephyr/zephyr.exe \
      --fs reliance --workload W4 --seed 1 --samples 8 --max-seconds 300 --out build/zephyr/emmc-powercut/replay
  ```

  Exit 0, 2232 cases, 294 verifier processes, 31.9 s. The command and the two image builds are the W4 step of the
  script; the rest of the script was not part of this run.
- [`reliance-w4.json`](reliance-w4.json) is the run's output. No medium image is kept.
- The W1 to W3 run is unchanged: the same command as in the [Reliance Edge record](../replay-reliance/README.md)
  gives 4947 cases, and the FatFs run 1182. W4 is a run of its own, with its own JSON and counts.

## The workload

- The settings file is `/MMC:/settings/cfg`, with `CONFIG_SETTINGS_FILE_MAX_LINES=6`. The baseline holds three
  keys (`w4/a`, `w4/b`, `w4/c`) at step 0, saved by the backend.
- Step n (1 to 24) saves key (n - 1) % 3 with a 16-byte record that carries the step, the key number and a
  checksum, through `settings_save_one`, and marks when the call returns. A save that would push the file past
  the line limit compacts it: the backend writes `cfg.cmp`, closes it, closes the old file and renames
  `cfg.cmp` onto `cfg`.
- The run phase stops with an error if the final file is as long as one line per save, so a W4 that never
  compacts cannot pass. The Settings File line limit is the only reason it compacts.
- `verify` mounts without formatting, initialises the settings and loads each key:
  - I3, reported as `replace`: the active settings file exists and loads, and every key has a valid record
    (right key, right checksum) of a step that saves that key. A missing key or file is not complete.
  - I2, reported as `data`: complete, and no key is older than the newest acknowledged step that saved it.
  - I5, reported as `recommit`: a further save succeeds and is there after a remount. The usual file check of
    `recommit` also runs.
  - `cmp=0|1` tells whether `cfg.cmp` is left behind. It is counted and is not a failure.

## Result

| Profile | Model | Cases | Passed | Cut leaves `cfg.cmp` |
| --- | --- | --- | --- | --- |
| cached | lost | 188 | 188 | 67 |
| cached | prefix | 446 | 446 | 179 |
| cached | reordered | 444 | 444 | 185 |
| cached | torn | 366 | 366 | 144 |
| no cache | prefix | 364 | 364 | 145 |
| no cache | torn | 366 | 366 | 144 |

Every case passed I1 to I5 with `read_error=0`, including every metaroot sector under `torn` (326 and 342 of
the torn cases tore a write). The cached trace holds 24 marks, 80 flushes and 40 commits (a flush, one whole
metaroot, a flush), in 1321 events and 1216 written sectors. The no-cache trace holds 1240 events.

- **The active settings file was never missing.** Across 2232 cases (and 864 cases that left a temporary),
  `replace=1` held everywhere. The spec says that with an atomic rename the `.cmp` recovery step of
  `samples/storage_sd` should not be needed here, and W4 tests that rather than assuming it. In the model it is
  not needed: a cut between the temporary's creation and the rename leaves the old file whole and a stray
  temporary, which the next compaction replaces (`settings_file_create_or_replace` removes an existing one) and
  which the backend's loader ignores while the active file exists. This is a result about the model and this
  file system's atomic rename, not about a device.
- A leftover temporary is not an error here, so nothing in the composition removes it. It costs the space of one
  compacted file until the next compaction.
- The staged `leftover` state below shows that a leftover with an intact file is accepted, and counted.

## Controls and staged states

Each is required to come out as stated, and did.

| Check | Run | Required | Result |
| --- | --- | --- | --- |
| Flush control | `red-nofix-settings`, cached, `lost`, after each of the 24 marks | `data=0` with `mount=1` | 24 of 24 |
| Early acknowledgement control | `red-fix-settings`, `run-early` (each mark before its save), cached, `lost`, after each mark | `data=0` with `mount=1` | 24 of 24 |
| Damaged image control | the baseline with both metaroots overwritten | `mount=0` | `mount=0` (each metaroot alone still mounts) |
| Staged intact | the image after a complete run, unchanged | accepted, `cmp=0` | as required |
| Staged leftover | the same, with an empty `cfg.cmp` beside `cfg` | accepted, `cmp=1` | as required |
| Staged missing | the same, with `cfg` removed | `replace=0`, `data=0`, `mount=1` | as required |

## Checks shown to fail (mutation)

The harness is a local script and is not kept. Each mutant edits the workload or its configuration, rebuilds
both images, runs the replay above and must make it fail.

| Mutant | Result |
| --- | --- |
| Staleness is never reported (the freshness comparison is made unreachable) | caught: the flush control and the early acknowledgement control did not fail as required (51 failure lines) |
| A missing key counts as complete | not caught, and equivalent for the missing file: the file check alone still reports it |
| No check that the settings file exists | not caught, and equivalent: the missing-key check alone still reports a missing file |
| Both of the two above | caught: the staged missing file was accepted as complete |
| A line limit of 1000, so that W4 never compacts | caught: the run phase stops with `-95` (`-ENOTSUP`), and the replay reports the failed run |
| Every mark before its save | caught: I2 is violated on `red-fix-settings` (1256 failure lines, all after a mark, under `lost`, `prefix` and the others) |
| A leftover temporary counts as a failure | caught: I3 is violated in the cases that leave one (867 failure lines), which shows that a leftover is a state of its own |

The two checks of a missing file are redundant on purpose, so each alone survives its own mutant. I5 has no
mutant of its own, see the limits.

## Link check of the Settings scenario

`scripts/test-file-emmc-compile.sh` builds `crux.sample.storage_emmc.settings` for
`mimxrt1060_evk@B/mimxrt1062/qspi` next to the two scenarios of the [link record](../rt1060-link/README.md), and
links nothing that runs. It is the link-only check that the issue asks for, with the owner's link-only exception.

- The image links `emmc_configuration_start`, `crux_config_zephyr_init` and the Zephyr Settings File backend, and
  not `red_format` or FatFs. The sample has no Configuration consumer, so `crux_config_save` is not referenced
  and `configuration.c` is not in the image.
- `.config` has `CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE=y`, `CONFIG_SETTINGS=y`, `CONFIG_SETTINGS_FILE=y` and
  `CONFIG_SETTINGS_FILE_PATH="/MMC:/settings/cfg"`. The other two images have neither of the first two.
- The build used the three staged copies (`settings_file.c`, `settings_line.c`, `settings_store.c`) and none of the
  three pinned originals. The other two images used no staged copy.
- The Settings configuration with the fixture-only `CRUX_SETTINGS_FILE_CONTROL` fails at CMake and says that the
  control is a native_sim setting.
- FLASH 73,356 B and RAM 72,448 B, against 68,252 and 71,808 B for the image without the mount and 69,584 and
  72,320 B with it, which this change did not alter.

Checks shown to fail, each run on a copy of the green output with `CRUX_FILE_EMMC_SKIP_BUILD=1`, or on a changed
source, and each made the script, or the gate, fail with the stated message:

| Mutant | Message |
| --- | --- |
| The Settings option removed from the Settings image | `CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE=y is missing` |
| `CONFIG_SETTINGS_FILE` removed from it | `CONFIG_SETTINGS_FILE=y is missing` |
| The settings path under `/SD:` | `CONFIG_SETTINGS_FILE_PATH="/MMC:/settings/cfg" is missing` |
| The settings path `/MMC:/cfg`, not under `/MMC:/settings` | the same |
| The Settings option set in the image without the mount | `CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE is set` |
| `CONFIG_SETTINGS` set in the image with the mount only | `CONFIG_SETTINGS is set` |
| The `.elf` of the image with the mount in place of the Settings image | `emmc_configuration_start is not linked` |
| The Settings `.elf` in place of the image with the mount | `the Settings File provider is linked` |
| A pinned `settings_file.c` in the Settings build's compile commands | `the build used staged copies [...] and pinned sources [...]` |
| A staged `settings_store.c` missing from them | the same |
| A staged copy in the build of the image without the mount | `staged Settings File copies ['settings_file.c'] in a build without them` |
| The control guard loosened in `zephyr/CMakeLists.txt` | `The eMMC composition configured with` the control |
| The control ignored, so the corrections are staged in the control build | the control suite passes, which the gate refuses (`unexpectedly passed`) |
| The Kconfig dependency on `CRUX_FS_RELIANCE_EDGE` removed | the gate: `The Settings File selection was not dropped for the missing file system` |

## Time added

The W4 replay took 31.9 s emulated, 294 verifier processes. Building the two images adds about two minutes
emulated, most of it Reliance Edge and the Zephyr kernel, which ccache shortens. These are added to
`scripts/test-storage-emmc.sh`, which has not run in CI before this branch. Nothing was measured on a CI runner.

## Limits

- One seed (1) and 8 samples per profile and model, with the mandatory positions: each side of every flush and of
  every metaroot sector write. The sample count alone selects positions.
- The corrected backend, not the pinned one. The pinned backend fails 12 cases of the Settings File suite on this
  file system, in the control of `scripts/test-storage-emmc.sh`.
- Three keys, 24 saves and a line limit of 6. A larger file compacts more slowly and the cut windows are longer.
- I5 is checked by a save and a remount in the verifier, and has no control of its own: nothing here makes a
  further save fail.
- The model has no damage outside the write being made. A torn sector is one sector.
- No result here is physical evidence.
