# Transactional storage on eMMC Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the unchanged `crux/file.h` and `crux/configuration.h` consumers on Reliance Edge on an eMMC disk, verified on `native_sim` and by an RT1060 link, before any board.

**Architecture:** Reliance Edge is a Zephyr VFS file system behind a new adapter. A test-only SD host driver lets the unmodified MMC stack run on `native_sim`. A build-local patch adds the missing eMMC cache flush. A host driver replays recorded writes to prove power-cut invariants, with negative controls.

**Tech Stack:** C17, Zephyr v4.4.2, Ztest and Twister on `native_sim`, Python 3 `unittest`, west, Reliance Edge v3.0.

**Spec:** [docs/plans/emmc-reliance-design.md](../../plans/emmc-reliance-design.md). Read it first. F, D, I and W numbers below refer to it.

## Global Constraints

- Pins: Zephyr `dccb09599635bdff17633fa7e9dab014b91dce90`; Reliance Edge `6706984258c5bb61c778fe8ca2edd04b559efb78` (v3.0). Plan baseline: main `a80acd7`.
- This work does not change `crux/file.h` or `crux/configuration.h`, and adds no `crux/` public header. The File additions already on `main` (roots, stat, size, rename, listing) must work on this file system.
- No Reliance Edge source is copied or derived. Write the adapter from the interface in `redosserv.h` and `redposix.h`. `git grep 'Tuxera US Inc' -- zephyr components samples tests scripts patches` must find nothing.
- No product information and no personal path in any file, commit, issue or pull request.
- No image built with Reliance Edge is uploaded by CI or stored under `docs/evidence/emmc-reliance/`.
- Disk name `MMC`, mount `/MMC:`, directories `/MMC:/files` and `/MMC:/settings`.
- Policy: no automatic format, no directory creation, `bBlockIoRetries` 0, `REDCONF_ASSERTS` 0, `fAtomicSectorWrite` false.
- Protected paths follow `AGENTS.md`. New scripts are allowed within the task. Editing an existing protected file, including `.github/workflows/zephyr.yml`, needs the owner's explicit approval first. `scripts/test-storage-emmc.sh` and `scripts/test-file-emmc-compile.sh` are this work's own runners once created: later tasks may add suites to them and never remove one. Accepted evidence stays unchanged: each new result is a new record under `docs/evidence/emmc-reliance/<topic>/`.
- TDD: write the failing test and see it fail for the expected reason before the code. Never weaken an existing test.
- Where checks run: Zephyr commands run in a Linux container unless a step says otherwise. Local `native_sim` runs are 64-bit and at most 4 jobs wide. The Dev Container image has no 32-bit libraries, so the 32-bit runs, `scripts/test-storage-simulators.sh` as a whole, and the full Host checks run in CI on the Draft PR. Before a branch can be pushed, run the named 64-bit subset and record that the rest is pending.
- Target alignment: this work's File selection sets `SDHC_BUFFER_ALIGNMENT` to 32 and keeps the D-cache check, as the RT1060 selections do (spec F17).
- Physical programming, reset and power operations are out of this plan.
- Style: `.clang-format` for owned C, `scripts/check-format.sh` before each commit, Conventional Commits, and this attribution on each commit: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

**Delivery:** one branch and Draft PR per issue. `CONTRIBUTING.md` prefers a branch from the default branch; these are stacked on purpose, each on the previous issue's branch until that one merges, because each depends on the last, and each is rebased when its base merges. Merging needs the owner. Each PR records its protected paths, checks and independent review as `AGENTS.md` requires. The issue numbers below are the design's numbers 1 to 8; replace them with GitHub numbers once created. Issue 9 is a planning item with no task here.

## Review Focus

Each line has its test in the owning task.

1. A volume configured at a sector offset beside another region: no write leaves its range (`test_writes_stay_inside_the_configured_sector_range`, Task 7).
2. A medium formatted with a different profile: the mount fails and the medium is unchanged, not reformatted (`test_mismatched_profile_mount_fails_and_digest_is_unchanged`, Task 7).
3. Unmounting with a file still open: refused, and FatFs habits do not carry over (`test_unmount_with_an_open_handle_is_refused`, Task 5).
4. The mount point used as a path, as `crux_file_zephyr_bind` and the layout check do with its children: `fs_stat` and `fs_opendir` on `/MMC:` work (`test_mount_point_itself_is_the_root`, Task 5).
5. A second mount of the one volume: rejected without disturbing the first (`test_second_mount_is_rejected`, Task 5).

---

### Task 0: Isolated build workspace

No commit. Other sessions share the existing workspace and its Docker, so nothing here changes them.

- [ ] **Step 1: Check the free space on the host**

Check the host's own free space, not `df` inside a container, because Docker's disk shares it.
Expected: at least 10 GiB free. Below that, start no new heavy Docker work and ask the owner. Below 5 GiB, also stop any running build and tell the owner. Filling the disk breaks Docker for every session. A comparable workspace is about 3.3 GiB with its build output.

- [ ] **Step 2: Create a workspace beside the repository, as a clone rather than a worktree**

A worktree's `.git` file names a host path that does not exist in the container. Clone with `--reference <existing checkout> --dissociate`, fetch `docs/emmc-reliance-design`, and set `origin` to the GitHub remote.

- [ ] **Step 3: Initialise it in the Dev Container image**

Bind-mount the workspace at `/workspaces/crux`, use the environment from `.devcontainer/devcontainer.json`, and mount the existing workspace read-only for `west update --path-cache` so that objects are not downloaded twice. Run `west init -l crux-pilot`, `west update`. First try a native-architecture container for `native_sim/native/64`, because emulation is slow; if it does not build there, use the amd64 image and keep runs narrow.

Expected: `git -C zephyr rev-parse HEAD` prints the Zephyr pin. Recheck free space before each cross-build.

---

### Task 1: Record the design and the licence boundary (issue 1)

**Files:**
- Modify: `west.yml`, `tests/build/test_ssh_boundaries.py:149`
- Already committed: `docs/plans/emmc-reliance-design.md`, this plan

**Interfaces:**
- Produces: west project `reliance-edge`, group `crux-reliance`, path `modules/fs/reliance-edge`.

- [ ] **Step 1: Show the project is absent**

Run: `west list reliance-edge`
Expected: an error, because the project is unknown.

- [ ] **Step 2: Add the project**

Add remote `tuxera` (`https://github.com/tuxera`), add `-crux-reliance` to `group-filter`, and add project `reliance-edge` with `revision: 6706984258c5bb61c778fe8ca2edd04b559efb78 # v3.0`, `path: modules/fs/reliance-edge`, `groups: [crux-reliance]`. Comment that the group is off by default so that an ordinary workspace does not fetch this GPLv2 source, as the secure management comment does. `group-filter` becomes `[-crux-secure-management, -crux-reliance]`, so the literal that `test_group_on_by_default` edits must follow it: update only that literal, keeping its assertion.

- [ ] **Step 3: Verify both states**

Run: `west update && test ! -e ../modules/fs/reliance-edge && echo absent`
Expected: `absent`.
Run: `west config manifest.group-filter -- +crux-reliance && west update reliance-edge && git -C ../modules/fs/reliance-edge rev-parse HEAD`
Expected: the Reliance Edge pin. Leave the group enabled in this workspace only.
Run: `python3 -m unittest discover -s tests/build -p 'test_ssh_boundaries.py'`, and the same for `test_crux_pins.py`, `test_product_template.py` and `test_zephyr_baseline.py`.
Expected: PASS, including `test_group_on_by_default`, which must still fail the check when the group filter is emptied.

- [ ] **Step 4: Commit, push and open the Draft PR**

```bash
git add west.yml tests/build/test_ssh_boundaries.py
git commit -m "build: pin Reliance Edge in an optional manifest group"
```

The PR body records the review of the design from the review record, and the owner's approval of the spec.

---

### Task 2: eMMC simulator and MMC stack tests (issue 2)

**Files:**
- Create: `tests/support/emmc_sim/{emmc_sim.c,emmc_sim.h,emmc_sim_bottom.c,emmc_sim_bottom.h,emmc_sim.cmake,Kconfig}`, `tests/support/emmc_sim/dts/bindings/crux,emmc-sim.yaml`
- Create: `tests/native/emmc_stack/{CMakeLists.txt,Kconfig,prj.conf,app.overlay,main.c,testcase.yaml}`
- Create: `tests/native/emmc_restart/{CMakeLists.txt,Kconfig,prj.conf,app.overlay,main.c,run_restart.py}`
- Create: `scripts/test-storage-emmc.sh`
- Modify: `tests/README.md`

**Interfaces:**
- Produces, in `emmc_sim.h`:

