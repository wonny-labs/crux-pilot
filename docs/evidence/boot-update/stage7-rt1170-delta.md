# Stage 7: RT1170-EVK Rev A1 layout and boot differences (desk study)

Issue: wonny-labs/crux-pilot#141, scope item 5. Prepared 2 November 2026. "Plan" means the [pre-M8 investigation plan](../../plans/pre-m8-investigation-plan.md). "Decision record" means the [boot and update investigation record](../../boot-update-investigation.md).

Scope: documentation and pinned-source reading only, under approval A0. No build was run. No RT1170 sysbuild configure was run, because that needs A1 and a Dev Container. No tool was downloaded. No hardware was used. No repository, branch, issue or pull request was changed by this study.

Status: first draft by the study author, spot-checked against the pins when the record was assembled (see [Assembly check](#assembly-check)). Not independently reviewed yet.

Owner decision of 2 November 2026: RT1170 boot work is part of #141 again as a desk and build-only item. Physical verification of RT1170 is deferred to #155. RT1060-EVKB Rev B stays the primary and only board for physical validation. This file replaces the removed Stage 7 text.

## Summary for the decision record

Nothing in this file is adopted. All rows are desk results.

### Main differences

| Topic | RT1060-EVKB Rev B (existing record) | RT1170-EVK Rev A (this study) |
| --- | --- | --- |
| External flash | 8 MiB QSPI NOR at `0x60000000` | IS25WP128, 16 MiB QSPI NOR at `0x30000000` |
| Upstream partitions | `mcuboot` 128 KiB, `image-0` and `image-1` 3 MiB each, `storage` 1,920 KiB | `mcuboot` 128 KiB, `image-0` and `image-1` 7 MiB each, `storage` 1,920 KiB |
| Scratch partition | None | None |
| SDRAM | 32 MiB at `0x80000000`, started by DCD in the boot header | 64 MiB at `0x80000000`, started by XMCD in the boot header (DCD disabled) |
| FCB offset | `0x0` | `0x400` |
| IVT and boot data offsets | `0x1000` and `0x1020` | `0x1000` and `0x1020` (Inference from header sizes) |
| DCD or XMCD offset | DCD at `0x1030` | XMCD at `0x1040` |
| XIP encryption | BEE | OTFAD (the SPSDK data also lists IEE). No BEE found for this SoC. |
| Upstream MCUboot sample target | Resolves to default revision C | Resolves to default revision B, which is not Rev A |
| MCUboot board files at the pin | None for RT1060 | None for the EVK. Only a `vmu_rt1170` configuration file exists for this SoC. |
| Boot switch, debug probe | SW4 on the EVKB. Probe not recorded here. | SW1 in the upstream board document. On-board OpenSDA DAPLink probe. Rev A1 user guide not readable. |

### Applicability of the #141 findings

| Finding | RT1170-EVK Rev A |
| --- | --- |
| Update mode ranking | Carries over. Revert and swap cost grows with the 7 MiB slots. |
| RAM load with revert as rank 1 | Carries over with RT1170 changes (XMCD, 64 MiB SDRAM, SoC clock code). Needs its own overlay. |
| Staging variant (c) | Carries over. More SDRAM for the buffer. |
| 64 KiB alignment | Rule carries over. Upstream boundaries already conform. Real erase unit is unknown for Rev A. |
| XIP write hazards (#22) | The mechanism carries over. The #22 evidence does not. Unknown for RT1170. |
| Older-image rejection and version policy | Carries over unchanged. It is MCUboot logic. |
| Signing and HAB tool findings | `imgtool` and "no CSF in a Zephyr image" carry over. SPSDK support and the HAB open-configuration result are unknown. |
| BEE findings | Do not apply. OTFAD replaces BEE and its key and fuse needs are unknown. |

### Not verified (headline items)

- Every NXP reference manual, security reference manual, application note and the Rev A1 user guide was unreadable. All boot ROM, HAB and fuse behaviour for RT1170 is Not verified.
- Boot switch settings and serial downloader settings for the Rev A1 PCB are Not verified.
- That the Rev A1 PCB corresponds to Zephyr revision `A`, and that it carries the flash and SDRAM that the devicetree names, are Not verified.
- The OTFAD key source and fuse need, and whether any open-configuration evaluation is possible without fuse writes, are Not verified.
- MCUboot size, RAM use and a working RAM load configuration for RT1170 are Not verified. They need an RT1170 sysbuild configure and build (A1 and a Dev Container).

## Evidence labels

The labels are used exactly as in the decision record. None is upgraded.

| Label | Use in this file |
| --- | --- |
| Verified on pinned source | Checked at a cited file and line of a pinned revision. It shows what code and configuration allow, not what a board does. |
| Verified by public vendor document | A public vendor page, repository file or tool data file says so. It is the vendor's statement, not a Crux result. Where a page was read only through a summarising fetch, the label says so. |
| Verified in repository | Read from a committed crux-pilot record. This label is new in Stage 7. It is weaker than "Verified by artefact" when the record is a document rather than a build output. |
| Inference | Reasoned from verified lines. |
| Not verified | Not checked, not accessible, or only inferred. The reason is given. |

No row is "Verified by build". No row is verified on hardware.

## Method and limits

| Item | Value |
| --- | --- |
| Zephyr pin (`Z`) | `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0) |
| MCUboot pin (`M`) | `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2` |
| `hal_nxp` pin (`H`) | `2c2f28ac333e995d7279777409817c4b4f92c1ec`, paths below `mcux/mcux-sdk-ng/` |
| Public vendor files | `mcuxsdk-examples` at `3151bc057b8924db54c7395d39b213c05ab94dc3` (`X`). SPSDK tag `v3.12.0` (`S`). U-Boot tag `v2026.10` (`U`). These are the same sources that Stage 3 used. |
| crux-pilot read | `main` at `47da9e5` for the RT1170 feasibility record. Corrected on 2 November 2026 after independent review 3: the first draft cited `5efdd8a`, which is the head of the closed PR #148, not `main`. That copy has a 2-line status block that `main` lacks, so all line numbers cited from that record were re-checked at `47da9e5` and corrected. Branch `docs/boot-update-investigation` at `89514e6` for the decision record and evidence files. |
| Access | Pinned files were read as raw files at the pins. The MCUboot `boot/zephyr/boards/` listing came from a read-only blobless Git fetch of the pin made outside the repository. Local copies are not committed. |
| Limits of absence claims | A claim that something is absent is limited to the files named in the row, or to a file-name listing where stated. No content search of a whole tree was made. |
| NXP documents | Not readable. See [Failed accesses](#8-failed-accesses). |
| Revision names | "Rev A" is Zephyr's board revision `A`. "Rev A1" is the PCB marking named in #33 and in the [RT1170 feasibility record](../../rt1170-evk-a1-feasibility.md). That record says the marking was not independently checked. |

Row identifiers use the prefix S7. Rows S7-L are layout rows. S7-B are boot header rows. S7-S are boot ROM and security rows. S7-A are applicability rows. Existing identifiers (A, B, C, D, S2 and L rows) point to the earlier evidence files.

## 1. Rev A layout

### 1.1 Rows

| ID | Claim | Source | Label |
| --- | --- | --- | --- |
| S7-L1 | The board has revisions `A` and `B`. The default revision is `B`. | Z `boards/nxp/mimxrt1170_evk/board.yml`:7-12 (default at line 9) | Verified on pinned source |
| S7-L2 | The upstream board document says Zephyr supports "the initial revision of this EVK, as well as rev EVKB". The repository assumes that the PCB marked Rev A1 is Zephyr revision `A`. The marking was not checked. | Z `boards/nxp/mimxrt1170_evk/doc/index.rst`:6-9. `docs/rt1170-evk-a1-feasibility.md` lines 5-8 and 145-148 | Document text: Verified on pinned source. Mapping of the PCB marking: Verified in repository that it is an assumption. It is Not verified. |
| S7-L3 | Rev A external flash: a node `is25wp128` (compatible `soc-nv-flash`) under the FlexSPI NOR controller. Size is 16 MiB (`size` of 16\*8 Mbit). JEDEC ID is `9d 70 18`. Maximum SPI frequency is 104 MHz. | Z `mimxrt1170_evk.dtsi`:265-288 (size at 274, frequency at 276, JEDEC ID at 278) | Verified on pinned source |
| S7-L4 | Flash base is the FlexSPI memory window `0x30000000`, size 16 MiB. Zephyr derives `FLASH_BASE_ADDRESS` from the second `reg` entry of the FlexSPI node. The recorded CI build shows base `0x30000000`, 16 MiB. | Z `mimxrt1170_evk.dtsi`:270. Z `soc/nxp/common/Kconfig.flexspi_xip`:13-16. `docs/rt1170-evk-a1-feasibility.md` lines 140-141 | Verified on pinned source for the first two. Verified in repository for the CI record. |
| S7-L5 | Rev A inherits its partitions from the shared `.dtsi`. The cm7 `.dts` has no flash overlay for Rev A. There is no `_A.overlay`. | Z `mimxrt1170_evk.dtsi`:290-318. Z `mimxrt1170_evk_mimxrt1176_cm7.dts` (whole file). `..._cm7_A.overlay` returned HTTP 404. | Verified on pinned source for the files read. A full directory listing was not made. |
| S7-L6 | Partitions: `mcuboot` `0x000000`-`0x020000` (128 KiB). `image-0` `0x020000`-`0x720000` (7 MiB). `image-1` `0x720000`-`0xe20000` (7 MiB). `storage` `0xe20000`-`0x1000000` (2 MiB minus 128 KiB, that is 1,920 KiB). All boundaries are multiples of 64 KiB. | Z `mimxrt1170_evk.dtsi`:299-317. The multiple check is arithmetic on those values. | Verified on pinned source. The 64 KiB check is an Inference from the cited values. |
| S7-L7 | Erase and write block: `erase-block-size` is 4 KiB and `write-block-size` is 1. The Rev A flash configuration block declares a 4 KiB sector, a 64 KiB block and a non-uniform block size. | Z `mimxrt1170_evk.dtsi`:287-288. Z `boards/nxp/mimxrt1170_evk/xip/evkmimxrt1170_flexspi_nor_config.c`:82-88 | Verified on pinned source |
| S7-L8 | There is no scratch partition. There is no `zephyr,bootloader-info` chosen node. The files read have no retained-memory node. | Z `mimxrt1170_evk.dtsi` (whole file). Z `mimxrt1170_evk_mimxrt1176_cm7.dts`:21-34 (chosen list) | Verified on pinned source for the two files. Whole-tree absence is Not verified. |
| S7-L9 | Chosen nodes in the cm7 `.dts`: `zephyr,sram` is `sdram0` (line 22). `zephyr,flash-controller` is `ext_flash_ctrl` (28). `zephyr,flash` is `is25wp128` (29). `zephyr,code-partition` is `slot0_partition` (30). ITCM and DTCM are also chosen (23-24). | Z `mimxrt1170_evk_mimxrt1176_cm7.dts`:21-34 | Verified on pinned source |
| S7-L10 | SDRAM is present. `sdram0` is `memory@80000000`, 64 MiB, with the comment "Winbond W9825G6KH-5I". The board document lists 512 Mbit SDRAM. | Z `mimxrt1170_evk_mimxrt1176_cm7.dts`:36-40. Z `doc/index.rst`:21 | Verified on pinned source |
| S7-L11 | The board document table still says that the SDRAM is "enabled via device configuration data (DCD)". The board Kconfig and CMake use XMCD instead. The document is out of date against the code. | Z `doc/index.rst`:87-90. Z `Kconfig.defconfig`:8-13. Z `CMakeLists.txt`:27-31 | Verified on pinned source |
| S7-L12 | Other RAM that the SoC offers: ITCM and DTCM (chosen), and `ocram1` at `0x20240000` and `ocram2` at `0x202c0000`. | Z `mimxrt1170_evk_mimxrt1176_cm7.dts`:23-24. Z `dts/arm/nxp/imxrt/nxp_rt1170.dtsi`:15-26 | Verified on pinned source |
| S7-L13 | Where a RAM load would place the image, by default. The MCUboot executable RAM bounds and the imgtool `--load-addr` both come from the `zephyr,sram` chosen node. On this board that node is SDRAM at `0x80000000`. The bootloader's own RAM and the image load window therefore start at the same address. | B53, B54, B56 (Stage 1) with S7-L9 | Rules: Verified on pinned source (Stage 1). The overlap on this board: Inference. Not verified by build. |
| S7-L14 | Other board differences, not used by the #141 findings: the Rev A debug probe is OpenSDA DAPLink, the EVKB probe is MCU-Link. The console connector is J11 on Rev A and J86 on the EVKB. LinkServer uses a separate device string per revision. Rev A has the FXOS8700 sensor and a KSZ8081 PHY node. The EVKB overlay removes both. | Z `doc/index.rst`:310-311 and 355-356. Z `board.cmake`:13-17. Z `mimxrt1170_evk.dtsi`:96-107 and 153-160. Z `..._cm7_B.overlay`:76-89 | Verified on pinned source |
| S7-L15 | Twister metadata is inconsistent for Rev A. The Rev A entry lists `ram: 256` and `flash: 16384` (KiB) while the devicetree RAM is 64 MiB SDRAM. The Rev B entry lists 65,536 for both. | Z `mimxrt1170_evk_mimxrt1176_cm7.yaml`:7, 14-15. Z `..._cm7_B.yaml`:7, 14-15 | Verified on pinned source. Effect on the MCUboot sample: Not verified. |

### 1.2 Rev A and Rev B compared (devicetree)

| Item | Rev A (default `.dtsi` and `.dts`) | Rev B overlay |
| --- | --- | --- |
| Flash part | IS25WP128, 16 MiB, JEDEC `9d 70 18`, 104 MHz | W25Q512NW, 64 MiB, JEDEC `ef 60 20`, 133 MHz |
| `mcuboot` | `0x0`, 128 KiB | `0x0`, 128 KiB |
| `image-0` | `0x20000`, 7 MiB | `0x20000`, 7 MiB |
| `image-1` | `0x720000`, 7 MiB | `0x720000`, 7 MiB |
| `storage` | `0xe20000`, 2 MiB minus 128 KiB | `0xe20000`, 50 MiB minus 128 KiB |
| SDRAM node | 64 MiB at `0x80000000` | Not changed by the overlay |
| Source | Z `mimxrt1170_evk.dtsi`:265-321 | Z `..._cm7_B.overlay`:23-74 |

Label: Verified on pinned source. The board document says the EVKB flash is "Supported for XIP only" (Z `doc/index.rst`:95-98). It gives no such note for the Rev A flash (91-94). What the note means for flash writes is Not verified.

## 2. Boot header and boot configuration

### 2.1 Rows

| ID | Claim | Source | Label |
| --- | --- | --- | --- |
| S7-B1 | The NXP boot header is built when `NXP_IMXRT_BOOT_HEADER` is set. It depends on `!(BOOTLOADER_MCUBOOT \|\| SECOND_CORE_MCUX)`. The boot device choice defaults to FlexSPI NOR unless an XSPI node is enabled. | Z `soc/nxp/imxrt/Kconfig`:24-30 and 36-39 | Verified on pinned source |
| S7-B2 | The FCB comes from a board file chosen by revision. Rev A uses `xip/evkmimxrt1170_flexspi_nor_config.c`. Rev B uses `xip/evkbmimxrt1170_flexspi_nor_config.c`. | Z `boards/nxp/mimxrt1170_evk/CMakeLists.txt`:15-26 | Verified on pinned source |
| S7-B3 | The FCB offset for RT11xx is `0x400`. The generic default is `0x0`, but the RT11xx SoC `Kconfig.defconfig` sets `0x400`. Zephyr loads `Kconfig.defconfig` files first so that they override later defaults. RT1060 records show the FCB at `0x0` (Stage 4 artefact). | Z `soc/nxp/imxrt/Kconfig`:69-77. Z `soc/nxp/imxrt/imxrt11xx/Kconfig.defconfig`:25-26. Z `Kconfig.zephyr`:16-33 | Verified on pinned source. RT1060 offset: Verified by artefact (Stage 4). |
| S7-B4 | Rev A FCB content: read command `0xEB` with 3-byte address, 9 dummy cycles, 4 pads, serial clock "133 MHz", 16 MiB device size, 256-byte page, 4 KiB sector, 64 KiB block. The devicetree gives 104 MHz as the maximum frequency. The cause of the 133 MHz against 104 MHz difference is Not verified. | Z `evkmimxrt1170_flexspi_nor_config.c`:13-14, 28-29, 43-47, 82-88. Z `mimxrt1170_evk.dtsi`:276 | Values: Verified on pinned source. Significance: Not verified. |
| S7-B5 | Rev B FCB differs: read command `0xEC` with 4-byte address, 8 dummy cycles, 64 MiB device size, erase `0x21` and program `0x12` with 4-byte address. A Rev B boot configuration is therefore not the Rev A configuration. | Z `evkbmimxrt1170_flexspi_nor_config.c`:13-14, 31, 45-48, 61, 69 (file diff against the Rev A file) | Verified on pinned source. That a Rev B image would not boot a Rev A PCB is an Inference. Not verified. |
| S7-B6 | Zephyr's RT11xx IVT is a HAB IVT, version 4.1, size `0x2000`. The IVT offset for FlexSPI NOR is `0x1000`. The boot data holds `start` and `size`. For an XIP image `size` is `_flash_used`. For a non-XIP image `start` is the SRAM base and `size` is `_image_ram_size`. The IVT is built for the Cortex-M7 only. | H `devices/RT/RT1170/MIMXRT1176/xip/fsl_flexspi_nor_boot.h`:26-67 and 122-128. Z `imxrt11xx/soc.c`:76-103. Z `imxrt/Kconfig`:94-97 | Verified on pinned source |
| S7-B7 | The IVT `csf` field is `CSF_ADDRESS`, which is 0. The `dcd` field is NULL when DCD is not enabled. | H same header:116. Z `imxrt11xx/soc.c`:94-101 | Verified on pinned source |
| S7-B8 | Zephyr uses XMCD for SDRAM on this board and not DCD. The board defaults `EXTERNAL_MEM_CONFIG_DATA` and `NXP_IMX_EXTERNAL_SDRAM` to `y` for the Cortex-M7. XMCD cannot be selected together with DCD. | Z `Kconfig.defconfig`:8-13. Z `imxrt/Kconfig`:103-120 | Verified on pinned source |
| S7-B9 | The XMCD offset is `0x1040` for FlexSPI NOR. Only the RT118x series uses `0x800`. The XMCD content has a SEMC header, a simplified option block with a 198 MHz SDRAM clock, CS0 size 64 MB and a 32-bit port. It has no CRC word. | Z `imxrt/Kconfig`:122-127. Z `boards/nxp/mimxrt1170_evk/xmcd/xmcd.c`:15-31 | Verified on pinned source |
| S7-B10 | The board CMake compiles the XMCD and the FCB. It does not compile `dcd/dcd.c`, although that file exists in the board directory. If SDRAM is the RAM and XMCD is off, it warns that the configuration "may not boot". | Z `boards/nxp/mimxrt1170_evk/CMakeLists.txt`:7-37 | Verified on pinned source for this CMake file |
| S7-B11 | Linker placement: the FCB goes at `FLEXSPI_CONFIG_BLOCK_OFFSET`. The IVT, boot data, and the XMCD section follow at `IMAGE_VECTOR_TABLE_OFFSET`, with the XMCD at its own offset. | Z `soc/nxp/imxrt/CMakeLists.txt`:13-39. Z `soc/nxp/imxrt/boot_header.ld`:31-42 | Verified on pinned source |
| S7-B12 | Expected header addresses for a Rev A XIP image: FCB `0x30000400`, IVT `0x30001000`, boot data `0x30001020`, XMCD `0x30001040`. The boot data address follows from a 32-byte IVT and the XMCD address from the Kconfig offset. The RT1060 addresses are `0x60000000`, `0x60001000`, `0x60001020` and DCD `0x60001030`. | S7-B3, S7-B6, S7-B9. RT1060: Stage 4 `placement-rt1060.json` | Inference for RT1170. Verified by artefact (Stage 4) for RT1060. The CI map of the RT1170 build was not read. |
| S7-B13 | What an MCUboot-built application omits. With `BOOTLOADER_MCUBOOT` the application has no boot header. It therefore has no FCB, no IVT, no boot data and no XMCD. All of these sections are linked only under `NXP_IMXRT_BOOT_HEADER`. The bootloader keeps its own header (A6, A7). | Z `imxrt/Kconfig`:24-30. Z `soc/nxp/imxrt/CMakeLists.txt`:10-11 and 13-39. Z board `CMakeLists.txt`:7-31 | Verified on pinned source |
| S7-B14 | The application relies on the bootloader's XMCD having run to have SDRAM ready. That the boot ROM runs the XMCD of the bootloader image is vendor ROM behaviour. This is the same limit as B86 for RT1060. | S7-B13 with B86 | Build logic: Verified on pinned source. ROM behaviour: Not verified. |
| S7-B15 | The bootloader and the application must use the same board revision target. Sysbuild builds MCUboot with the application's board target (B2, an Inference in Stage 1). The revision then selects the FCB (S7-B2). | B2. Z board `CMakeLists.txt`:15-19 | Inference |
| S7-B16 | `FLASH_MCUX_FLEXSPI_XIP` is not set for RAM load. For a Cortex-M7 XIP image Zephyr relocates code to RAM by default, and `FLASH_MCUX_FLEXSPI_XIP_MEM` defaults to RAM. The RT11xx SoC relocates its `flexspi.c` into that memory. | Z `soc/nxp/common/Kconfig.flexspi_xip`:23-28 and 33-47. Z `imxrt11xx/CMakeLists.txt`:13-18 | Verified on pinned source |
| S7-B17 | A ROM RAM loader option exists (`NXP_FLEXSPI_ROM_RAMLOADER`). It builds an image that the boot ROM can copy from FlexSPI to RAM, and it requires a non-XIP image. This is the same finding as D2 for RT1060. | Z `soc/nxp/common/Kconfig.rom_loader`:4-21 | Verified on pinned source |

### 2.2 What differs from RT1060

| Item | RT1060-EVKB | RT1170 Rev A | Effect |
| --- | --- | --- | --- |
| FCB offset | `0x0` | `0x400` | The first 1 KiB of the flash image is not the FCB. SPSDK lists a key blob area there (S7-S6). |
| SDRAM start | DCD | XMCD | The DCD byte table is replaced by a four-word XMCD. |
| Header size | FCB, IVT, boot data, DCD | FCB, IVT, boot data, XMCD | The `.boot_hdr.*` structure is the same. Values differ. |
| Boot configuration source | One FCB file for revisions B and C (B7) | One FCB file per revision (S7-B2) | A target revision change changes the boot configuration. |
| Application header | None under MCUboot | None under MCUboot | Same finding (A7). |

## 3. Boot ROM and security differences

### 3.1 Rows

| ID | Claim | Source | Label |
| --- | --- | --- | --- |
| S7-S1 | A Zephyr-built RT1170 image has no CSF. The finding A1 and A2 for RT1060 holds for the RT1170 header. | S7-B6, S7-B7 | Verified on pinned source |
| S7-S2 | SPSDK treats this SoC as an alias of RT1166 and lists HABv4 for it. Its data names a certificate block of type `srk_table_hab` and a HAB initial load size of `0x2000` for FlexSPI NOR. The predecessor name is `rt117x`. | S `spsdk/data/devices/mimxrt1176/database.yaml`:5 and 10. S `mimxrt1166/database.yaml`:126-134 | Verified by public vendor document (tool data, not a ROM specification) |
| S7-S3 | SPSDK bootable image layout for FlexSPI NOR: key blob at `0x0000`, FCB at `0x0400`, key store at `0x0800`, HAB container at `0x1000`. For RT1060 the FCB is at `0` and the BEE headers are at `0x400` and `0x800` (D4). SD, MMC and eMMC layouts put the HAB container at `0x400`. | S `mimxrt1166/database.yaml`:234-263. Stage 3 D4 | Verified by public vendor document (tool data) |
| S7-S4 | HAB behaviour in open configuration with blank SRK hash fuses on RT1170, and which events the ROM logs. | RT1170 reference manual and security reference manual (not readable) | Not verified |
| S7-S5 | The pinned RT1176 ROM API driver has FlexSPI NOR functions and a run-bootloader entry. It has no HAB entry. The ROM tree address depends on the chip revision (`MISC_DIFPROG` value `0x001170a0` selects `0x0020001c`, others `0x0021001c`). Reading HAB events from Zephyr would need new code, as for RT1060 (A8). | H `devices/RT/RT1170/MIMXRT1176/drivers/romapi/fsl_romapi.h`:28-30 and 414-422. H same directory `fsl_romapi.c`:101-107 | Verified on pinned source for these files. Whether the Rev A1 PCB carries silicon with that identifier is Not verified. |
| S7-S6 | RT1170 uses OTFAD for encrypted XIP and not BEE. The vendor document says OTFAD is available on RT1160 and RT1170, with up to four regions, and that context 0 is reserved for the bootloader region that the ROM configures. The vendor lists BEE boards as RT1020 to RT1064 and OTFAD boards as RT1152, RT1160 and RT1170-EVKB. The SPSDK RT1166 data has no BEE section. | X `ota_examples/_doc/encrypted_xip_otfad.md`:7. X `ota_examples/_doc/encrypted_xip.md`:177-190. S `mimxrt1166/database.yaml` (no `bee` text found) | Verified by public vendor document. That RT1170 has no BEE is an Inference from absence in these files. |
| S7-S7 | SPSDK also has an IEE section for this device, with user key fuses and a pointer to "Application Note 13250" and the security reference manual. Which engine covers which memory on the EVK, and how it relates to the ROM, is Not verified. | S `mimxrt1166/database.yaml`:171-192 (pointer at 175) | Verified by public vendor document (tool data). Applicability: Not verified. |
| S7-S8 | Two key sources are documented for OTFAD. The vendor MCUboot example protects the key with a CAAM red blob that uses the OTPMK, and says "no fuse selection required". The provisioning section of the same document, which is written for RW61x and is not used as RT1170 evidence (S7-S10), states that its operation provisions a key encryption key permanently. S7-S13 therefore keeps the OTFAD key and fuse need Not verified. The SPSDK data lists a key encryption key in fuses 128 to 131, an OTFAD configuration fuse (fuse 71) and scramble fuses (132 to 135). It says that encrypted XIP "can be also enabled by the BOOT_CFG pin" and that `ENCRYPT_XIP_EN` is part of the `BOOT_CFG1` fuse. Which source applies to Rev A, and which needs fuse writes, is Not verified. | X `encrypted_xip_otfad.md`:22-30 and 40. S `mimxrt1166/database.yaml`:111-118, 195-231 (lines 225-226 for the enable bit and pin) | Statements: Verified by public vendor document. Which applies to Rev A: Not verified. |
| S7-S9 | The vendor OTFAD example targets MIMXRT1170-EVKB (Rev B). Its plain layout uses a 256 KiB bootloader and two 2 MiB slots with direct-XIP and flash remapping. Its encrypted XIP custom build uses an overwrite strategy. The Rev A EVK is not in the vendor list. The encrypted XIP flow of that vendor extension supports only overwrite-only mode with OTFAD, as it does with BEE (C7). | X `_boards/evkbmimxrt1170/ota_examples/mcuboot_opensource/example_board_readme.md`:4, 10-23. X `encrypted_xip.md`:156-170 and 186-190 | Verified by public vendor document |
| S7-S10 | A section of the vendor OTFAD document that tells the reader how to provision the bootloader names an RW61x device (line 147). It is not used here as RT1170 evidence. | X `encrypted_xip_otfad.md`:133-163 | Verified by public vendor document (text read in full). Applicability to RT1170: Not verified. |
| S7-S11 | Address binding. The OTFAD counter block holds the system address, so ciphertext depends on its address, as for BEE (C5). Swap modes would need re-encryption per slot. | X `encrypted_xip_otfad.md`:9-18 | Statement: Verified by public vendor document. Effect on swap modes: Inference. |
| S7-S12 | Zephyr does not wire OTFAD, IEE or BEE. The driver list names the CAAM driver (for entropy) and the ROM API drivers only. The `hal_nxp` tree has `fsl_otfad.h`, `fsl_iee.h` and `fsl_caam.h`. | Z `modules/hal_nxp/mcux/mcux-sdk-ng/drivers/drivers.cmake` (case-insensitive search, CAAM at line 74). H `drivers/otfad/fsl_otfad.h`, `drivers/iee/fsl_iee.h`, `drivers/caam/fsl_caam.h` (existence) | Verified on pinned source for these files. Whole-tree absence is Not verified. |
| S7-S13 | Whether an open-configuration evaluation of HAB or OTFAD is plausible without fuse writes. The documents that could be read do not say so. The SPSDK note on the enable pin (S7-S8) concerns enabling the engine, not the key or the whole flow. | S7-S4, S7-S8 | Not verified |
| S7-S14 | The ROM serial downloader uses the SDP protocol over USB. SPSDK lists USB VID `0x1FC9` and PID `0x013D` for the ROM and VID `0x15a2` and PID `0x0073` for the flashloader. | S `mimxrt1166/database.yaml`:62-73 | Verified by public vendor document (tool data) |
| S7-S15 | Boot switch settings for Rev A1, including serial downloader. The Rev A1 user guide was not readable. The upstream board document says to set SW1 to `0001b` before flashing and to `0010b` to run, and to press SW4 to reset. It does not name a revision and it covers Rev A and the EVKB. The mapping of these patterns to boot modes is not stated. | Z `doc/index.rst`:6-9 and 371-380. NXP guide: see [Failed accesses](#8-failed-accesses) | Document text: Verified on pinned source. Rev A1 settings: Not verified. |
| S7-S16 | Boot from SD. The U-Boot document for `imxrt1170-evk` gives SW1 `1 0 1 0` and SW2 `0 0 0 0 \| 0 0 0 0 \| 1 0 0 0` for SD card boot, where 1 is the top position. It does not name a revision. Whether this works on Rev A1 and whether it needs fuse settings is Not verified. | U `doc/board/nxp/imxrt1170-evk.rst`:34-45 | Verified by public vendor document (open-source project document). Rev A1 and fuse needs: Not verified. |
| S7-S17 | The EVK has a TF socket on uSDHC1 (`sdhc0` alias, `sdmmc` disk "SD"). The hardware list names no eMMC. It lists 512 Mbit octal flash and a 2 Gbit raw NAND flash. | Z `mimxrt1170_evk.dtsi`:19. Z `..._cm7.dts`:127-137. Z `doc/index.rst`:19-26 | Verified on pinned source |
| S7-S18 | Closed configuration procedure, fuse irreversibility, and board availability for RT1170-EVK. | RT1170 security reference manual (not readable). No availability page was read. | Not verified |
| S7-S19 | Recovery of a corrupted bootloader through the ROM. The ROM serial downloader exists (S7-S14) and a run-bootloader ROM entry exists (S7-S5). That a damaged FlexSPI image still allows entry to the downloader is Not verified. | S7-S5, S7-S14 | Not verified |

### 3.2 HAB and encryption, compared

| Item | RT1060-EVKB (existing record) | RT1170 Rev A (this study) |
| --- | --- | --- |
| IVT and CSF | HAB IVT, `csf` 0 (A1, A2) | HAB IVT 4.1, `csf` 0 (S7-B6, S7-B7) |
| HAB tool support | SPSDK alias of RT1024 data, `srk_table_hab` (B7, B8) | SPSDK alias of RT1166 data, `srk_table_hab` (S7-S2) |
| Open configuration behaviour | Not verified (A13) | Not verified (S7-S4) |
| XIP encryption engine | BEE | OTFAD, with IEE also listed by SPSDK |
| Key source | Conflict between C2 and C4. Not verified. | Two documented sources (S7-S8). Not verified. |
| Fuse needs | Not verified (C9) | Not verified (S7-S8, S7-S13) |
| Zephyr driver wiring | None (C3) | None found (S7-S12) |
| Vendor MCUboot support | Overwrite-only (C7) | Overwrite-only for OTFAD (S7-S9) |

## 4. Rev A and Rev B resolution of the MCUboot sample target

| ID | Claim | Source | Label |
| --- | --- | --- | --- |
| S7-A1 | MCUboot's sample lists `mimxrt1170_evk/mimxrt1176/cm7` and `mimxrt1160_evk/mimxrt1166/cm7` and `vmu_rt1170/mimxrt1176/cm7`. The RT1170 entry has no `@` revision. | M `boot/zephyr/sample.yaml`:29-30 and 37 | Verified on pinned source |
| S7-A2 | A target with no revision resolves to the default revision of the board. For this board that is `B`, which is not Rev A. The repository feasibility record states the same default. | Z `board.yml`:9. `docs/rt1170-evk-a1-feasibility.md` lines 14-16 | Verified on pinned source. Verified in repository. |
| S7-A3 | Upstream therefore lists the Rev B configuration. That means the W25Q512NW layout, 133 MHz, `0xEC` read and the Rev B FCB. The Rev A configuration is not what the sample target selects. | S7-A1, S7-A2, S7-B5, S7-L6 | Inference |
| S7-A4 | What the sample exercises. The entry sits in `platform_allow` of the base sample `sample.bootloader.mcuboot`. It is not in that sample's `integration_platforms`. The only RAM load sample entry lists `mimxrt1020_evk`. A listing is not a passing build and not a hardware test. What upstream CI builds or runs for RT1170 is Not verified. | M `sample.yaml`:6-8, 29-30, 40-43 and 101-106 | Listing: Verified on pinned source. Build and hardware status: Not verified. |
| S7-A5 | MCUboot has no RT1170-EVK board file. The `boot/zephyr/boards/` listing has `vmu_rt1170_mimxrt1176_cm7.conf` and the RT1050 hyperflash RAM load overlay, and nothing for RT1060 or RT1170 EVK. The `vmu_rt1170` file sets `BOOT_MAX_IMG_SECTORS=1024` and `BOOT_ERASE_PROGRESSIVELY=y`. That board's slot sizes were not read. | M `boot/zephyr/boards/` (file-name listing at the pin). `vmu_rt1170_mimxrt1176_cm7.conf`:4-5 | Verified on pinned source (file-name listing, not a content search) |
| S7-A6 | With a 4 KiB erase block, each 7 MiB slot has 1,792 sectors. Each 3 MiB RT1060 slot has 768. `BOOT_MAX_IMG_SECTORS_AUTO` is on by default and derives the count from the slot size and erase size. MCUboot RAM cost for 1,792 sectors was not measured. | Z `mimxrt1170_evk.dtsi`:287 and 304-312. M `boot/zephyr/Kconfig`:866-885. S2-11 | Arithmetic: Inference. RAM cost: Not verified (needs a build). |

## 5. Applicability of the #141 findings to RT1170-EVK Rev A

Result key: **Carries over** means the finding stands for RT1170 on the evidence read. **Changes** means the finding needs an RT1170 specific change. **Unknown** means the RT1060 evidence does not transfer and RT1170 evidence was not found.

| ID | Finding | Result | Reason | Label |
| --- | --- | --- | --- | --- |
| S7-A7 | Update mode ranking (rank 1 RAM load with revert, rank 2 swap using offset, rank 3 direct-XIP with revert) | Carries over, with a change in size | The mode logic is MCUboot and sysbuild code that does not depend on the board (B1, B3, B10, B11, B14). Rev A has no scratch partition, so swap using offset stays the default (B11, S7-L8). The ranking rests on which actor writes NOR while it executes from NOR. That reasoning is the same. A revert erases a whole slot, which is 7 MiB here against 3 MiB. The vendor RT1170-EVKB example uses direct-XIP with flash remapping (S7-S9). Remapping on Rev A is Not verified. | Logic: Verified on pinned source. Size effect: Inference. Remapping: Not verified. |
| S7-A8 | RAM load with revert as rank 1 | Changes (needs an RT1170 overlay) | Carries over: the RAM link comes from sysbuild (B68a). The upgrade request problem for an application on slot 1 (B63a) is code that does not depend on the board. The share-data backend needs a `zephyr,bootloader-info` node that Rev A lacks (B64, S7-L8). The bootloader RAM and image load window default to the same SDRAM start (S7-L13). Changes: SDRAM is 64 MiB and is started by XMCD (S7-B8, S7-B9). No MCUboot RAM load overlay exists for this board (S7-A5). A retention region must lie outside the load window. ITCM, DTCM and OCRAM are alternatives (S7-L12). The SoC `clock_init` reconfigures the system PLL2 and PLL3 and their PFDs (Z `imxrt11xx/soc.c`:225-253) and warns, under the condition "If DCD is used", that the SEMC clock source must not change (199-203). This board uses XMCD, not DCD, so the comment does not apply as written. Whether the same caution applies to an XMCD-initialised SDRAM is Not verified. RT10xx also reconfigures PLLs (Z `imxrt10xx/soc.c`:113-153). Whether the application's clock set-up disturbs SDRAM that executes the image is Not verified for both boards. | Code: Verified on pinned source. Overlay need: Inference. SDRAM clock effect: Not verified. |
| S7-A9 | Staging variant (c), NOR slot, optionally through an SDRAM buffer | Carries over | It uses stock MCUboot and needs no board feature. SDRAM is larger (64 MiB) than the 7 MiB slot (S7-L6, S7-L10). The microSD socket exists on uSDHC1 (S7-S17). The conclusion that microSD staging removes no NOR write rests on trailer writes (S2-2, S2-3). Those are MCUboot logic. | Verified on pinned source. Capacity: Inference. |
| S7-A10 | 64 KiB alignment of fenced partition boundaries | Rule carries over. Real erase unit is unknown. | The upstream Rev A boundaries are all multiples of 64 KiB (S7-L6). The devicetree says 4 KiB and the FCB says 4 KiB sector and 64 KiB non-uniform block (S7-L7). The finding for RT1060 came from the LinkServer flash algorithm in #14. The algorithm for the Rev A device string (Z `board.cmake`:14) was not read. | Boundaries: Verified on pinned source. Erase unit on Rev A: Not verified. |
| S7-A11 | XIP write hazards (#22 closure obligations) | Mechanism carries over. Evidence does not. Unknown. | RT1170 uses the same driver family (`nxp,imx-flexspi-nor`, Z `mimxrt1170_evk.dtsi`:273) with relocation of FlexSPI code to RAM (S7-B16). This is an Inference, because the RT1060 compatible string was not re-read here. The #22 matrix names addresses, symbols and a data-closure failure from RT1060 images. None of that was produced for RT1170. The bootloader also executes in place at `0x30000000`. | Driver family: Inference. RT1170 closure: Not verified. |
| S7-A12 | Older-image rejection and application version policy | Carries over | It is MCUboot comparison logic with no board input (B13 to B26). | Verified on pinned source |
| S7-A13 | Signing tool findings (`imgtool` for images, SPSDK after A2, no CST) | Carries over for `imgtool`. Unknown for SPSDK on RT1170. | `imgtool` has no board input (A10). SPSDK lists the device (S7-S2), but A14 stays open: whether it accepts a Zephyr image with FCB, IVT and now XMCD is Not verified. The vendor RT1170 example also sets a 1 KiB header and 4-byte alignment (S7-S9). That is a vendor choice, not a requirement. | `imgtool`: Verified on pinned source. SPSDK: Not verified. |
| S7-A14 | HAB finding: a Zephyr image has no CSF, so the ROM has nothing to authenticate | Carries over | Same IVT structure with `csf` 0 (S7-S1). | Verified on pinned source |
| S7-A15 | HAB finding: open-configuration behaviour and event readback | Unknown | The security reference manual is not readable (S7-S4). The ROM driver has no HAB entry (S7-S5). | Not verified |
| S7-A16 | BEE not part of the candidate path | Does not apply. Replaced by OTFAD. | No BEE for this SoC in the files read (S7-S6). OTFAD key source and fuse needs differ and are open (S7-S8). Address binding gives the same constraint for swap modes (S7-S11). With RAM load the application runs from SDRAM, so XIP encryption does not apply to it, as in C10. | Mixed. See rows cited. |
| S7-A17 | Boot media analogue (microSD for eMMC) | Unknown | The board has no eMMC (S7-S17). SD boot settings are given by a project document for an unnamed revision (S7-S16). Zephyr cannot build an SD boot image for the bootloader (D1 applies to the shared Kconfig, S7-B1). | Not verified for Rev A1 |
| S7-A18 | Boot-time SDRAM readiness for an application built for MCUboot | Carries over, with XMCD in place of DCD | The application has no header (S7-B13). The reliance on the ROM running the bootloader's configuration is Not verified (S7-B14). | Not verified |
| S7-A19 | Execution model recommendation and M6 and M7 evidence effect | Not assessed | RT1170 is not part of the M6 and M7 scope. A boot composition change on RT1170 would change image identity and header addresses in the same way (S7-B12, S7-B13). Timing evidence does not exist for RT1170. | Inference |

## 6. Items that would need hardware later (#155)

This list names what cannot be settled at the desk. It does not propose any hardware operation and it does not authorise one. Any such work needs its own approval for the task at hand.

| # | Item | Related rows |
| --- | --- | --- |
| 1 | Check that the PCB marking Rev A1 corresponds to Zephyr revision `A`, and which flash and SDRAM parts are fitted | S7-L2, S7-L3, S7-L10 |
| 2 | Whether the Rev A boot configuration (FCB read command, dummy cycles, clock) is accepted by the fitted flash | S7-B4 |
| 3 | Boot switch and serial downloader settings of Rev A1, and what SW1 patterns select | S7-S15, S7-S16 |
| 4 | That the ROM processes the bootloader's XMCD and that SDRAM state survives the jump to the application | S7-B14, S7-A18 |
| 5 | Real erase unit and the behaviour of the probe's flash algorithm for the Rev A device string | S7-A10 |
| 6 | Silicon revision of the fitted part and its effect on the ROM tree | S7-S5 |
| 7 | RAM load copy time, watchdog behaviour during copy and whole-slot erase, and cache and MPU state at the jump on the Cortex-M7 | S7-A8, open items of the decision record |
| 8 | Application clock set-up against a running SDRAM image | S7-A8 |
| 9 | Revert, interrupted update and recovery behaviour of any mode | S7-A7, S7-S19 |
| 10 | #22 style closure for the RT1170 FlexSPI write path, including bus masters | S7-A11 |
| 11 | HAB event readback in open configuration. This would need a separate approval as in decision 8 of the record. | S7-S4, S7-S5 |
| 12 | OTFAD key source behaviour and fuse needs. No fuse operation is proposed. | S7-S8, S7-S13 |
| 13 | SD card boot behaviour on Rev A1 | S7-S16, S7-A17 |

### Items that need a build (A1 and a Dev Container) or documents, not hardware

| # | Item | Needs |
| --- | --- | --- |
| 1 | RT1170 sysbuild configure for rank 1 and rank 2, `.config` checks, bootloader size and RAM cost, sector count effect | A1 scope extended to RT1170 and a Dev Container. Not run here. |
| 2 | A Crux overlay for bootloader RAM, load window and retention region | Build result |
| 3 | Read header addresses from the retained map of the RT1170 CI build. The artefacts expire on 15 November 2026 and an RT1060 artefact download was refused earlier. | Artefact access, or a rebuild |
| 4 | RT1170 reference manual boot and security chapters, security reference manual, the OTFAD and HAB application notes, and the Rev A1 user guide | Readable copies, for example through an NXP account |
| 5 | SPSDK behaviour with a Zephyr image that has FCB, IVT and XMCD | A2 and a tool check |

## 7. Not verified

| Item | Needs |
| --- | --- |
| All RT1170 boot ROM behaviour, including HAB open configuration, event logging and XMCD processing | Reference manuals (not readable) |
| Rev A1 boot switch and serial downloader settings | Rev A1 user guide (not readable) |
| That the Rev A1 PCB is Zephyr revision `A` with the devicetree's flash and SDRAM | Board inspection |
| OTFAD key source, fuse needs and any no-fuse evaluation | Security reference manual |
| Which of OTFAD and IEE covers which memory on the EVK | Security reference manual |
| Meaning of "Supported for XIP only" for the EVKB flash | Upstream clarification |
| Cause of the 133 MHz FCB clock against the 104 MHz devicetree maximum | Flash data sheet |
| MCUboot size, RAM use and a working RAM load for RT1170 | Build |
| Flash remapping on Rev A and its use for rank 3 | Reference manual |
| Whole-tree absence of RT1170 board files and of OTFAD, IEE and BEE wiring | Content search of the pinned trees |
| What upstream CI builds or runs for the RT1170 MCUboot sample | Upstream CI records |
| Effect of the application's PLL set-up on SDRAM in RAM load | Build and hardware |
| M4 and dual-core interaction with a bootloader (`SECOND_CORE_MCUX` removes the header option) | Not examined. The pilot uses the Cortex-M7 only. |
| Real erase unit and flash algorithm behaviour on Rev A | Board measurement |
| Whether the SDRAM part named in the devicetree comment (`W9825G6KH-5I`) matches the 64 MiB node and the 512 Mbit in the board document. Added when the record was assembled. The part data sheet was not read. | Part data sheet, board inspection |

## 8. Failed accesses

| Access | Result |
| --- | --- |
| `www.nxp.com` RT1170-EVK hardware user guide (`MIMXRT1170EVKHUG`), by webapp and docs URL | Connection refused by the proxy (HTTP 403 on CONNECT) |
| `www.nxp.com` i.MX RT1170 reference manual (`IMXRT1170RM`), by webapp and docs URL | Same refusal |
| `www.nxp.com` application notes AN13264 and AN12079 | Same refusal |
| `nxp.com` AN12107 by direct fetch | Refused. A summarising fetch worked and showed that the note covers RT1050 only. It was not used. |
| NXP community pages by direct fetch (`Booting-IMXRT1170` and `Boot-ROM-API-on-IMXRT1176`) | Direct fetch failed. A summarising fetch of the first page worked. It concerns a custom board, gives no EVK switch settings, and was not used as evidence. |
| Zephyr `nxp_rt1170_cm7.dtsi` | One transient transfer failure. A retry worked. |
| Upstream files not found at the pin | `boards/nxp/mimxrt1170_evk/Kconfig`, `..._cm7_A.overlay`, `..._cm7_A.conf`, `..._cm7_B.conf`, `soc/nxp/imxrt/imxrt11xx/mpu_regions.c`, SPSDK `mimxrt1170`, `mimxrt1160` and `mimxrt1180` device directories (the device data was read through `mimxrt1176` and `mimxrt1166`) and the vendor `evkmimxrt1170` OTA example directory. These are 404 results, not access refusals. |

## 9. Proposed wording for the decision record

This was a proposal for the maintainer. It was integrated into the decision record on 2 November 2026, with edits.

| Item | Proposed text |
| --- | --- |
| Scope table row 5 | "5. RT1170-EVK Rev A1 layout and boot differences. Desk and build-only. Stage 7. Physical verification deferred to #155." |
| Stage list row | "7 RT1170 delta: `evidence/boot-update/stage7-rt1170-delta.md`" |
| Result sentence | "On pinned source the RT1170-EVK Rev A has a 16 MiB flash at `0x30000000` with 7 MiB slots, 64 MiB SDRAM started by XMCD, the FCB at `0x400`, and OTFAD in place of BEE. The mode ranking, version policy, staging variant (c) and the no-CSF finding carry over. RAM load needs an RT1170 overlay. The XIP write evidence, HAB behaviour and OTFAD key and fuse needs are unknown. The upstream MCUboot sample target resolves to Rev B. No RT1170 build or hardware result exists." |
| New owner decision | None required. Items in section 6 stay deferred to #155. |

## Assembly check

When this file was added to the repository, these claims were re-read at the pins. All matched as written.

| Claim | Rows | Checked at |
| --- | --- | --- |
| Revisions `A` and `B`, default `B` | S7-L1, S7-A2 | Z `boards/nxp/mimxrt1170_evk/board.yml`:7-12 |
| IS25WP128, 16 MiB at `0x30000000`, 104 MHz, JEDEC `9d 70 18`, 4 KiB erase block, write block 1 | S7-L3, S7-L4, S7-L7 | Z `mimxrt1170_evk.dtsi`:270-288 |
| Partitions `mcuboot` 128 KiB, `image-0` `0x20000` 7 MiB, `image-1` `0x720000` 7 MiB, `storage` `0xe20000` | S7-L6 | Z `mimxrt1170_evk.dtsi`:299-317 |
| `zephyr,sram` is `sdram0`, 64 MiB at `0x80000000` | S7-L9, S7-L10 | Z `mimxrt1170_evk_mimxrt1176_cm7.dts`:21-40 |
| FCB offset generic default `0x0` for FlexSPI NOR, RT11xx default `0x400` | S7-B3 | Z `soc/nxp/imxrt/Kconfig`:69-72, Z `soc/nxp/imxrt/imxrt11xx/Kconfig.defconfig`:25-26 |
| XMCD defaults on for the Cortex-M7, depends on DCD being off, offset `0x1040` for FlexSPI NOR | S7-B8, S7-B9 | Z board `Kconfig.defconfig`:8-13, Z `soc/nxp/imxrt/Kconfig`:103-127, Z board `xmcd/xmcd.c`:15-31 |
| Board CMake picks the FCB file by revision and builds XMCD, not DCD | S7-B2, S7-B10 | Z board `CMakeLists.txt`:7-37 |
| `CSF_ADDRESS` 0, `IVT_SIZE` `0x2000` | S7-B6, S7-B7 | H `devices/RT/RT1170/MIMXRT1176/xip/fsl_flexspi_nor_boot.h`:65 and 116 |
| Sample entries without revision for RT1160, RT1170 and `vmu_rt1170` | S7-A1 | M `boot/zephyr/sample.yaml`:29-30 and 37 |
| MCUboot board files: only `vmu_rt1170_mimxrt1176_cm7.conf` and the RT1050 RAM load overlay for i.MX RT | S7-A5 | File-name listing of M `boot/zephyr/boards/` |
| OTFAD on RT1160 and RT1170, context 0 for the ROM-configured bootloader region, CAAM red blob with OTPMK and "no fuse selection required", vendor board lists | S7-S6, S7-S8 | X `ota_examples/_doc/encrypted_xip_otfad.md`:7 and 20-30, X `ota_examples/_doc/encrypted_xip.md`:177-191 |

Rows not listed here were not re-checked when the record was assembled. Their labels are the study author's.
