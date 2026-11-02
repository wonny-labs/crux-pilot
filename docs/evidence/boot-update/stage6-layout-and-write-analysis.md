# Stage 6: fenced layout proposal and NOR write analysis (RT1060-EVKB)

| Field | Value |
| --- | --- |
| Date | 2 November 2026 |
| Status | **Proposal and analysis only.** No layout, bootloader, manifest change or execution model is adopted. The layout below is a **PROPOSAL**. It needs owner decision A3 and later implementation approval. |
| Inputs | [Stage 4](stage4.md) placement, [Stage 5](stage5-trial-builds.md) build outputs (results archive SHA-256 `8e2e9839eae863b79a42a7a294a97e39f093a83c3f48d0598f9fb582f01cd3ed`), the [#22 phase matrix](../../flash-xip-evidence.md), Stage 1 and Stage 2 rows |
| Evidence class | Cross-compilation outputs (build-only), pinned-source rows and a small static disassembly of the archived MCUboot ELF files (section 3). No hardware result exists. |
| Labels | **Verified by build** (read from the trial outputs), **Verified on pinned source** (Stage 1 and Stage 2 rows), **Inference**, **Not verified**. "Verified on hardware" is never used. |
| Source column | Results paths are relative to the results directory of the archive. `A/` is `build-a-ramload-revert/`, `B/` is `build-b-swap-offset/`. |

## 1. Fenced layout proposal (PROPOSAL, not adopted)

### 1.1 What was checked

| Check | Result | Label | Source |
| --- | --- | --- | --- |
| Board devicetree partitions in the trial builds | `boot_partition` (`mcuboot`) `0x0` size `0x20000`, `slot0_partition` (`image-0`) `0x20000` size `0x300000`, `slot1_partition` (`image-1`) `0x320000` size `0x300000`, `storage_partition` (`storage`) `0x620000` size `0x1e0000`. Unchanged by the Build A overlays. | Verified by build | `baseline/dts-partitions-raw.txt`, `A/mcuboot/zephyr.dts`, `B/mcuboot/zephyr.dts` |
| Same partitions in Stage 4 | Stage 4 recorded the same four ranges from the pinned board file | Verified on source (Stage 4) | [stage4.md](stage4.md) section 4.2 |
| Erase and write block in the devicetree | `erase-block-size = <0x1000>`, `write-block-size = <0x1>` | Verified by build | `*/checks.txt` section 5 |
| Bootloader link fence | MCUboot `CONFIG_FLASH_LOAD_OFFSET=0x0`, `CONFIG_FLASH_LOAD_SIZE=0x20000`. Linker FLASH region 128 KiB. Code partition `boot_partition`. | Verified by build | `*/mcuboot/dot-config.txt`, `*/checks.txt` section 4, `A/mcuboot/dts-key-nodes.txt` |
| Application link fence | `CONFIG_FLASH_LOAD_OFFSET=0x20000`, `CONFIG_FLASH_LOAD_SIZE=0x300000`. Build B linker FLASH region 3,141,482 B (slot minus the 4,246 B footer estimate). Build A FLASH region 3,145,530 B, unused, because the image is linked to SDRAM. | Verified by build | `*/app/dot-config.txt`, `*/checks.txt` section 4 |
| Slot size given to imgtool | `--slot-size 3145728` (`0x300000`) in both builds | Verified by build | `*/imgtool-args-app.txt` |
| Rejection of an oversized image | Not tested. No oversized link or oversized signed image was built. | Not verified | None |

### 1.2 NOR layout

All offsets are relative to the NOR base `0x60000000`. Ends are exclusive. The
proposal keeps the upstream board partitions unchanged for both modes. No
partition overlay is needed for the layout itself.

| Region | Offset | End | Size | 64 KiB aligned | RAM load with revert (Build A) | Swap using offset (Build B) |
| --- | --- | --- | --- | --- | --- | --- |
| `mcuboot` | `0x000000` | `0x020000` | 128 KiB | Yes | MCUboot with the NXP boot header: FCB at `0x0`, IVT `0x1000`, boot data `0x1020`, DCD `0x1030`, vectors `0x2000`. 51,760 B used, headroom 79,312 B. | Same header. 53,940 B used, headroom 77,132 B. |
| `image-0` | `0x020000` | `0x320000` | 3 MiB | Yes | Slot 0. Holds a signed image (header `0x400`). MCUboot copies it to SDRAM `0x80000000`. Image footer estimate 198 B at the slot end. | Primary slot. The application executes in place, vectors at `0x60020400`. Link limit 3,141,482 B. |
| `image-1` | `0x320000` | `0x620000` | 3 MiB | Yes | Slot 1, equivalent to slot 0 (the highest valid version is loaded, B65) | Secondary slot. The update starts at the second 4 KiB sector, `0x321000` (S2-9, pinned source, not shown by the build). |
| `storage` | `0x620000` | `0x800000` | 1,920 KiB | Yes | No owner proposed. See section 1.4. | Same |

Sources: section 1.1, `*/checks.txt`, `*/mcuboot-boot-header.txt`,
`*/mcuboot/nm-n.txt`, `B/app-flexspi-window-report.txt`. The 64 KiB alignment
column is arithmetic on the offsets.

### 1.3 RAM layout (Build A only)

| Region | Range | Use | Source |
| --- | --- | --- | --- |
| OCRAM2, trimmed | `0x20200000` to `0x2027ff00` | MCUboot RAM (`zephyr,sram` of MCUboot). Used `0x20200000` to `0x20203940`. | `A/mcuboot/dts-key-nodes.txt`, `A/mcuboot/nm-key-symbols.txt` |
| `RetainedMem` | `0x2027ff00` to `0x20280000` (256 B) | Shared boot information from MCUboot to the application. Excluded from both images' RAM. | `A/*/dts-key-nodes.txt`, `A/checks.txt` section 4 |
| SDRAM | `0x80000000` to `0x82000000` (32 MiB) | MCUboot executable RAM window. Application code, data and stack (used up to `0x8000a240`). | `A/checks.txt` section 1, `A/app/nm-key-symbols.txt` |

In Build B, MCUboot keeps the board default `zephyr,sram` (SDRAM) and uses
27,904 B from `0x80000000` (`B/checks.txt` section 4). The application later
uses the same SDRAM. MCUboot has finished by then, so this is not a conflict.

### 1.4 Storage region

The `storage` partition has no owner. #14 selected microSD for M4 file data
([storage assessment](../../storage-configuration-assessment.md), marked
superseded for M4 storage selection). The earlier NOR proposal in that
assessment (LittleFS and a 64 KiB Settings/NVS area inside this range) is
historical.

| Option | Effect | Trade-off |
| --- | --- | --- |
| Kept: declared at `0x620000`, no owner and no writer in M8 | No partition change. Slot offsets stay where they are. A later NOR data decision can use it without moving slots. | 1,920 KiB unused. Settings/NVS and Settings/ZMS fall back to `storage_partition` if enabled without an explicit `zephyr,settings-partition` (storage assessment, "Before any later write-enabled image"). A composition that enables them would write here. |
| Removed: no data partition on NOR | Makes it explicit that no data writer owns NOR. The space could be given to the slots. | Needs a devicetree overlay. Larger slots make a whole-slot revert erase and a padded image larger (section 2 and owner decision 13). |
| Moved to microSD | Matches the M4 decision for file data | Not a NOR layout question. It does not remove any update write to NOR. |

**Recommendation in this proposal: kept, with no owner and no writer in M8.**
The use of the region stays an owner decision. Any composition that enables
Settings on NOR must name its partition explicitly.

### 1.5 Slot size

The proposal keeps 3 MiB slots. Current images are far smaller (30,688 B to
37,536 B in the trial, up to 107,872 B in Stage 4). Under RAM load with revert a
revert erases the whole slot (B61, B65), and a padded image is the whole slot
size (owner decision 13). Smaller slots would reduce both. Changing the slot
size needs a devicetree overlay and is not recommended before the erase time of
a whole slot is measured on the board. This is an option for the owner, not a
recommendation.

### 1.6 Erase unit and alignment

All four proposed boundaries are 64 KiB multiples. The devicetree declares
4 KiB erase blocks, and MCUboot derives its sector layout from the driver
(`BOOT_MAX_IMG_SECTORS_AUTO=y`, both builds). The storage assessment records
4 KiB sectors and 64 KiB blocks for this NOR, with 4 KiB erase alignment in the
native driver and 64 KiB blocks for large aligned requests. The LinkServer Flash
algorithm used on this board detected the 8 MiB device as 128 sectors of 64 KiB
and wrote in 64 KiB sectors
([#14 comment of 30 October 2026](https://github.com/wonny-labs/crux-pilot/issues/14#issuecomment-6530776373)).
That does not show that 4 KiB erase is unavailable. **Keep all partition
boundaries 64 KiB aligned until the erase size the update path relies on is
measured on the board.** Swap using offset places its update one 4 KiB sector
into the secondary slot (S2-9) and rounds its status to 4 KiB (Stage 5
section 9). It therefore depends on 4 KiB erase working as declared. RAM load
with revert writes a small trailer and erases whole slots, which are 64 KiB
multiples. Both statements are Inference from the cited rows.

## 2. NOR write inventory

### 2.1 Common build facts for every bootloader write

These facts hold for every bootloader row below, in both builds.

| Fact | Detail | Label | Source |
| --- | --- | --- | --- |
| MCUboot executes in place from the same NOR it writes | Code partition `boot_partition`. Text, `rodata`, vectors and all MCUboot logic, including `flash_area_write` and `flash_area_erase`, are at `0x6000xxxx`. | Verified by build | `*/mcuboot/dts-key-nodes.txt` (A), `*/mcuboot/readelf-l.txt`, `*/mcuboot/nm-n.txt` |
| FlexSPI driver linked | `flash_mcux_flexspi_nor.c` with `CONFIG_FLASH_MCUX_FLEXSPI_XIP=y` and `XIP_MEM="ITCM"`. NOR, MEMC and HAL transfer functions run from ITCM (`.itcm_text_reloc`, run address `0x0`, load address `0x600022c0`, `0x2160` B). | Verified by build | `*/mcuboot-flash-driver.txt`, `*/mcuboot/readelf-l.txt`, `*/mcuboot/nm-n.txt` |
| Closure against the #22 matrix | Not shown. See section 3. | **Not verified** | Section 3 |

### 2.2 RAM load with revert (Build A)

| Phase | Actor | Write | Where and extent | Executes from during the write | Linked in the trial build | Label |
| --- | --- | --- | --- | --- | --- | --- |
| Download | Application | Signed image into the inactive slot | Inactive slot, image size (up to 3 MiB if padded) | SDRAM | No. The trial application has `# CONFIG_FLASH is not set` and no update code. | Path Verified on pinned source (S2-2, B63a). Not in the trial build (Verified by build). |
| Pending (test request) | Application | Trailer magic, and swap information, in the target slot | Slot trailer | SDRAM | No | Verified on pinned source (S2-2, S2-3). The stock request always targets slot 1 (B63a). A padded image carries the magic already (Inference). |
| Copy | Bootloader | `copy_done` in the selected slot's trailer, on first selection. The copy itself only reads NOR into SDRAM. | Slot trailer (small) | NOR (XIP) | Yes: `boot_write_copy_done`, `boot_write_trailer`, `boot_write_trailer_flag`, `boot_load_image_to_sram` | Function presence Verified by build. Behaviour Verified on pinned source (B61). |
| Confirm | Application | `image_ok` in the active slot's trailer | Active slot trailer | SDRAM | No. `boot_write_img_confirmed` is not in the application ELF. | Verified on pinned source (B63). Not in the trial build. |
| Revert | Bootloader | Erase of the whole slot of a selected but unconfirmed image, or of a selected image without GOOD magic | Whole slot, 3 MiB | NOR (XIP) | Yes: `boot_scramble_slot`, `boot_scramble_region`, `boot_erase_region` | Function presence Verified by build. Behaviour Verified on pinned source (B61, B62 as corrected). |
| Failure | Bootloader | Erase of an image that fails to load or validate, in either slot | Slot | NOR (XIP) | Yes: `boot_remove_image_from_flash`, `boot_scramble_slot` | Function presence Verified by build. Behaviour Verified on pinned source (B65). |

With the default signed image of the trial (no `--pad`, no `--confirm`), the
revert row applies on the first selection of a newly written image unless the
application writes the magic (Stage 5 section 9, Inference).

### 2.3 Swap using offset (Build B)

| Phase | Actor | Write | Where and extent | Executes from during the write | Linked in the trial build | Label |
| --- | --- | --- | --- | --- | --- | --- |
| Download | Application | Signed image into the secondary slot from its second sector | Secondary slot, image size | NOR (XIP), the open #22 case | No. `# CONFIG_FLASH is not set`, no update code. | Verified on pinned source (S2-2, S2-9). Not in the trial build. |
| Pending | Application | Magic and swap information in the secondary trailer | Secondary trailer | NOR (XIP) | No | Verified on pinned source (S2-2, S2-3) |
| Swap | Bootloader | Sector-by-sector swap of both slots up to the larger image size, swap status and swap information | Both slots, up to the larger image size, plus trailer and status sectors | NOR (XIP) | Yes: `swap_run`, `boot_copy_region`, `boot_erase_region`, `boot_write_status`, `boot_write_swap_info`, `boot_write_swap_size`, `boot_write_magic`, `swap_scramble_trailer_sectors` | Function presence Verified by build. Behaviour Verified on pinned source (S2-4, S2-5, M `loader.c`:1141-1197, 1227). |
| Swap completion | Bootloader | `copy_done` | Primary trailer | NOR (XIP) | Yes: `swap_set_copy_done`, `boot_write_copy_done` | Same |
| Confirm | Application | `image_ok` in the primary trailer | Primary trailer | NOR (XIP) | No | Verified on pinned source (S2-2, S2-3) |
| Revert | Bootloader | Second swap of the same extent, then `image_ok` | Both slots, up to the larger image size | NOR (XIP) | Yes: as Swap, plus `swap_set_image_ok`, `boot_write_image_ok` | Function presence Verified by build. Behaviour Verified on pinned source (S2-1, S2-6). |
| Failure | Bootloader | Erase of an invalid secondary image | Secondary slot | NOR (XIP) | Yes: `boot_scramble_slot`, `boot_scramble_region` | Function presence Verified by build. Behaviour Verified on pinned source (S2-7). |

Function lists: `A/mcuboot/nm-n.txt` and `B/mcuboot/nm-n.txt`. Presence of a
function shows that the write path is linked. It does not show when it runs.

## 3. Bootloader write path against the #22 phase matrix

The phase headings are those of the [#22 matrix](../../flash-xip-evidence.md).
The #22 matrix covers the application images I and P, not MCUboot. Nothing below
closes any #22 obligation. **Closure of the bootloader write path against the
#22 matrix is Not verified** in every phase.

| Phase | Build evidence for MCUboot (both builds unless stated) | Remaining closure obligation |
| --- | --- | --- |
| Startup / driver probe | MCUboot enables the Flash driver (`CONFIG_FLASH_INIT_PRIORITY=50`, `CONFIG_MEMC_MCUX_FLEXSPI_INIT_PRIORITY=0`). The ITCM relocation segment has run address `0x0`, load address `0x600022c0` and size `0x2160`, the same values that #22 recorded for the inherited image I. MCUboot RAM is OCRAM2 in Build A and SDRAM in Build B, so Build B MCUboot also depends on the DCD having initialised SDRAM. (`*/mcuboot/readelf-l.txt`, `*/mcuboot/dot-config.txt`) | Relocation and copy order before first use, SDRAM accessibility (Build B), probe branches and failure cleanup. Not verified. |
| Normal read / idle | MCUboot reads slots through `flash_area_read`. Build A copies the image into SDRAM. Build B validates in place. (`*/mcuboot/nm-n.txt`) | When Flash is readable again after a write, and that no read overlaps a write. Not verified. |
| Entry / preparation | Not analysed | As in #22. Not verified. |
| Program / erase busy | `flash_flexspi_nor_write`, `flash_flexspi_nor_erase`, `flash_flexspi_nor_wait_bus_busy`, `memc_flexspi_transfer`, `memc_flexspi_wait_bus_idle` and `FLEXSPI_TransferBlocking` run from ITCM. **`memc_flexspi_config_0` is in NOR `rodata`** (`0x6000bfd8` in Build A, `0x6000c650` in Build B). (`*/mcuboot/nm-n.txt`) Static ELF check by the author, in both builds: `flash_flexspi_nor_wait_bus_busy` calls `memc_flexspi_transfer` (`bl` at ITCM `0xe06`). `memc_flexspi_transfer` loads the device's config pointer at `0x1fe4` (`ldr r3, [r0, #0x4]`) and dereferences it at `0x1fea` (`ldr r6, [r3]`) before it calls `FLEXSPI_TransferBlocking`. The FlexSPI controller device record (`__device_dts_ord_546`, name `spi@402a8000`, in NOR `device_area`) holds the address of `memc_flexspi_config_0` as its config pointer. | **The data-closure failure that #22 found in image I is present in the MCUboot write path of both builds**: during busy polling the bootloader reads the FlexSPI register base through a pointer into NOR, at the same instruction addresses that #22 recorded (`0x1fe4`, `0x1fea`). Instructions and pointer value: Verified by build (author disassembly of the archived ELF files, see the note below). That the device passed at run time is the shallow RAM copy that keeps this pointer comes from the #22 source analysis (S), so that step is an Inference for MCUboot. The PoC placement (P), which moved this object to SDRAM, is not used by MCUboot. Full transitive closure (stack, other constants, indirect references) is Not verified. |
| Successful completion | Not analysed | Ready status, controller reset and cache operations, return into XIP. Not verified. |
| Failure / exhausted wait | The pinned driver ignores some errors and has no software deadline (#22, S). MCUboot has `# CONFIG_WATCHDOG is not set` and `# CONFIG_BOOT_WATCHDOG_FEED is not set`. (`*/mcuboot/dot-config.txt`) | A stuck busy state in the bootloader has no software exit and no watchdog in this configuration. Board watchdog state at boot is Not verified. |
| Interrupts / exceptions | Vectors at NOR `0x60002000`. `z_arm_fault` at `0x600058b4` (A) and `0x60005fb0` (B). `z_arm_nmi` at `0x600093de` (A) and `0x60009cae` (B). All in NOR. (`*/mcuboot/nm-n.txt`) | Vector fetches, NMI and fault paths during a busy NOR. Not verified. |
| DMA / other bus masters | No DMA, networking or Ethernet driver is enabled in either MCUboot `.config`. Cache support is configured (`CONFIG_ICACHE=y`, `CONFIG_DCACHE=y`). Whether caches are on during a write was not checked. `CONFIG_MPU_ALLOW_FLASH_WRITE=y`. (`*/mcuboot/dot-config.txt`) | Bus masters left active by the boot ROM, cache coherence after writes. Not verified. |

Note on the static check: it was made by the author of this record outside
the Dev Container, with `llvm-objdump` 18.1.3 (`--triple=thumbv7em-none-eabi
--mcpu=cortex-m7`) on `*/mcuboot/zephyr.elf` from the results archive, and by
reading the device record in `*/mcuboot/zephyr.bin`. It covers only the
functions named in the busy row. It is not a closure audit.

Consequence for the mode ranking: the bootloader write path carries the same
open #22 items in both modes. Swap using offset writes much more under
bootloader XIP per update (both slots up to the image size, and again per
revert). RAM load with revert writes one trailer flag per new image and erases
a whole slot per revert. The ranking is unchanged (Inference).

## 4. Effect on M6 and M7 evidence

| Effect | Build evidence and consequence | Label |
| --- | --- | --- |
| Image identity | The application loses the NXP boot header and DCD (`qspi_flash_config`, `image_vector_table`, `boot_data`, `dcd_data` absent from Build B). Its hashes change (baseline `zephyr.bin` `4be80bc7…`, Build B `b845ee5b…`, Build A `ee0aad8f…`). Signed files differ on each signing run, while the unsigned binary and the image digest (`SHA256` TLV) do not (Stage 8). Evidence that names an application image should therefore cite the unsigned binary hash or the image digest, plus the bootloader hash and the public key hash. | Verified by build. Citation rule is a recommendation. |
| Link address | Standalone `0x60000000` (vectors `0x60002000`). Build B `0x60020000` (vectors `0x60020400`). Build A `0x80000000` (vectors `0x80000400`). | Verified by build |
| Programming and debug scripts | Two images at two offsets: MCUboot at `0x0` and the signed application at `0x20000` (`--hex-addr 0x20000`). Under RAM load with revert the default signed image would be erased on first selection (Stage 5 section 9), so programming needs a padded and confirmed image, or a confirmed image option, or application code (owner decision 13). The Build A application ELF has its LOAD segments in SDRAM, so downloading that ELF with a debugger would not program the slot. A debugger would load symbols only (Inference). The Build A trial also needs four overlay and fragment files that are not in the repository. | Offsets Verified by build. Erase on selection and debugger effect are Inference. |
| Two images to identify | MCUboot hash, signed or unsigned application hash, public key hash and MCUboot configuration must all be recorded for any evidence run. The trial shows they are available and reproducible for the unsigned binaries (Stage 8). | Verified by build |
| Timing-dependent evidence | Build A executes the application from SDRAM after a CPU copy. Build B executes in place at a new address. The copy time, boot time and run-time timing were not measured. Which existing records depend on timing is Not verified. | Not verified |
| SDRAM dependence | Build A: MCUboot RAM is on-chip (OCRAM2), but the application code, data and stack are in SDRAM. Build B: MCUboot RAM is in SDRAM, so the bootloader itself depends on the DCD. Both rely on the bootloader DCD (`dcd_data` at `0x60001030`). That the boot ROM runs it is Not verified. | Placement Verified by build. ROM behaviour Not verified. |
| Peripheral behaviour | No change expected in driver code. Not tested. | Not verified |

### 4.1 Timing recommendation: confirmed

Stage 2 recommended keeping the standalone XIP image as the reference until M7
target validation closes, and changing the reference composition at the start
of M8. **The build results confirm this recommendation.** Reasons:

1. Both trial compositions change the application identity, link address and
   programming procedure (section 4). Changing during M6 or M7 would make
   evidence collected before and after the change refer to different images.
2. The trial found no build blocker for either mode, so no earlier change is
   needed to unblock M8 work.
3. Open items must be settled before the switch: owner decision 13 (how a new
   image is marked for test and how a programmed image is confirmed), the
   programming procedure for two images, and the #22 data-closure item for the
   bootloader write path (section 3).
4. The trial ran at crux-pilot `1d6ac4f`, which is older than `f57ff22`. The
   composition must be rebuilt at the revision that starts M8.

Refinement: make the switch as one step at M8 start, covering the bootloader,
the layout, the signed-image options and the programming scripts together, so
that no intermediate composition becomes evidence.

## 5. Open items

| Item | Needs |
| --- | --- |
| Remediation of the bootloader data-closure failure (section 3), for example the PoC placement of the MEMC configuration in RAM, and its effect on an unmodified upstream bootloader | Owner decision on scope. A change to the bootloader build is outside #141. |
| Full #22 closure of the bootloader write path | Not expected within #141 |
| Rejection of an oversized link or signed image at the slot boundary | A deliberate oversized build |
| Whole-slot erase duration under bootloader XIP and watchdog behaviour | Hardware, outside #141 |
| Erase size the update path may rely on | Board measurement |
| Use of the `storage` region | Owner decision |
| Slot size | Owner decision, after the erase time is known |
