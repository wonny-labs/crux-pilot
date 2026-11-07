# eMMC boot, write-protection and power-loss behaviour, and hardware options

Issue [#298](https://github.com/wonny-labs/crux-pilot/issues/298). Read on 7 November 2026. The decision record is
[boot-emmc-investigation.md](../../boot-emmc-investigation.md).

## Read this first

- **What the code does is not what the standard requires.** Rows about Linux, U-Boot, mmc-utils and Zephyr say what that code
  does. **The JEDEC eMMC standard (JESD84-B51x) could not be read**: `jedec.org` returns HTTP 403 to scripted access. Every
  statement about what the standard requires is Not verified. The nearest public text is a vendor datasheet that restates the
  standard's register access legend. It is a restatement, not the standard.
- Rows marked "author re-read" were fetched again by the author of the record and checked against the cited lines. Other rows
  were read by a research subagent with file and line citations and were **not** re-read by the author. They are the
  subagent's finding.
- Absence claims cover only the files named in the row.
- Sources and revisions: Linux v6.12 (tag, `adc218676eef25575469234709c2d87185ca223a`), U-Boot v2026.10 (tag,
  `a58e2b7af42c179ace2c6210de0a219fa3f3cf3c`), mmc-utils master `0c1c179b82ecd813a6b1396f757b599c2e284708` (its only tag is
  v1.0), Zephyr v4.4.2 (`dccb09599635bdff17633fa7e9dab014b91dce90`). The Linux, U-Boot and mmc-utils trees are single points in
  time.
- No hardware was used. Nothing here is evidence about any eMMC part or board.

## Labels

| Label | Meaning |
| --- | --- |
| Verified on source (author re-read) | Read at the cited file and line by the author. Shows what the code does. |
| Verified on source (subagent) | Read by a research subagent at the cited file and line, not re-read by the author. |
| Verified by public document | A public vendor document or datasheet says so. A vendor statement, not the standard and not a Crux result. |
| Third-party statement | A blog, forum, news article or community page. A hint, not evidence. |
| Inference | Reasoned from the rows named. |
| Not verified | Not retrieved or not stated by any retrieved source. |

## A. Boot partitions and write protection (question 3)

| # | Claim | Source | Label |
| --- | --- | --- | --- |
| E1 | Linux creates the two boot areas as `boot0` and `boot1` block devices of BOOT_MULT x 128 KiB and marks them **read-only by default**. Linux documentation: write access needs `force_ro` cleared. A boot partition can be locked read-only until the next power-on with `ro_lock_until_next_power_on`, which exists only if the card supports it and is read-only if the card has disabled it. Permanent locking exists but is deliberately not offered through sysfs. | Linux v6.12 `Documentation/driver-api/mmc/mmc-dev-parts.rst` lines 12 to 41, `drivers/mmc/core/mmc.c` lines 495 to 501 | Verified on source (author re-read, documentation and the default-read-only comment) |
| E2 | In Linux code, `force_ro` flips only the block-layer flag and sends nothing to the card. The power-on lock writes `BOOT_WP |= B_PWR_WP_EN` with CMD6. | Linux v6.12 `drivers/mmc/core/block.c` lines 270 to 371, 1166 to 1177 | Verified on source (subagent). `B_PWR_WP_EN` and the attribute names author re-read. |
| E3 | mmc-utils `writeprotect boot set` writes power-on protection by default. The `-p` flag (permanent protection) is compiled in only when `DANGEROUS_COMMANDS_ENABLED`, and its help text calls it a one-time-programmable, unreversible change. `bootpart enable` selects boot0, boot1 or the user area as the boot partition and sets the boot acknowledge bit. | mmc-utils `mmc.c` lines 57 to 76, 115 to 118, `mmc_cmds.c` lines 203 to 372, 610 to 680 | Verified on source (author re-read for the `-p` flag and help text; subagent for the rest) |
| E4 | U-Boot `mmc wp` sets only power-on protection and has no permanent-protect command in the files read. `mmc partconf` writes the whole PARTITION_CONFIG byte from three arguments. | U-Boot v2026.10 `drivers/mmc/mmc.c` lines 935 to 965, `drivers/mmc/mmc_boot.c` lines 209 to 230, `cmd/mmc.c` lines 112 to 134, 952 to 1003, 1127 to 1150 | Verified on source (subagent) |
| E5 | `RST_n_FUNCTION`: U-Boot and mmc-utils both treat non-zero values as write-once. mmc-utils refuses to change the field once it is permanently 1 or 2. Linux uses the hardware reset only if the field is 1. | U-Boot `drivers/mmc/mmc_boot.c` lines 232 to 242, `cmd/mmc.c` lines 1005 to 1042; mmc-utils `mmc.c` lines 133 to 139, `mmc_cmds.c` lines 752 to 797; Linux `drivers/mmc/core/mmc.c` lines 2241 to 2249 | Verified on source (subagent) |
| E6 | Linux resets PARTITION_ACCESS to 0 at every card initialisation if it is non-zero. U-Boot sends CMD0 during initialisation and sets its tracked partition to 0, with a comment that the internal partition resets to the user partition at every CMD0. | Linux `drivers/mmc/core/mmc.c` lines 1765 to 1775; U-Boot `drivers/mmc/mmc.c` lines 3012 to 3023 | Verified on source (author re-read) |
| E7 | A public vendor datasheet restating JESD84-B51A defines register access codes: R/W is one-time-programmable and readable. R/W/E keeps its value across power failure, hardware reset and CMD0. R/W/C_P is writable again after a power failure or hardware reset clears it and is not cleared by CMD0. R/W/E_P is reset by power failure, hardware reset and CMD0. In its EXT_CSD table PARTITION_CONFIG is R/W/E and R/W/E_P (two sub-fields), BOOT_WP is R/W and R/W/C_P, BOOT_CONFIG_PROT is R/W and R/W/C_P, BOOT_BUS_CONDITIONS is R/W/E, USER_WP is R/W, R/W/C_P and R/W/E_P, RST_n_FUNCTION is R/W, BOOT_SIZE_MULT is read-only. The extracted table is garbled, so the bit ranges of each code are not recoverable. | Exascend EM300 eMMC5.1 datasheet REV 001, section 9-3 legend and Table 11 (`exascend.com`, text extracted by the subagent) | Verified by public document (a restatement of the standard, subagent read) |
| E8 | The same datasheet says a hardware reset puts the device in Pre-Idle and disables power-on write protection set before the reset, and that the device is in boot mode after a power cycle, after CMD0 with argument `0xF0F0F0F0` or after a hardware reset. | same datasheet, sections 6.3 and 6.11 | Verified by public document (restatement, subagent read) |
| E9 | The PARTITION_ACCESS bits are probably the volatile sub-field and BOOT_PARTITION_ENABLE and the boot acknowledge bit probably the non-volatile sub-field. A power-on-protected boot partition is unlocked by the next power cycle or RST_n pulse, and a plain CMD0 probably does not unlock it. No source states the sub-field assignment or the CMD0 point directly. | E6, E7, E8 | Inference |
| E10 | The permanent protection bits, the bits that disable power-on or permanent protection, and `RST_n_FUNCTION` are one-time-programmable in every source read. Setting one on a shared device cannot be undone. | E3, E5, E7 | Verified on source and by public document (consistent), semantics of the disable bits Not verified |
| E11 | A third-party post proposes a bootloader A/B update on eMMC: write the inactive boot area while booting from the other, then switch with `mmc partconf` or `mmc bootpart enable`. It says BOOT_PARTITION_ENABLE is non-volatile. It does not claim that the switch is atomic under power loss. | `thegoodpenguin.co.uk` blog, "Power failure tolerant bootloader flashing on eMMC" | Third-party statement |
| E12 | Whether a CMD6 write of an EXT_CSD byte, including the boot partition switch, is atomic under power loss is not stated in any free source retrieved. BOOT_CONFIG_PROT is not referenced by Linux or U-Boot in the files read, and mmc-utils only prints it. | Linux `drivers/mmc/core/mmc_ops.c` lines 595 to 645 (waits for busy, no atomicity statement) | Verified on source (absence, files named, subagent). The atomicity is Not verified. |

## B. Cache, ordering and reliable write (question 5)

| # | Claim | Source | Label |
| --- | --- | --- | --- |
| E13 | Linux enables the eMMC cache at every initialisation when the device reports one, flushes it with a CMD6 `FLUSH_CACHE` on a block flush request, and maps FUA writes to eMMC Reliable Write when the card and host support it. So the kernel relies on flush for ordering and on Reliable Write for forced unit access. | Linux v6.12 `drivers/mmc/core/mmc.c` lines 1861 to 1887, 2078 to 2105, `drivers/mmc/core/block.c` lines 1322 to 1360, 1750 to 1760, 2458 to 2478 | Verified on source (subagent) |
| E14 | A vendor datasheet restating JESD84-B51A says a 512-byte reliable write keeps the old data at an address valid until the new data is fully programmed, so a sudden power loss cannot leave undefined data there. CACHE_CTRL is R/W/E_P in the same datasheet, so the cache enable is lost at power failure, reset and CMD0. | Exascend EM300 REV 001 sections 6.4, 6.13, Table 11 | Verified by public document (restatement, subagent read) |
| E15 | Third-party documentation says that with write reliability enabled, writes are atomic at eMMC level and refers to a vendor note on power-loss data integrity that was not retrieved. A public littlefs discussion asks whether reliable write also protects translation-layer state and leaves it unresolved. | `developer.toradex.com` eMMC page; `github.com/littlefs-project/littlefs/issues/1083` | Third-party statement |
| E16 | Whether a plain single-sector write is atomic under power loss, and whether the cache guarantees write ordering, are not established by any free source retrieved. | n/a | Not verified |

## C. Zephyr SD/MMC stack (questions 2, 3 and 5)

| # | Claim | Source | Label |
| --- | --- | --- | --- |
| E17 | At the pin, `mmc_card_init` turns the eMMC cache on when the device reports one, and the disk `SYNC` ioctl only waits for the card to be ready, under a comment that says the stack does not support enabling caching. The comment is unchanged on Zephyr `main` (`0c63b6bf`). This matches finding F2 of the M14 design and the correction merged for #272. | Zephyr v4.4.2 `subsys/sd/mmc.c` lines 49 to 51, 187 to 188, 618 to 639, `subsys/sd/sd_ops.c` lines 871 to 877, `drivers/disk/mmc_subsys.c` lines 88 to 94 | Verified on source (author re-read for `sd_ops.c`) |
| E18 | The stack toggles card power through the host and sends CMD0 on every initialisation. Real power removal depends on the host driver and the board's power GPIO. | Zephyr v4.4.2 `subsys/sd/sd.c` lines 22 to 33, 47 to 52, 150 to 170, 227 to 236, 270 | Verified on source (subagent; CMD0 lines author re-read) |
| E19 | The stack does not read or write PARTITION_CONFIG, BOOT_WP, BOOT_BUS_CONDITIONS, `RST_n_FUNCTION`, the reliable-write fields or the background-operation fields, has no RPMB or boot-partition access path, and issues writes without the reliable-write bit. So it leaves the card in the partition that CMD0 or power-on left (the user area, by E6 to E9). | Zephyr v4.4.2 `subsys/sd/mmc.c`, `sd.c`, `sd_ops.c`, `drivers/disk/mmc_subsys.c`, `drivers/sdhc/imx_usdhc.c` (searched for partition, boot, rpmb, reliable, PART_CONFIG, BKOPS, USER_WP, RST_N, FLUSH, CACHE) | Verified on source (files named only; author re-read for the first four) |

## D. Hardware (the hardware question)

| # | Claim | Source | Label |
| --- | --- | --- | --- |
| H1 | Zephyr's board page for the MIMXRT1060-EVK lists SDRAM, QSPI flash, HyperFlash and a TF socket for an SD card, and does not mention eMMC. | Zephyr v4.4.2 `boards/nxp/mimxrt1060_evk/doc/index.rst` lines 19 to 26, 233 to 243 | Verified on source (subagent; absence in that file) |
| H2 | Zephyr enables only `usdhc1` on the EVK, with a power GPIO, a card-detect GPIO and an SD disk child. NXP's SDK board configuration for the EVKB and the EVKC defines the SD, MMC and SDIO hosts all as USDHC1. NXP's SDK examples tree has no `mmccard` example for any RT1060 board. | Zephyr `boards/nxp/mimxrt1060_evk/mimxrt1060_evk.dtsi` lines 249 to 265; `nxp-mcuxpresso/mcuxsdk-examples` at `7f27ca7c`, `_boards/evkbmimxrt1060/sdmmc_config.h` | Verified on source (subagent; the dtsi lines author re-read) |
| H3 | The RT1062 pad data shows USDHC1 with CMD, CLK and DATA0 to DATA3 only, and USDHC2 with DATA0 to DATA7, CMD, CLK and RESET_B. On the EVK as Zephyr configures it, the USDHC2 pad group `SD_B1_00` to `SD_B1_11` is taken by FlexSPI. An 8-bit USDHC2 port is therefore not free on the EVK without rework. | `hal_nxp` `7554bc0f` `dts/nxp/nxp_imx/rt/mimxrt1062dvl6a-pinctrl.dtsi`; Zephyr `boards/nxp/mimxrt1060_evk/mimxrt1060_evk-pinctrl.dtsi` | Verified on source for the pad data. The EVK use is an Inference, the schematic was not retrieved. |
| H4 | NXP's EVKB product page lists an SD card slot and no eMMC. The EVKB and EVKC user manuals and the EVKB schematic are behind a login or blocked. | `nxp.com` product page (summarising read) | Verified by public document (summarising read). Manuals and schematic: Not verified. |
| H5 | NXP's generic and RT1060 Manufacturing User Guides document eMMC as a non-XIP boot device with normal and fast boot, with images at offset `0x400`, and never name a boot partition or a pin set (see [vendor-documents.md](vendor-documents.md) V4, V5). An NXP SDK driver example, `mmccard_boot_blocking`, shows how to access an MMC boot partition and boot from it, stores a boot configuration including bus width and timing, supports on-board eMMC or MMC Plus cards, advises against an eMMC socket because of high-speed timing, and lists no supported board. It is a generic driver example, **not a statement about the RT1060 ROM.** | `docs.mcuxpresso.nxp.com` `mmccard_boot_blocking` readme (author re-read) | Verified by public document (full page text) |
| H6 | Public third-party items found, listed without recommendation: an RT1064 module with an optional eMMC (a news article says so, the vendor page does not mention it), bare adapter boards that put a BGA eMMC chip in a microSD form factor (a shop listing and a community design), a module whose product page failed TLS verification and was not read, and chip-off socket readers aimed at data recovery. For none of them was boot compatibility with an RT1060, the achievable bus width or the supply handling confirmed. | Third-party pages named in the research notes; not retrieved or summarising reads | Third-party statement. Not verified for any use with the RT1060. |
| H7 | Minimum needs for a meaningful validation, as reasoning from E1 to E19 and H1 to H5: an eMMC on a uSDHC whose ROM boot pads, boot-mode selection and fuse settings are documented for the RT1060 or RT1062, with RST_n and the 8-bit pad set available. Controllable power for both eMMC supplies, so a cut can land during a write, a CMD6 switch or a cache flush, because an MCU reset does not remove eMMC power. Sacrificial eMMC parts, because several protection bits are permanent. A way to read EXT_CSD and the raw boot partitions independently of the unit under test. Observability of CLK, CMD, DAT0, RST_n and the boot-mode pins. External tooling for PARTITION_CONFIG and write protection, because Zephyr has none (E19). | E1 to E19, H1 to H5 | Inference |

## Not retrieved

| # | Item | What would settle it |
| --- | --- | --- |
| Y1 | JESD84-B51x text: EXT_CSD reset behaviour per byte, PARTITION_CONFIG sub-field volatility (E9), the effect of CMD0, RST_n and a power cycle on write protection, BOOT_CONFIG_PROT, atomicity of CMD6 writes and single-sector writes, cache ordering. | A copy of JESD84-B51x, if the owner has a JEDEC login |
| Y2 | The RT1060 reference manual, data sheet and the EVK schematics and user manuals. | NXP documents behind a login |
| Y3 | A power-loss application note from an eMMC vendor, and the datasheet text on the disable bits of write protection. | Vendor documents |
| Y4 | Capability of the Zephyr uSDHC driver for 8-bit transfers and HS200 at the pins, and the power GPIO on the EVKB and EVKC (the Zephyr dts uses GPIO1 pin 5, the NXP SDK for the EVKB uses GPIO1 pin 19, not reconciled). | Source reading of the driver and the schematic |
