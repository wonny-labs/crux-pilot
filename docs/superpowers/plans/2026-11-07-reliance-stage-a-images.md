# Reliance Edge Stage A images Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build, check and rehearse on `native_sim` the images that Stage A of the eMMC physical gate flashes to the EVKB microSD, so that the board session needs no software work.

**Architecture:** One new sample, `samples/storage_reliance_sd`, mounts Reliance Edge on disk `SD` with the existing `emmc_storage.c`. The append and replace tests of the FatFs microSD runs are reused unchanged: their `sd_interrupt.c`, `sd_replace.c` and `main.c` use only the File API and a small mount helper, which a shim header maps onto `emmc_storage.h`. A format image (its own option, off by default) writes the volume once. A `native_sim` runner rehearses format, append and replace with process restarts before any board time.

**Tech Stack:** C17, Zephyr v4.4.2 (west workspace outside the repository), Twister, Python 3 `unittest`, the Dev Container (`.superpowers/sdd/2026-11-03-emmc-reliance/crun`, amd64 under emulation).

**Spec:** `docs/emmc-reliance-physical-gate.md` (Stage A steps and Precondition 6), `docs/plans/emmc-reliance-design.md` (D9: platform compositions never format; F9 atomic rename), `docs/emmc-reliance-composition.md`.

## Global Constraints

- Public repository: no personal paths, no product information, no Reliance Edge source or derived text. `git grep 'Tuxera US Inc' -- zephyr components samples tests scripts patches | wc -l` must print 0 before every commit.
- No hardware is touched by this plan. No image binary is committed or uploaded from CI. Images that link the GPLv2 source are test images.
- Protected paths (`AGENTS.md`): `scripts/test-file-emmc-compile.sh` and `scripts/test-storage-emmc.sh` may be extended and nothing removed (no suite, board, option or flag). Do not edit `.github/`, `docs/evidence/`, `scripts/test-storage-simulators.sh`, or any other protected file. If a task needs one, stop and report.
- Every option that writes the medium is off by default. `red_format` is linked only in the format image.
- Mount point stays `/MMC:` whatever the disk name is. Disk name for Stage A: `SD`. Board: `mimxrt1060_evk@B/mimxrt1062/qspi`. `CONFIG_SDHC_BUFFER_ALIGNMENT` is 32 (default of `CONFIG_CRUX_FILE_RELIANCE_EDGE`). Linked size ends below `0x60600000`.
- British English, plain style. Every commit ends with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Containers: 64-bit `native_sim` only, `-j 4` at most, `run_in_background` for long commands, check `df -h /System/Volumes/Data` before each build (below 10 GiB start nothing heavy, below 5 GiB stop), remove build output when done. Never kill Docker or hung clients; if Docker hangs, run `open -a Docker` once and wait.

## Review Focus

- The format image run on a card that already holds a volume must refuse and write nothing (a second flash of it must not erase the test data).
- The default image (no test option) must never write the card: a power cut on it can harm nothing.
- A card formatted by a different profile must report a mount failure and never be reformatted by an image that only mounts.
- After a restart, the append run recovers at least as many records as the last acknowledgement the host saw (acknowledged data is not lost), and the host script parses the Reliance output unchanged.
- After a restart of the replace test on Reliance Edge, `cfg` is never missing (atomic rename); a leftover `cfg.cmp` is classified, not an error.

---

### Task 1: Sample `storage_reliance_sd` with the append and replace images, and its link check

**Files:**
- Create: `samples/storage_reliance_sd/{CMakeLists.txt,Kconfig,prj.conf,testcase.yaml,sd_storage.h}`
- Modify: `scripts/test-file-emmc-compile.sh` (additions only: the new scenarios and their checks)
- Modify: `tests/README.md` (one paragraph naming the sample and its check)