```c
enum emmc_sim_op { EMMC_SIM_OP_READ, EMMC_SIM_OP_WRITE, EMMC_SIM_OP_FLUSH };
/* Fail the nth operation (1-based) for `attempts` consecutive tries; UINT32_MAX is a hard fault. */
void emmc_sim_fail(enum emmc_sim_op op, uint32_t nth, uint32_t attempts);
void emmc_sim_fail_command(uint8_t opcode, uint32_t nth, uint32_t attempts);
void emmc_sim_reject_next_switch(void);        /* next CMD6 sets status bit 7 */
void emmc_sim_status_not_ready(uint32_t count); /* next `count` CMD13 report not ready */
void emmc_sim_hold_busy(bool busy);            /* sdhc_card_busy reports busy now */
/* Armed now, taking effect only after the next CMD6, so that a wait before the flush does not use
 * them up. Bit 7 from a rejected switch appears in the first CMD13 after that CMD6. */
void emmc_sim_busy_after_switch(bool forever);
void emmc_sim_not_ready_after_switch(uint32_t count);
void emmc_sim_clear_faults(void);
void emmc_sim_set_cache_kb(uint32_t kb);       /* takes effect at the next card init */
struct emmc_sim_stats {
    uint32_t reads, writes, flush_commands, status_reads;
    uint32_t misaligned_transfers; /* data buffers not aligned to CONFIG_SDHC_BUFFER_ALIGNMENT */
    bool cache_on;
};
void emmc_sim_get_stats(struct emmc_sim_stats *out);
void emmc_sim_reset_stats(void);
int emmc_sim_read_persistent(uint32_t sector, void *out, uint32_t count);
void emmc_sim_trace_mark(uint32_t id);
```

- Produces, for test applications: `include(<path>/emmc_sim.cmake)` then `crux_emmc_sim_add(app)`; `list(APPEND DTS_ROOT <crux>/tests/support/emmc_sim)` before `find_package(Zephyr)`; the test's `Kconfig` has `rsource` of the simulator's `Kconfig` and `source "Kconfig.zephyr"`.
- Produces, Kconfig: `CONFIG_CRUX_EMMC_SIM` (default y, `depends on DT_HAS_CRUX_EMMC_SIM_ENABLED && BOARD_NATIVE_SIM`, `select SDHC_SUPPORTS_NATIVE_MODE`), `CONFIG_CRUX_EMMC_SIM_CACHE_KB` (default 64), `CONFIG_CRUX_EMMC_SIM_SECTORS` (default 8192); `configdefault SDHC_BUFFER_ALIGNMENT` `default 32`, the value the RT1060 selections use, so that the bounce path runs here too.
- Produces, command line of the simulator: `--emmc-image=PATH`, `--emmc-trace=PATH`, `--emmc-cache-kb=N`, registered with `NATIVE_TASK(…, PRE_BOOT_1, …)` as `drivers/flash/flash_simulator.c` does. An application's own options, such as a phase, are registered in its `main.c`, as `tests/native/file_fatfs_persist/main.c` does.
- Produces, trace format: little-endian records, header `uint32_t type, sector, count, id`, followed by `count × 512` bytes for type 1. Types: 1 write, 2 flush, 3 cache on, 4 mark.

**Model values, from the stack's parser at the pin:**

| Register | Value |
| --- | --- |
| OCR, CMD1 | `MMC_OCR_PWR_BUSY_FLAG`, `MMC_OCR_SECTOR_MODE`, `MMC_OCR_VDD27_36FLAG` |
| CSD, CMD9 | `csd_structure` 3; `xfer_rate` `MMC_MAXFREQ_10MHZ \| MMC_MAXFREQ_MULT_26` |
| EXT_CSD | bytes 215:212 sector count; 196 `0x03` (HS 52, HS 26); 192 rev 8; 252:249 cache size in KiB; 183, 185, 187, 33 as last switched; 32 reads 0 |
| CMD13 | ready bit 8, state TRAN in bits 12:9, bit 7 from a rejected switch, cleared by this read |
| CMD8 | with data: 512 bytes of EXT_CSD. Without data: `-ETIMEDOUT`, the SD probe |
| Host | `bus_4_bit_support` and 8-bit caps true, `vol_330_support` true, `vol_180_support` false, `hs200_support` false, `is_spi` false, `f_min` 400 kHz, `f_max` 52 MHz, `power_delay` 1 |

- [ ] **Step 1: Write `tests/native/emmc_stack/main.c`**

Suite `emmc_stack`, medium disk `MMC`. The app overlay has a `crux,emmc-sim` node with a `zephyr,mmc-disk` child (`disk-name = "MMC"`, `bus-width = <4>`). `prj.conf`: `CONFIG_ZTEST=y`, `CONFIG_DISK_ACCESS=y`, `CONFIG_SD_DATA_TIMEOUT=100`. Each case resets stats and faults.

```c
ZTEST(emmc_stack, test_init_reaches_ready)          /* disk_access_init ok; status DISK_STATUS_OK */
ZTEST(emmc_stack, test_geometry_matches_the_model)  /* count 8192, size 512 */
ZTEST(emmc_stack, test_single_and_multi_block_roundtrip)
ZTEST(emmc_stack, test_unaligned_buffer_roundtrip)
ZTEST(emmc_stack, test_out_of_range_is_rejected)    /* last sector + 2: -EINVAL, no write counted */
ZTEST(emmc_stack, test_transient_write_fault_is_retried) /* attempts 1: returns 0 */
ZTEST(emmc_stack, test_hard_write_fault_returns_nonzero) /* zassert_not_equal(rc, 0) */
ZTEST(emmc_stack, test_hard_read_fault_returns_nonzero)
ZTEST(emmc_stack, test_busy_past_the_timeout_returns_error)
ZTEST(emmc_stack, test_cache_state_follows_the_profile)
    /* stats.cache_on == (CONFIG_CRUX_EMMC_SIM_CACHE_KB > 0) */
ZTEST(emmc_stack, test_reads_see_held_data)
    /* cached: read returns new data while emmc_sim_read_persistent returns old.
       Without a cache: both return new data. */
ZTEST(emmc_stack, test_transfers_are_aligned_for_dma)
    /* BUILD_ASSERT(CONFIG_SDHC_BUFFER_ALIGNMENT == 32); after aligned and unaligned reads and
       writes: misaligned_transfers == 0 */
ZTEST(emmc_stack, test_sync_sends_no_flush_at_the_pin)
    /* cached, option off: write, CTRL_SYNC returns 0, flush_commands == 0, persistent is old.
       Without a cache: persistent is new, flush_commands == 0. */
```

`testcase.yaml`: `crux.emmc.stack.cached`; `crux.emmc.stack.nocache` with `extra_configs: [CONFIG_CRUX_EMMC_SIM_CACHE_KB=0]`. `platform_allow: [native_sim/native, native_sim/native/64]`.

- [ ] **Step 2: See it fail**

Run: `west twister -p native_sim/native/64 -T crux-pilot/tests/native/emmc_stack -O crux-pilot/build/zephyr/twister-emmc-stack --inline-logs`
Expected: a configuration failure: the test's CMake includes `emmc_sim.cmake`, which does not exist yet.

- [ ] **Step 3: Implement the simulator**

`emmc_sim.c` with `DT_DRV_COMPAT crux_emmc_sim`, implementing `request`, `set_io`, `get_card_present`, `card_busy`, `get_host_props`, `reset`. Persistent state and a same-sized shadow with a per-sector dirty bitmap are static RAM. With the cache on, a write goes to the shadow, a read prefers it, and `FLUSH_CACHE` moves dirty sectors to persistent state. CMD6 accepts bytes 183, 185, 187, 33 and 32 and rejects others through status bit 7. Each injected transfer fault honours `cmd->retries`, attempting `retries + 1` times as `imx_usdhc.c` does. Any other command returns `-ENOTSUP`. `emmc_sim_bottom.c`, added with `target_sources(native_simulator INTERFACE …)`, holds host `open`, `pread`, `pwrite` and `write`; the image is loaded at boot and updated whenever persistent state changes.

- [ ] **Step 4: See it pass**

Run: the command in Step 2.
Expected: both scenarios PASS with 13 executed cases each. If the stack sends a command the model refuses, read why before adding it, and record the addition.

- [ ] **Step 5: Write the restart test**

`tests/native/emmc_restart/main.c` has phases `write` and `verify`, selected by its own `--phase` option. Its `prj.conf` sets `CONFIG_NATIVE_SIM_SLOWDOWN_TO_REAL_TIME=n`, because without Ztest the default is real time. `run_restart.py --exe PATH`:

- `test_flushed_data_survives_a_process_restart`: write with sync, new process reads it back.
- `test_unflushed_cache_does_not_survive_exit`: cached profile, write without flush, new process reads the old bytes.
- `test_image_size_mismatch_is_an_error`: an image of the wrong size makes the process exit non-zero.

