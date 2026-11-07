# Public vendor documents on RT1060 SD and eMMC boot

Issue [#298](https://github.com/wonny-labs/crux-pilot/issues/298). Read on 7 November 2026. The decision record is
[boot-emmc-investigation.md](../../boot-emmc-investigation.md).

## What could and could not be read

**The i.MX RT1060 Reference Manual (IMXRT1060RM), chapter "System Boot", was not retrieved.** It is the document that
settles most of questions 1, 3 and 4 for the boot ROM. `https://www.nxp.com/docs/en/reference-manual/IMXRT1060RM.pdf` returned
HTTP 404, and the download link redirects to an NXP sign-in page. A third-party copy exists but exceeds the size limit of
the fetch tool, and downloading a file from a third party needs the owner's approval, so it was not fetched. Nothing in this
record fills that gap from memory.

What was read:

| Document | Read how |
| --- | --- |
| "i.MX RT1060 Manufacturing User Guide", MCUXpresso SDK HTML edition, pages `imx_rt_bootable_image`, `generate_imx_rt_bootable_image`, `generate_sb_file_for_bootable_image_programming`, `overview` | **Full text of each page** converted from HTML by the author with a script, then searched and read around keywords. Figures (including the eMMC BD-file figures) are not in the text. |
| Same guide, Rev. 2, 08/2018, PDF (`community.nxp.com` attachment) | Text extracted locally from the PDF by a research subagent. Tables and figures were not recoverable. Counts as a summarising read, not a full read. |
| SPSDK v3.12.0 documentation `blhost`, `sdphost`, `nxpimage` (`spsdk.readthedocs.io`) | `blhost`: full page text read by the author. `sdphost` and `nxpimage`: summarising fetch by a subagent, partial. |
| AN12240 (RT1060 enhanced features), AN12107 Rev. 5 (SD and octal flash boot, **RT1050**) | Text extracted locally from the PDFs by a subagent. AN12107 is for another device and is not used as an RT1060 fact. |
| NXP community threads | Returned HTTP 403 to the fetch tool. Only search snippets were seen. **They are not used as evidence anywhere in this record.** |

## Labels

| Label | Meaning |
| --- | --- |
| Verified by public vendor document (full page text) | The author read the whole text of the page. It is the vendor's statement, not a Crux result. |
| Verified by public vendor document (summarising read) | Read through a summarising fetch or a partial text extraction. The vendor's statement, not a Crux result. |
| Other device | The document is for RT1050 or another family. Never an RT1060 fact. |
| Not verified | Not retrieved or not stated by any retrieved document. |

## Findings

| # | Claim | Source | Label |
| --- | --- | --- | --- |
| V1 | RT1060 bootable images are XIP or non-XIP. A non-XIP image is "usually for the NAND, SD, and eMMC devices": the boot device memory differs from the destination memory, and the ROM loads the image into the destination memory. | `https://docs.mcuxpresso.nxp.com/mcuxsdk/latest/html/middleware/mcu_bootloader/docs/iMXRT1060_Manufacturing_User_Guide/topics/imx_rt_bootable_image.html` | Verified by public vendor document (full page text) |
| V2 | The IVT offset is `0x1000` for external memories that support XIP and `0x400` otherwise. The example given is FlexSPI NOR on the RT1060 at `0x60001000`. The page refers to the processor reference manual for more. | same | Verified by public vendor document (full page text) |
| V3 | An image consists of an IVT and boot data (required), a DCD (optional, "usually" SDRAM or DDR set-up), the application, and an optional CSF and key blob. The IVT is 32 bytes, with the DCD, boot-data and CSF pointers. Boot data is the image start address, the image length and a plugin flag. | same page, and the Rev. 2 PDF section 3.2 | Verified by public vendor document (full page text for the HTML page) |
| V4 | For SD and for eMMC the guide states that the image "always starts at offset 0x400", and that the programming input is the `_nopadding.bin` output of `elftosb`. | `.../topics/generate_sb_file_for_bootable_image_programming.html` | Verified by public vendor document (full page text) |
| V5 | The guide has a normal eMMC boot flow of six BD steps and a fast boot flow of nine. Both contain an optional step that programs "optimal eMMC boot parameters" into fuses. Fast boot adds a second option block, a re-enable and a load of the user data area. The eMMC section **does not mention a boot partition**. The only partition-like term is "User Data area" in the fast flow. | same page | Verified by public vendor document (full page text). The option-block and fuse values are only in figures and are Not verified. |
| V6 | The guide says that some features need fuses programmed first: "to enable Fast boot mode for eMMC, enable HAB closed mode". Its HAB-closed example programs the SRK table and a security configuration fuse. | same page, "Generate SB file for fuse programming" | Verified by public vendor document (full page text) |
| V7 | `elftosb` accepts `ivtOffset` `0x400` (default) or `0x1000` for an i.MX RT image, `startAddress` is where "image should be loaded by ROM", and a DCD file is placed at `0x40` after the IVT offset. | `.../topics/generate_imx_rt_bootable_image.html` | Verified by public vendor document (full page text) |
| V8 | The examples on that page use `0x400` for unsigned and encrypted images and `0x1000` for signed and plugin images. This differs from V4. The documents do not reconcile the two for a signed image on SD or eMMC. | same page (summarising read of the examples) | Verified by public vendor document (summarising read). The conflict is open. |
| V9 | The ROM provides a serial downloader over UART or USB-HID into internal RAM, with the SDP command interface, for flash programming. The tool path loads a Flashloader into SRAM. The guide programs eMMC and SD with BD commands `enable`, `erase` and `load` for `sdcard` and `mmccard`, from serial downloader mode. | `.../topics/overview.html` (full page text). BD commands and scope: Rev. 2 PDF sections 1, 2 and 5 | Overview: full page text. BD commands: summarising read. |
| V10 | `blhost` memory identifiers include `0x120` (uSDHC SD memory) and `0x121` (uSDHC MMC memory). The table is not tied to a chip family, so it is not by itself an RT1060 statement. | `https://spsdk.readthedocs.io/en/v3.12.0/apps/blhost.html` | Verified by public vendor document (full page text) |
| V11 | The Rev. 2 PDF gives, for the RT1060 evaluation kit, serial downloader as switches 1-OFF, 2-OFF, 3-OFF, 4-ON and QSPI internal boot as 1-OFF, 2-OFF, 3-ON, 4-OFF. The guide names the switch SW7, which is the Rev A naming. On the EVKB the boot switch is SW4 (#141 Stage 3 D3). | Rev. 2 PDF section 7.2 | Verified by public vendor document (summarising read). Applies to the evaluation kit named in the guide, not necessarily to the EVKB. |
| V12 | The guide describes HAB signing for the RT1060 (`elftosb` flags for signed and signed-and-encrypted images with a CSF appended), and a plugin flow in which the ROM authenticates a non-XIP image in RAM that a plugin loaded. It does not state the behaviour on a failed authentication, in open or closed configuration, for the SD or eMMC path. | Rev. 2 PDF sections 5.8, 6, 7.1 | Verified by public vendor document (summarising read). The SD and eMMC authentication behaviour is Not verified. |
| V13 | RT1050 (other device): BOOT_MODE[1:0] is sampled at power-on reset, with `00` boot from fuses, `01` serial downloader, `10` internal boot with BOOT_CFG pins, `11` reserved. The note lists SD and MMC among the boot devices and secure boot with HAB on all boot devices. | AN12107 Rev. 5 sections 2.1, 2.2, 2.4 | Other device (RT1050). Not an RT1060 fact. |

## Not retrieved, so Not verified for the RT1060

| # | Item | What would settle it |
| --- | --- | --- |
| X1 | Whether the ROM reads the eMMC user area or a boot partition, and how it treats `PARTITION_CONFIG` and the boot partition enable bits. The Manufacturing Guide never mentions a boot partition (V5). | Reference manual, chapter System Boot |
| X2 | Which BOOT_CFG pins or fuses select SD or eMMC, bus width, speed, fast boot, and which uSDHC instance the ROM uses. | Reference manual |
| X3 | The values in the eMMC BD files. | The figures of the guide, or the Flashloader release package (a download, not done) |
| X4 | BOOT_MODE and BT_FUSE_SEL on the RT1060, what the ROM does when no valid image is found, and what it does when HAB authentication fails, in open and in closed configuration. | Reference manual, security reference manual |
| X5 | Whether the ROM runs the DCD before it jumps to a non-XIP image, in what order, which commands are allowed, and whether it initialises uSDHC and the card, in what mode, and what state it leaves the card in. | Reference manual |
| X6 | Whether the current RT1060 Flashloader can program an eMMC through an SD adapter, and which uSDHC instance it uses. A forum snippet and a community fork claim fixed instances and a failure through an adapter. Not used. | Flashloader source in an SDK, or the MCU Bootloader reference manual for the release |
| X7 | Whether a signed image on SD or eMMC uses IVT offset `0x400` or `0x1000` (V4, V8). | Reference manual, CST user guide |
