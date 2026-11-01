# Storage configuration assessment

**Historical assessment — superseded for M4 storage selection.** The QSPI NOR/LittleFS and separate Settings/NVS preference below was replaced by the [30 October 2026 M4 decision in Issue #14](https://github.com/wonny-labs/crux-pilot/issues/14). The selected Pilot File path is microSD with Zephyr VFS/FatFs, while Settings file-backed persistence remains conditional on qualification. Preserve this assessment as earlier technical and Flash/XIP safety evidence, not as the current storage recommendation.

**Status: recommendation for independent review and human decision; not adopted.**
Scope: [Issue #14](https://github.com/wonny-labs/crux-pilot/issues/14) under the
existing [Storage and configuration milestone](https://github.com/wonny-labs/crux-pilot/milestone/3).
The current task permits investigation and non-destructive software checks only.
No partition, file system, Settings backend, public interface or hardware operation
is authorised by this assessment. Issues #15 and #16 remain behind their affected
technical decision gates.

## Recommendation and disposition

Retain **LittleFS on the onboard QSPI NOR as the preferred file-storage candidate**,
with **Zephyr Settings/NVS in a separate 64 KiB region**. This avoids coupling
parameter persistence to file-volume capacity, mount and formatting failures.
Prefer existing Zephyr/NXP facilities; no generic storage wrapper is proposed.

**Do not approve target writes yet.** The pinned native driver discards program/erase
command results and polls Flash busy state without a software deadline. Existing
image bounds also do not protect data. A successful compile would not resolve
those defects, prove relocation closure or qualify persistence. Approval should
first settle the provisional medium/layout/backend direction and the bounded
feasibility work described below, rather than declare the configuration safe.

## Verified baseline and evidence limits

Implementation baseline: main `fd9e341ed4c49654a8498198627a84092aad912c`, including
accepted reference-controller integration from PR #13 (`ce0b40f`). Dependencies:
Zephyr `684c9e8f32e4373a21098559f748f06915f950c9`, NXP HAL
`2c2f28ac333e995d7279777409817c4b4f92c1ec`; SDK 1.0.1/GNU Arm 14.3.0.
The manifest and native source, not current upstream defaults, govern this assessment.

Reuse the [foundation storage assessment](platform-m1-readiness.md#rom-image-placement-and-writable-storage),
[execution profile](execution-profile.md), [physical periodic-controller record](m2-physical-verification.md),
and [accepted integrated-controller handoff](m3-validation.md#final-integrated-physical-handoff).
The milestone's original references to unmerged/unfinished controller work are
historical: that integration is now accepted. Its Flash writes, persistence and
concurrent-storage behaviour were never qualified. Historical Time Pilot
milestone identifiers are distinct from the current roadmap.

The [verification record](evidence/storage-configuration/verification.json) and
[raw cross-build output](evidence/storage-configuration/build-output.txt) separate
source findings, generated software evidence and arithmetic checks. No board,
UART, debugger, programming tool or physical storage read was used. Existing bytes
in the proposed data region, the fitted chip's exact suffix, SFDP/status/protection
state, fuse state, wear and recovery behaviour remain **unknown**. No region is
assumed erased merely because no application currently owns it.

## Medium, image and linker boundaries

The pinned [board DTS](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/boards/nxp/mimxrt1060_evk/mimxrt1060_evk_mimxrt1062_qspi.dts)
and revision-B [boot configuration](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/boards/nxp/mimxrt1060_evk/xip/evkbmimxrt1060_flexspi_nor_config.c)
describe 8 MiB/64 Mbit NOR at CPU base `0x60000000`, expected JEDEC `9d 70 17`,
256-byte program pages, 4 KiB erase sectors, 64 KiB erase blocks and a one-byte
Flash API write granule. The one-byte API granule does not mean byte erase or
arbitrary overwriting: NOR programming clears bits, and reuse requires erase.
The revision-B boot configuration selects `FLEXSPI_SERIAL_CLK_120MHZ`; the DTS
permits 104 MHz for the runtime driver.
Runtime driver clock/SFDP setup is separate from the ROM's initial configuration.

[NXP's EVKB user manual](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/mcuxpresso-ide/9718/1/MIMXRT1060EVKBUM%20%282%29.pdf),
Rev. 0, table 17, identifies U24 as **IS25WP064AJBLE**, 1.8 V/64 Mbit. Do not
substitute IS25WP064D timing numbers for an unconfirmed fitted A-part. The
software geometry and prior board identification are evidence of the selected
configuration, not a fresh physical ID/geometry measurement. Obtain the exact
applicable data sheet and operating-condition maxima before setting test bounds.

Current standalone boot/XIP configuration has `FLASH_LOAD_OFFSET=0`,
`USE_DT_CODE_PARTITION=n`, `FLASH_LOAD_SIZE=0` and no MCUboot. ROM headers remain
at `0x60000000` (FCB), `0x60001000` (IVT), `0x60001020` (boot data), and
`0x60001030` (DCD). RAM sections can have Flash load addresses: protect their LMA
ranges as well as executable VMAs and the complete binary span.

| Inherited DTS label | Relative range, end exclusive | Current meaning |
| --- | --- | --- |
| `mcuboot` | `0x000000–0x020000` | Standalone ROM headers/image already occupy this area; no installed bootloader follows from the label. |
| `image-0` | `0x020000–0x320000` | Not the current standalone link base. |
| `image-1` | `0x320000–0x620000` | No configured update/rollback slot. |
| `storage` | `0x620000–0x800000` | Candidate 1,920 KiB data envelope, not currently an approved writable allocation. |

The actual linker exposes 8 MiB, so the small present image does **not** fence
future growth away from storage. Simply enabling `USE_DT_CODE_PARTITION` with
the inherited `slot0_partition` would move the image to `0x60020000`; that is
not a safe transparent change to the standalone boot arrangement.

## Native write/erase and XIP constraints

Pinned sources: [Flash driver](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/drivers/flash/flash_mcux_flexspi_nor.c),
[FlexSPI controller](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/drivers/memc/memc_mcux_flexspi.c),
[relocation configuration](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/soc/nxp/common/Kconfig.flexspi_xip),
and [NXP HAL](https://github.com/zephyrproject-rtos/hal_nxp/blob/2c2f28ac333e995d7279777409817c4b4f92c1ec/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c).

- Reads validate the device range; erase requires 4 KiB-aligned offset and length.
  Writes split at 256-byte page boundaries and normally copy input to a RAM
  buffer. Device-level range checks admit the whole chip; they do not protect
  firmware from a raw `flash_write`/`flash_erase` call. Whole-device erase has an
  explicit chip-erase path. Aligned sufficiently large requests use 64 KiB blocks.
- The XIP path locks interrupts around the **whole API request**, waits for the
  FlexSPI bus, performs program/erase and busy polling, resets controller buffers,
  then unlocks. A large request may combine many operations in one critical section.
  RAM buffering protects Flash-resident write input but is not proof that every
  called function, literal, veneer, device/API structure and error path avoids XIP.
- Build relocation selects ITCM for the Flash/JESD216/controller text on this
  SoC; NXP quick-access placement and compiler-emitted libc/cache helpers must
  also be checked in the final ELF/disassembly. Inspect VMA **and** LMA, startup
  copies and remaining calls/data references; placing a few top-level symbols in
  ITCM is insufficient. Flash and MEMC logging must remain off on critical paths.
- Prefetch must be idle before controller changes. The driver resets FlexSPI
  AHB state and invalidates the changed data-cache range. This is not a general
  instruction-cache coherence or read-while-write guarantee. Data partitions must
  never contain executed code. DMA/bus masters must not fetch mapped Flash during
  a busy interval. Ordinary IRQ masking does not make NMI/fault/zero-latency paths
  safe; vectors and exceptional execution require explicit audit.
- Enabling `CONFIG_FLASH` also adds native initialisation before application
  storage requests: probe, bus/LUT/clock setup and device-register configuration.
  The driver intentionally spins on an XIP initialisation failure because it
  cannot safely return to Flash. Even a no-write application therefore needs a
  separately approved startup/recovery check before this image can run on target.
- **Error/termination blocker:** `flash_flexspi_nor_write` and `_erase` ignore the
  returns from write-enable, program/erase and busy-wait helpers and return zero
  after their loops. `flash_flexspi_nor_wait_bus_busy` has no elapsed-time bound;
  HAL bus-idle/transfer waits add further unbounded conditions. A lower-level
  command failure may be hidden as success, or a hung operation may never return.
  A VFS/Settings timeout cannot pre-empt this IRQ-masked native loop. Readback
  alone does not repair error propagation or bound a stalled controller.

Separate regions on the same NOR/FlexSPI bus isolate addresses, not execution
latency. Existing controller work uses a 100 ms loop, 500 ms work freshness,
16 s network freshness and a 2 s hardware watchdog when explicitly armed.
IRQ-masked storage can delay timer/UART/Ethernet servicing and health observations;
an independent watchdog can still reset the processor while Flash remains busy.
Existing M3 measurements and stack margins do not cover any of this workload.
Do not feed blindly, extend freshness, disable an armed watchdog or claim suspend/
resume support merely because the NOR data sheet describes it: this driver does
not implement an application read-while-write scheduling policy.

## Proposed allocation and enforcement — approval required

Use the inherited data envelope while making standalone firmware ownership explicit.
All bounds are relative to the NOR base; ends are exclusive. These values are a
**proposal only**: no overlay, chosen node, linker or backend selection is changed.

| Proposed owner | Relative range | CPU range | Raw capacity |
| --- | --- | --- | --- |
| Standalone firmware, including ROM headers and RAM load images | `0x000000–0x620000` | `0x60000000–0x60620000` | 6,272 KiB |
| LittleFS, exclusive file-volume ownership | `0x620000–0x7f0000` | `0x60620000–0x607f0000` | 1,856 KiB / 464 sectors |
| Settings/NVS, no file-system ownership | `0x7f0000–0x800000` | `0x607f0000–0x60800000` | 64 KiB / 16 sectors |

The arithmetic is disjoint, covers exactly 8 MiB, and aligns all boundaries to
both 4 KiB and 64 KiB. Raw capacity is not usable file or parameter capacity.
The 64 KiB initial parameter budget is a small reserved fraction, not a measured
schema requirement; review it against #16's actual live set and save frequency.

Before any later write-enabled image can be accepted:

1. Replace inherited partition ownership with one standalone firmware area and
   the two disjoint data areas; remove overlapping obsolete slot declarations.
   Point the code chosen node at the **zero-offset** firmware area and the
   Settings chosen node at its separate area. This is not adoption of MCUboot,
   OTA, secure boot or a reserved second-image slot.
2. Make the actual linker region end at `0x60620000` (through the approved code
   partition/load-size configuration), and check every Flash LMA/ELF load segment,
   binary span and header against it. Require a deliberately oversized link to
   fail and an image manifest crossing the boundary to be rejected before download.
3. Route native VFS and Settings through their distinct Flash areas. Explicitly
   set `zephyr,settings-partition`; both Settings/NVS and Settings/ZMS otherwise
   fall back to `storage_partition`, which could collide with LittleFS. Validate
   area IDs/offsets/sizes at build and startup. A partition label or `read-only`
   property alone is not a security boundary against raw Flash APIs or a debugger.
4. Permit formatting only in a separately authorised maintenance procedure, with
   whole-chip erase and image-range writes excluded. Set LittleFS
   `FS_MOUNT_FLAG_NO_FORMAT`; keep NVS `NVS_INIT_BAD_MEMORY_REGION` and ZMS
   force-mount recovery off. Mount/load can itself perform native metadata repair:
   it is not a read-only operation. No automatic wipe after corruption.
5. Extend programmer/manifest checks to the approved firmware ceiling and prove
   sector erase behaviour preserves both data areas. Existing image validation
   allows load ranges up to `0x60800000` and is insufficient. Reject arbitrary
   binaries, mass erase and implicit recovery writes; retain/verify data and a
   recoverable normal image under the later physical procedure.
6. Keep crash dumps/other Flash clients disabled unless explicitly allocated.
   The board's default firmware-dump start `0x60500000` lies in the proposed
   protected firmware envelope. A data area is not free space for another owner.

## File system and Settings trade-offs

**LittleFS preference retained, not qualified.** Zephyr provides a native NOR block
adapter and VFS integration. LittleFS offers copy-on-write/metadata recovery and
wear distribution, but depends on truthful, bounded lower-layer operations.
Use explicit sync/close success as the persistence boundary; a successful buffered
write alone is insufficient. Test full/reclamation, metadata/file integrity and
interrupted operations for the exact configuration; no blanket power-loss safety
is claimed. Disable automatic formatting as above. File-volume metadata, free
blocks and open-file/cache memory reduce the 1,856 KiB raw budget.

The current `west.yml` import allowlist excludes **littlefs**, and the module is
not installed in this workspace. Pinned Zephyr's manifest selects littlefs
`8f5ca347843363882619d8f96c00d8dbd88a8e79`. Later approved integration must add
that module to the allowlist and verify the resolved revision; do not silently
upgrade Zephyr or copy vendor code. No LittleFS/backend integration is built here.

| Settings choice | Capacity, integrity and ownership | Assessment |
| --- | --- | --- |
| **NVS, separate region** | 16 proposed 4 KiB sectors, one kept empty for GC. Usable capacity is less than 60 KiB before metadata/live-copy constraints. Each record has 8-byte metadata; Settings stores name and value separately, plus index maintenance. Enable `NVS_DATA_CRC` from initial provisioning (4 additional bytes per record); full-value reads are required to check it. | Preferred for a small NOR parameter set: bounded allocation independent of file-volume full/mount/format state, no external module. Not independent of the shared Flash latency or driver defects. Explicitly set `SETTINGS_NVS_SECTOR_COUNT=16`/multiplier 1; the default count is 8, not automatically the entire proposed area. |
| **ZMS, separate region** | At least two sectors, 32-bit IDs/default ATE format; Settings adds hashed-name/list bookkeeping and collision handling. Supports NOR and overwrite-capable memories; optional data CRC and format choices matter. Default Settings/ZMS uses the whole chosen area unless custom count is selected. | Viable alternative, not ruled out. No RRAM/MRAM or measured capacity/endurance need currently justifies choosing it over NVS. Compare actual live-set and GC write amplification if NVS constraints become material; keep `SETTINGS_ZMS_FORCE_MOUNT` off. |
| **File-backed Settings on LittleFS** | Appended records, compaction/temp-file/rename and close errors; mount must succeed before Settings access. Shares space, failure, wear and formatting with ordinary files; reserve headroom for compaction, not just current file size. | Suitable only if the shared failure/capacity domain is accepted. Saves a separate raw allocation, but file-full or mount failure would also prevent parameter persistence. A directory is not capacity isolation. Not preferred for this initial controller. |

Sources: pinned [NVS documentation](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/doc/services/storage/nvs/nvs.rst),
[ZMS documentation](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/doc/services/storage/zms/zms.rst),
[Settings selection and capacity options](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/subsys/settings/Kconfig),
and the corresponding `subsys/settings/src/settings_{nvs,zms,file}.c` at that revision.

The pinned Settings/NVS initialiser rejects sector sizes above `UINT16_MAX`;
the proposal is
sixteen **4 KiB** sectors. Maximum individual values and recoverable live-set
size must leave GC/metadata room. Unchanged NVS values avoid an unnecessary write,
but name/index metadata and collection still cost writes/erases. Estimate endurance
from the fitted part's minimum erase endurance, actual save rate, live records and
measured GC amplification; no lifetime estimate is supportable from capacity alone.

Freeze backend, CRC mode, sector geometry, names and application encoding/version
before provisioning. Enabling CRC on an existing non-CRC NVS volume invalidates
its contents; NVS and ZMS formats are not interchangeable. Read all of a value,
validate length/type/range before applying it, retain defaults/last accepted values
on rejected data, and expose active-versus-persisted state. Backend metadata/CRC
cannot supply schema validity, authenticity, confidentiality or an atomic
multi-parameter transaction. Application-owned commit timing and recovery semantics
remain #16 work after approval; Settings Kconfig prose is not proof of atomicity.

**Conditional alternative:** the observed XIP/driver constraints justify considering
FatFs on microSD if bounded native NOR operation cannot be established. The board
has native `usdhc1`/SD disk support, SD_B0 bus pins, GPIO1.5 power and GPIO2.28
card detect. These differ in the pinned mux from FlexSPI SD_B1, SW5 GPIO5.0,
UART1 AD_B0_12/13 and Ethernet B1/EMC paths, but current card availability, wiring,
power and full pin ownership remain unverified. It avoids writes to the XIP NOR,
not all controller latency or corruption risk; FAT interrupted updates/removable
media and parameter placement need their own decision. No card is assumed present,
no alternative is adopted and no broad peripheral investigation is required now.

## Feasibility gates and decisions

| Gate | Evidence and predeclared disposition needed | Approval boundary |
| --- | --- | --- |
| Driver correctness and termination | Review every critical-path return/wait, narrow upstream-compatible remediation or evidenced fixed baseline, software error/stuck-busy controls, and safe timeout exit while NOR may remain busy. All accepted writes must report actual failure. Do not return into XIP merely because a timer expired. | Human decision on remediation scope before adopting a patch or dependency change; unresolved failure blocks target writes. |
| Relocation/cache/bus safety | Inspect final ELF/disassembly/call/data closure, RAM buffers/vectors, interrupts/NMI/DMA and startup/initialisation paths, including error paths and cache reset behaviour. Prove no Flash fetch while unavailable. | Compile-only evidence can support review; physical qualification needs a separately reviewed procedure. |
| Allocation and boot/download protection | Approve exact areas, zero-offset standalone boot and firmware-growth budget; test oversized image/link and boundary rejection; map all other Flash clients. Establish backup/restore and precise authorised erase ranges. | No partition adoption or first format before recorded human decision. |
| Bounded later physical feasibility | Identify fitted part/SFDP/status and existing bytes, agree applicable maximum timings, one authorised sacrificial data sector, contents/sentinels, repetitions, operation/abort bounds and recovery image. Test RAM- and Flash-sourced programming, erase/readback, near-boundary rejection and truthful errors. Flash-busy reset handling must be established before deliberate interruption. | Separate explicit board/Flash authority; current task performs none. Unknown data is not permission to erase it. |
| Controller integration | Initially propose isolated maintenance with network traffic/watchdog arming off. Establish elapsed IRQ-off times, resource use and storage errors before concurrent integration; reassess 100 ms execution/500 ms freshness/2 s watchdog without relaxing them to obtain PASS. Serialise storage ownership without prematurely selecting a new framework/thread model. | Review application health/scheduling changes before adoption. No inherited M3 approval covers storage. |
| Persistence qualification | #15: mount/CRUD/sync, full/reclamation, reboot, write/mount failures and bounded interrupted-write recovery. #16: defaults/type/range/invalid/missing data, save/load failures, reboot and interrupted-save outcomes with live-vs-persisted semantics. Predeclare dataset, raw/usable capacity, spare room, repetitions and integrity expectations. | Begin dependent implementation only after the affected #14 decisions; general power-loss/long-run/M5 claims remain excluded. |

## Non-destructive verification

The existing Linux amd64 Dev Container image, pinned SDK and cached dependencies
were used in a new container with network disabled and no board/device/socket/home
mount. Build outputs are isolated under `build/storage-assessment/`; generated
images are **compile-only, not approved for flashing**. No file-system or Settings
code, module import, partition overlay or application behaviour was added.

Reproduce the two relevant cross-builds inside the documented Linux environment:

```sh
repo="$PWD"
common="$repo/samples/controller/zephyr/m3.conf;$repo/samples/controller/zephyr/m3-integrated.conf;$repo/samples/controller/zephyr/m3-mdio-completion.conf;$repo/samples/controller/zephyr/m3-native-output.conf;$repo/samples/controller/zephyr/m3-normal.conf"
overlay="$repo/samples/controller/zephyr/m3-network.overlay"
west build -b 'mimxrt1060_evk@B//qspi' \
  -d build/storage-assessment/reference-normal samples/controller/zephyr -- \
  -DZEPHYR_EXTRA_MODULES="$repo" -DEXTRA_CONF_FILE="$common" -DDTC_OVERLAY_FILE="$overlay"
cat > build/storage-assessment/flash-compile.conf <<'CONF'
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_FLASH_LOG_LEVEL_OFF=y
CONFIG_MEMC_LOG_LEVEL_OFF=y
CONF
west build -b 'mimxrt1060_evk@B//qspi' \
  -d build/storage-assessment/flash-driver samples/controller/zephyr -- \
  -DZEPHYR_EXTRA_MODULES="$repo" \
  -DEXTRA_CONF_FILE="$common;$repo/build/storage-assessment/flash-compile.conf" \
  -DDTC_OVERLAY_FILE="$overlay"
```

Both relevant cross-builds passed. The unchanged normal configuration produces a
107,872-byte BIN; the compile-only Flash-driver variant produces 117,292 bytes
and uses 8,544 bytes of ITCM. Both generated linkers still expose **all 8 MiB**.
Selected driver, controller and HAL routines are located in ITCM; the write
buffer/LUT are in SDRAM. Disassembly also shows veneers to Flash-resident `memcpy`
and cache helpers. The buffer copy is before programming with prefetch draining,
and cache invalidation is after completion; these addresses alone neither prove
an unsafe busy-phase fetch nor establish complete closure. Final call/data and
failure-path review remains required. The build does not establish startup or
runtime success after enabling the native Flash driver.

The initial preliminary build omitted the qualified network overlay and is excluded
from reference evidence. Its log was retained separately; the relevant reference
and driver variants both use the existing overlay above. Generated configuration,
DTS, linker regions, ELF load/section information and selected symbol locations
are recorded with hashes in the verification JSON. These establish only the
reported build facts, not startup correctness, complete relocation closure,
write/erase latency or physical integrity. No LittleFS, NVS or ZMS runtime test is
claimed. The layout arithmetic independently checks contiguity, exclusivity,
device bounds and erase alignment; it does not implement those bounds.

## Issue #14 coverage and self-review

| Acceptance criterion | Assessment disposition |
| --- | --- |
| 1. Geometry, headers, image/linker, existing data and native constraints | Source/generated configuration evidence above; existing physical data and exact fitted-part/protection state explicitly unknown. |
| 2. XIP relocation/cache/interrupt/shared-bus and controller effects | Critical-path analysis above; driver error/deadline and full relocation closure remain feasibility blockers, not silently passed. |
| 3. Allocation/alignment/capacity/ownership and growth/program/format protection | Exact proposed ranges and enforcement requirements above; no partition adopted or protection claimed implemented. |
| 4. LittleFS preference or justified alternative | LittleFS preferred with pinned-module prerequisite; microSD fallback considered only for the identified material constraints. |
| 5. Settings backend choice and limits | Separate NVS recommended; ZMS/file-backed capacity, integrity, recovery, ownership and migration trade-offs recorded. |
| 6. Remaining feasibility, qualifying limits/recovery and human decisions | Gates above identify what can be decided now and what requires source remediation, affected implementation approval and separate hardware authority. |

Critical self-review: a partition label, RAM placement of selected symbols, backend
CRC, prior target success, or a clean compile cannot establish safe write-enabled
XIP operation. Checked that firmware LMA/header ownership, the ignored native errors,
unbounded waits, automatic formatting, Settings fallback-area collision, the missing
LittleFS module and unknown existing data remain explicit. Recommendations do not
change the approved backlog, platform API, current controller or historical results.
Independent technical review and the human medium/layout/backend/remediation
choices remain the delivery gate; Issue #14 stays open.