- [ ] **Step 6: Create `scripts/test-storage-emmc.sh`**

Refuse to run outside Linux, as `scripts/test-file-persistence.sh` does. Run the Twister suite, then `run_restart.py`, then a Python check in the style of `scripts/test-storage-simulators.sh` that requires `{'native_sim/native/64': 13}` for each scenario, adding `native_sim/native` when `CRUX_STORAGE_32BIT=1`.

- [ ] **Step 7: Verify and commit**

Run: `scripts/test-storage-emmc.sh && scripts/check-format.sh`
Expected: PASS. Add a section to `tests/README.md` stating that the simulator is a model and is not device evidence.

```bash
git add tests/support/emmc_sim tests/native/emmc_stack tests/native/emmc_restart scripts/test-storage-emmc.sh tests/README.md
git commit -m "test: add an eMMC simulator and MMC stack tests on native_sim"
```

---

### Task 3: eMMC cache flush correction (issue 3)

**Files:**
- Create: `patches/zephyr/mmc-cache-flush.patch`, `scripts/prepare-mmc-cache-flush.py`, `zephyr/mmc_cache_flush.cmake`, `tests/native/emmc_stack/test_prepare.py`
- Modify: `zephyr/Kconfig`, `zephyr/CMakeLists.txt`, `patches/zephyr/README.md`, `tests/native/emmc_stack/main.c`, `tests/native/emmc_stack/testcase.yaml`, `scripts/test-storage-emmc.sh`, `tests/README.md`

**Interfaces:**
- Consumes: the Task 2 control API.
- Produces: `CONFIG_CRUX_MMC_CACHE_FLUSH` (bool, default n, `depends on MMC_STACK`).

- [ ] **Step 1: Write the failing tests**

Compile under `#if defined(CONFIG_CRUX_MMC_CACHE_FLUSH)`. Compile `test_sync_sends_no_flush_at_the_pin` under the opposite condition, unchanged, so that it stays as the control.

```c
ZTEST(emmc_stack, test_sync_flushes_cached_writes)
    /* cached: write; persistent old; CTRL_SYNC 0; flush_commands == 1; persistent new */
ZTEST(emmc_stack, test_sync_without_cache_sends_no_flush)      /* nocache: flush_commands == 0 */
ZTEST(emmc_stack, test_rejected_flush_returns_eio)
    /* reject_next_switch; CTRL_SYNC == -EIO; flush_commands == 1; next CTRL_SYNC == 0 */
ZTEST(emmc_stack, test_switch_error_in_any_status_response_is_detected)
    /* reject_next_switch + not_ready_after_switch(2): bit 7 is in the first CMD13 after the CMD6,
       while the card is not yet ready, and clears; -EIO; flush_commands == 1 */
ZTEST(emmc_stack, test_flush_command_failure_returns_error)    /* fail_command(6,…): nonzero */
ZTEST(emmc_stack, test_busy_before_the_flush_returns_error_and_sends_none)
    /* hold_busy(true): CTRL_SYNC == -EBUSY, the value of the wait in step 1; flush_commands == 0 */
ZTEST(emmc_stack, test_busy_after_the_flush_returns_etimedout)
    /* busy_after_switch(true): -ETIMEDOUT; flush_commands == 1 */
ZTEST(emmc_stack, test_status_never_ready_after_the_flush_returns_etimedout)
    /* not_ready_after_switch(UINT32_MAX): -ETIMEDOUT; flush_commands == 1 */
ZTEST(emmc_stack, test_reinit_without_cache_clears_the_flag)
    /* set_cache_kb(0), DEINIT, INIT, reset stats (DEINIT itself flushed), write, sync:
       flush_commands == 0; then restore the profile and reinit, for later cases */
```

Add scenarios `crux.emmc.stack.flush.cached` and `.flush.nocache` with `CONFIG_CRUX_MMC_CACHE_FLUSH=y`. Cases that need the other profile use `ztest_test_skip()`, and the gate's expected counts name executed cases, not skips.

`test_prepare.py`, in the style of `tests/native/configuration_settings_file/test_prepare.py`: a source with one changed byte fails; an output path inside Zephyr fails; a good run writes the file and its `.json`.

- [ ] **Step 2: See them fail**

Run: the Twister command for the flush scenarios.
Expected: a Kconfig or CMake error, because the option does not exist.

- [ ] **Step 3: Write the patch**

In a scratch copy of pinned `subsys/sd/mmc.c`: define a private flag `MMC_CACHE_ON_FLAG BIT(15)` for `card->flags`, set by `mmc_set_cache` when it enables the cache. Define `mmc_flush_cache(struct sd_card *card)` with D3's four steps and a final clause that any failure returns an error. `mmc_ioctl` calls it for `DISK_IOCTL_CTRL_SYNC` when the flag is set, and otherwise behaves as today. Define `MMC_SWITCH_FLUSH_CACHE_ARG` in the file's existing style with index 32 and value 1, and `MMC_R1_SWITCH_ERROR BIT(7)`. Step 3 polls `sdhc_card_busy` with `k_busy_wait(125)` up to `CONFIG_SD_DATA_TIMEOUT`. Step 4 sends CMD13 directly through `sdhc_request`.

- [ ] **Step 4: Write the wiring**

`prepare-mmc-cache-flush.py`, modelled on `scripts/prepare-nxp-mdio.py`, with both hashes computed from the pinned source and the patched result. `zephyr/mmc_cache_flush.cmake`, modelled on `samples/controller/zephyr/mdio_completion.cmake`, targets `subsys__sd`, requires exactly one `subsys/sd/mmc.c` in it, adds `${ZEPHYR_BASE}/subsys/sd` to its private include directories, and gives the generated file `-Wall;-Wextra;-Werror`. `zephyr/CMakeLists.txt` includes it under `if(CONFIG_CRUX_MMC_CACHE_FLUSH)`.

- [ ] **Step 5: See them pass**

Run: `scripts/test-storage-emmc.sh` extended with the two scenarios and `python3 tests/native/emmc_stack/test_prepare.py`.
Expected: flush cases PASS; the control scenarios still PASS `test_sync_sends_no_flush_at_the_pin`; `git -C ../zephyr diff --exit-code -- subsys/sd/mmc.c` is clean. The script's expected counts name executed cases per scenario, and are set from this first passing run.

- [ ] **Step 6: Mutation check, recorded in the PR**

Replace Step 3 of the flush with `sdmmc_wait_ready` in a scratch copy, and rebuild. Expected: `test_rejected_flush_returns_eio` and `test_switch_error_in_any_status_response_is_detected` FAIL. Discard the change.

- [ ] **Step 7: Commit**

Add a section to `patches/zephyr/README.md` in the style of the MDIO one: what, why, scope, limits, and that an upstream change is intended and needs approval.

```bash
git add patches/zephyr scripts/prepare-mmc-cache-flush.py zephyr tests/native/emmc_stack scripts/test-storage-emmc.sh tests/README.md
git commit -m "fix(storage): flush the eMMC cache on disk sync through an opt-in patch"
```

---

### Task 4: Power-cut replay on FatFs (issue 4)

**Files:**
- Create in `tests/native/emmc_powercut/`: `CMakeLists.txt`, `Kconfig`, `prj.conf`, `app.overlay`, `fatfs.conf`, `main.c`, `workloads.c`, `workloads.h`, `fs_backend.h`, `fs_fatfs.c`, `replay_model.py`, `test_replay_model.py`, `run_replay.py`
- Modify: `scripts/test-storage-emmc.sh`, `tests/README.md`

**Interfaces:**
- Consumes: `emmc_sim_trace_mark`, `--emmc-*` options, the trace format.
- Produces, `fs_backend.h`: `int powercut_fs_format(void); int powercut_fs_mount(void); int powercut_fs_unmount(void);`, mount `/MMC:`.
- Produces, `workloads.h`:

```c
enum powercut_workload { POWERCUT_W1, POWERCUT_W2, POWERCUT_W3, POWERCUT_W4 };
int powercut_prepare(enum powercut_workload w);          /* baseline content, on the no-cache profile */
int powercut_run(enum powercut_workload w, bool early_ack);
struct powercut_result { bool mounted, data, read_error, replaced, untouched, recommit; };
void powercut_verify(enum powercut_workload w, uint32_t acked, struct powercut_result *out);
```

- Produces, the application's own options in `main.c`: `--phase=provision|run|run-early|verify`, `--workload=W1`, `--acked=N`. `verify` prints exactly one line: `POWERCUT mount=0|1 data=0|1 read_error=0|1 replace=0|1 untouched=0|1 recommit=0|1`. `read_error=1` means a read of a file that should exist returned an error other than not-found, as distinct from a stale value.
- Produces, `replay_model.py`:

