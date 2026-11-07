# eMMC boot and update feasibility for the boot platform

| Field | Value |
| --- | --- |
| Issue | [#298](https://github.com/wonny-labs/crux-pilot/issues/298) "Investigate eMMC boot and update feasibility for the boot platform" |
| Status | **Draft. Desk results only.** No build, hardware, fuse or debugger operation was run. Nothing in this record is adopted. It makes no eMMC support claim. It adds no M8 or M9 exit criterion. |
| Pinned upstream | The pins named in [#141](https://github.com/wonny-labs/crux-pilot/issues/141): Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0), MCUboot `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2`, `hal_nxp` `2c2f28ac333e995d7279777409817c4b4f92c1ec`. **`main` now pins Zephyr v4.4.2** (`dccb09599635bdff17633fa7e9dab014b91dce90`) with MCUboot `6d3b3d2c38ab20c242e5b9abb04d050086383eb2` and `hal_nxp` `7554bc0f6f6c284972eba7b2c17b16fbb9cc0ad0`. Every Zephyr, MCUboot and `hal_nxp` file cited (rows P) was compared at both sets of pins and is identical. The Linux, U-Boot, mmc-utils and vendor rows cite one revision each. |
| crux-pilot read | `main` at `3e0a5d7` |
| Related records | The #141 decision record is in Draft PR [#151](https://github.com/wonny-labs/crux-pilot/pull/151) (branch `docs/boot-update-investigation`). The M14 design is [emmc-reliance-design.md](plans/emmc-reliance-design.md). |
| Evidence | [docs/evidence/boot-emmc/](evidence/boot-emmc/README.md) |
| Reference manual source | **This revision of the record (after the first review round) reads the RT1060 Reference Manual Rev. 3, 07/2021, in a third-party-hosted copy** (`www.pjrc.com`, SHA-256 in the evidence). It was downloaded with the owner's approval in chat on 7 November 2026. It has **not** been compared with the `nxp.com` document. The same manual text is reported independently in PR [#302](https://github.com/wonny-labs/crux-pilot/pull/302), the #299 record (evidence row B-84 and others). |

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
standard requires. **The JEDEC eMMC standard could not be read.** The RT1060 reference manual was read only in a
third-party-hosted copy of Rev. 3 (label "Verified by public vendor document", with the copy named). Statements that rest on
the standard are expectations. Third-party and forum statements are hints and are not used as evidence. The evidence files say
which rows the author re-read and which a research subagent read.

## Answers in brief

| # | Question | Answer | Label |
| --- | --- | --- | --- |
| 1 | What does the boot ROM load from the boot area, and is it authenticated? | The ROM reads `BOOT_PARTITION_ENABLE` in Ext_CSD[179], selects the named boot partition with a switch command, or boots from the user partition if none is named. It copies the first 4 KB into RAM, checks the IVT (offset `0x400` for SD and eMMC), performs a DCD check and extraction, and after that copies the image to the destination in the boot data. That image is the first-stage bootloader, not the application. The ROM validates it with HAB: in Closed configuration an authentication error aborts the boot to the serial downloader (for expansion devices with a redundant image, a primary failure first switches to the secondary image), and in Open configuration errors are logged and the image runs even without authentication data. A Zephyr-built image has no CSF, so a missing CSF would not stop the boot in Open configuration and would in Closed (Inference from the manual and pinned source). Pinned Zephyr cannot build an SD or eMMC boot image, and whether the ROM accepts a Zephyr-laid-out header on SD or eMMC is not known. The ROM statements are the manual's, not board results. | Vendor document (reference manual, third-party copy) and pinned source. Offset inside the boot partition Not verified. |
| 2 | Can the MCUboot port read slots from the eMMC user area, and what would a backend cost? | No, not as it is. The port reads slots only through Zephyr's flash map, which sits on flash-API devices. The port and its hooks contain nothing that reads a disk or an MMC, and the Zephyr tree has, by file name, no flash driver on top of a disk. A backend is a new flash-API device over the disk layer, which the port's general `zephyr,flash-controller` branch may select without a port change (a single-device assumption in the port is Not verified for slots on eMMC with the bootloader elsewhere), and it brings the SD/MMC stack and uSDHC driver into the bootloader. Size and time cost is Not measured. It is bootloader customisation of the class of variant (b) in #141 and needs its own scope decision. | Verified on pinned source. Cost Not verified. |
| 3 | How is a damaged bootloader recovered, given boot partitions and write protection? | The ROM selects **one** boot partition by `BOOT_PARTITION_ENABLE`. The manual text read has **no fallback between the two boot partitions**, so an A/B scheme with `boot0` and `boot1` would need software to switch the field, which is an Inference. What the ROM does have: an error in internal boot goes to the serial downloader, a recovery device on LPSPI if its fuse is set, SD/MMC manufacture mode (1-bit bus) before the serial downloader, and a redundant image mechanism for expansion devices (a secondary image table at `0x200`, switched automatically only after a primary authentication failure in Closed configuration, or selected by the application through the ROM's Enter Bootloader API). How that mechanism relates to eMMC boot partitions is Not verified. The serial downloader with NXP's Flashloader needs physical access. Linux marks the boot partitions read-only by default in the host (a host flag, not card protection), power-on protection on the card is released by a power cycle, and permanent protection is irreversible, all as code and vendor-datasheet behaviour. Zephyr's stack has no support for any of it. | Reference manual (third-party copy), Linux, U-Boot and mmc-utils source. Behaviour on a board Not verified. |
| 4 | Which stage initialises SDRAM and the SD/MMC controller before the image is loaded? | SDRAM: the boot ROM, through the **DCD** in the first 4 KB of the image. The manual says the ROM performs a DCD check and extraction before it takes the destination and copies the image, and that the DCD may program the SEMC registers. That the DCD writes, and so the SDRAM bring-up, happen before the copy is an Inference from that order. Zephyr does not initialise the SEMC itself. SD/MMC controller: the ROM initialises the selected uSDHC port and the card to read the image (IOMUX, controller reset, identification, CMD0, bus width, boot partition switch). Zephyr's stack then resets the host, power-cycles the card where the board allows it and sends CMD0 again, so no later stage depends on what the ROM left. The state the ROM leaves is Not verified. | Reference manual (third-party copy) and pinned source. Card state at hand-over Not verified. |
| 5 | What does a tested transactional data store prove about atomic image activation? | Little. What exists on `main` is a simulator model with FatFs observations and no Reliance Edge result. A passing file-system store would show old-or-new files after modelled cuts. MCUboot's activation is a trailer state machine on raw slots that depends on write order and on atomic writes of one alignment unit, neither of which the store tests. The reusable parts are the flush barrier and the replay method. | Verified in repository. Consequences Inference. |
| H | Hardware for meaningful validation | An eMMC on a uSDHC with documented ROM boot settings, switchable power for both eMMC supplies, sacrificial parts, independent EXT_CSD access and signal observation. The EVKB has an SD slot and no eMMC. See [Hardware needed](#hardware-needed). | Inference |

## 1. What the ROM loads from the boot area, and authentication

### What is verified

Manual rows are in [vendor-documents.md](evidence/boot-emmc/vendor-documents.md#rt1060-reference-manual-rev-3-third-party-copy)
(R1 to R12, Rev. 3, third-party-hosted copy, not compared with `nxp.com`). Other vendor rows are V1 to V13 there.

| Point | Result | Label |
| --- | --- | --- |
| Image type | SD and eMMC are non-XIP boot devices: the ROM loads the image from the boot device into the destination memory. | Verified by public vendor document (V1, and R3) |
| Boot partition | For an MMC 4.x card the ROM reads `BOOT_PARTITION_ENABLE` in Ext_CSD[179] after initialisation and selects that boot partition with a switch command. If none is named, or the user partition is named, it boots from the user partition. | Verified by public vendor document, reference manual copy (R1) |
| What the ROM copies | The first 4 KB go to internal RAM. The ROM checks the IVT header, performs a DCD check, and after a successful DCD extraction takes the destination pointer and length from the boot data and copies the image there. The first 4 KB must hold the IVT, DCD and boot data. The maximum image size is 32 MB. | R3 |
| Placement | The IVT offset is `0x400` for SD and eMMC and `0x1000` for FlexSPI NOR. The Manufacturing Guide programs SD and eMMC images at offset `0x400` and never mentions a boot partition. | R4, V2, V4, V5 |
| Selection of port and device | USDHC-1 or USDHC-2, SD or MMC boot, bus width, fast boot and more are set by fuses, with GPIO overrides when `BT_FUSE_SEL` is 0. Fast boot and the HAB closed mode need fuses. | R5, V6 |
| Authentication | In Internal Boot the ROM validates the image with HAB. Closed: authenticated before it runs, errors logged, boot aborted to the serial downloader. Open: errors logged, boot continues, and the image runs even if authentication data is missing or incorrect. | R7, R8 |
| What Zephyr builds | The boot-device choice has no SD or MMC entry. A non-XIP boot header exists (boot data start at the SRAM base), but the header layout, with the configuration block at the FlexSPI offset, is the FlexSPI one. | Verified on pinned source ([P1, P4, P6](evidence/boot-emmc/pinned-source-findings.md#a-zephyr-boot-header-and-boot-device-question-1-and-4)) |
| Zephyr image and HAB | `CSF_ADDRESS` is `0`, so a Zephyr-built image carries no CSF. | Verified on pinned source (P5) |

### What this means

- **The stage in the boot area is the first-stage bootloader** (R3: the ROM copies one image of the length in the boot data).
  It is not the application. It must fit what the boot partition can hold, which Linux derives from `BOOT_MULT` in 128 KiB
  units (E1) and is a property of the eMMC part.
- **Boot partition or user area is decided by the card's configuration, not by the image.** The ROM follows
  `BOOT_PARTITION_ENABLE`, and a user area boot happens when no boot partition is enabled. An eMMC boot therefore does use the
  boot partition mechanism of the card when it is enabled, and the first stage lives in the boot partition then. Whether the
  `0x400` offset counts from the start of the boot partition is **Not verified** (X1).
- **A Crux change would be needed to build the image.** Pinned Zephyr has no SD or eMMC boot-device option (P1, P8). Whether the
  ROM accepts a header laid out as Zephyr does it today on SD or eMMC is not known (Inference): the manual requires the IVT at
  `0x400` (R4), and the Zephyr layout puts a FlexSPI configuration block at the start of the image (P6).
- **Authentication.** The first stage of a Zephyr build has no CSF (P5). By the manual, in Open configuration authentication errors do
  not stop the boot (R8), and in Closed configuration an authentication error aborts to the serial downloader, after a switch to
  the secondary image where redundant boot is set up (R8, R9). So a missing CSF would not stop a boot in Open configuration
  and would in Closed (Inference from R8 and P5). Whether the ROM accepts a Zephyr header on SD or eMMC at all is not known. This narrows #141
  A13, which was Not verified for the general behaviour. The content of the HAB events for a blank SRK hash and the
  CSF placement for SD and eMMC are still Not verified (X4, X7). A hardware root of trust needs the Closed configuration,
  which needs fuse programming. Fuse programming and closed configurations remain product decisions.
- **Signed images and IVT offset.** The Manufacturing Guide's signed-image examples use `0x1000`. The reference manual gives
  `0x400` for SD and eMMC and `0x1000` for NOR (R4), so those examples probably describe NOR boot (Inference).

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
| First-stage placement | The bootloader is loaded by the ROM into RAM, so it is linked for RAM, not for XIP NOR. | Where in RAM follows the boot data (R3). The boot header for SD or eMMC needs the IVT at `0x400` (R4), see section 1. |
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

Manual rows R1 to R12 are in
[vendor-documents.md](evidence/boot-emmc/vendor-documents.md#rt1060-reference-manual-rev-3-third-party-copy) (Rev. 3,
third-party-hosted copy, not compared with `nxp.com`). PR [#302](https://github.com/wonny-labs/crux-pilot/pull/302) reports
more rows of the same manual for the #299 questions, and this record does not repeat them.

| Point | Result | Label |
| --- | --- | --- |
| ROM chooses one partition | The ROM reads `BOOT_PARTITION_ENABLE` and selects that boot partition, or the user partition when none is named. The text read has no fallback from one boot partition to the other. | Verified by public vendor document, reference manual copy (R1, R2). Absence limited to the text read. |
| Errors | In Internal Boot an error sends the boot to the serial downloader. In Closed configuration an authentication error does the same. In Open configuration authentication errors do not stop the boot. | R7, R8 |
| Recovery steps | The ROM tries a recovery device on LPSPI if its fuse is set, then SD/MMC manufacture mode on a 1-bit bus, then the serial downloader. | R11 |
| Redundant image | For expansion devices a secondary image table at `0x200` selects a secondary image. The ROM switches to it automatically after a primary authentication failure in Closed configuration, and an application can select the redundant image through `runBootloader`. How this relates to the two eMMC boot partitions is not stated. | R9, R12 |
| Re-entry | The pinned RT1062 ROM API has `ROM_RunBootloader(arg)`. The manual defines `arg`: it can enter the serial downloader or select a redundant image after an update. | Verified on pinned source (P9) and reference manual copy (R12) |
| Two copies exist on the card | eMMC has two boot partitions. Linux creates `boot0` and `boot1`, each read-only by default in the host. `mmc-utils` and U-Boot select which one is the boot partition, or the user area. | Verified on source of Linux, U-Boot and mmc-utils ([E1, E3, E4](evidence/boot-emmc/emmc-semantics-and-hardware.md#a-boot-partitions-and-write-protection-question-3)) |
| Write protection | Power-on protection of a boot partition is a feature of the card. A public datasheet that restates the standard says a power cycle or a hardware reset releases it. Permanent protection and the bits that disable protection are one-time-programmable. | E1 to E3, E7, E8, E10. Datasheet is a restatement. |
| Reset effects | Linux writes PARTITION_ACCESS back to 0 at every card initialisation. U-Boot's code comment says the internal partition resets to the user partition at every CMD0 and sets its tracked partition to 0. | E6 |
| Zephyr | The stack has no access to PARTITION_CONFIG, boot partitions, boot write protection or RPMB. | P16, E19 |
| Serial downloader | The ROM has a serial downloader over UART or USB-HID. NXP's Flashloader, run from it, programs SD and eMMC, with `blhost` memory identifiers `0x120` and `0x121`. The guide shows it running from serial downloader mode. | Verified by public vendor document ([V9, V10](evidence/boot-emmc/vendor-documents.md#findings)) |

### What is not verified

- Whether the ROM's secondary image mechanism (R9) works across the two eMMC boot partitions, or only within the area that
  `BOOT_PARTITION_ENABLE` selects, and whether it can be driven in Open configuration. The text names Closed configuration for
  the automatic switch.
- Which error classes of the internal boot send the ROM to the serial downloader, for instance a corrupt IVT or DCD (X4).
- Whether a CMD0 releases power-on protection, whether a CMD6 write of the boot-partition enable bits is atomic under power
  loss, and what the disable bits do (E9, E12, Y1). The standard could not be read.
- Whether the current Flashloader programs an eMMC on the RT1060 and on which uSDHC (X6). A community report of a failure
  behind an SD adapter is a single unverified hint.
- Everything on a board. No fallback, redundancy or recovery step was exercised.

### What this means

- **No automatic partition fallback in the manual text read.** The ROM follows one `BOOT_PARTITION_ENABLE` value (R1, R2). Two
  boot partitions could hold two bootloader copies, and `mmc-utils` and U-Boot show that the enable bits can be switched
  from software, but the switch would be done by software, not by the ROM (Inference).
- **A ROM-level redundancy mechanism exists, with limits.** The secondary image table and `runBootloader` (R9, R12) are a
  ROM A/B path for expansion devices. Its automatic trigger is an authentication failure in Closed configuration, which
  needs fuse programming. Whether it fits eMMC boot partitions is Not verified. Using it, or the boot partition switch, is
  a design question for #299.
- **Hardware recovery exists:** a first stage whose IVT, DCD or boot data the ROM rejects would, if that counts as an internal
  boot error (X4), lead to the recovery steps and then the serial downloader (R7, R11). Damage to the content behind a valid
  header is not detected in Open configuration without a CSF (Inference from R8 and P5). The serial downloader with the Flashloader can
  rewrite the medium, but it needs physical access to the boot-mode switches and a host tool. It is not field recovery.
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
| DCD and the image copy | For SD and MMC boot the ROM copies the first 4 KB into internal RAM, checks the IVT, performs a DCD check, and after a successful DCD extraction takes the destination from the boot data and copies the image. The DCD may write the SEMC registers, which the manual names for SDRAM, and is at most 1,768 bytes. In Closed and Open configuration alike the DCD is processed if present. | Verified by public vendor document, reference manual copy (R3, R8, R10). That the DCD writes run before the copy is an Inference from this order. |
| ROM and the SD/MMC controller | The ROM configures the IOMUX from the bus-width fuse, resets the uSDHC, identifies the card at about 400 kHz, sends CMD0, switches bus width and frequency for MMC and selects the boot partition with a switch command. The USDHC port is chosen by fuse or GPIO override. | R5, R6 |
| Card state at hand-over | What state the ROM leaves the card in when the image starts, and whether the selected boot partition stays selected: not stated. | **Not verified** (X5) |
| Zephyr's own initialisation | The Zephyr uSDHC driver applies the pin state, initialises and resets the controller, and changes pin states with the clock. The stack toggles the card's power through the host, sends CMD0 and runs the full identification. The EVK node sets a power delay of 1000 ms. | Verified on pinned source (P12 to P14, E18) |

### Stage sequence this implies

Built from the rows above. Step 1 is the manual's statement, not a board result. Step 2 rests on pinned source for the Zephyr
stack and is an Inference for the slot read through a backend that does not exist. Step 3 is an Inference.

1. **Boot ROM.** Initialises the uSDHC and the card, reads the first stage from the boot device (section 1), performs the DCD check and extraction, whose writes bring up the SDRAM, and then copies the image to its destination (R3, R6). The order of the DCD writes against the copy is an Inference from R3. The step rests on the manual copy.
2. **First stage (bootloader).** Initialises uSDHC and the card with the Zephyr stack, from reset, so nothing the ROM left is
   relied upon (P13, P14, Inference). Reads the slot into SDRAM, validates it and jumps.
3. **Application.** Initialises the stack again where it needs it, which also resets the host and the card.

Two points that matter for the design, both Inference (a third, in the stage list above, is marked where it occurs):

- **The first stage can itself be loaded into SDRAM.** The non-XIP boot data in Zephyr defaults to the SRAM base, which is
  SDRAM on this board (P4, P11), and the manual says the ROM performs a DCD check and extraction before it takes the destination
  and copies the image (R3). So SDRAM can be the destination if the DCD in the first 4 KB brings it up, provided the DCD writes run before the copy (Inference). The first 4 KB must hold
  the IVT, the DCD and the boot data (R3), and the DCD is limited to 1,768 bytes (R10). That the Zephyr EVK DCD fits and
  works for an SD or eMMC image was not checked. Linking the first stage into on-chip RAM avoids the dependency. The #141
  trial links the RAM-load MCUboot into OCRAM2 for a different reason, the load-window clash with SDRAM (#141 Stage 5, in PR #151).
- **The ROM probably leaves the card pointed at the boot partition it selected.** The ROM selects the boot partition with a switch
  command (R1). A stage that did not reset the card would read that partition. Zephyr's initialisation sends CMD0 and power
  cycles the card where the board allows it (P14, E18), and the Linux and U-Boot code treats CMD0 as returning to the user
  partition (E6). The final state is Not verified (X5), so the first stage should not rely on it (Inference).
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
| An RT1060 or RT1062 board with an eMMC on a uSDHC that the ROM can select (USDHC-1 or USDHC-2, R5), with the boot fuses or GPIO overrides settable, RST_n available and the 8-bit pad set free | The ROM boot path is the first unknown (section 1) | **The EVKB does not offer it.** Its public pages and Zephyr's and NXP's SDK files show an SD slot on USDHC1 and no eMMC. The 8-bit pad set of USDHC2 is taken by FlexSPI on the EVK as Zephyr configures it, and the schematic was not retrieved. |
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
| "Recovery of a corrupted bootloader" Not verified | Narrowed (section 3). The manual copy gives the error path to the serial downloader, the recovery device and manufacture mode order, the redundant image mechanism and the Enter Bootloader API. Behaviour on a board is still Not verified. |
| Stage 3 A13: open-configuration behaviour of the ROM Not verified | Narrowed. The manual copy says that in Open configuration HAB runs, errors are logged and the image runs even without authentication data, and that Closed configuration aborts to the serial downloader (R8), after a switch to the secondary image where redundant boot is set up (R9). The HAB event content for a blank SRK hash is still Not verified. |
| Stage 3 D10: the ROM runs the bootloader DCD before the application, Not verified | For SD and MMC boot the manual says the ROM performs a DCD check and extraction before it copies the image (R3), from which the order of the DCD writes is an Inference. Whether it holds for the NOR path was not read. |
| Rank 1, RAM load with revert, and the staging variant (c) with a NOR slot | Unaffected for the pilot hardware. For an eMMC platform the slot lives on eMMC and variant (b) becomes the staging path, with the cost in section 2. The NOR evidence (#22 data closure, write extents) probably does not carry over to eMMC, and the eMMC ordering and atomicity questions in section 5 are new (Inference). |
| Write inventory by actor | On eMMC the bootloader writes through a block device with a cache, not through FlexSPI during XIP, so the #22 closure matrix does not apply. It is replaced by the flush and atomicity questions of section 5 (Inference). |
| Trial builds | They were NOR builds. None was run for eMMC. |
| Pins | #141 names Zephyr v4.4.0. `main` pins v4.4.2. Its cited files are identical at both. Its build trial used the older tree, as it states. |

## Open questions and owner decisions

| # | Question | Recommendation |
| --- | --- | --- |
| 1 | The reference manual was read only in a third-party-hosted Rev. 3 copy, and the JEDEC eMMC standard not at all. Can the owner supply the `nxp.com` manual (to compare R1 to R12, X8) and JESD84-B51x (for Y1)? | Supply both. A comparison of the manual copy is cheap. The JEDEC text settles the atomicity and reset questions of sections 3 and 5. |
| 2 | Scope for an eMMC flash-area backend (variant (b) for eMMC) in the bootloader, as the next step after this record | Do not start before question 1. Then decide between a read-only RAM-load first stage and a full backend, and decide where the driver lives (Zephyr, Crux module or out of tree). |
| 3 | Software-only replay of MCUboot's trailer sequences on the existing eMMC simulator, to test ordering and alignment assumptions (section 5) | Propose it as a follow-up issue under M14 or M8 after the scope in question 2. It needs no hardware. |
| 4 | Is there an eMMC-bearing RT1060 or RT1062 target for M8 and M9 validation, or does the pilot hardware remain the only one? | Owner decision. Until one exists, eMMC boot and update stay unvalidated, and the NOR results stay the only evidence. |
| 5 | Fuse programming, closed configuration and permanent boot-partition protection | Remain product decisions, as in #141 and #299. Nothing in this record needs them. |
| 6 | Input to #299 | Section 3 is the eMMC input. The first gap to close there is whether the ROM's secondary image mechanism (R9) and the Enter Bootloader API (R12) work with eMMC boot partitions, and what the ROM does with an invalid image in Open configuration. |

## Not verified

| Item | Needs |
| --- | --- |
| Whether the manual copy (Rev. 3, third-party-hosted) equals the `nxp.com` document, and whether a later revision or an erratum changes R1 to R12 | The `nxp.com` document and the errata sheet |
| Where the first stage sits inside an eMMC boot partition (whether `0x400` counts from the partition start), and the size the boot partition allows | A board, or another manual section |
| Whether the ROM's secondary image mechanism works across the two eMMC boot partitions, and whether it can be used in Open configuration | A board, the manual's remaining sections |
| Which internal boot errors lead to the serial downloader, and the HAB event content for a blank SRK hash | Security reference manual |
| The state of the uSDHC and the card when the image starts, and whether the boot partition selection survives | A board |
| The CSF placement and IVT offset of a signed SD or eMMC image | CST user guide |
| Whether the ROM accepts a Zephyr-built non-XIP header on SD or eMMC | A board |
| Size, RAM and time of the SD/MMC stack inside MCUboot | A build of a prototype, which needs approval |
| The standard's reset behaviour per EXT_CSD byte, the effect of CMD0, the atomicity of CMD6 writes and single-sector writes, and cache ordering | JESD84-B51x |
| Whether the current Flashloader programs an eMMC on the RT1060 | Flashloader source or reference manual |
| Whole-tree absence of SD or MMC handling in MCUboot and Zephyr | Content search of the pinned trees |
| Any behaviour on a board | Hardware, outside #298 |

## Method

| Item | How it was done |
| --- | --- |
| Source access | `gh api` and `raw.githubusercontent.com` for the pinned revisions of Zephyr, MCUboot and `hal_nxp`, at both sets of pins. Nothing was cloned and no dependency checkout was touched. |
| Documents | Public NXP and SPSDK pages. Four pages of the RT1060 Manufacturing Guide were read in full text by the author. **Chapter 9 of the RT1060 Reference Manual Rev. 3 was read by the author in a third-party-hosted copy, downloaded with the owner's approval on 7 November 2026, as locally extracted text (tables partly garbled).** Other documents were read through a summarising fetch or local text extraction by research subagents, and the evidence files say which. |
| Linux, U-Boot, mmc-utils | Fetched at the revisions named in the evidence. The key rows were re-read by the author. |
| Repository | `git show` and file reads at `3e0a5d7`. |
| Builds, tools, hardware | None. |

This record does not claim that any image boots from eMMC, that any board boots from an eMMC boot partition (the manual copy says what the ROM does, no board was used), that a
bootloader can be recovered, that MCUboot works on eMMC, that a flash-area backend is sound or cheap, that the flush
correction or a transactional store makes image activation atomic, that any hardware option works, or that any M8 or M9
criterion is met.
