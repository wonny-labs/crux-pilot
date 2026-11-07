# eMMC boot and update feasibility for the boot platform

| Field | Value |
| --- | --- |
| Issue | [#298](https://github.com/wonny-labs/crux-pilot/issues/298) "Investigate eMMC boot and update feasibility for the boot platform" |
| Status | **Draft. Desk results only.** No build, hardware, fuse or debugger operation was run. Nothing in this record is adopted. It makes no eMMC support claim. It adds no M8 or M9 exit criterion. |
| Pinned upstream | The pins named in [#141](https://github.com/wonny-labs/crux-pilot/issues/141): Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0), MCUboot `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2`, `hal_nxp` `2c2f28ac333e995d7279777409817c4b4f92c1ec`. **`main` now pins Zephyr v4.4.2** (`dccb09599635bdff17633fa7e9dab014b91dce90`) with MCUboot `6d3b3d2c38ab20c242e5b9abb04d050086383eb2` and `hal_nxp` `7554bc0f6f6c284972eba7b2c17b16fbb9cc0ad0`. Every Zephyr, MCUboot and `hal_nxp` file cited (rows P) was compared at both sets of pins and is identical. The Linux, U-Boot, mmc-utils and vendor rows cite one revision each. |
| crux-pilot read | `main` at `3e0a5d7` |
| Related records | The #141 decision record is in Draft PR [#151](https://github.com/wonny-labs/crux-pilot/pull/151) (branch `docs/boot-update-investigation`). The M14 design is [emmc-reliance-design.md](plans/emmc-reliance-design.md). |
| Evidence | [docs/evidence/boot-emmc/](evidence/boot-emmc/README.md) |

## Purpose and scope

#298 asks whether the boot and update approach recommended in #141 can serve a platform that boots from eMMC, loads firmware
into external SDRAM and executes from SDRAM, without assuming that a NOR-based result carries over. The pilot hardware boots
from QSPI NOR, so everything here for eMMC is either a source reading or an expectation from public documents. It informs the
public milestones M8 "Boot platform foundation and verified boot" and M9 "Secure firmware update and recovery".

Out of scope, as in the issue: any hardware, fuse or debugger operation, any manifest or dependency change, implementation of
boot or update features, adoption of any layout or bootloader, and merging or closing anything. The planned check for an
evaluation-kit analogue in the M10 provider decision was **not repeated**. No result of that check was found in `docs/`
at `3e0a5d7` or in the issues searched (titles of all issues, and the text of #176 and #299), so none is used here.

Wording that this record keeps from #141: pinned Zephyr cannot build an SD or eMMC boot image, and that the board boots from
SD without fuse settings is Not verified.

## Evidence labels

| Label | Meaning |
| --- | --- |
| Verified on pinned source | Read at a cited file and line of a pinned revision. Shows what the code allows, not what a board does. |
| Verified by public vendor document | A public vendor page, datasheet or repository says so. The vendor's statement, not a Crux result. |
| Verified in repository | Read from a committed crux-pilot document or source at `3e0a5d7`. |
| Inference | Reasoned from the rows named. The reasoning is stated. |
| Not verified | Not retrieved, not stated by any retrieved source, or needs hardware. |

Rows from Linux, U-Boot and mmc-utils are Verified on source of those projects and say what that code does, not what the eMMC
standard requires. **The RT1060 reference manual and the JEDEC eMMC standard could not be read.** Statements that rest on
them are expectations. Third-party and forum statements are hints and are not used as evidence. The evidence files say which
rows the author re-read and which a research subagent read.

## Answers in brief

| # | Question | Answer | Label |
| --- | --- | --- | --- |
| 1 | What does the boot ROM load from the boot area, and is it authenticated? | NXP's RT1060 guide says SD and eMMC are non-XIP boot devices: the ROM copies an image that starts at device offset `0x400` into RAM and starts it, with an optional DCD. That image is the first stage, here the bootloader, not the application. Whether the ROM reads an eMMC **boot partition** or the user area is Not verified, and the guide never mentions a boot partition. A Zephyr-built image has no CSF, so nothing is authenticated. Whether the ROM authenticates an SD or eMMC image on the RT1060 is Not verified. | Vendor document, pinned source. Boot partition and authentication Not verified. |
| 2 | Can the MCUboot port read slots from the eMMC user area, and what would a backend cost? | No, not as it is. The port reads slots only through Zephyr's flash map, which sits on flash-API devices. The port and its hooks contain nothing that reads a disk or an MMC, and the Zephyr tree has, by file name, no flash driver on top of a disk. A backend is a new flash-API device over the disk layer, which the port's general `zephyr,flash-controller` branch may select without a port change (a single-device assumption in the port is Not verified for slots on eMMC with the bootloader elsewhere), and it brings the SD/MMC stack and uSDHC driver into the bootloader. Size and time cost is Not measured. It is bootloader customisation of the class of variant (b) in #141 and needs its own scope decision. | Verified on pinned source. Cost Not verified. |
| 3 | How is a damaged bootloader recovered, given boot partitions and write protection? | Not established for the RT1060. eMMC has two boot partitions and tools to switch between them, which suggests two bootloader copies, but whether the RT1060 ROM boots from them or falls back to the other is Not verified. The one vendor-documented route is the ROM serial downloader with the NXP Flashloader, which needs physical access. Linux marks the boot partitions read-only by default in the host (a host flag, not card protection), power-on protection on the card is released by a power cycle, and permanent protection is irreversible, all as code and vendor-datasheet behaviour. Zephyr's stack has no support for any of it. | Mixed, see section 3. RT1060 behaviour Not verified. |
| 4 | Which stage initialises SDRAM and the SD/MMC controller before the image is loaded? | SDRAM: the DCD in the first-stage image header, which the ROM is expected to run. Zephyr does not initialise the SEMC. The ROM's DCD order for the SD and eMMC path is Not verified. SD/MMC controller: the ROM initialises it to read the first stage (details Not verified), but Zephyr's stack resets the host and the card itself, so no later stage depends on what the ROM left. | Pinned source for Zephyr. ROM behaviour Not verified. |
| 5 | What does a tested transactional data store prove about atomic image activation? | Little. What exists on `main` is a simulator model with FatFs observations and no Reliance Edge result. A passing file-system store would show old-or-new files after modelled cuts. MCUboot's activation is a trailer state machine on raw slots that depends on write order and on atomic writes of one alignment unit, neither of which the store tests. The reusable parts are the flush barrier and the replay method. | Verified in repository. Consequences Inference. |
| H | Hardware for meaningful validation | An eMMC on a uSDHC with documented ROM boot settings, switchable power for both eMMC supplies, sacrificial parts, independent EXT_CSD access and signal observation. The EVKB has an SD slot and no eMMC. See [Hardware needed](#hardware-needed). | Inference |

## 1. What the ROM loads from the boot area, and authentication

### What is verified

| Point | Result | Label |
| --- | --- | --- |
| Image type | RT1060 images are XIP or non-XIP. A non-XIP image is "usually" for NAND, SD and eMMC: the ROM loads it from the boot device into the destination memory. | Verified by public vendor document (full page text) ([V1](evidence/boot-emmc/vendor-documents.md#findings)) |
| Placement | The guide states that the SD image and the eMMC image "always" start at offset `0x400`, programmed from the `elftosb` `_nopadding.bin`. The general rule is IVT offset `0x1000` for XIP-capable memories and `0x400` otherwise. | V2, V4 |
| Contents | IVT and boot data are required. Boot data holds the start address and the length. The DCD, CSF and key blob are optional. The `elftosb` `startAddress` is where the ROM loads the image. | V3, V7 |
| Programming flows | The guide has a normal and a fast eMMC flow. Fast boot and HAB closed mode need fuses. The eMMC section does not mention a boot partition. | V5, V6 |
| What Zephyr builds | The boot-device choice has no SD or MMC entry. A non-XIP boot header exists (boot data start at the SRAM base), but the header layout, with the configuration block at the FlexSPI offset, is the FlexSPI one. | Verified on pinned source ([P1, P4, P6](evidence/boot-emmc/pinned-source-findings.md#a-zephyr-boot-header-and-boot-device-question-1-and-4)) |
| Zephyr image and HAB | `CSF_ADDRESS` is `0`, so a Zephyr-built image carries no CSF and the ROM has nothing to authenticate. | Verified on pinned source (P5) |

### What this means

- **The stage in the boot area is the first-stage bootloader** (Inference from V1, V3: the ROM loads one image of the length
  in the boot data). It is not the application. Its size limit, how it is placed in RAM and the signing details are in the
  reference manual and are Not verified.
- **Boot partition or user area is open.** The issue speaks of the "boot area". The only RT1060 document read describes
  programming at device offset `0x400` and never names a boot partition (V5). An NXP SDK driver example shows boot-partition
  access for the MMC driver, but it is generic, lists no board, and is not a statement about the RT1060 ROM
  ([H5](evidence/boot-emmc/emmc-semantics-and-hardware.md#d-hardware-the-hardware-question)). Which of the two the ROM reads,
  and which BOOT_CFG pins or fuses select it, are **Not verified** ([X1, X2](evidence/boot-emmc/vendor-documents.md#not-retrieved-so-not-verified-for-the-rt1060)).
- **A Crux change would be needed to build the image.** Pinned Zephyr has no SD or eMMC boot-device option (P1, P8). Whether the
  ROM accepts a header laid out as Zephyr does it today on SD or eMMC is not known (Inference).
- **Authentication.** The first stage of a Zephyr build is not authenticated: no CSF (P5). What an open-configuration ROM does with an
  image that has no CSF is Not verified ([#141 Stage 3 A13](https://github.com/wonny-labs/crux-pilot/pull/151)). NXP documents a signing flow and shows the ROM authenticating a RAM-resident
  image in a plugin flow (V12). It does not state the SD or eMMC behaviour for the RT1060, in open or closed
  configuration, so whether the ROM would authenticate that stage is **Not verified**. A hardware root of trust would need
  the closed configuration, which needs fuse programming. #141 recorded the same limit for NOR, and fuse programming and
  closed configurations remain product decisions.
- **Signed images and IVT offset.** The guide says `0x400` for SD and eMMC and uses `0x1000` in its signed-image examples
  (V4, V8). The two are not reconciled. Open (X7).

## 2. MCUboot reading image slots from the eMMC user area

### What is verified

| Point | Result | Label |
| --- | --- | --- |
| Slot access | MCUboot reads and writes slots only through `flash_area_read`, `flash_area_write` and `flash_area_erase`. Zephyr implements them as `flash_read`, `flash_write` and `flash_erase` on the partition's backing **flash-API device**. | Verified on pinned source ([P17](evidence/boot-emmc/pinned-source-findings.md#c-zephyr-flash-map-and-the-mcuboot-port-question-2), P24, P26) |
| Device choice | The port takes one flash device from the chosen node `zephyr,flash-controller`. For this SoC family the branch tests that the node's parent is `nxp,imx-flexspi`. Sector information comes from `flash_get_page_info_by_offs`. | P19 |
| Partitions | The default flash map is built from `fixed-partitions` nodes. The backing device is the grandparent node. | P18 |
| No SD or MMC code | None of the 76 source, Kconfig, CMake, overlay and configuration files under `boot/zephyr/` mentions `mmc`, `sdhc`, `sdmmc`, `disk_access` or `usdhc`. No file name in either MCUboot tree contains such a term. The Zephyr tree has no flash driver on top of a disk. Its only flash and disk pairing is the reverse, a flash exposed as a disk. | P22, P23 (files read and file names only) |
| Hooks | MCUboot's hooks can replace the image header read, the image validation, the update and the slot-id lookups. They do not replace the three `flash_area` calls. A `boot_go_hook` can skip MCUboot's own `boot_go()`, which would bypass its validation and revert logic (Inference). | P25 |
| Erase | The port supports devices without explicit erase (`MCUBOOT_STORAGE_WITHOUT_ERASE`) and says that some drivers emulate erase by a write. | P21 |
| Whole-image read | RAM load copies an image with one `flash_area_read` of the whole image into the destination. | P24 |
| uSDHC and card | The Zephyr uSDHC driver and the SD/MMC stack bring the host and card up themselves. The stack has a 512-byte sector, no boot-partition, write-protect, RPMB or reliable-write code, and turns the eMMC cache on. | P13 to P16, [E17 to E19](evidence/boot-emmc/emmc-semantics-and-hardware.md#c-zephyr-sdmmc-stack-questions-2-3-and-5) |

### Answer

**No.** At either set of pins the MCUboot port cannot read slots from an eMMC user area without new code. The port never
touches a disk, and its slot access is bound to flash-API devices.

### What a backend would be

Each item is an Inference from the rows above unless marked.

| Element | What it needs | Notes |
| --- | --- | --- |
| Flash-API device over the disk layer | A new driver that exposes `disk_access_*` on the MMC disk as `flash_read`, `flash_write` and `flash_erase`, with a write block size, an erased value, and a page layout of uniform 512-byte sectors. | No such driver exists in the pinned tree (P23). Erase would be emulated, which the port already allows (P21). Whether it is a Zephyr driver, a Crux module or an out-of-tree device is a scope decision. |
| Port device selection | Set the new device as the chosen `zephyr,flash-controller`. The FlexSPI branch of `flash_map_extended.c` then does not match, and the general branch (lines 49 to 52) selects the chosen node (P19). | May need no port change. The port assumes one flash device id for every area and one base (P19), so slots on eMMC with the bootloader running from RAM, or any second device, is an Inference and Not verified. The hooks cannot replace the `flash_area` calls (P25). |
| Devicetree | `fixed-partitions` under the new device node, and a chosen node for it. | The slot offsets must lie outside any file-system volume on the same medium. The M14 design allows its volume at a sector offset, so this is possible in principle. The volume's sector count would have to be set explicitly, not left automatic. |
| SD/MMC stack and uSDHC driver in the bootloader | Host driver, stack, clocks, pins, DMA with cache alignment (M14 finding F17: the D-cache line rule and `SDHC_BUFFER_ALIGNMENT`), and the M14 cache-flush correction. | Size, RAM, boot time and stack are **Not measured**. #141 measured the NOR MCUboot at 51,760 B (RAM load with revert) in a 128 KiB partition. That figure says nothing about the SD/MMC stack. |
| Write semantics | Alignment and ordering rules for the trailer, see section 5. | The eMMC cache needs a flush at the points where MCUboot orders its writes. |
| First-stage placement | The bootloader is loaded by the ROM into RAM, so it is linked for RAM, not for XIP NOR. | Where in RAM, and the boot header for SD or eMMC, depend on section 1 and on the reference manual. |
| Updating the bootloader itself | Boot-partition access, switching and write protection. | None of it exists in Zephyr's stack (P16, E19). This belongs to [#299](https://github.com/wonny-labs/crux-pilot/issues/299). |

**Read-only variant.** Plain RAM load without test, confirm and revert only reads slots (P24). A read-only backend would be
smaller, but #141 ranks RAM load with revert first because it provides test, confirm and revert, and those need writes
(`copy_done`, whole-slot erase).

**Relation to #141.** This is variant (b) in #141, "bootloader reads from microSD", moved to eMMC. #141 judged it not
recommended within #141: it needs a flash-area backend or hooks, a host driver and possibly a file system in the
bootloader. Reading raw slot offsets needs no file system in the bootloader. The effort is still bootloader
customisation, so it **needs its own scope decision** before any prototype. The effect on the #141 recommendation of RAM
load with a NOR slot is in [Effect on #141](#effect-on-141).

## 3. Recovering a damaged bootloader

### What is verified, and by whom

| Point | Result | Label |
| --- | --- | --- |
| Two copies exist | eMMC has two boot partitions. Linux creates `boot0` and `boot1`, each read-only by default. `mmc-utils` and U-Boot select which one is the boot partition, or the user area. | Verified on source of Linux, U-Boot and mmc-utils ([E1, E3, E4](evidence/boot-emmc/emmc-semantics-and-hardware.md#a-boot-partitions-and-write-protection-question-3)) |
| Write protection | Power-on protection of a boot partition is a feature of the card. A public datasheet that restates the standard says a power cycle or a hardware reset releases it. Permanent protection and the bits that disable protection are one-time-programmable. | E1 to E3, E7, E8, E10. Datasheet is a restatement. |
| Reset effects | Linux writes PARTITION_ACCESS back to 0 at every card initialisation. U-Boot's code comment says the internal partition resets to the user partition at every CMD0 and sets its tracked partition to 0. | E6 |
| Zephyr | The stack has no access to PARTITION_CONFIG, boot partitions, boot write protection or RPMB. | P16, E19 |
| Serial downloader | The ROM has a serial downloader over UART or USB-HID. NXP's Flashloader, run from it, programs SD and eMMC, with `blhost` memory identifiers `0x120` and `0x121`. The guide shows it running from serial downloader mode. | Verified by public vendor document ([V9, V10](evidence/boot-emmc/vendor-documents.md#findings)) |
| ROM re-entry | The pinned RT1062 ROM API has `ROM_RunBootloader(arg)`. The meaning of `arg` is in the reference manual. | Verified on pinned source (P9). Argument Not verified. |

### What is not verified

- Whether the RT1060 ROM boots from boot partition 1, boot partition 2 or the user area, which one it selects, and what it does
  when the image there is invalid, including a fall back to the other partition or to the serial downloader (X1, X4).
- Whether a CMD0 releases power-on protection, whether a CMD6 write of the boot-partition enable bits is atomic under power
  loss, and what the disable bits do (E9, E12, Y1). The standard could not be read.
- Whether the current Flashloader programs an eMMC on the RT1060 and on which uSDHC (X6). A community report of a failure
  behind an SD adapter is a single unverified hint.

### What this means

- **Expectation, not result:** two boot partitions give room for two bootloader copies, and `mmc-utils` and U-Boot show that the
  enable bits can be switched. Whether that gives automatic recovery on the RT1060 depends on the ROM (Inference).
- **Hardware recovery exists on paper:** the serial downloader with the Flashloader can rewrite the medium, but it needs
  physical access to the boot-mode switches and a host tool. It is not field recovery.
- **Write protection constrains updates.** A bootloader update from the application needs the boot partition writable at
  that time. Power-on protection set at every boot would need a power cycle to release it. Permanent protection would end
  any bootloader update and any in-field recovery of that partition. Permanent protection, fuse settings and closed
  configurations stay product decisions. This record recommends nothing about them.
- **Software gap:** no boot-partition handling exists in Zephyr's stack (P16, E19). The options belong to
  [#299](https://github.com/wonny-labs/crux-pilot/issues/299), for which this section is the eMMC input.

## 4. Which stage initialises SDRAM and the SD/MMC controller

| Point | Result | Label |
| --- | --- | --- |
| SDRAM start-up in Zephyr | The EVK board enables the DCD and SDRAM by default. The DCD in the boot header programs clocks and the SEMC. The EVK board dtsi has no `semc` reference and `soc.c` does not initialise the SEMC. The board build warns that a RAM base in SDRAM without a DCD may not boot. | Verified on pinned source (P10 to P12). DCD content: [#141 Stage 3 D7](https://github.com/wonny-labs/crux-pilot/pull/151). |
| DCD in the vendor documents | The DCD is optional, "usually" for SDRAM or DDR, found through the IVT. `elftosb` places it at `0x40` after the IVT offset. | V3, V7 |
| Does the ROM run the DCD, and when | Not stated in any RT1060 text retrieved, for the SD or eMMC path: the order against the copy of the image, and the allowed commands. | **Not verified** (X5) |
| uSDHC and card at hand-over | Which mode the ROM uses and what state it leaves the card in: not stated. | **Not verified** (X5) |
| Zephyr's own initialisation | The Zephyr uSDHC driver applies the pin state, initialises and resets the controller, and changes pin states with the clock. The stack toggles the card's power through the host, sends CMD0 and runs the full identification. The EVK node sets a power delay of 1000 ms. | Verified on pinned source (P12 to P14, E18) |

### Stage sequence this implies

Expectation for an eMMC boot, built from the rows above. Step 1 is an expectation from vendor documents. Step 2 rests on pinned
source for the Zephyr stack and is an Inference for the slot read through a backend that does not exist. Step 3 is an Inference.

1. **Boot ROM.** Reads the first stage from the boot device (section 1), runs the DCD, which brings up the SDRAM (expected, X5).
2. **First stage (bootloader).** Initialises uSDHC and the card with the Zephyr stack, from reset, so nothing the ROM left is
   relied upon (P13, P14, Inference). Reads the slot into SDRAM, validates it and jumps.
3. **Application.** Initialises the stack again where it needs it, which also resets the host and the card.

Two points that matter for the design, both Inference:

- **If the first stage is itself loaded into SDRAM, SDRAM must be up before the ROM copies it.** The non-XIP boot data in
  Zephyr defaults to the SRAM base, which is SDRAM on this board (P4, P11). Whether the ROM runs the DCD before the copy is
  Not verified. Linking the first stage into on-chip RAM avoids the question. The #141 trial links the RAM-load
  MCUboot into OCRAM2 for a different reason, the load-window clash with SDRAM (#141 Stage 5, in PR #151).
- **Nothing in Zephyr initialises the SEMC in software**, so SDRAM start-up stays a DCD matter for every stage that needs it
  (P12). The DCD is board-specific, and its SDRAM timings belong to the SDRAM part.

## 5. What a tested transactional data store proves about atomic image activation

### What exists on `main`

| Point | Result | Label |
| --- | --- | --- |
| Design | The M14 design covers a data file system on the eMMC user area. Boot partitions, RPMB, reliable write and a bootloader that reads the file system are out of scope. The boot path is left to #141 and M8. | Verified in repository ([design](plans/emmc-reliance-design.md), D8, open items) |
| Flush defect and correction | At the pin, a disk sync does not flush the eMMC cache. A build-local correction (`patches/zephyr/mmc-cache-flush.patch`) is on `main`. | Verified in repository. Defect: Verified on pinned source (E17). |
| Simulator and replay | An eMMC simulator and a power-cut replay on FatFs are on `main`. Their record says every result is an observation of the simulator's model, and none of it is evidence about power loss on an eMMC device. FatFs fails the file-replacement workload in every model, which the record traces to its rename order. | Verified in repository ([replay record](evidence/emmc-reliance/replay-fatfs/README.md)) |
| Reliance Edge | The adapter, the replay on Reliance Edge and the invariants the design is built to assert are **not on `main`**. The tree has the design and plan documents, an optional `reliance-edge` project in `west.yml` (group `crux-reliance`, off by default), and a replay runner that rejects `--fs reliance` with an error. `patches/zephyr/README.md` says the adapter does not exist yet. Issue #253 is open. | Verified in repository. No result exists. |

### What a passing store would show, and what it would not

A passing replay would show, **inside the simulator's model**, that after modelled cuts a replaced file is the complete old or
the complete new version, and that acknowledged data survives. That bears on **publishing** an image or an update record as
a file.

It would not show atomic **activation**, for these reasons (Inference unless noted):

| Reason | Detail |
| --- | --- |
| Different state machine | MCUboot activation is a trailer state machine on raw slots. A test request writes the magic first, then `image_ok` when confirming, then the swap info, each through `flash_area_write` (P26, verified on source). Its safety rests on those writes reaching the medium in that order and on each write being atomic for one alignment unit. A file-system store tests neither the order nor the raw trailer. |
| Ordering | On an eMMC with a cache, writes can reach the medium out of order unless flushed. The M14 work found this for the stack (E17) and corrected it for the file-system path. A backend would need a flush between every pair of writes that MCUboot orders. Whether the cache preserves order without a flush is Not verified (E16). |
| Write granularity | The stack's sector is 512 bytes (P15). MCUboot's trailer flags are written in units of the flash write alignment. A backend that reports a small alignment would update a flag by rewriting a whole sector, and a torn sector can damage neighbouring flags. Whether a single-sector write is atomic is Not verified (E16), and the M14 design assumes it is not (`fAtomicSectorWrite` is false). A backend would have to report the sector as its alignment, which would probably enlarge the trailer and needs a `BOOT_MAX_ALIGN` of at least 512 because `boot_write_trailer` rejects a larger alignment (P26), or use reliable write, which Zephyr's stack lacks (P16). Both are Inference. |
| Boot-partition switch | If activation were done by switching the boot partition, the atomicity of that CMD6 write under power loss is Not verified (E12, Y1). |
| Out of scope of the store | The ROM does not read through a file system, and M14 excludes the boot partitions and a bootloader that reads the file system. |
| Integrity is separate | MCUboot validates the image hash and signature on every boot in RAM load. A torn image is detected by that check and not by the store. |

### What is reusable

- The **flush correction** is a prerequisite for any write ordering on eMMC, for a file system or for a flash-area backend.
- The **replay method** (cut models, a medium rebuilt per cut position, negative controls that must fail) can be applied to
  the `flash_area` calls of a backend, driven with MCUboot's real trailer sequences, on the existing simulator. That would
  be software-only work. It is proposed in [Open questions](#open-questions-and-owner-decisions), not started.
- Any atomic-activation claim needs its own evidence on the backend. A claim from the file-system store would not carry over.

## Hardware needed

All of this is Inference from the evidence files ([emmc-semantics-and-hardware.md](evidence/boot-emmc/emmc-semantics-and-hardware.md),
H1 to H7), apart from the board facts, which are Verified on pinned source or by public vendor document.

| Need | Why | State today |
| --- | --- | --- |
| An RT1060 or RT1062 board with an eMMC on a uSDHC whose ROM boot pads, boot-mode selection and fuse settings are documented, with RST_n and the 8-bit pad set available | The ROM boot path is the first unknown (section 1) | **The EVKB does not offer it.** Its public pages and Zephyr's and NXP's SDK files show an SD slot on USDHC1 and no eMMC. The 8-bit pad set of USDHC2 is taken by FlexSPI on the EVK as Zephyr configures it, and the schematic was not retrieved. |
| Switchable power for both eMMC supplies, under test control, with defined timing | A cut must land during a write, a CMD6 switch or a cache flush. An MCU reset does not remove eMMC power. | Not available on the EVKB for an eMMC |
| Sacrificial eMMC parts | Permanent protection, the disable bits and `RST_n_FUNCTION` are one-time-programmable | Not available |
| Independent access to EXT_CSD and raw boot partitions, for example a Linux host with an MMC-capable controller and `mmc-utils` | To set up and check the unit under test without the unit's own stack | Not established. Whether a USB card reader passes eMMC commands is not shown by any source read. |
| Signal observation of CLK, CMD, DAT0, RST_n and the boot-mode pins, and a debug probe that is independent of eMMC power | To see boot-mode commands, acknowledgements and retries | A J-Link and the EVKB exist (`docs/baseline.md`) |
| External tooling for PARTITION_CONFIG and write protection, or a separately approved Zephyr extension | Zephyr's stack has none (P16, E19) | Not available |

**Option for a first, partial step.** The EVKB's TF socket with an eMMC-on-microSD adapter could exercise Zephyr's MMC stack,
EXT_CSD handling, the cache behaviour and power cuts of the user area on a four-bit bus. It would not show ROM boot from eMMC,
because whether the ROM boots an eMMC from that socket, and the signalling level, are not established (H5, H6). Third-party
adapters and modules exist, and none was shown to work with the RT1060, so they are listed in the evidence and are not
recommended. This record proposes no hardware operation. Each one needs its own approval for the task.

## Effect on #141

#141 is a Draft and is not adopted. These are effects on its wording and recommendations, for the owner. Neither record's
status changes.

| #141 point | Effect |
| --- | --- |
| Boot media row (D1 to D5): pinned Zephyr cannot build an SD or eMMC boot image | Confirmed at both sets of pins (P1, P8). D3 and D4 are unchanged. D5 is partly answered: the vendor guide documents the SD and eMMC flows, but SD boot on the EVKB without fuse settings is still Not verified. |
| "Recovery of a corrupted bootloader" Not verified | Narrowed for eMMC (section 3). Still Not verified for the RT1060 ROM. |
| Rank 1, RAM load with revert, and the staging variant (c) with a NOR slot | Unaffected for the pilot hardware. For an eMMC platform the slot lives on eMMC and variant (b) becomes the staging path, with the cost in section 2. The NOR evidence (#22 data closure, write extents) probably does not carry over to eMMC, and the eMMC ordering and atomicity questions in section 5 are new (Inference). |
| Write inventory by actor | On eMMC the bootloader writes through a block device with a cache, not through FlexSPI during XIP, so the #22 closure matrix does not apply. It is replaced by the flush and atomicity questions of section 5 (Inference). |
| Trial builds | They were NOR builds. None was run for eMMC. |
| Pins | #141 names Zephyr v4.4.0. `main` pins v4.4.2. Its cited files are identical at both. Its build trial used the older tree, as it states. |

## Open questions and owner decisions

| # | Question | Recommendation |
| --- | --- | --- |
| 1 | The RT1060 reference manual and the JEDEC eMMC standard decide most Not verified rows (X1 to X5, X7, Y1). Can the owner supply them, or approve a download of a named copy? | Supply the reference manual chapter System Boot and JESD84-B51x. This removes the largest uncertainty at no hardware cost. |
| 2 | Scope for an eMMC flash-area backend (variant (b) for eMMC) in the bootloader, as the next step after this record | Do not start before question 1. Then decide between a read-only RAM-load first stage and a full backend, and decide where the driver lives (Zephyr, Crux module or out of tree). |
| 3 | Software-only replay of MCUboot's trailer sequences on the existing eMMC simulator, to test ordering and alignment assumptions (section 5) | Propose it as a follow-up issue under M14 or M8 after the scope in question 2. It needs no hardware. |
| 4 | Is there an eMMC-bearing RT1060 or RT1062 target for M8 and M9 validation, or does the pilot hardware remain the only one? | Owner decision. Until one exists, eMMC boot and update stay unvalidated, and the NOR results stay the only evidence. |
| 5 | Fuse programming, closed configuration and permanent boot-partition protection | Remain product decisions, as in #141 and #299. Nothing in this record needs them. |
| 6 | Input to #299 | Section 3 is the eMMC input. The first gap to close there is how the ROM chooses between boot partitions. |

## Not verified

| Item | Needs |
| --- | --- |
| Boot partition versus user area for the RT1060 ROM, the pins or fuses that select SD or eMMC, bus width, fast boot values, the uSDHC instance | Reference manual |
| The ROM's behaviour for an invalid image, fallback, BOOT_MODE versus BT_FUSE_SEL, and HAB failure handling in open and closed configuration | Reference manual, security reference manual |
| Whether the ROM runs the DCD before the copy of a non-XIP image, and the state of uSDHC and the card at hand-over | Reference manual |
| Whether the ROM authenticates an SD or eMMC image, and the IVT offset of a signed one | Reference manual, CST guide |
| Whether the ROM accepts a Zephyr-built non-XIP header on SD or eMMC | Reference manual, a board |
| Size, RAM and time of the SD/MMC stack inside MCUboot | A build of a prototype, which needs approval |
| The standard's reset behaviour per EXT_CSD byte, the effect of CMD0, the atomicity of CMD6 writes and single-sector writes, and cache ordering | JESD84-B51x |
| Whether the current Flashloader programs an eMMC on the RT1060 | Flashloader source or reference manual |
| Whole-tree absence of SD or MMC handling in MCUboot and Zephyr | Content search of the pinned trees |
| Any behaviour on a board | Hardware, outside #298 |

## Method

| Item | How it was done |
| --- | --- |
| Source access | `gh api` and `raw.githubusercontent.com` for the pinned revisions of Zephyr, MCUboot and `hal_nxp`, at both sets of pins. Nothing was cloned and no dependency checkout was touched. |
| Documents | Public NXP and SPSDK pages. Four pages of the RT1060 Manufacturing Guide were read in full text by the author. Other documents were read through a summarising fetch or local text extraction by research subagents, and the evidence files say which. |
| Linux, U-Boot, mmc-utils | Fetched at the revisions named in the evidence. The key rows were re-read by the author. |
| Repository | `git show` and file reads at `3e0a5d7`. |
| Builds, tools, hardware | None. |

This record does not claim that any image boots from eMMC, that the RT1060 ROM boots from an eMMC boot partition, that a
bootloader can be recovered, that MCUboot works on eMMC, that a flash-area backend is sound or cheap, that the flush
correction or a transactional store makes image activation atomic, that any hardware option works, or that any M8 or M9
criterion is met.