```python
SECTOR = 512
Event = namedtuple('Event', 'kind sector data mark')   # 'write' is per sector after expansion
def parse_trace(raw: bytes) -> list[Event]
def mandatory_positions(events, metaroot_sectors=frozenset()) -> list[int]
def build_image(baseline: bytes, events, position: int, model: str, seed: int,
                ignored_flushes=frozenset(), keep_sectors=frozenset()) -> bytes
def acked_at(events, position: int) -> int
```

Models are `lost`, `prefix`, `reordered`, `torn`. A write is only ever applied whole, except for the torn sector.

**Workloads, deterministic from the step number:** W1 appends fixed-size records to `files/w1`, with a sync and a mark after each, 16 records. W2 repeats 8 times: write `settings/w2.tmp` with a generation, a length and a CRC-32, sync, close, `fs_rename` onto `settings/w2`, mark. W3 runs 12 steps over five files: create, overwrite, remove, `mkdir` and `rmdir`, each followed by a sync of what changed and a mark. An untouched file, `files/keep`, is written at provision and never again.

**Checks:** `data` (I2): every acknowledged step's result is present, allowing any later unacknowledged step's complete result. `replace` (I3): `settings/w2` exists and is one complete generation not older than acknowledged. `untouched` (I4): `files/keep` is byte-exact. `recommit` (I5): write, sync, unmount and mount succeed on the rebuilt image.

- [ ] **Step 1: Write `test_replay_model.py`, which needs no build**

```python
class ReplayModelTest(unittest.TestCase):
    def test_trace_is_expanded_to_sectors(self): ...       # a 3-sector write gives 3 events
    def test_truncated_trace_is_an_error(self): ...
    def test_everything_before_the_last_flush_is_present(self): ...  # for all four models
    def test_lost_keeps_nothing_after_the_last_flush(self): ...
    def test_prefix_keeps_oldest_in_order(self): ...
    def test_reordered_is_deterministic_by_seed(self): ...  # same seed equal, other seed differs
    def test_torn_mixes_only_the_next_sector(self): ...
    def test_mandatory_positions_include_both_sides_of_each_flush(self): ...
    def test_mandatory_positions_include_each_metaroot_sector(self): ...
    def test_ignored_flush_is_not_a_barrier(self): ...
    def test_keep_sectors_survive_a_reordered_draw(self): ...
    def test_acked_at_counts_marks_up_to_position(self): ...
```

- [ ] **Step 2: See it fail, then implement `replay_model.py`, and see it pass**

Run: `python3 tests/native/emmc_powercut/test_replay_model.py -v`
Expected first: an import error. Expected after: all PASS.

- [ ] **Step 3: Write the application**

`main.c` registers its options, runs the phase and exits with its status. `prj.conf` sets `CONFIG_NATIVE_SIM_SLOWDOWN_TO_REAL_TIME=n`. `fs_fatfs.c` uses `FS_FATFS` with `FS_MOUNT_FLAG_NO_FORMAT`; `fatfs.conf` has the FatFs options of `tests/native/file_sd_composition/prj.conf`, with `CONFIG_CRUX_EMMC_SIM_CACHE_KB` left at its default and the workload selected at run time.

- [ ] **Step 4: Write `run_replay.py`**

`--exe-fix PATH --exe-nofix PATH --fs fatfs|reliance --seed N --samples N --max-seconds N --out DIR`. It provisions a baseline per file system once, on `--emmc-cache-kb=0`. It runs each workload twice, once on the cached profile and once on `--emmc-cache-kb=0`, to get two traces. A no-cache trace has no flush events, so it runs only `prefix` and `torn`, its mandatory positions are the metaroot sectors, and the commit pattern check does not apply to it. For each position and model it rebuilds a 4 MiB image in place, runs `verify`, and classifies the line.

To keep the run bounded, mandatory positions run these models: both sides of a flush with `lost`, `prefix` and one `reordered` draw; each metaroot sector with `prefix`, `torn` and one `reordered` draw. In addition, `--samples` positions per workload and model are drawn by seed. The sample count alone selects positions, never the clock, so the same seed gives the same JSON apart from timing. `--max-seconds` fails the run if exceeded; it never skips work. It writes `DIR/<fs>.json` with counts, seeds and failures, and keeps the medium image of a failing case. It exits non-zero if an asserted invariant fails or a required control does not fail. For `--fs fatfs` nothing is asserted except the controls.

Controls on FatFs in this task:

- Flush control: `fat-nofix`, cached, `lost`, W1 must report `data=0` at a position after a mark, from a baseline made on the no-cache profile.
- Damaged image: `fat-fix`, a baseline with its boot sector overwritten must report `mount=0`.

- [ ] **Step 5: Build the images and run**

Add to `scripts/test-storage-emmc.sh`: build `fat-fix` (`-DEXTRA_CONF_FILE=fatfs.conf -DCONFIG_CRUX_MMC_CACHE_FLUSH=y`) and `fat-nofix` under `build/zephyr/emmc-powercut/`, with `ZEPHYR_MODULES` naming FatFs as the storage gate does. Then run `test_replay_model.py` and `run_replay.py --fs fatfs --seed 1 --samples 8 --max-seconds 300`.
Expected: both controls fail as required; observations for W1 to W3 on both profiles in the JSON; exit 0. Run twice to confirm that the same seed gives the same JSON, apart from timing. Measure the run time and set the final `--samples` in the script from it: at most 5 minutes on one CI runner, reducing samples and never mandatory positions. If only an emulated local run is available, record its time as such and pass a larger `--max-seconds` for that run, stating it; the 5 minutes applies to the CI runner. Record the environment with every result. Record the counts and the observations as a new record in `docs/evidence/emmc-reliance/replay-fatfs/README.md`, with the revision and commands.

- [ ] **Step 6: Commit**

```bash
git add tests/native/emmc_powercut scripts/test-storage-emmc.sh tests/README.md docs/evidence/emmc-reliance
git commit -m "test: add power-cut replay on the eMMC simulator with FatFs"
```

---

### Task 5: Reliance Edge port (issue 5, first part)

**Files:**
- Create in `zephyr/fs/reliance_edge/`: `CMakeLists.txt`, `Kconfig`, `fs_reliance_edge.h`, `fs_reliance_edge.c`, `os_bdev.c`, `os_task.c`, `os_mutex.c`, `os_clock.c`, `config/redconf.h`, `config/redconf.c`, `config/redtypes.h`, `config/redostypes.h`, `config/redosconf.h`
- Modify: `zephyr/Kconfig`, `zephyr/CMakeLists.txt`
- Create: `tests/native/file_reliance/{CMakeLists.txt,prj.conf,app.overlay,main.c,testcase.yaml}`

**Interfaces:**
- Consumes: `modules/fs/reliance-edge`.
- Produces, `fs_reliance_edge.h`:

```c
#define CRUX_FS_RELIANCE_EDGE FS_TYPE_EXTERNAL_BASE   /* a private composition header */
```

The mount's `storage_dev` is the disk name string, and `fs_data` is unused. `fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)"MMC", NULL, 0)` formats, only with the format option.

- Produces, Kconfig: `CONFIG_CRUX_FS_RELIANCE_EDGE` (`depends on FILE_SYSTEM && DISK_ACCESS`), `CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT` (default n, `depends on FILE_SYSTEM_MKFS`), and integers `_BUFFER_COUNT` (12), `_HANDLE_COUNT` (8), `_TASK_COUNT` (8), `_SECTOR_OFFSET` (0), `_SECTOR_COUNT` (0, automatic). The adapter has `BUILD_ASSERT(MAX_FILE_NAME >= REDCONF_NAME_MAX)`, because with only an external file system Zephyr's fallback is 12, so an application sets `CONFIG_FILE_SYSTEM_MAX_FILE_NAME=63`.
- Produces, CMake: `CRUX_RELIANCE_EDGE_DIR` (default `${ZEPHYR_BASE}/../modules/fs/reliance-edge`), `CRUX_RELIANCE_EDGE_CONFIG_DIR` (default `config/`), `CRUX_EMMC_FLUSH_CONTROL` (test fixtures only), and `CRUX_RELIANCE_EDGE_SOURCES`, one overridable list, by default for v3.0: `core/driver/{blockio,buffer,buffercmn,core,dir,format,imap,imapextern,imapinline,inode,inodedata,volume}.c`, `posix/{path,posix}.c`, `util/{bitmap,crc,endian,ftype,heap,memory,namelen,perm,sign,string}.c`, `bdev/bdev.c`.