**Interfaces:**
- Consumes: `emmc_storage_start()` and `enum emmc_storage_status` from `samples/storage_emmc/emmc_storage.{h,c}`; `main.c`, `sd_interrupt.{c,h}` of `samples/storage_sd_interrupt`; `main.c`, `sd_replace.{c,h}` of `samples/storage_sd_replace`.
- Produces: Kconfig `SAMPLE_RELIANCE_SD_APPEND_TEST`, `SAMPLE_RELIANCE_SD_REPLACE_TEST` (both off by default, mutually exclusive), `SAMPLE_EMMC_START` (off by default, as in `samples/storage_emmc/Kconfig`; the mount-check image of Stage A steps 3 and 4) and `SAMPLE_EMMC_DISK_NAME` defined here with default `"SD"`; shim `sd_storage.h` that maps `SD_STORAGE_SETTINGS_DIR`, `SD_STORAGE_READY`, `enum sd_storage_status` and `sd_storage_start` onto the `EMMC_STORAGE_*` names, so the reused sources compile unchanged. Twister scenarios `crux.sample.storage_reliance_sd` (default, touches no card), `.start` (mount check, `SAMPLE_EMMC_START=y`), `.append`, `.replace`; each scenario states its own `platform_allow` (Task 3 adds `native_sim/native/64` scenarios to the same file).

- [ ] **Step 1: Extend `scripts/test-file-emmc-compile.sh` first (red).** Add a second twister run on `samples/storage_reliance_sd` for the board, and assertions per scenario, in the style of the existing Python block: the four scenario names are exactly the expected ones and build only; `.config` has `CONFIG_DISK_DRIVER_SDMMC=y`, `CONFIG_CRUX_FS_RELIANCE_EDGE=y`, `CONFIG_CRUX_FILE_RELIANCE_EDGE=y`, `CONFIG_SDHC_BUFFER_ALIGNMENT=32`, no `CONFIG_DISK_DRIVER_MMC`, no `CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT`; the ELF has no `red_format`; `emmc_storage_start` is present in `.start`, `.append` and `.replace` and absent from the default image, `sd_interrupt_append` is present in `.append` only and `sd_replace_generation` in `.replace` only (so the default image provably touches no card); the end of the linked image is below `0x60600000`. Print the ELF SHA-256 of each image to the log only.
- [ ] **Step 2: Run it and see it fail** for the right reason: `crun 'scripts/test-file-emmc-compile.sh'` exits non-zero because the sample directory does not exist.
- [ ] **Step 3: Create the sample.** `prj.conf`: the lines of `samples/storage_emmc/prj.conf` with `CONFIG_DISK_DRIVER_SDMMC=y` instead of the MMC driver and flush option, plus `CONFIG_CRC=y` and `CONFIG_MAIN_STACK_SIZE=8192`; `CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT` stays off. `CMakeLists.txt`: refuse unless `CONFIG_CRUX_FILE_RELIANCE_EDGE`, and on every board except `native_sim` also unless `CONFIG_DISK_DRIVER_SDMMC`; compile `../storage_emmc/emmc_storage.c` plus, by option, `../storage_sd_interrupt/{main.c,sd_interrupt.c}` or `../storage_sd_replace/{main.c,sd_replace.c,../storage_sd_interrupt/sd_interrupt.c}`; with `SAMPLE_EMMC_START` or no option compile `../storage_emmc/main.c`; include directories in this order: this directory (the shim first), `../storage_emmc`, `../storage_sd_interrupt`, `../storage_sd_replace`, `zephyr/fs/reliance_edge`, `components/file/ports/zephyr`. `Kconfig`: the three symbols above, `mainmenu "Crux Reliance Edge microSD Stage A images"`, with help text that says the test options write to the card and need approval for the current task. `testcase.yaml`: build-only, tags `[filesystem, crux, link_only]`, four scenarios each with `platform_allow: [mimxrt1060_evk@B/mimxrt1062/qspi]` (not in a `common:` block), the non-default ones setting their option through `extra_configs`.
- [ ] **Step 4: Run the link check and see it pass.** Expected: the new scenarios link and every assertion holds. Then mutate (revert after each) and show the check fails: drop `CONFIG_DISK_DRIVER_SDMMC` from `prj.conf`; set `CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT=y` in the default scenario; let the default image call the append test (remove the option guard in `main`); set `CONFIG_SDHC_BUFFER_ALIGNMENT=16`.
- [ ] **Step 5: Run the existing checks that share the files:** `crun 'scripts/test-file-emmc-compile.sh'` (the original three scenarios still pass), `scripts/check-format.sh`, `scripts/check-dependencies.sh`. Commit: `feat(storage): add the Reliance Edge microSD Stage A sample and its link check`.

