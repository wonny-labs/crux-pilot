# Transactional storage on eMMC: design

Status: design proposal for owner review. Nothing here is adopted, and no result here shows that anything works: the
findings are source readings, and every test and tool described below is still to be built. This document authorises
no physical operation, merge or upstream contribution.

Baseline: main `a80acd7`, Zephyr v4.4.2 `dccb095`, Reliance Edge v3.0 `6706984`.

## Intent

This document describes a target profile, not any particular product.

**What the owner asked for (3 November 2026, session):** prepare Crux for eMMC with a Reliance file system, alongside
today's FatFs on microSD, knowing that it may not get all the way. Review it, plan it, create a milestone and issues, and
develop it. Board tests may come later, but the work should be verified by unit and integration tests, with `native_sim`,
before the board.

**Owner's answers (same session):**

- The file system is Reliance Edge, not Reliance Nitro.
- Develop and test against the public GPLv2 source. A product build uses the commercial kit.
- The scope is the data file system. The boot path is out of scope.
- The MCU is in the RT1060 series.

**Owner's decisions on changes after `main` moved (4 November 2026, session, "추천안대로 해줘", accepting these three as recommended):**

- The RT1060 link may compile the File additions (F18). This is an exception to the contract's condition that target work on
  them stays behind M7. It covers linking only, and nothing runs on a target.
- The File service and the credential store on this profile are out of this milestone, with a separate assessment issue.
- The File contract is not changed for atomic replace now. It is reconsidered after physical evidence, and the assessment
  issue records this.

**Success means:**

1. A consumer of `crux/file.h` runs unchanged on Reliance Edge on an eMMC disk, in a composition that can be copied.
2. A successful sync on eMMC really reaches the medium, in the software model and by source inspection.
3. Power-cut replay on `native_sim` shows the guarantees that Reliance Edge is chosen for, with negative controls that
   show the replay can fail.
4. The same code links for RT1060, with measured size and stack figures.
5. The physical procedure is written and ready, and it is clear what only a board can answer.

**Assumptions and limits, not established by the owner:**

- A commercial kit's API and OS-service interface match the base API used here. This is checked when a kit is available.
- An eMMC device may or may not have a volatile write cache, so both are tested.
- The sector size is 512 bytes. The stack fixes this (`subsys/sd/mmc.c:589`), so it is a limit of the stack, and this work
  does not test other sizes.

## Findings

Paths are in the pinned Zephyr unless they begin with `reliance-edge/` or `linux/`, or name a file of this repository.