**Configuration:** as the spec's default profile. `REDCONF_VOLUME_COUNT` 1, an empty path prefix, `REDCONF_BLOCK_SIZE` 4096, `REDCONF_BUFFER_ALIGNMENT` 32, `INODE_COUNT_AUTO`, `bBlockIoRetries` 0, `fAtomicSectorWrite` false, `ulSectorSize` `SECTOR_SIZE_AUTO` when `_SECTOR_COUNT` is 0 and 512 otherwise (a fixed count with an automatic size must equal the whole disk, `bdev/bdev.c`), `REDCONF_NAME_MAX` 63, `REDCONF_INODE_TIMESTAMPS` 0, `REDCONF_OUTPUT` 0, `REDCONF_ASSERTS` 0, `REDCONF_RENAME_ATOMIC` 1, `REDCONF_DELETE_OPEN` 0, `REDCONF_POSIX_OWNER_PERM` 0, `REDCONF_API_POSIX_CWD` 0, `REDCONF_API_POSIX_FORMAT` from the format option, `REDCONF_TRANSACT_DEFAULT` without link. Write these from `redconfigchk.h`'s requirements, not from a sample. Add `#error` checks, in `fs_reliance_edge.c`, that `REDCONF_TRANSACT_DEFAULT` includes `RED_TRANSACT_FSYNC`, and that `REDCONF_BUFFER_ALIGNMENT >= CONFIG_SDHC_BUFFER_ALIGNMENT` when SDHC is enabled.

**Guard, in the adapter's CMake:** if `CONFIG_DISK_DRIVER_MMC` is set, `CONFIG_CRUX_MMC_CACHE_FLUSH` is not, and `CRUX_EMMC_FLUSH_CONTROL` is not set on `native_sim`, then `FATAL_ERROR`. On any board other than `native_sim`, `CRUX_EMMC_FLUSH_CONTROL` is itself a `FATAL_ERROR`.

- [ ] **Step 1: Write the adapter-level failing tests in `tests/native/file_reliance/main.c`**

RAM disk named `MMC`, 8192 sectors. Link wraps of `disk_access_write`, `disk_access_read` and `disk_access_ioctl` inject faults by call count, including a positive return. Provisioning uses `fs_mkfs` with the format option on.

```c
ZTEST(file_reliance, test_blank_medium_mount_fails_and_digest_is_unchanged)
ZTEST(file_reliance, test_format_then_mount_and_unmount)
ZTEST(file_reliance, test_mount_point_itself_is_the_root)
    /* fs_stat and fs_opendir on "/MMC:" succeed; fs_stat type is DIR */
ZTEST(file_reliance, test_second_mount_is_rejected)
    /* a mount point of a different length, so that the adapter's check is reached, not the VFS's */
ZTEST(file_reliance, test_unmount_with_an_open_handle_is_refused)
ZTEST(file_reliance, test_open_flags_are_translated)
    /* read-only refuses write; create creates; missing without create is -ENOENT; append appends */
ZTEST(file_reliance, test_read_write_seek_tell_truncate_stat_statvfs)
ZTEST(file_reliance, test_mkdir_readdir_unlink)
ZTEST(file_reliance, test_rename_replaces_an_existing_file)
ZTEST(file_reliance, test_error_values_are_zephyr_errno)
    /* ENOENT, EEXIST, ENOSPC, EROFS, EBUSY, ENOTEMPTY compared with Zephyr's own values */
```

- [ ] **Step 2: See them fail**

Run: `west twister -p native_sim/native/64 -T crux-pilot/tests/native/file_reliance -O crux-pilot/build/zephyr/twister-file-reliance --inline-logs`
Expected: a Kconfig failure: the option does not exist.

- [ ] **Step 3: Implement the port**

`fs_reliance_edge.c`: `struct fs_file_system_t`, registered with `SYS_INIT` calling `fs_register(CRUX_FS_RELIANCE_EDGE, …)`. `red_init` runs once before `red_mount2` or `red_format`. `zfp->filep` points to a static slot `{int32_t fd; bool dirty;}` from a table of size `REDCONF_HANDLE_COUNT`, freed at close; NULL means no open file, and a `fs_close` that finds NULL returns `-EBADF`. A local table translates errors with a `switch`, unknown to `-EIO`. One mount at a time: a second returns `-EBUSY`. Paths: `path + mountp->mountp_len`, with an empty remainder as `/`. Sizes above `INT32_MAX` give `-EINVAL` before the call.

`os_bdev.c`: `RedOsBDevOpen` does not initialise the disk; `RedOsBDevGetGeometry` uses the ioctls; read and write check `sector + count` fits 32 bits, and map any non-zero result to `-RED_EIO`; `RedOsBDevFlush` uses `DISK_IOCTL_CTRL_SYNC`. The mount and `mkfs` set the disk name in a file-local variable; `RedOsBDevConfig` returns success and is unused.

`os_task.c`: a table of `k_tid_t` of size `REDCONF_TASK_COUNT` protected by a `k_spinlock`, returning index + 1. A thread with no slot gets a stable value beyond the table, which Reliance Edge then refuses with `RED_EUSERS` and the adapter maps to `-ENOMEM`.

`os_mutex.c`: `k_mutex`, `k_panic` on an unexpected failure, as `file_native.c` does. `os_clock.c`: the functions `redosserv.h` declares and `core.c` calls with timestamps off, doing nothing and returning success.

- [ ] **Step 4: See them pass, and verify the guard**

Run: the command in Step 2. Expected: all PASS.
Run: a build of `tests/native/emmc_stack`'s overlay with the adapter on and the flush option off. Expected: `FATAL_ERROR` naming `CONFIG_CRUX_MMC_CACHE_FLUSH`.
Run: the suite's build with `-DCRUX_RELIANCE_EDGE_DIR=/nonexistent`. Expected: a configure error that names the group `crux-reliance`.
Add to `scripts/test-storage-emmc.sh`: a check that the tree exists, failing with that message rather than skipping, and the two negative builds above as gate steps.

- [ ] **Step 5: Commit**

```bash
git add zephyr tests/native/file_reliance scripts/test-storage-emmc.sh
git commit -m "feat(storage): add a Reliance Edge file system to the Zephyr VFS"
```

---

### Task 6: Failure paths of the adapter (issue 5, second part)

**Files:**
- Modify: `zephyr/fs/reliance_edge/fs_reliance_edge.c`, `tests/native/file_reliance/main.c`

**Interfaces:**
- Consumes: the wraps from Task 5.
- Produces: the close rule, using the `dirty` field of the slot from Task 5.

- [ ] **Step 1: Write the failing tests**

```c
ZTEST(file_reliance, test_block_write_failure_makes_the_volume_read_only)
    /* fault on a commit: sync is an error; a later write gives -EROFS */
ZTEST(file_reliance, test_block_read_failure_makes_the_volume_read_only)
    /* the read returns an error; a later write gives -EROFS; a later read of another file works */
ZTEST(file_reliance, test_flush_failure_reports_and_remount_holds_a_complete_transaction)
    /* after remount, file content equals either the previous or the failed commit, whole */
ZTEST(file_reliance, test_positive_disk_result_is_a_failure)
ZTEST(file_reliance, test_close_with_unsaved_writes_on_read_only_volume_reports_erofs)
    /* two handles: A writes; B's commit fails; fs_close(A) == -EROFS. A is released: a second
       fs_close returns -EBADF, and after a remount HANDLE_COUNT files can be opened */
ZTEST(file_reliance, test_close_after_a_good_sync_on_read_only_volume_reports_ok)
ZTEST(file_reliance, test_close_with_only_reads_on_read_only_volume_reports_ok)
ZTEST(file_reliance, test_remount_after_a_critical_error_restores_writing)
ZTEST(file_reliance, test_read_only_mount_refuses_writes_and_never_writes_the_disk)
```

- [ ] **Step 2: See them fail**

Run: the Task 5 command.
Expected: `test_close_with_unsaved_writes_on_read_only_volume_reports_erofs` FAILS with result 0, showing F16. Any other failure is read before continuing.

- [ ] **Step 3: Implement the close rule**

Set the flag in `write` and `truncate`, clear it on successful `sync`. In `close`: if it is set, call `red_fsync`, then `red_close` whatever the result, and return the first failure.

- [ ] **Step 4: See them pass, and commit**

Run: the Task 5 command. Expected: all PASS.

```bash
git add zephyr/fs/reliance_edge tests/native/file_reliance
git commit -m "fix(storage): report unsaved writes when closing on a read-only volume"
```

---

### Task 7: File provider on Reliance Edge (issue 5, third part)

**Files:**
- Modify: `zephyr/Kconfig`, `zephyr/CMakeLists.txt:92-100`, `components/file/ports/zephyr/CMakeLists.txt`, `components/file/ports/zephyr/file_native.c:10-37`, `docs/file-contract.md`, `tests/README.md`
- Modify: `tests/native/file_reliance/{CMakeLists.txt,main.c,prj.conf,testcase.yaml}`, `scripts/test-storage-emmc.sh`. The CMake adds `tests/file/conformance.c` and links `crux::file`