### Task 2: Format image

**Files:**
- Create: `samples/storage_reliance_sd/format.c`, `samples/storage_reliance_sd/format.h`
- Modify: `samples/storage_reliance_sd/{CMakeLists.txt,Kconfig,testcase.yaml}`, `scripts/test-file-emmc-compile.sh` (additions only)

**Interfaces:**
- Consumes: Task 1's sample; `fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)disk, NULL, 0)`; `CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT`.
- Produces: Kconfig `SAMPLE_RELIANCE_SD_FORMAT` (off by default; selects `CRUX_FS_RELIANCE_EDGE_FORMAT`; mutually exclusive with the two test options); `enum reliance_format_status { RELIANCE_FORMAT_OK = 0, RELIANCE_FORMAT_NO_MEDIA, RELIANCE_FORMAT_VOLUME_PRESENT, RELIANCE_FORMAT_MKFS_FAILED, RELIANCE_FORMAT_MOUNT_FAILED, RELIANCE_FORMAT_MKDIR_FAILED }` and `enum reliance_format_status reliance_format_once(void)` in `format.h`; console line `reliance_format: <status>`; scenario `crux.sample.storage_reliance_sd.format`.

- [ ] **Step 1: Extend the link check (red).** Add the `.format` scenario: its ELF has `red_format`, every other scenario's ELF does not (this replaces the "absent everywhere" assertion with "present only in `.format`"); `CONFIG_SAMPLE_RELIANCE_SD_FORMAT=y` is in its `.config` only.
- [ ] **Step 2: Run it and see it fail** (no such scenario).
- [ ] **Step 3: Implement `reliance_format_once` in `format.c`.** Order: `disk_access_init` (fail: `NO_MEDIA`); try `fs_mount` with the same mount definition as `emmc_storage.c` (success: unmount and return `VOLUME_PRESENT`, writing nothing); `fs_mkfs`; mount; `fs_mkdir` `/MMC:/files` and `/MMC:/settings`; unmount. `main` of the format image calls it once and prints `reliance_format: <status>`, then returns. The function writes only after the mount attempt failed.
- [ ] **Step 4: Run the link check, pass.** Mutants: remove the `VOLUME_PRESENT` early return (caught in Task 3's rehearsal, record it there), put `red_format` into the default image (caught here).
- [ ] **Step 5: Commit:** `feat(storage): add a guarded format image for the Reliance Edge Stage A card`.

### Task 3: `native_sim` rehearsal of format, append and replace

**Files:**
- Create: `samples/storage_reliance_sd/boards/native_sim_native_64.overlay` (an eMMC simulator node whose disk is named `SD`, as `tests/native/emmc_restart/app.overlay` does) and `samples/storage_reliance_sd/boards/native_sim_native_64.conf`, `tests/native/reliance_stage_a/run_rehearsal.py`, `tests/native/reliance_stage_a/test_rehearsal.py`
- Modify: `samples/storage_reliance_sd/testcase.yaml` (native_sim build-only scenarios), `scripts/test-storage-emmc.sh` (additions only: build the sample for `native_sim/native/64` in its four modes and run the rehearsal), `tests/README.md`

**Interfaces:**
- Consumes: Task 1 and 2 images built for `native_sim/native/64`; `samples/storage_sd_interrupt/host/interrupt_capture.py` and `samples/storage_sd_replace/host/replace_capture.py` (their line parsers and judging functions, imported, not copied); the process-restart pattern of `tests/native/emmc_restart/run_restart.py`.
- Produces: `run_rehearsal.py --format-exe F --append-exe A --replace-exe R --image PATH` exit 0 only when every check holds; the checks below.

- [ ] **Step 1: Write `test_rehearsal.py` (red)** with unit tests of the runner's own judging helpers: a console log with ACK 1..40 then a restart whose RECOVER says `last_valid=40` is accepted; `last_valid=39` is rejected (acknowledged record lost); a replace log whose `cfg` is `none` after a restart is rejected; a leftover `cmp=valid` is accepted and counted.
- [ ] **Step 2: Run it and see it fail** (module missing): `python3 tests/native/reliance_stage_a/test_rehearsal.py`.
- [ ] **Step 3: Implement the runner.** Scenarios on one simulator image file: (a) format image on a blank medium: `reliance_format: 0`; a second run: `reliance_format: 2` (volume present) and the medium digest unchanged; (b) `.start` image on the formatted medium: `emmc_storage_start: 0` and zero simulator writes; on a blank medium: status 2 (mount failed), zero writes and an unchanged digest; the default image prints nothing and writes nothing; (c) append image: run until at least 200 ACKs, `SIGKILL`, restart, repeat five times, using the host script's own line parser; each restart's RECOVER `last_valid` is at least the last ACK seen, `damaged=0`; (d) replace image: same with `CLASS` lines; `cfg` is never `none` after the first generation; counts of `cmp=valid` leftovers are printed. The `native_sim` build uses `boards/native_sim_native_64.conf` with `CONFIG_DISK_DRIVER_MMC=y` and `CONFIG_CRUX_MMC_CACHE_FLUSH=y` (the simulator is an MMC device, and the adapter's guard needs the flush correction on an MMC disk); run the no-cache and the 64 KiB-cache simulator profiles. **State the limit in the runner's header and in the PR: the rehearsal exercises the application logic, the File provider and Reliance Edge on a model, and it does not exercise the SD host driver, the SD layer or a real card.**
- [ ] **Step 4: Run it in the container and see it pass.** Show the counts. Mutate (revert after each): remove the `VOLUME_PRESENT` return (scenario (a) fails); make `sd_interrupt_append` skip the sync (scenario (c) fails on the cached profile); make the replace test remove `cfg` before the rename (scenario (d) fails).
- [ ] **Step 5: Whole-script run.** Run `scripts/test-storage-emmc.sh` once, in the background, and report its exit status and time. Commit: `test: rehearse the Reliance Edge Stage A images on native_sim`.

### Task 4: Run sheet in the gate document

**Files:**
- Modify: `docs/emmc-reliance-physical-gate.md` (Dependencies section and a new "Stage A run sheet" section), `docs/emmc-reliance-composition.md` (one pointer)

**Interfaces:**
- Consumes: the image names, options and scenario names from Tasks 1 to 3.
- Produces: a run sheet that names each Stage A step with its image and exact command shape.

- [ ] **Step 1: Update "Dependencies: what exists and what does not":** the four images now exist as build-only Twister scenarios and a `native_sim` rehearsal; they are not flashed, not independently reviewed for the board, and no card was formatted.
- [ ] **Step 2: Add the "Stage A run sheet":** per step of "Stage A steps": the image (restore image is the default image of `samples/storage_sd`, which touches no card; format, blank-card and formatted-card checks use the images of Tasks 1 and 2; append and replace use `.append` and `.replace`), the build options, the host driver command with the parameters already used in the FatFs runs (`--off-seconds 3`, `--min-run`, `--max-run`, `--boot-timeout 25`, `--cycles`), the cut command for the hub port that was verified on the day, and the rule that each cut pause is 3 s or less because the supply of this bench returned about 4.4 s after a port cut on 2026-11-07 (cause not known). Do not state any result.
- [ ] **Step 3: Run the link and anchor checks:** `git grep -n 'emmc-reliance-physical-gate' docs | head`, then check that every relative link in the changed files resolves. State that `scripts/check_docs.py` was not run if lychee is not installed.
- [ ] **Step 4: Commit:** `docs: add the Stage A run sheet to the eMMC physical gate`.

## Self-review

- **Spec coverage:** gate Precondition 6 (images exist, options off by default, `red_format` only in the format image, alignment and size recorded) is Tasks 1 and 2; Stage A steps 2 to 6 have an image each; step 1 and step 7 use the existing restore image; the supply and flash-backup preconditions are board work and stay in the gate document.
- **Not in this plan:** any board work, an independent review of the images for the board (the gate's precondition 6 requires it against the approved board task before flashing; the whole-branch review of the PR is a software review, not that one), a host reader for a Reliance Edge volume, the flash-write failure seen on 2026-11-07, and the unexplained return of the supply.