| # | Finding | Source | Consequence |
| --- | --- | --- | --- |
| F1 | An MMC disk driver and protocol stack exist, selected by a `zephyr,mmc-disk` child of an SD host node. | `drivers/disk/mmc_subsys.c`, `subsys/sd/mmc.c`, `dts/bindings/sd/zephyr,mmc-disk.yaml` | No new block driver is needed. |
| F2 | `mmc_card_init` turns on the eMMC cache whenever the device reports one. `DISK_IOCTL_CTRL_SYNC` only waits for the card to be ready, under a comment saying that the stack does not enable caching. No flush command exists in the stack. Zephyr `main` read the same on 3 November 2026. | `subsys/sd/mmc.c:188`, `:618`; `subsys/sd/sd_ops.c:871-877` | On a device with a cache, a successful sync does not imply that data reached the medium. Not yet reproduced by running it. |
| F3 | Reliance Edge's commit depends on flush as a barrier: it flushes before writing the metaroot, writes it, then flushes again. Its comments state the corruption window if the device reorders writes. | `reliance-edge/core/driver/volume.c:686-709` | F2 undermines the reason for choosing this file system. Changing the file system does not fix it. |
| F4 | `sdmmc_wait_ready` polls CMD13 through `sdmmc_read_status`, which tests only ready-for-data and the transfer state. Linux documents the eMMC switch error bit as clear by read. | `subsys/sd/sd_ops.c:20-61`, `:65-90`; `linux/include/linux/mmc/mmc.h:131`, `:156` | A wait through this function consumes a switch error and discards it. D3 must not use it after the flush command. |
| F5 | The stack has no boot partition, RPMB or reliable write code. | `subsys/sd/` | Out of scope here, and a stated gap for later boot work. |
| F6 | Zephyr has no SD host emulator, and the upstream MMC test needs a real card. | `drivers/sdhc/`, `tests/subsys/sd/mmc/README.txt` | The MMC stack cannot currently run on `native_sim`. |
| F7 | The public Reliance Edge source has no Zephyr port. Its `os/` directory has FreeRTOS, Linux, stub, U-Boot and Windows. A port implements the services in `redosserv.h`. | `reliance-edge/os/`, `reliance-edge/include/redosserv.h` | Crux writes the port. |
| F8 | The VFS accepts external file systems. | `include/zephyr/fs/fs.h:68`, `subsys/fs/fs.c:908` | The file system can be added without changing Zephyr. |
| F9 | Zephyr's FatFs rename removes the destination first, which left no settings file in 29 of 100 cuts on the board. With `REDCONF_RENAME_ATOMIC`, Reliance Edge documents an atomic replace. | `subsys/fs/fat_fs.c:177-186`, [replace-cut evidence](../evidence/microsd-replace-cut/README.md), `reliance-edge/posix/posix.c:3085-3089` | The specific FatFs failure already measured is one that this file system documents as addressed. Replay checks the claim. |
| F10 | The File provider's target selection requires EVKB Rev B QSPI, FatFs and the SDMMC disk. An MMC disk does not satisfy it. | `zephyr/Kconfig` (`CRUX_FILE_FATFS_RT1060`), `zephyr/CMakeLists.txt:92-98`, `components/file/ports/zephyr/file_native.c:10-37` | A new, board-independent selection is needed. |
| F11 | Configuration's Zephyr port is restricted to the simulator and NVS. The Settings File backend hides I/O faults, and the corrections exist only in a test fixture. | `components/configuration/ports/zephyr/configuration_settings.c:12-15`, [test guide](../../tests/README.md), `tests/native/configuration_settings_file/` | No target Settings provider exists. It is new work, not a connection. |
| F12 | Crux Logging's build refuses `LOG_BACKEND_FS`. | `components/logging/ports/zephyr/CMakeLists.txt` | File logging stays with #112. |
| F13 | The public Reliance Edge source is GPL version 2 only. The FSF lists Apache-2.0 as compatible with GPLv3 and not with GPLv2. The source separates kits, and some features exist only in the commercial one. | `reliance-edge/include/redposix.h` (header), `reliance-edge/README.md`, `reliance-edge/include/redapimacs.h:49`, [FSF licence list](https://www.gnu.org/licenses/license-list.html) | See [D6](#d6-licence-boundary). |
| F14 | A failed commit, or a failed block read or write on a mounted volume, calls `RedVolCriticalError`. It sets the volume read-only and, with `REDCONF_ASSERTS` on, calls `RedOsAssertFail`, whose stub never returns. | `reliance-edge/core/driver/volume.c:727`, `:1001-1021`; `reliance-edge/core/driver/blockio.c:94`, `:152`; `reliance-edge/core/include/redcoremacs.h:120-125`; `reliance-edge/os/stub/services/osassert.c` | Asserts stay off in this profile, or a media error would hang the caller. One media error turns the volume read-only until a remount. See [D9](#d9-failure-policy). |
| F15 | The CI artefact list uploads 40 named `zephyr.elf` and `zephyr.bin` paths of cross-builds. Only its Twister search and wildcards exclude images. | `.github/workflows/zephyr.yml` (upload step) | A Reliance Edge build added in the same pattern would publish a GPLv2-linked image. See [D6](#d6-licence-boundary). |
| F16 | On a read-only volume, `red_close` skips the transaction and returns success, while `red_fsync` reports `RED_EROFS`. | `reliance-edge/posix/posix.c:6328`, `:6357-6360`, `:4022-4025`; `reliance-edge/core/driver/core.c:391` | A handle with unsaved writes could close with `OK` after another handle's commit failed, and its data would be gone. See [the adapter](#reliance-edge-adapter). |
| F17 | On the RT1060 the USDHC HAL invalidates a read target by whole D-cache lines, so a target that shares a line with another object can lose that object's write. The two existing RT1060 File selections set `SDHC_BUFFER_ALIGNMENT` to 32, and the SD layer bounces anything less aligned. The setting and the check name those selections only. | `zephyr/Kconfig` (`configdefault SDHC_BUFFER_ALIGNMENT`), `components/file/ports/zephyr/file_native.c:19-25`, `docs/microsd-composition.md` | A new target selection must set and check the same value, or it would silently have the 4-byte default. |
| F18 | The File contract gained roots, stat, size, rename and listing (#224). `crux_file_rename` refuses an existing destination. The additions are accepted for Host and `native_sim` work, and target and physical work on them "stays behind M7 and needs its own approval". | `docs/file-contract.md`, `components/file/src/file_port.h`, `tests/native/file_roots/` | The provider must run them on this file system too. Atomic replace is reachable through the VFS, not through `crux_file_rename`. |

## Decisions proposed

### D1. Integrate as a Zephyr VFS file system

Reliance Edge is registered through `fs_register` under `FS_TYPE_EXTERNAL_BASE`.

- The existing Zephyr File provider and the Settings File backend run on it through the VFS.
- **Rejected: a `crux_file` provider that calls `red_*` directly.** It has one layer fewer, but Settings would need a
  second route, and the existing VFS-based File mapping, including its documented failure handling, would not be reused.

### D2. Public contracts do not change

This work does not change `crux/file.h` or `crux/configuration.h`. It runs the contract as `main` has it, including the
additions in F18. The adapter has one private header for compositions, which gives
the file system type and is not a `crux/` API. The File contract's "no atomic update" statement stays: what this
composition offers is recorded in its own document, as the microSD composition does. Strengthening the contract is a
later decision, after physical evidence.

### D3. Correct the eMMC flush with an opt-in, build-local patch

`CONFIG_CRUX_MMC_CACHE_FLUSH`, off by default, replaces `subsys/sd/mmc.c` in the existing SD library with a generated
copy, in the same way as `patches/zephyr/nxp-enet-mdio-completion.patch`: the original's hash is checked, the Zephyr
checkout is never edited, and drift fails the configuration.

Behaviour of the patched `mmc_ioctl` on `DISK_IOCTL_CTRL_SYNC`, when the cache was enabled:

1. Hold the card lock and call `sdmmc_wait_ready`, which finishes earlier writes and clears any stale status.
2. Send CMD6 to write `FLUSH_CACHE` (EXT_CSD byte 32) with value 1, with `CONFIG_SD_DATA_TIMEOUT` as the command timeout.
3. Poll `sdhc_card_busy`, bounded by the same timeout. `sdmmc_wait_ready` is not used here (F4).
4. Send CMD13 through `sdhc_request` until the card is ready and in the transfer state, bounded by the same timeout. If
   the switch error bit (bit 7) is set in any response, the sync returns `-EIO`. A timeout returns `-ETIMEDOUT`.

A failure in any of these steps, including the wait in step 1 and the CMD6 itself, returns an error.

Whether the cache is on is held in a private bit of `card->flags`, set by the patched `mmc_set_cache`. `sd_init` clears
the flags on each initialisation. A device without a cache gets no flush command. Everything is in one file because
`mmc_ioctl` and `mmc_set_cache` both live in `mmc.c`. The disk de-initialisation path also goes through `mmc_ioctl`, but it
discards the result (`drivers/disk/mmc_subsys.c:88-94`), so nothing here relies on it.

Linux has the equivalent test and command (`_mmc_cache_enabled`, `_mmc_flush_cache` in `drivers/mmc/core/mmc.c`) and allows
30 s, while Zephyr's `CONFIG_SD_DATA_TIMEOUT` defaults to 10 s. A composition that needs longer raises that value.

**Guard:** the adapter's CMake fails the configuration if Reliance Edge and `CONFIG_DISK_DRIVER_MMC` are selected without
this option. The negative controls set `CRUX_EMMC_FLUSH_CONTROL`, a CMake variable for test fixtures that is never a product
configuration, as `CRUX_SETTINGS_FILE_CONTROL` is. It is honoured only on `native_sim`, so a target image cannot use it. FatFs on an MMC disk is not a Crux File selection, so the guard does not
cover it. The composition document states that any use of an MMC disk needs this option.

**Rejected:**

- *Leave the cache off.* It also needs the patch, gives up write performance, and leaves FatFs and any other user of the
  MMC disk with no flush if the cache is ever enabled.
- *A Crux-owned `crux,mmc-disk` driver.* It needs no patch, but it duplicates `mmc_subsys.c`, sends commands from outside
  the stack, and does not lead to a correction upstream.

The upstream change is the intended end state and needs its own approval. The patch is removed when it is available at
the pinned Zephyr.

### D4. A test-only eMMC simulator

A `native_sim` SD host driver that behaves as an eMMC device, so that the unmodified `mmc_subsys.c` and `mmc.c` run
without hardware. It lives under `tests/` and cannot be selected on a target. It is a model written from the stack's
needs, so results show how the stack behaves against that model and not how a real device behaves.

### D5. Prove power-cut behaviour by replay, not by killing processes

The simulator records every write and flush. A host driver rebuilds the medium as it could be at each cut point, starts
a new process on it, and checks invariants. Unlike the existing kill-based fixture, this controls the cut point and can
model a device that reorders cached writes, which is the behaviour F2 and F3 are about.

### D6. Licence boundary

- Reliance Edge source is never copied into this repository. It is fetched by west from `tuxera/reliance-edge` at a pinned
  commit, in a new optional group, `crux-reliance`, excluded by default, as the secure management stacks are.
- The adapter, port and configuration are original Apache-2.0 work, written against the interface and not derived from
  the stub or port sources. Built, they include GPLv2-only headers, which is why the owner confirms this reading before the
  adapter is published.
- Images that link the public source are test images and are not distributed. No such image is uploaded by CI (F15) or
  stored in `docs/evidence/emmc-reliance/`. The FSF position in F13 means that an image combining it with Zephyr should not
  be distributed under GPLv2 either, so a product must use the commercial kit. This is an engineering reading, not legal
  advice.
- A kit is selected by a CMake variable, never committed. No kit source, kit-derived text or product information enters
  this public repository, its issues or its pull requests.

### D7. FatFs and microSD stay as they are

They remain the EVKB evaluation profile, including #14, #15 and #16. This work adds a composition and removes nothing.

### D8. Scope

**In scope:** the eMMC user area as one data volume, optionally at a sector offset; Reliance Edge behind the VFS; the File
provider selection; a reference composition; a Settings File target provider; software verification; an RT1060 link and
measurements; the physical procedure as a document.

**Out of scope:** boot partitions, RPMB, reliable write, a boot loader that reads the file system, HS200 and HS400
qualification, TRIM and discard, several volumes, wear reporting, encryption, file logging (#112), commercial kit
integration beyond the selection point, any change to FatFs behaviour, and the File service and credential store on this
profile, which issue 9 assesses.

### D9. Failure policy

The microSD composition's rules carry over: no automatic format, no directory creation, no platform retry loop, and the
composition owns mount order and recovery.

- `bBlockIoRetries` is 0, so the file system adds no hidden retry. The stack's own transfer retries
  (`CONFIG_SD_DATA_RETRIES`, default 3) stay as they are, and the simulator honours `cmd->retries` as the i.MX host does.
- `REDCONF_ASSERTS` is 0 (F14). A media error must reach the caller.
- `fAtomicSectorWrite` is false, so a torn metaroot falls back to the other one rather than failing the mount
  (`reliance-edge/core/driver/volume.c:467-470`, `:492-495`).
- The formatter is compiled only with `CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT`, off by default, so a composition image cannot
  format by accident, and a check confirms that `red_format` is not linked.

## Architecture

```text
Consumer     crux/file.h                         crux/configuration.h         (both unchanged)
Provider     components/file/ports/zephyr        Settings File target provider (new, issue 7)
VFS          Zephyr fs.c
Adapter      zephyr/fs/reliance_edge             VFS operations and Reliance Edge OS services (new)
File system  Reliance Edge                       public source in tests, commercial kit in a product
Block        Zephyr disk_access, disk "MMC"
             drivers/disk/mmc_subsys.c
             subsys/sd/mmc.c                     build-local flush correction (new)
Host         crux,emmc-sim on native_sim         imx_usdhc.c on a target
```

### eMMC simulator

`tests/support/emmc_sim/`: driver, binding `crux,emmc-sim` (with a `zephyr,mmc-disk` child), a private control header and
a native simulator bottom file for host file access, as Zephyr's flash simulator does.

| Area | Behaviour |
| --- | --- |
| Commands | CMD0, 1, 2, 3, 6, 7, 8 (EXT_CSD, and a timeout for the SD interface probe), 9, 13, 17, 18, 24, 25, the set the stack sends for eMMC at the pin. Others return an error, so that an unexpected stack change is visible. That includes CMD55, which the stack sends after a failed write (`subsys/sd/sd_ops.c:691`). |
| Registers | OCR, CID, CSD and EXT_CSD with values that the stack's parser needs. CMD6 accepts bus width (byte 183), timing (185), power class (187), cache control (33) and flush cache (32), and rejects other bytes. |
| Status | CMD13 reports a rejected switch through bit 7, and reading it clears it, as Linux documents. A sticky bit would hide the defect in F4. `card_busy` reports the busy period after CMD6 and writes. |
| Profiles | Selected on the command line: cache size (zero means no cache), capacity. |
| Volatile cache | With the cache on, writes are held until `FLUSH_CACHE`, and reads see the held data. Without it, writes reach the persistent image in order. |
| Host files | `--emmc-image` is loaded at start, and persistent state is written to it. `--emmc-trace` records writes, flushes, cache control and workload marks in order, with data. |
| Fault injection | Fail the Nth read, write, flush or command; report a switch error; hold the card busy past a timeout. Injected transfer faults honour `cmd->retries`. |

### Flush correction

As in D3: `patches/zephyr/mmc-cache-flush.patch`, a new preparation script, and CMake wiring in the module that requires
exactly one `mmc.c` in the `subsys__sd` library and adds `subsys/sd` to the include path for the generated copy. Tests in
`tests/native/emmc_stack` assert the commands seen by the simulator.

### Power-cut replay

`tests/native/emmc_powercut/`, run by a Python driver. A workload runs once against the simulator and records the trace,
with a mark after each call that returned success. The trace is expanded to sectors, because one command can carry many,
and one Reliance Edge block is eight (`reliance-edge/core/driver/blockio.c:133-135`, `subsys/sd/sd_ops.c:664`).

**Images:**

| Image | File system | Flush correction | Use |
| --- | --- | --- | --- |
| `red-fix` | Reliance Edge | on | Asserted run, barrier, early acknowledgement and damaged image controls |
| `red-nofix` | Reliance Edge | off, with `CRUX_EMMC_FLUSH_CONTROL` | Reliance Edge flush control |
| `fat-fix` | FatFs | on | Recorded observation; damaged image control in issue 4, before `red-fix` exists |
| `fat-nofix` | FatFs | off | FatFs flush control |

**Baseline:** each file system has one formatted, provisioned medium image, made on the no-cache profile by a test image
built with the format option. Every run and control starts from it, so that a control's result comes from the workload and
not from the format, which would be lost on a cached device without the correction.

**Cut models,** with sectors as the unit. Everything up to the last barrier is on the medium.

| Model | Sectors written since the last flush |
| --- | --- |
| Lost | None survive. |
| Prefix | The oldest ones survive, in order. |
| Reordered | A seeded random subset survives. |
| Torn | As Prefix, and the next sector holds a seeded mix of old and new bytes. |

Without a cache, only Prefix and Torn apply. Sector-level damage outside the write being made is not modelled; a real
device can do this, and only the physical gate can address it.

**Cut positions:** each side of every flush and each sector of every metaroot write are always tested, whatever the CI
budget. These are the barrier windows and the torn metaroot case. Other positions are sampled by seed. Metaroot writes are
recognised from their block numbers (`BLOCK_NUM_FIRST_METAROOT`, `reliance-edge/core/driver/volume.c:697`). On FatFs, only
the flush positions apply.

**Workloads:** W1 appends records with a sync each; W2 replaces a file through a temporary and a rename, the Settings
compaction pattern; W3 creates, overwrites and removes several files, including directory changes; W4 (issue 7) saves
through the real Settings backend.

**Invariants, checked in a new process on each rebuilt image:**

- I1: the volume mounts without formatting.
- I2: data acknowledged before the cut is intact.
- I3: a replaced file is the complete old or the complete new version, never missing or mixed.
- I4: files not touched since their acknowledgement are unchanged.
- I5: the volume accepts a new write, sync and remount.

**Results:** on `red-fix`, I1 to I5 are asserted. On `fat-fix` the same run is recorded as an observation, because FatFs
does not claim them.

**Negative controls, required to fail:**

| Control | Image and model | Required result | What it shows |
| --- | --- | --- | --- |
| FatFs flush | `fat-nofix`, cached, Lost | I2 in W1 | A sync does not reach the medium without the correction. |
| Reliance Edge flush | `red-nofix`, cached, Lost | I2 in W1 | The same on this file system. Any file system would fail this, so it does not show replay's power. |
| Barrier | `red-fix` trace, cached, Reordered, with the flush before each metaroot write not treated as a barrier, and a directed draw that keeps the whole metaroot and randomises the rest | I1 or I4 fails, or a read of acknowledged data reports an error: a result that an old state cannot explain | Replay catches the window in F3. |
| Early acknowledgement | A workload that marks before its sync, `red-fix`, cached, Lost | I2 | The marks and I2 work. |
| Damaged image | A verifier given a deliberately damaged image | It reports the damage | The verifier can fail. |

The driver prints seeds and the failing trace position, and keeps only the medium image of a failing case. The medium is
4 MiB and is rewritten in place. The CI selection is bounded to at most 5 minutes after the mandatory positions, and a
longer sweep is a manual option. The plan sets the exact counts after the first measurement.

### Reliance Edge adapter

`zephyr/fs/reliance_edge/`, enabled by `CONFIG_CRUX_FS_RELIANCE_EDGE`. This is outside `components/` because it has no
portable contract and no `crux/` API: it is a Zephyr file system, and it belongs to the Zephyr module.

**CMake:** `CRUX_RELIANCE_EDGE_DIR` defaults to `modules/fs/reliance-edge` in the workspace. A missing tree is a configure
error that names the west group. The source list is one variable, because a commercial kit may have different files, and
`CRUX_RELIANCE_EDGE_CONFIG_DIR` can replace the configuration.

**VFS mapping:**

| VFS | Reliance Edge | Note |
| --- | --- | --- |
| `mount`, `unmount` | `red_init` once, `red_mount2`, `red_umount` | `FS_MOUNT_FLAG_READ_ONLY` becomes `RED_MOUNT_READONLY`. An unformatted medium fails the mount and is never formatted. |
| `open`, `close` | `red_open`, `red_close` | The descriptor is stored in the VFS object without allocation. Open flags are translated, because the two sets differ numerically. See below for close. |
| `read`, `write`, `lseek`, `tell`, `truncate`, `sync` | `red_read`, `red_write`, `red_lseek`, `red_ftruncate`, `red_fsync` | A size that cannot be represented is rejected before the call. |
| `opendir`, `readdir`, `closedir` | `red_opendir`, `red_readdir`, `red_closedir` | |
| `unlink`, `rename`, `mkdir`, `stat`, `statvfs` | `red_unlink`, `red_rename`, `red_mkdir`, `red_stat`, `red_statvfs` | `rename` replaces atomically in this configuration. |
| `mkfs` | `red_init` once, `red_format` | Only with the format option. |

**Close (F16):** the adapter keeps a flag for each descriptor, set by a write or truncate and cleared by a successful sync.
If it is set at close, the adapter calls `red_fsync` first. If that fails, it still closes the descriptor and returns the
failure, so a read-only volume gives `-EROFS`. Without this, close could report success for writes that were lost.

**Paths:** the VFS passes full paths, so the adapter removes the mount point. The volume has an empty prefix, and an empty
remainder means the root.

**Errors:** an explicit switch from `RED_E*` to Zephyr errno values, because the two number spaces are not assumed equal;
anything unknown is `-EIO`; `RED_EUSERS` is `-ENOMEM`, which the File provider reports as `NO_RESOURCES`.

**OS services:**

- Block I/O uses `disk_access_*` on the disk named by the mount, with sector numbers checked against 32 bits and flush
  through `DISK_IOCTL_CTRL_SYNC`. Any non-zero result is a failure, because the stack can return a positive value after a
  failed write (`subsys/sd/sd_ops.c:473-476`, `:691-694`). The composition has already initialised the disk, so opening the
  block device does not repeat it.
- The mutex is a `k_mutex`.
- The task ID is a unique, non-zero 32-bit value for each thread on both `native_sim` widths, from a small table rather than
  a truncated pointer. The table has its own lock, because `RedOsTaskId` is also called outside the file system mutex
  (`reliance-edge/posix/posix.c:5130`). The adapter has no hook for thread exit, so a thread that exits keeps its slot, and
  `REDCONF_TASK_COUNT` bounds the threads that ever use the file system. This limit is documented and tested.
- The port also supplies the OS type and configuration headers, and whatever clock functions `redosserv.h` needs with
  timestamps off.

**Configuration, default profile:** POSIX API; rename, unlink, mkdir, rmdir, ftruncate and readdir on, with atomic rename;
links, symlinks, working directory, owners and permissions, and deleting open files off; one volume; 4 KiB blocks;
timestamps off, so that no clock contract is needed; output and asserts off. Build-time checks: `REDCONF_NAME_MAX` and
`CONFIG_FILE_SYSTEM_MAX_FILE_NAME` against the File provider's 63-character limit, because Zephyr falls back to 12 with only
an external file system (`include/zephyr/fs/fs_interface.h`); `REDCONF_BUFFER_ALIGNMENT` against
`CONFIG_SDHC_BUFFER_ALIGNMENT`, which this selection sets to 32 with the same D-cache check as the existing ones (F17);
`REDCONF_HANDLE_COUNT` against the File provider's file and listing pools, because the two share handles here; and
`REDCONF_TRANSACT_DEFAULT` must include `RED_TRANSACT_FSYNC`, because without it `red_fsync` commits nothing and returns
success (`reliance-edge/posix/posix.c:3972-3973`), which would defeat sync and the close rule. Transactions commit on create, mkdir, rename, unlink, fsync, close, a full volume, unmount and `red_sync`, following the
upstream sample without link. Buffer, handle and task counts, sector offset and sector count are Kconfig values. Automatic
sector count is the default.

Several of these values are checked against the medium at mount (`reliance-edge/core/driver/volume.c:343-356`), so a
formatted medium and the image that mounts it must use the same profile.

### File provider selection

`CONFIG_CRUX_FILE_RELIANCE_EDGE`, on any board, requires the adapter and is mutually exclusive with the existing File
selections. `zephyr/Kconfig`, `zephyr/CMakeLists.txt`, the port's CMake and the guards in `file_native.c` change together.
FatFs name encoding and full-volume heuristics stay FatFs-only; they should not be needed here, and tests confirm that.
The additions in F18 run through the same provider code. `crux_file_rename` keeps its contract and refuses an existing
destination, so its stat-before-rename path applies here as it does on FatFs.

### Reference composition

`samples/storage_emmc`, project code in the style of `samples/storage_sd`: disk `MMC`, mount `/MMC:`, directories
`/MMC:/files` and `/MMC:/settings`. It initialises the disk, mounts without format, checks the layout, unmounting if
anything is missing, then binds the File root. It reports the same kinds of status as the microSD composition. The disk
name is a build setting, so the same code can run on the EVKB microSD.

An overlay swaps the EVKB `usdhc1` child for a four-bit `zephyr,mmc-disk` and removes card detection. **It exists to link
only.** The EVKB has no eMMC, and nothing here claims that the overlay matches any board.

### Settings File target provider

`CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE` provides a target selection for Configuration on Settings File, on this file
system only. The corrections from `tests/native/configuration_settings_file` are promoted to an opt-in build-local copy
used by the provider and the fixture alike, because the pinned backend hides I/O faults (F11).

- With atomic rename, the `.cmp` recovery step in `samples/storage_sd` should not be needed here, and W4 tests that rather
  than assuming it.
- The Settings backend can create directories (`subsys/settings/src/settings_file.c:514-552`), so the provider initialises
  only after the composition's layout check, and a test confirms that order.
- The 2 November decision that the FatFs composition has no Settings target provider is unaffected.

## Failure behaviour

| Event | Required result |
| --- | --- |
| Blank or foreign medium | Mount fails, the composition reports it, and the medium digest is unchanged. |
| Missing directory | The volume is unmounted and the status reported; nothing is created. |
| Block write fails | The call that reaches it gets an error, and the volume becomes read-only (F14). Accepted counts follow the File contract. |
| Block read fails | The caller gets an error, and the volume becomes read-only (F14). Reading is still attempted afterwards. The composition's recovery is to close handles and remount. |
| Commit or flush fails | The sync reports an error and the volume becomes read-only. After a remount, the volume holds a complete transaction: the previous one, or the failed one if it did reach the medium. An error is not proof that nothing changed. |
| Later calls on a read-only volume | Writes and syncs report `ACCESS`. A close of a handle with unsaved writes also reports `ACCESS` (F16), tested with two handles. |
| Flush rejected by the device | `DISK_IOCTL_CTRL_SYNC` returns an error, tested with a status bit that clears on read (F4). |
| Card busy past the timeout | An error, not a hang. |
| Volume full | `NO_SPACE` or `PARTIAL` per the File contract, and space returns after removal. |
| Too many threads | `NO_RESOURCES`, with no effect on other threads. |
| Power cut | The invariants above. |

## Verification

| Layer | Runs | Shows | Does not show |
| --- | --- | --- | --- |
| Host | Existing File and Configuration suites | Portable behaviour stays unchanged | Anything about Reliance Edge |
| `native_sim`, RAM disk | `tests/native/file_reliance`, the File behaviours of `file_fatfs` and `file_roots` plus atomic rename through the VFS, close on a read-only volume, thread limit and error mapping | Adapter and provider behaviour | The MMC stack, power loss |
| `native_sim`, eMMC simulator | `tests/native/emmc_stack`, stack init, geometry, reads, writes, bounds, faults and flush commands, with and without a cache | The unmodified stack against the model | Real device behaviour, timing, DMA |
| `native_sim`, composition | `tests/native/file_emmc_composition`, the sequence and failures on the simulator | The composition path end to end | Target behaviour |
| Replay | `tests/native/emmc_powercut` | Invariants under modelled cuts, with controls | FTL behaviour, damage outside the current write |
| Process restart | Simulator image across processes | Acknowledged data survives exit | Power loss |
| Cross-build | RT1060 link, with `red_format` absent; size and stack figures | It links, and what it costs | Anything at run time |
| Physical, later | Separately approved | Timing, DMA and cache, device power loss, endurance | |

Passing one layer implies nothing about the next. `native_sim` runs at 64 bits locally and at both widths in CI where the
gate supports them. Results go to `docs/evidence/emmc-reliance/` with revision, commands and failures, and without images.

A new `scripts/test-storage-emmc.sh` runs these. Issue 2 creates it and later issues extend it, which AGENTS.md allows for a
test runner of the work being changed. Once it builds Reliance Edge, it fails if the optional tree is missing, rather than
skipping.

**Physical later, in order.** The adapter and the provider do not depend on the medium, so the first step needs no eMMC:
Reliance Edge on the EVKB microSD with the existing power-cut rig compares directly with the FatFs results. A real eMMC then
needs a board with one. Every step needs approval for that task.

## Delivery

New milestone: **M14 — Transactional storage on eMMC**. Issues and pull requests are generic and carry no product
information. Each issue has its own Draft PR and independent review. Issue 1 owns this document.

| # | Issue | Depends on | Acceptance |
| --- | --- | --- | --- |
| 1 | Record the design and the licence boundary | | This document reviewed; `west.yml` has the optional pinned group; a default workspace does not fetch the `reliance-edge` project, checked by its directory being absent. |
| 2 | eMMC simulator and MMC stack tests | 1 | The unmodified stack initialises and reads and writes on the simulator; bounds and faults behave as listed; an image survives a process restart; `scripts/test-storage-emmc.sh` runs them; a test shows that a sync sends no flush at the pinned revision. |
| 3 | eMMC cache flush correction | 2 | The sync test passes with the option on; a control build still shows the defect; no flush without a cache; a rejected flush, with a bit that clears on read, and a busy timeout return errors; drift fails the configuration; recorded in `patches/zephyr/README.md`. |
| 4 | Power-cut replay | 2, 3 | W1 to W3 run on `fat-fix` and are recorded; cut models and sampling are deterministic by seed; mandatory positions are all tested; the FatFs flush control and the damaged image fail as required. |
| 5 | Reliance Edge VFS adapter and File conformance | 1, 4 | `tests/native/file_reliance` passes; replay asserts I1 to I5 on `red-fix`, including a torn metaroot; the Reliance Edge flush, barrier, early acknowledgement and damaged image controls fail as required; the CMake guard fails without the option or the control variable; `git grep 'Tuxera US Inc' -- zephyr components samples tests scripts patches` finds nothing; a build without the tree fails with the named message. |
| 6 | Reference composition and RT1060 link | 3, 5 | The composition tests pass on the simulator; the RT1060 image links with `red_format` absent; figures recorded; a composition document exists; the proposed CI change fetches the optional group and uploads no image from a Reliance Edge build. |
| 7 | Configuration on Settings File target provider | 5, 6 | The Configuration suite passes on this file system, including injected faults; W4 asserts I1 to I5; layout check before provider init is tested; a control on the unpatched backend fails as characterised. |
| 8 | Physical validation procedure | 5, 6 | A gate document in the style of `docs/microsd-physical-gate.md` exists. It authorises nothing, and no hardware is touched. |
| 9 | Assess the File service and credential store on this profile | 5 | A planning item, not scheduled in this plan: a written assessment of what they rely on from FatFs, what changes on this file system, and whether File should offer atomic replace, decided after physical evidence. |

**Approvals this needs beyond the milestone and issues the owner asked for:**

- The owner's confirmation of the licence reading in D6 before the issue 5 pull request publishes the adapter.
- Editing `.github/workflows/zephyr.yml` to fetch the optional group and run the new gate. It is a protected path and is
  proposed in issue 6's PR. `scripts/bootstrap-zephyr.sh` runs a plain `west update`, so the group needs its own step.
- Editing any other existing protected file, should one prove necessary. The design avoids this with new scripts.
- Any target or physical run of the File additions (F18). The owner's decision above covers the RT1060 link only.
- Any upstream Zephyr issue or pull request.
- Merging, and every physical step.

## Risks and limits

- **Simulator fidelity.** The simulator and the replay are models by the same author as the code they test. The negative
  controls show that the checks can fail, not that the model matches a device. Independent review should compare the model
  with the stack and the specification where available. The clear-by-read point here rests on Linux's documentation, not on
  JEDEC's text, which was not available.
- **Commercial kit difference.** The source shows that kits differ (F13). The adapter stays on the base API to limit this,
  and it must be checked against a kit before use.
- **Zephyr updates.** The flush patch is tied to the pinned file by hash, so a Zephyr update must re-derive it. This is
  intended.
- **Synchronous cost.** `sdmmc_wait_ready` and the flush poll busy-wait in the calling thread, and each commit flushes twice
  (`subsys/sd/sd_ops.c:65-90`, F3). File and Settings callers should not be high priority threads. Only a board can measure
  this.
- **One error stops writes.** A single media error makes the volume read-only until a remount (F14). The composition has no
  automatic remount, in line with D9, so the application sees it and decides.
- **Wear.** Automatic transactions on every close and create write more than a batched policy. The profile prefers safety.
  The transaction policy can be changed through the configuration directory, within the build-time check on fsync. Endurance
  is not assessed.
- **DMA and cache on the target.** The driver uses DMA with a non-cached descriptor table, while Crux images run from
  SDRAM with a data cache. F17 prevents the known line-sharing hazard at build time and the simulator counts misaligned
  transfers, but a link and a simulator show nothing about the hardware, and no board has run the alignment change, on FatFs
  or here. The bounce path's effect on speed is unmeasured.

## Open items

- **`crux_file_rename` refuses an existing destination (F18),** because of FatFs, and the File service composes replacement
  from remove and rename. On this file system that gives up the atomic replace it offers, so a cut between the two can leave
  no target. Atomic replace is reachable only through the VFS, as Settings does. Issue 9 tracks this.
- **The File service and the credential store** are built on the File contract, and their designs lean on FatFs behaviour.
  They have not been assessed on this profile, so nothing here shows that they work on it. Issue 9 tracks this.
- **The boot path** remains with #141 and M8, where F5 needs to be considered.