**Interfaces:**
- Consumes: `crux_file_zephyr_bind(const char *root)`, `crux::file`.
- Produces: `CONFIG_CRUX_FILE_RELIANCE_EDGE` (`depends on CRUX_PILOT_MODULE && CRUX_FS_RELIANCE_EDGE`), on any board, mutually exclusive with the other three File selections and the compile check, enforced in Kconfig, CMake and with `#error`, as the existing ones are.

- [ ] **Step 1: Write the failing tests**

Ztest runs cases in name order and the File provider binds once per process, so choose names or a setup function that make the order work, as `tests/native/file_fatfs/main.c` does. Add the File-level cases, with the same behaviour as that file, except that names are not encoded and can be seen on the medium:

`test_maximum_length_name_roundtrip`, `test_distinct_portable_names_do_not_alias`, `test_create_replace_truncates_and_data_is_visible_after_in_process_remount`, `test_read_write_seek_and_eof_bounds`, `test_access_mapping_handle_lifetime_and_remove`, `test_file_handle_pool_limit_and_reuse`, `test_unmounted_volume_is_not_ready`, `test_read_write_mode_updates_in_place_and_never_creates`, `test_short_read_at_eof_returns_ok_with_the_available_count`, `test_storage_full_is_observable_and_space_is_reclaimed`, `test_write_that_only_partly_fits_returns_partial_with_progress`, `test_creating_a_file_on_a_full_volume_reports_no_space_not_access`, `test_corrupted_media_is_not_formatted_and_recovers_when_restored`, `test_bind_rejects_invalid_roots_and_a_second_bind`, `test_seek_to_eof_is_allowed_and_a_rejected_seek_keeps_the_position`, `test_removing_a_missing_file_reports_not_found`, `test_file_api_uses_a_separate_settings_directory`, `test_creating_empty_files_on_an_exhausted_volume_reports_no_space`.

`test_creating_empty_files_on_an_exhausted_volume_reports_no_space` keeps its `NO_SPACE` assertion, but not FatFs's `created > 0`, because here the first create may already find no free block. `test_torn_sync_images_mount_and_leave_unrelated_files_intact` is not repeated, because Task 8's replay covers it at more positions, with I1 and I4 under `torn`.

The File additions, as `tests/native/file_roots/main.c` runs them, with `tests/file/conformance.c` compiled in and an environment whose roots are `/MMC:/a` and `/MMC:/b` and whose foreign files are names that fail the name rule: `test_conformance_on_reliance_edge` (the six `crux_file_conformance_*` functions), `test_a_rename_survives_a_remount_and_never_leaves_two_names`, `test_rename_keeps_an_existing_destination`, `test_listing_skips_directories_and_foreign_names`, `test_an_unmounted_volume_is_not_ready`.

Plus:

```c
ZTEST(file_reliance, test_names_are_stored_unencoded)      /* fs_stat of the plain name succeeds */
ZTEST(file_reliance, test_access_after_a_critical_error_reports_access)
ZTEST(file_reliance, test_task_ids_are_unique_across_threads)
    /* several threads each record RedOsTaskId(): all values are non-zero and distinct. Opening a
       file would not show this, because threads sharing an ID share a slot and both still work */
ZTEST(file_reliance, test_thread_limit_reports_no_resources)
    /* one thread beyond the slots: CRUX_FILE_ERR_NO_RESOURCES; earlier threads still work. Runs last
       or in its own scenario, because exited threads keep their slots */
ZTEST(file_reliance, test_mismatched_profile_mount_fails_and_digest_is_unchanged)
    /* change the formatted medium's block size field and CRC, as a foreign profile would */
ZTEST(file_reliance, test_writes_stay_inside_the_configured_sector_range)
    /* scenario with SECTOR_OFFSET=128, SECTOR_COUNT=4096: min and max written sectors in range */
```

Add the scenario `crux.file.reliance.offset`. Set `CONFIG_CRUX_FILE_RELIANCE_EDGE=y` and `CONFIG_FILE_SYSTEM_MAX_FILE_NAME=63`.

`RED_ENFILE`, no inode slot left, maps to `-ENOSPC`, so an exhausted volume reports `NO_SPACE` for an empty file too (spec, "Volume full"). `test_error_values_are_zephyr_errno` gains this case.

- [ ] **Step 2: See them fail**

Expected: a Kconfig failure.

- [ ] **Step 3: Implement the selection**

Add the Kconfig symbol. Add it to the two conditions in `zephyr/CMakeLists.txt`. In the port's CMake, add a block that builds `crux_file_zephyr` and returns, including the simulator's `-m32`, `-m64` and `TOOLCHAIN_C_FLAGS` handling on `native_sim`. In `file_native.c`, treat the selection as allowed on any board, and do not define `CRUX_FILE_NATIVE_FATFS`. Add, for this selection: `BUILD_ASSERT(CONFIG_FILE_SYSTEM_MAX_FILE_NAME >= CRUX_FILE_NATIVE_LEAF_MAX)`; a check that `REDCONF_HANDLE_COUNT` covers `CRUX_FILE_POOL_SIZE + CRUX_FILE_DIR_POOL_SIZE`, through a Kconfig minimum or a CMake check, because `file_native.c` does not include Reliance Edge headers; and the existing `CONFIG_DCACHE` alignment `_Static_assert`, moved so that it covers this selection too, under `CONFIG_SDHC`, because the symbol exists only there and this selection is valid on a board without SDHC. Add this selection to the condition of `configdefault SDHC_BUFFER_ALIGNMENT` in `zephyr/Kconfig`, leaving the FatFs window line as it is, and add `crux_file_api`'s `CRUX_FILE_POOL_SIZE=4` definition in the new CMake block, as the others have.

- [ ] **Step 4: See them pass, and record the case counts**

Run: the Task 5 command.
Expected: all PASS, with no FatFs behaviour in the image: `nm zephyr.exe | grep -c f_mount` prints 0.
Run, for the existing selections: 64-bit Twister on `tests/native/file_fatfs`, `file_roots`, `file_littlefs` and `file_sd_composition`, and `scripts/test-file-rt1060-compile.sh`. Expected: the same counts as `scripts/test-storage-simulators.sh` requires. The whole gate runs in CI once the branch can be pushed.
Add both scenarios to `scripts/test-storage-emmc.sh` with exact expected counts. Add a short "Reliance Edge mapping" section to `docs/file-contract.md`, stating that the contract's statements are unchanged, and correct its sentence that the additions have run on FatFs only.

- [ ] **Step 5: Commit**

```bash
git add zephyr components/file/ports/zephyr tests/native/file_reliance scripts/test-storage-emmc.sh docs/file-contract.md tests/README.md
git commit -m "feat(file): select the File provider on Reliance Edge"
```

---

### Task 8: Power-cut replay on Reliance Edge (issue 5, last part)

**Files:**
- Create: `tests/native/emmc_powercut/{fs_reliance.c,reliance.conf}`
- Modify: `tests/native/emmc_powercut/{CMakeLists.txt,run_replay.py,replay_model.py,test_replay_model.py}`, `scripts/test-storage-emmc.sh`, `tests/README.md`

**Interfaces:**
- Consumes: Tasks 3 to 7.
- Produces: `metaroot_sectors(offset: int = 0) -> frozenset[int]`, `flushes_before_metaroot(events) -> frozenset[int]` in `replay_model.py`.

`BLOCK_NUM_FIRST_METAROOT` and the block size give the sectors. On a cached trace the driver confirms each commit's pattern, flush, metaroot sectors, flush, and fails loudly if it is not found, because another layout would silently void the barrier control.

- [ ] **Step 1: Write the failing tests**

```python
def test_metaroot_sectors_cover_both_blocks(self): ...
def test_flushes_before_metaroot_finds_the_first_of_each_pair(self): ...
def test_unexpected_commit_pattern_is_an_error(self): ...
```

And in `run_replay.py`, `--fs reliance` asserts I1 to I5 and `read_error=0` on `red-fix` for W1 to W3 on both the cached and the no-cache traces, and requires these controls:

| Control | Run | Required |
| --- | --- | --- |
| Reliance Edge flush | `red-nofix`, cached, `lost`, W1 | `data=0` after a mark |
| Barrier | `red-fix` cached trace of W1, `reordered`, `ignored_flushes=flushes_before_metaroot`, `keep_sectors=metaroot_sectors` | `mount=0`, `untouched=0` or `read_error=1`, at some position inside a barrier window. `data=0` alone does not count, because an old state explains it |
| Early acknowledgement | `red-fix`, phase `run-early`, `lost` | `data=0` |
| Damaged image | `red-fix`, a baseline with both metaroots overwritten | `mount=0` |

- [ ] **Step 2: See them fail, then implement**

`fs_reliance.c` implements `fs_backend.h`. `reliance.conf` sets the adapter, format and name options. Build `red-fix` with the correction and `red-nofix` with `-DCRUX_EMMC_FLUSH_CONTROL=ON`.

