# Pinned-source findings for the eMMC boot investigation

Issue [#298](https://github.com/wonny-labs/crux-pilot/issues/298). Read on 7 November 2026. Desk reading only: no build, no
hardware, no fuse operation, no dependency or manifest change. The decision record is
[boot-emmc-investigation.md](../../boot-emmc-investigation.md).

## Pins

| Short name | Revision | Used as |
| --- | --- | --- |
| `zephyr@684c9e8` | `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0) | The pin named in [#141](https://github.com/wonny-labs/crux-pilot/issues/141) |
| `zephyr@dccb095` | `dccb09599635bdff17633fa7e9dab014b91dce90` (v4.4.2) | The pin in this repository's `west.yml` on `main` (`3e0a5d7`) |
| `mcuboot@ee39e2d` | `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2` | The pin named in #141 |
| `mcuboot@6d3b3d2` | `6d3b3d2c38ab20c242e5b9abb04d050086383eb2` | Pinned by `zephyr@dccb095` `west.yml` line 338 |
| `hal_nxp@2c2f28a` | `2c2f28ac333e995d7279777409817c4b4f92c1ec` | The pin named in #141 |
| `hal_nxp@7554bc0` | `7554bc0f6f6c284972eba7b2c17b16fbb9cc0ad0` | Pinned by `zephyr@dccb095` `west.yml` line 216 |

**The revisions named in #141 are no longer the revisions on `main`.** `west.yml` on `main` pins Zephyr v4.4.2, whose own
`west.yml` pins MCUboot and `hal_nxp` at the second set above. MCUboot is four commits ahead and `hal_nxp` one commit ahead
of the #141 pins (GitHub compare API, `ahead_by`). Every file cited below was fetched at both sets of pins with
`raw.githubusercontent.com` and compared byte for byte. **All of them are identical.** A row therefore holds at both pins
unless it says otherwise. Line numbers are those of the files at the #141 pins, which equal those at the `main` pins.

Method limits:

- Files were read with `gh api` and `raw.githubusercontent.com`. Nothing was cloned and no dependency checkout was touched.
- **Absence claims are limited to the files and tree listings named in the row.** A row that says "file-name search" used the
  recursive tree listing of the pinned commit (`gh api repos/<owner>/<repo>/git/trees/<sha>?recursive=1`), not a content
  search of the whole tree.
- The MCUboot Zephyr-port absence claims use all 76 non-board source, Kconfig, CMake, devicetree-overlay and configuration
  files under `boot/zephyr/` at both pins. They are identical at both pins (76 compared, 0 different). Board overlays under
  `boot/zephyr/boards/` were checked by file name only.

## Labels

| Label | Meaning |
| --- | --- |
| Verified on pinned source | Read at the cited file and line of a pinned revision. Shows what the code allows, not what a board does. |
| Inference | Reasoned from verified lines. The reasoning is stated. |
| Not verified | Not checked or not checkable from source. |

## A. Zephyr boot header and boot device (question 1 and 4)

| # | Claim | Source | Label |
| --- | --- | --- | --- |
| P1 | The RT10xx boot-device choice offers XSPI NOR, FlexSPI NOR, FlexSPI NAND, SEMC NOR and SEMC NAND. It has no SD or MMC entry. | `zephyr` `soc/nxp/imxrt/Kconfig` lines 36 to 65 | Verified on pinned source |
| P2 | The IVT offset default is `0x1000` for FlexSPI NOR and SEMC NOR and `0x400` for NAND. | same, lines 94 to 97 | Verified on pinned source |
| P3 | The boot header is built only when the image is not an MCUboot application (`depends on !(BOOTLOADER_MCUBOOT \|\| SECOND_CORE_MCUX)`). | same, lines 24 to 30 | Verified on pinned source |
| P4 | For a non-XIP build the boot data is `start = CONFIG_SRAM_BASE_ADDRESS` and `size = &_image_ram_size`. For an XIP build it is `CONFIG_FLASH_BASE_ADDRESS` and `_flash_used`. The IVT carries the DCD pointer when `CONFIG_DEVICE_CONFIGURATION_DATA` is set, and `csf = CSF_ADDRESS`. | `zephyr` `soc/nxp/imxrt/imxrt10xx/soc.c` lines 51 to 76 | Verified on pinned source |
| P5 | `CSF_ADDRESS` is `0`, so a Zephyr-built image has no CSF. | `hal_nxp` `mcux/mcux-sdk-ng/devices/RT/RT1050/MIMXRT1052/xip/fsl_flexspi_nor_boot.h` line 91 (the RT1062 reuses this directory, #141 Stage 3 A2) | Verified on pinned source |
| P6 | The boot-header linker script places the FlexSPI configuration block at `CONFIG_FLEXSPI_CONFIG_BLOCK_OFFSET` for every RT10xx boot header, then the IVT, boot data and DCD at `CONFIG_IMAGE_VECTOR_TABLE_OFFSET`. The CMake rules do the same and add `XIP_EXTERNAL_FLASH` to the compile definitions. The configuration-block offset symbol is defined only under `BOOT_FLEXSPI_NOR`. | `zephyr` `soc/nxp/imxrt/boot_header.ld` lines 12 to 43, `soc/nxp/imxrt/CMakeLists.txt` lines 13 to 40, `soc/nxp/imxrt/Kconfig` lines 67 to 79 | Verified on pinned source |
| P7 | The only ROM-loader option is FlexSPI-to-RAM (`NXP_FLEXSPI_ROM_RAMLOADER`). | `zephyr` `soc/nxp/common/Kconfig.rom_loader` lines 4 to 21 | Verified on pinned source |
| P8 | The pinned Zephyr therefore has no option that produces an SD or eMMC boot image. A non-XIP boot header with the data start at the SRAM base exists (P4), but the header layout is the FlexSPI one (P6). Whether the ROM accepts a header built that way on SD or eMMC is not known. | P1, P4, P6 | Inference |
| P9 | The pinned RT1062 ROM API driver has no HAB entry. It has `ROM_RunBootloader(void *arg)`, which calls `runBootloader` in the ROM bootloader tree found through the pointer at `0x0020001c`. The header says to see the System Boot chapter of the reference manual for the argument. | `hal_nxp` `mcux/mcux-sdk-ng/devices/RT/RT1060/MIMXRT1062/drivers/romapi/fsl_romapi.c` lines 53, 63, 74 to 85, `fsl_romapi.h` lines 386 to 401 | Verified on pinned source. The meaning of `arg` is Not verified. |

## B. Board, SDRAM and the SD host (question 4)

| # | Claim | Source | Label |
| --- | --- | --- | --- |
| P10 | The EVK board sets `DEVICE_CONFIGURATION_DATA` and `NXP_IMX_EXTERNAL_SDRAM` to `y` by default. The board CMake adds `dcd/dcd.c` when the DCD is enabled and warns if the RAM base is SDRAM without a DCD ("This configuration may not boot"). | `zephyr` `boards/nxp/mimxrt1060_evk/Kconfig.defconfig` lines 8 to 12, `CMakeLists.txt` lines 43 to 52 | Verified on pinned source |
| P11 | `zephyr,sram` is `sdram0`, 32 MiB at `0x80000000`, described as an SDR SDRAM part on the evaluation kit. | `zephyr` `boards/nxp/mimxrt1060_evk/mimxrt1060_evk.dtsi` lines 28 to 42 | Verified on pinned source |
| P12 | `usdhc1` is the SD slot: pin states default, slow, med and fast, a power GPIO, card-detect GPIO and a `zephyr,sdmmc-disk` child named `SD`. The node sets `power-delay-ms = <1000>` (line 250). The same node is aliased as `m2_wifi_sdio`. The EVK dts has no `semc` reference, and the SoC code does not initialise the SEMC. | same, lines 249 to 265 and 344. `soc/nxp/imxrt/imxrt10xx/soc.c` (no `semc` match) | Verified on pinned source |
| P13 | The Zephyr uSDHC driver initialises the controller itself: it applies the default pin state, calls `USDHC_Init` and resets the whole controller, and it applies slow, medium and fast pin states as the clock changes. It does not read any state left by the boot ROM. | `zephyr` `drivers/sdhc/imx_usdhc.c` lines 201, 314, 430 to 447, 1093 to 1128 | Verified on pinned source |
| P14 | The SD/MMC stack resets the card with CMD0 at the start of initialisation (`sd_idle`, and again before MMC initialisation), then probes SDIO, SD, and MMC in turn. | `zephyr` `subsys/sd/sd.c` lines 21 to 33, 48, 229 to 238 | Verified on pinned source |
| P15 | The stack has a sector size of 512 bytes (`SDMMC_DEFAULT_BLOCK_SIZE`), reads EXT_CSD, selects bus width and timing, and turns the cache on. | `zephyr` `subsys/sd/mmc.c` lines 170 to 188, 377 to 456, 589, 618 to 634 | Verified on pinned source |
| P16 | The files `subsys/sd/mmc.c`, `sd.c`, `sd_ops.c` and `drivers/disk/mmc_subsys.c` contain no reference to boot partitions, partition configuration, RPMB, boot or user write protection, or reliable write. The only write-protect matches are the SD CSD flags. | search of those four files, `sd_ops.c` lines 177 and 180 | Verified on pinned source (those four files only) |

## C. Zephyr flash map and the MCUboot port (question 2)

| # | Claim | Source | Label |
| --- | --- | --- | --- |
| P17 | A Zephyr `flash_area` is a partition of a **flash-API device**: it holds `fa_dev`, a `struct device *`. `flash_area_read`, `flash_area_write` and `flash_area_erase` call `flash_read`, `flash_write` and `flash_erase` on that device. `flash_area_align` calls `flash_get_write_block_size` and `flash_area_erased_val` calls `flash_get_parameters`. | `zephyr` `include/zephyr/storage/flash_map.h` lines 57 to 71, `subsys/storage/flash_map/flash_map.c` lines 56 to 82, 108 to 110, 134 to 138 | Verified on pinned source |
| P18 | The default flash map is built from devicetree `fixed-partitions` nodes. The backing device is the grandparent of the partition node (or the memory device of a mapped partition). | `zephyr` `subsys/storage/flash_map/flash_map_default.c` lines 43 to 65, `include/zephyr/devicetree/fixed-partitions.h` lines 96 to 98 | Verified on pinned source |
| P19 | The MCUboot Zephyr port gets its single flash device from the chosen node `zephyr,flash-controller` (or `zephyr,flash` on one SoC), with `FLASH_DEVICE_BASE` and `FLASH_DEVICE_ID` per branch, and stops with `#error "FLASH_DEVICE_ID could not be determined"` otherwise. For this SoC family the branch tests that the chosen node's parent is `nxp,imx-flexspi`. Sector information comes from `flash_get_page_info_by_offs`. | `mcuboot` `boot/zephyr/flash_map_extended.c` lines 43 to 75, 166 to 180, 195 to 210 | Verified on pinned source |
| P20 | The port selects the flash map as its I/O back end and the flash page layout when the device has one. | `mcuboot` `boot/zephyr/Kconfig` lines 1443 to 1450 | Verified on pinned source |
| P21 | The port supports devices without explicit erase (`MCUBOOT_STORAGE_WITHOUT_ERASE`, `depends on FLASH_HAS_NO_EXPLICIT_ERASE`, default `y`). Its help text says some drivers emulate erase by a write. | `mcuboot` `boot/zephyr/Kconfig` lines 1303 to 1316 | Verified on pinned source |
| P22 | None of the 76 source, Kconfig, CMake, overlay and configuration files read under `boot/zephyr/` contains `mmc`, `sdhc`, `sdmmc`, `disk_access` or `usdhc`. No file name in the recursive tree listing of either MCUboot pin contains `mmc`, `sdhc`, `disk` or `sdcard`. | all `boot/zephyr/` files (76 compared at both pins); file-name search of the recursive tree listing | Verified on pinned source (files read and file names only) |
| P23 | The Zephyr tree has no flash driver that sits on a disk or MMC device. The only flash-and-disk pairing is `drivers/disk/flashdisk.c`, the reverse (a flash device exposed as a disk), with binding `zephyr,flash-disk`. | file-name search of the recursive tree listing of `zephyr@684c9e8` and `zephyr@dccb095` (`flashdisk`, `zephyr,flash-disk`) | Verified on pinned source (file names only) |
| P24 | RAM load reads an image with one `flash_area_read` of the whole image into the destination: `flash_area_read(fap_src, 0, (void *)(IMAGE_RAM_BASE + img_dst), img_sz)`, and `boot_validate_slot` reads headers and trailers the same way. | `mcuboot` `boot/bootutil/src/ram_load.c` lines 149, 219; `boot/bootutil/src/loader.c` lines 603, 686, 2310 | Verified on pinned source |
| P25 | MCUboot's hooks can replace the image header read, the image hash and signature validation, the update, `flash_area_id_from_multi_image_slot` and `flash_area_get_device_id`. They do not replace `flash_area_read`, `flash_area_write` or `flash_area_erase`. | `mcuboot` `boot/bootutil/include/bootutil/boot_hooks.h` lines 122, 125 to 135, 152, 171, 253, 267 | Verified on pinned source (hook list as declared in that header) |
| P26 | The trailer protocol writes with `flash_area_write` in a fixed order. In the swap variant of `boot_set_next`, a test request writes the magic first, then `image_ok` when confirming, then the swap info. A write of the magic is padded with the erased value to the write alignment. `boot_write_trailer` pads to `flash_area_align` and the erased value. | `mcuboot` `boot/bootutil/src/bootutil_public.c` lines 303 to 337, 345 to 372, 528 to 590 | Verified on pinned source |

## D. What the evidence does not contain

- No build of any kind was run for this record. No size, RAM or timing figure exists for an SD or eMMC stack inside a
  bootloader.
- No content search of the whole pinned trees was run. Whole-tree absence of SD or MMC handling in MCUboot is Not verified,
  as it was in #141.