- [ ] **Step 3: Run it**

Run: `run_replay.py --fs reliance --seed 1 --samples 8 --max-seconds 300`
Expected: exit 0, with every mandatory position tested, including each metaroot sector under `torn`. Record the results as a new record in `docs/evidence/emmc-reliance/replay-reliance/README.md`. If an invariant fails on `red-fix`, this is a finding about the model, the adapter or the file system: investigate with superpowers:systematic-debugging, and do not relax the check.

- [ ] **Step 4: Verify the licence check, and commit**

Run: `git grep 'Tuxera US Inc' -- zephyr components samples tests scripts patches | wc -l`. Expected: 0.

```bash
git add tests/native/emmc_powercut scripts/test-storage-emmc.sh tests/README.md docs/evidence/emmc-reliance
git commit -m "test: assert power-cut invariants on Reliance Edge with negative controls"
```

- [ ] **Step 5: Ask the owner before publishing**

The spec requires the owner's confirmation of the licence reading before this issue's PR publishes the adapter. Stop and ask. Do not push the branch before the answer. Until then the CI gates in Global Constraints are pending and are recorded as such. After pushing, wait for CI on `scripts/test-storage-simulators.sh` and the Host checks.

---

### Task 9: Reference composition (issue 6, first part)

**Files:**
- Create: `samples/storage_emmc/{CMakeLists.txt,Kconfig,prj.conf,main.c,emmc_storage.c,emmc_storage.h,testcase.yaml}`
- Create: `tests/native/file_emmc_composition/{CMakeLists.txt,Kconfig,prj.conf,app.overlay,main.c,testcase.yaml}`
- Modify: `scripts/test-storage-emmc.sh`

**Interfaces:**
- Produces, `emmc_storage.h`, with the same shape as `sd_storage.h`: `enum emmc_storage_status { EMMC_STORAGE_READY = 0, EMMC_STORAGE_NO_MEDIA, EMMC_STORAGE_MOUNT_FAILED, EMMC_STORAGE_LAYOUT_MISSING, EMMC_STORAGE_BIND_FAILED, EMMC_STORAGE_ALREADY_STARTED }`, `enum emmc_storage_status emmc_storage_start(void); int emmc_storage_stop(void); int emmc_storage_remount(void);`. The macros `EMMC_STORAGE_MOUNT`, `EMMC_STORAGE_FILE_DIR`, `EMMC_STORAGE_SETTINGS_DIR` are fixed, and the disk name comes from `CONFIG_SAMPLE_EMMC_DISK_NAME` (default `"MMC"`).

- [ ] **Step 1: Write the failing tests on the simulator**

The same eight behaviours as `tests/native/file_sd_composition`, on `crux,emmc-sim` with the flush option on, with the digest over `emmc_sim_read_persistent`, and `stats.writes == 0` where a case requires no change, because the digest misses writes still held in the cache:

`test_0_the_medium_digest_sees_a_single_changed_byte`, `test_0_stop_and_remount_before_start_are_refused`, `test_a_blank_medium_fails_closed_and_is_never_formatted`, `test_b_missing_directories_are_not_created_by_the_firmware`, `test_c_one_missing_directory_is_still_not_ready`, `test_d_a_provisioned_medium_starts_after_earlier_failures`, `test_e_a_second_start_is_rejected_without_side_effects`, `test_f_the_provider_survives_an_application_unmount_and_remount`. The prefixes keep that fixture's order, because the provider binds once per process, and the new cases below use prefixes after `f`.

Plus:

```c
ZTEST(file_emmc_composition, test_g_file_sync_reaches_the_medium)
    /* crux_file_write + crux_file_sync: flush_commands grew, and emmc_sim_read_persistent
       differs from the digest taken before the write */
ZTEST(file_emmc_composition, test_h_card_init_failure_reports_no_media)
    /* a fresh disk init with fail_command(1, 1, UINT32_MAX): EMMC_STORAGE_NO_MEDIA. A scenario
       of its own, because the disk reference count blocks a second init in one process */
```

- [ ] **Step 2: See them fail, implement `emmc_storage.c`, see them pass**

Run: `west twister -p native_sim/native/64 -T crux-pilot/tests/native/file_emmc_composition -O crux-pilot/build/zephyr/twister-file-emmc-composition --inline-logs`
Expected first: the source is missing. Expected after: PASS.

- [ ] **Step 3: Add to the gate, and commit**

```bash
git add samples/storage_emmc tests/native/file_emmc_composition scripts/test-storage-emmc.sh
git commit -m "feat(storage): add the eMMC reference composition"
```

---

### Task 10: RT1060 link, measurements and composition document (issue 6, second part)

**Files:**
- Create: `samples/storage_emmc/boards/mimxrt1060_evk_mimxrt1062_qspi_B.overlay`, `scripts/test-file-emmc-compile.sh`, `docs/emmc-reliance-composition.md`, `docs/evidence/emmc-reliance/rt1060-link/README.md`
- Modify: `samples/storage_emmc/{prj.conf,testcase.yaml}`, `tests/README.md`, `docs/architecture.md`
- Proposed, owner's approval needed: `.github/workflows/zephyr.yml`

**Interfaces:**
- Consumes: `samples/storage_emmc`.

- [ ] **Step 1: Write the overlay and the check script**

Overlay on `&usdhc1`: `/delete-property/ cd-gpios;`, `/delete-node/ sdmmc;`, and child `mmc { compatible = "zephyr,mmc-disk"; disk-name = "MMC"; bus-width = <4>; status = "okay"; };`. A comment states that it exists to link only, and the EVKB has no eMMC. `scripts/test-file-emmc-compile.sh`, in the style of `scripts/test-file-rt1060-compile.sh`, builds with `--build-only` for `mimxrt1060_evk@B/mimxrt1062/qspi`.

- [ ] **Step 2: See the checks fail, and fix what they find**

The script requires, from the image and its `.config`: `CONFIG_DISK_DRIVER_MMC=y`, `CONFIG_DISK_DRIVER_SDMMC` unset, `CONFIG_CRUX_MMC_CACHE_FLUSH=y`, `CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT` unset, `CONFIG_SDHC_BUFFER_ALIGNMENT=32`; `nm` shows `red_format` absent; `compile_commands.json` lists the generated `mmc.c` and not `subsys/sd/mmc.c`, and the generated `.json` has the expected hashes. A second build with `-DCONFIG_CRUX_MMC_CACHE_FLUSH=n` must fail at CMake. A third with `-DCRUX_EMMC_FLUSH_CONTROL=ON` must fail at CMake.

- [ ] **Step 3: Measure**

With `CONFIG_STACK_USAGE=y` in a separate build: record FLASH and RAM from the link, the deepest static frames of `red_*` paths and of `mmc_flush_cache`, and the buffer memory, as `docs/microsd-composition.md` does. Record them in the new `docs/evidence/emmc-reliance/rt1060-link/README.md` with the revision and commands, and no image.

- [ ] **Step 4: Write `docs/emmc-reliance-composition.md`**

In the style of `docs/microsd-composition.md`: decisions in force, including the owner's link-only exception for the File additions (spec, Intent); the sequence; selection, including that any use of an MMC disk needs the flush option, and that FatFs on an MMC disk is not a Crux File selection; the commercial kit selection point; the failure table; software evidence as it actually is; what is not covered. Add a pointer in `docs/architecture.md`. Run `python3 scripts/check_docs.py`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add samples/storage_emmc scripts/test-file-emmc-compile.sh docs tests/README.md
git commit -m "build: link the eMMC composition for RT1060 and record its size"
```

- [ ] **Step 6: Ask the owner about CI**

Propose the change: a step that runs `west config manifest.group-filter -- +crux-reliance` and `west update reliance-edge`, because `scripts/bootstrap-zephyr.sh` runs a plain `west update`, then `CRUX_STORAGE_32BIT=1 scripts/test-storage-emmc.sh` and `scripts/test-file-emmc-compile.sh`. No new named `zephyr.elf` or `zephyr.bin` upload path. Apply it only after approval, listing the protected file in the PR with the approval quote. Without approval, the 32-bit runs and the CI no-image property stay unshown, and the issue records that as an open gap rather than closing.

---

### Task 11: Configuration on a Settings File target provider (issue 7)

**Files:**
- Create: `components/configuration/ports/zephyr/CMakeLists.txt`, `samples/storage_emmc/emmc_configuration.c`, `samples/storage_emmc/emmc_configuration.h`, `tests/native/configuration_settings_file/fixture_fatfs.h`, `tests/native/configuration_settings_reliance/{CMakeLists.txt,Kconfig,prj.conf,app.overlay,fixture_reliance.h,extra_cases.c,testcase.yaml}`, `docs/evidence/emmc-reliance/replay-settings/README.md`
- Modify: `tests/native/configuration_settings_file/main.c`, `components/configuration/ports/zephyr/configuration_settings.c:12-15`, `zephyr/Kconfig`, `zephyr/CMakeLists.txt`, `samples/storage_emmc/{main.c,prj.conf,Kconfig,CMakeLists.txt}`, `tests/native/emmc_powercut/{workloads.c,run_replay.py}`, `scripts/test-storage-emmc.sh`, `scripts/test-file-emmc-compile.sh`, `tests/README.md`, `docs/emmc-reliance-composition.md`

**No file moves.** The corrections stay where they are, used by the provider and the fixture alike through `tests/native/configuration_settings_file/candidate.cmake`. Moving them would touch `tests/native/settings_error`'s hash record, `.gitattributes` and frozen evidence for no behaviour. A product build reaches a file under `tests/` for this; that is recorded in the composition document.

**Unchanged and checked:** `scripts/test-storage-simulators.sh`, `tests/native/configuration_settings_file/prepare_provider.py`, the suite name `configuration_settings_file` that the gate's regular expression needs, its 25 cases and its 13 required control failures.

**Interfaces:**
- Consumes: `crux_config_zephyr_init` in `configuration_settings.h`, `emmc_storage_start`.
- Produces: `CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE` (`depends on CRUX_FS_RELIANCE_EDGE && SETTINGS_FILE`). `SETTINGS_FILE_PATH` is `/MMC:/settings/cfg` in the composition.
- Produces, `emmc_configuration.h`: `enum emmc_storage_status emmc_configuration_start(void);`, which calls `emmc_storage_start` and, only if that returns `EMMC_STORAGE_READY`, `crux_config_zephyr_init`.

- [ ] **Step 1: Make the existing suite's fixture a header, with no change in behaviour**

`main.c` gets `FIXTURE_ROOT`, the mount object and provisioning from `#include FIXTURE_HEADER`, default `fixture_fatfs.h`. The suite name stays the literal `configuration_settings_file` in `main.c`, because Twister scans it from the source and a macro there would fail every case with a suite mismatch. The read-back cases and `extern bool crux_probe_read_back` compile under `FIXTURE_HAS_READ_BACK`, defined only by the FatFs fixture, because that symbol exists only in the copy `prepare_provider.py` makes. Assertions stay as they are.

Run: `west twister -p native_sim/native/64 -T crux-pilot/tests/native/configuration_settings_file` with `ZEPHYR_MODULES` naming FatFs, `test_prepare.py`, and the control build and regular expression from `scripts/test-storage-simulators.sh:91-126`.
Expected: 25 executed cases PASS; the control fails exactly its 13 named cases.

- [ ] **Step 2: Write the failing suite**

`tests/native/configuration_settings_reliance` compiles that `main.c` with `FIXTURE_HEADER="fixture_reliance.h"` and `-Dconfiguration_settings_file=configuration_settings_reliance`, plus its own `extra_cases.c`, on a RAM disk (the fixture's mount object is not `static` where these cases need it), against the **real provider**, `configuration_settings.c` with the new selection, not the `prepare_provider.py` copy. So 24 of the shared cases run. Plus, in `extra_cases.c`, with a wrap counting `settings_subsys_init`:

```c
ZTEST(configuration_settings_reliance, test_compaction_leaves_no_cmp_file)
ZTEST(configuration_settings_reliance, test_start_on_a_medium_without_settings_dir_does_not_init_settings)
    /* emmc_configuration_start: LAYOUT_MISSING; settings_subsys_init calls == 0; digest unchanged */
ZTEST(configuration_settings_reliance, test_start_on_a_provisioned_medium_inits_settings_once)
```

Cases whose expected result comes from FatFs behaviour, such as `test_missing_file_with_a_compaction_leftover_is_an_error`, keep their assertion if the corrected backend gives it here. If one cannot hold on this file system, stop and record why before changing that case for this fixture, and never change it for the FatFs fixture.

- [ ] **Step 3: See it fail, implement, see it pass**

Expected first: a Kconfig failure. Twister scans the cases of `extra_cases.c` only, so the gate's expected count for this suite is set from the first passing run and checked against the case names in the run log.

In `configuration_settings.c`, keep the existing guard's text and wrap it, so that `prepare_provider.py` still finds it:

```c
#if defined(CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE)
#if !defined(CONFIG_SETTINGS_FILE)
#error "The Settings File selection requires CONFIG_SETTINGS_FILE"
#endif
#else
/* existing guard, unchanged */
#endif
```

Add `components/configuration/ports/zephyr/CMakeLists.txt`, and in `zephyr/CMakeLists.txt` add it under the new symbol, followed by `include(<crux>/tests/native/configuration_settings_file/candidate.cmake)` unless `CRUX_SETTINGS_FILE_CONTROL` is set, which is a `FATAL_ERROR` on any board other than `native_sim`. If that CMake does not work at module scope, stop and report: it runs at application scope today.

- [ ] **Step 4: Add the control**

In `scripts/test-storage-emmc.sh`, in the style of `scripts/test-storage-simulators.sh:91-126`: a separate `west build` of the new suite with `-DCRUX_SETTINGS_FILE_CONTROL=ON` and a run that must fail, compared with an exact set of case names. The first run characterises the set on this file system. Review it, record it in the script, and explain in the PR any difference from the FatFs set of 13. It is not a Twister scenario, because a scenario that must fail would stop the gate.

- [ ] **Step 5: Add W4**

In `workloads.c`: repeated `settings_save_one` calls, with `CONFIG_SETTINGS_FILE_MAX_LINES` low enough to force compaction, marking after each success. `verify` loads the settings and checks that each key's value is not older than acknowledged, reporting through `data`, and that a further save succeeds, through `recommit`. Build `red-fix-settings` from `reliance.conf` plus the Settings options, and add it to `run_replay.py`, asserting I1 to I5, where I3 for this workload means that the active settings file is complete, reported through `replace`. A cut between the temporary's close and the rename can leave a `.cmp` file, so a leftover is not a failure in itself: the active file must be intact and not older than acknowledged. Record leftovers in the JSON.

Run: `run_replay.py --fs reliance --workload W4 --seed 1 --samples 8 --max-seconds 300`
Expected: exit 0. If the active file is ever missing, that is a finding against the spec's statement about the `.cmp` recovery step, not something to relax.

- [ ] **Step 6: Link, record and commit**

`samples/storage_emmc/main.c` calls `emmc_configuration_start` when `CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE` is on. Extend `scripts/test-file-emmc-compile.sh` with a Settings scenario, and write a new record in `docs/evidence/emmc-reliance/replay-settings/README.md`.

Run: `scripts/test-file-emmc-compile.sh`. Expected: PASS.

```bash
git add components/configuration samples/storage_emmc scripts/test-storage-emmc.sh scripts/test-file-emmc-compile.sh zephyr tests docs
git commit -m "feat(configuration): add a Settings File target provider on Reliance Edge"
```

---

### Task 12: Physical validation procedure (issue 8)

**Files:**
- Create: `docs/emmc-reliance-physical-gate.md`
- Modify: `docs/emmc-reliance-composition.md`

- [ ] **Step 1: Write the gate document**

In the style of `docs/microsd-physical-gate.md`, in two stages. **Stage A:** Reliance Edge on the EVKB microSD, with the disk name `SD`, reusing the existing power-cut rig, comparing with the FatFs results in `docs/evidence/microsd-replace-cut/` and `docs/evidence/microsd-power-cut/`. **Stage B:** a board with a real eMMC: device identity, whether it reports a cache, flush latency and its effect on higher-priority threads, DMA and data cache placement against SDRAM, whether `sd_init` succeeds with the board's bus width and voltage, and power cuts during the replay workloads. List preconditions, stop conditions and records. State that it authorises nothing and that each step needs approval for that task.

- [ ] **Step 2: Verify, commit**

Run: `python3 scripts/check_docs.py`
Expected: PASS.

```bash
git add docs/emmc-reliance-physical-gate.md docs/emmc-reliance-composition.md
git commit -m "docs: write the eMMC and Reliance Edge physical gate"
```

**Before each issue's PR:** run `scripts/check-format.sh`, `scripts/check-dependencies.sh` and `scripts/test-storage-emmc.sh` locally, and for issue 6 `scripts/test-file-emmc-compile.sh`. `scripts/check-host.sh`, `scripts/analyze-host.sh` and `scripts/test-storage-simulators.sh` run in CI on the Draft PR; issues 5 and 7 touch what that gate covers, so their review waits for that CI result where the branch can be pushed. Record results, skips, what is pending and the tested revision. Dispatch the `reviewer` agent with the inputs `CLAUDE.md` lists. Use superpowers:verification-before-completion before claiming an issue done.
