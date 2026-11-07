# Public vendor documents on RT1060 SD and eMMC boot

Issue [#298](https://github.com/wonny-labs/crux-pilot/issues/298). Read on 7 November 2026. The decision record is
[boot-emmc-investigation.md](../../boot-emmc-investigation.md).

## What could and could not be read

**The i.MX RT1060 Reference Manual (IMXRT1060RM) was first not retrievable, and was then read in a third-party-hosted copy.**
`https://www.nxp.com/docs/en/reference-manual/IMXRT1060RM.pdf` returned HTTP 404, and NXP's download link redirects to a
sign-in page. A third-party-hosted copy of Rev. 3 exists. The owner approved downloading that one file in chat on 7 November
2026, after being told its name, host and size. The author then read chapter 9 "System Boot" in it, see
[the manual rows](#rt1060-reference-manual-rev-3-third-party-copy). **That copy has not been compared with a copy from
`nxp.com` and its revision is not the newest one, if a newer one exists.** The sections of this file that cite only the
Manufacturing Guide, the SDK pages and SPSDK are unchanged.

What was read:

| Document | Read how |
| --- | --- |
| i.MX RT1060 Processor Reference Manual, Rev. 3, 07/2021, third-party-hosted copy (see below) | Chapter 9 pages 196 to 202 and 241 to 276 as local text extraction, read by the author. Tables are partly garbled, figures are only extracted text. |
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
| Verified by public vendor document (reference manual copy) | Read by the author in the text of the third-party-hosted Rev. 3 copy of the reference manual. Page numbers are manual page numbers, which equal the PDF page index. |
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
| V8 | The examples on that page use `0x400` for unsigned and encrypted images and `0x1000` for signed and plugin images. This differs from V4. The Guide does not reconcile the two for a signed image on SD or eMMC. The reference manual gives `0x400` for SD and eMMC and `0x1000` for NOR (R4), so the `0x1000` examples probably describe NOR boot (Inference). | same page (summarising read of the examples); R4 | Verified by public vendor document (summarising read). The reference manual narrows it. |
| V9 | The ROM provides a serial downloader over UART or USB-HID into internal RAM, with the SDP command interface, for flash programming. The tool path loads a Flashloader into SRAM. The guide programs eMMC and SD with BD commands `enable`, `erase` and `load` for `sdcard` and `mmccard`, from serial downloader mode. | `.../topics/overview.html` (full page text). BD commands and scope: Rev. 2 PDF sections 1, 2 and 5 | Overview: full page text. BD commands: summarising read. |
| V10 | `blhost` memory identifiers include `0x120` (uSDHC SD memory) and `0x121` (uSDHC MMC memory). The table is not tied to a chip family, so it is not by itself an RT1060 statement. | `https://spsdk.readthedocs.io/en/v3.12.0/apps/blhost.html` | Verified by public vendor document (full page text) |
| V11 | The Rev. 2 PDF gives, for the RT1060 evaluation kit, serial downloader as switches 1-OFF, 2-OFF, 3-OFF, 4-ON and QSPI internal boot as 1-OFF, 2-OFF, 3-ON, 4-OFF. The guide names the switch SW7, which is the Rev A naming. On the EVKB the boot switch is SW4 (#141 Stage 3 D3). | Rev. 2 PDF section 7.2 | Verified by public vendor document (summarising read). Applies to the evaluation kit named in the guide, not necessarily to the EVKB. |
| V12 | The guide describes HAB signing for the RT1060 (`elftosb` flags for signed and signed-and-encrypted images with a CSF appended), and a plugin flow in which the ROM authenticates a non-XIP image in RAM that a plugin loaded. It does not state the behaviour on a failed authentication, in open or closed configuration, for the SD or eMMC path. | Rev. 2 PDF sections 5.8, 6, 7.1 | Verified by public vendor document (summarising read). The SD and eMMC authentication behaviour is Not verified. |
| V13 | RT1050 (other device): BOOT_MODE[1:0] is sampled at power-on reset, with `00` boot from fuses, `01` serial downloader, `10` internal boot with BOOT_CFG pins, `11` reserved. The note lists SD and MMC among the boot devices and secure boot with HAB on all boot devices. | AN12107 Rev. 5 sections 2.1, 2.2, 2.4 | Other device (RT1050). Not an RT1060 fact. |

## RT1060 reference manual, Rev. 3, third-party copy

Source: `https://www.pjrc.com/teensy/IMXRT1060RM_rev3.pdf`, 29,973,751 bytes, SHA-256
`a2e342e492eded2a1d626cf955de953ca28d8db8675b10937a5176fd6f2c9200`, 3,522 pages, footer "i.MX RT1060 Processor Reference
Manual, Rev. 3, 07/2021". Downloaded on 7 November 2026 into the author's scratchpad with the owner's approval in chat, read
as text extracted with macOS PDFKit, never executed. It is a copy hosted by a third party. It was not compared with a copy
from `nxp.com`. Tables are partly garbled in the extraction and figures are text only, so box order in flow figures is taken
from the surrounding prose and from the order in the text. PR [#302](https://github.com/wonny-labs/crux-pilot/pull/302)
(the #299 record) reports several of the same statements from the same copy in its evidence file
`docs/evidence/boot-bootloader-update/b-nxp-rom-fuses-hab.md`. The author checked rows R1 to R12 against the manual text
independently of that file. Where #302 states more than this file does, it is not repeated here.

| # | Claim | Manual page | Label |
| --- | --- | --- | --- |
| R1 | **Boot partition selection.** For an MMC 4.x card, after initialisation, the ROM reads the `BOOT_PARTITION_ENABLE` field of Ext_CSD[179] to get the boot partition to set. If no boot partition is named, or the user partition is named, the ROM boots from the user partition. The boot flow figure shows the steps "Extract the boot partition to set", "Got valid partition?" and "Send switch command to select partition". | 244 (Table 9-33), 246 to 247 (flow) | Verified by public vendor document (reference manual copy) |
| R2 | No fallback from one boot partition to the other appears in Table 9-33, in the flow figure text, or in sections 9.6.7.1 to 9.6.7.4 as extracted. | 243 to 254 | Verified by public vendor document (reference manual copy), absence limited to the extracted text read |
| R3 | **Image placement.** The ROM copies the first 4 KB of the boot device into internal RAM, checks the IVT header value `0xD1`, performs a DCD check, and **after a successful DCD extraction** takes the destination pointer and the length from the boot data structure and copies the image to the RAM from which it executes. The first 4 KB must hold the IVT, the DCD and the boot data. The maximum image size for SD and MMC boot is 32 MB, limited by the ADMA buffer descriptors the ROM allocates. | 243 to 244 | Verified by public vendor document (reference manual copy) |
| R4 | The IVT offset is `0x400` for SD, MMC, eSD, eMMC and SDXC, `0x1000` for FlexSPI NOR and SEMC NOR, and `0x400` for SPI NOR, SEMC NAND and FlexSPI NAND. It is the only fixed requirement on the image layout. | 256 (Table 9-36) | Verified by public vendor document (reference manual copy) |
| R5 | **Boot configuration.** The uSDHC port (USDHC-1 or USDHC-2, `0x450[1]`), fast boot (`0x450[0]`), SD power cycle (`0x450[3]`), SD or MMC boot-device selection, bus width (SD 1-bit or 4-bit, MMC 4-bit, 8-bit and 4-bit or 8-bit DDR), voltage and other fields are set by fuses, with GPIO overrides for them when `BT_FUSE_SEL` is 0. The manual names the fast boot bit differently in Table 9-31 (`0x450[0]`) and in the text of Table 9-33 (`BOOT_CFG1[4]`), and the two are not reconciled here. Several bit addresses are garbled in the extraction and are not recorded here. | 241 to 242 (Table 9-31), 200 | Verified by public vendor document (reference manual copy) |
| R6 | **ROM actions on the card.** The flow figure shows the ROM configuring the IOMUX from the bus-width fuse, resetting the uSDHC, setting an identification frequency of about 400 kHz, sending 80 clocks and a card reset CMD0, then for MMC the voltage validation, CMD9, a switch to the wider bus and higher frequency, and the selection of the boot partition by a switch command. The text says the identification frequency is 347.22 kHz and the normal run frequency 20 or 40 MHz. The manual does not state the state in which the card is left when the image starts. | 244 to 247 | Verified by public vendor document (reference manual copy) for the steps. The final card state is Not verified. |
| R7 | **Boot flow and errors.** In Internal Boot the ROM initialises the hardware, loads the image from the chosen device, validates it with the HAB library and jumps to an address from the image. "If an error occurs during the internal boot, the boot code jumps to the Serial Downloader." The text does not list the error classes. | 199 | Verified by public vendor document (reference manual copy) |
| R8 | **Security levels.** Closed: HAB runs, the DCD is processed if present, the image is authenticated before it runs, detected errors are logged, and the boot flow aborts with control passed to the serial downloader. Open: HAB runs the same way, errors are logged and have no influence on the boot flow, and the manual says the image runs even if the authentication data is missing or incorrect. With `BT_FUSE_SEL` 0 and Boot From Fuses mode, the ROM jumps directly to the serial downloader. | 199 to 200 | Verified by public vendor document (reference manual copy) |
| R9 | **Redundant boot for expansion devices.** With `PERSIST_SECONDARY_BOOT` 0 the ROM uses address `0x0` for the primary image. With it 1 the ROM reads a secondary image table at `0x200` on the boot media (tag `0x00112233`, first 512-byte sector number) and uses that address. The primary image must reserve the table. In Closed mode, if primary authentication fails, the ROM sets the bit and resets, and the secondary image is used. The layout figure shows an optional MBR at `0x0`, the table at `0x200` and the program image starting from the IVT at `0x400`. The text does not say how the secondary image relates to the two eMMC boot partitions. | 252 to 254 | Verified by public vendor document (reference manual copy). The relation to eMMC boot partitions is Not verified. |
| R10 | **DCD.** The DCD is configuration data in the image that the ROM interprets, for example to program the SEMC for SDRAM. It is at most 1,768 bytes. The valid DCD address ranges include the SEMC registers (`0x402F0000` to `0x402F3FFF`), CCM, CCM analog and IOMUXC blocks. | 258, 261 (Table 9-42) | Verified by public vendor document (reference manual copy) |
| R11 | **Recovery order.** The chip supports recovery devices on an LPSPI port if the recovery fuse is set. When internal boot and recovery boot (if enabled) fail, the boot goes to SD/MMC manufacture mode, which uses a 1-bit bus whatever the fuse says, before the serial download mode. | 274 (sections 9.10, 9.11) | Verified by public vendor document (reference manual copy) |
| R12 | **Enter Bootloader API.** The ROM bootloader offers `runBootloader(arg)` so that an application can boot a newly updated application safely after in-application programming, or re-enter the serial downloader. `arg` points to a word: tag `0xEB` in bits 31 to 24, boot mode in 23 to 20 (0 determined by the boot mode register or fuses, 1 serial downloader), serial downloader media in 19 to 16 (0 auto, 1 USB, 2 UART) and **boot image selection** in 3 to 0 (0 primary image, 1 redundant image with the primary image bypassed, 2, 3), effective only if the boot mode is 0. The manual's examples are `0xeb100000` (serial downloader over USB) and `0xeb000001` (select the redundant image after a reliable update). | 287 (section 9.13.3) | Verified by public vendor document (reference manual copy) |

## Not retrieved, so Not verified for the RT1060

The reference manual copy (R1 to R12) closed or narrowed X1, X2, X4, X5 and X7 of the first version of this file. What remains:

| # | Item | State | What would settle it |
| --- | --- | --- | --- |
| X1 | Where the first stage sits inside an eMMC **boot partition** and how large the boot partition may be. R1 says which partition the ROM selects. R4 gives the IVT offset `0x400` for the device. The manual text read does not say that the offset counts from the start of the selected boot partition. The Manufacturing Guide programs at offset `0x400` and never mentions a boot partition (V5). | Narrowed. Partition selection closed (R1). Offset within the partition Not verified. | A board, or a statement in another manual section |
| X2 | The exact fuse bit addresses of several boot-configuration fields (the table is garbled in the extraction), the fast boot values, and the eMMC option-block and fuse values used by the Guide's BD files. | Narrowed (R5). Port, device, bus width and fast boot exist as fuses and GPIO overrides. | A clean copy of Table 9-31, the Guide's figures or the Flashloader package |
| X3 | The values in the eMMC BD files. | Open | The figures of the Guide, or the Flashloader release package (a download, not done) |
| X4 | Which error classes of the internal boot send the ROM to the serial downloader, and the exact HAB event content for a blank SRK hash. R7 and R8 give the general behaviour, R11 the order of recovery steps. | Narrowed | Security reference manual, the ROM HAB documents |
| X5 | The state in which the ROM leaves the uSDHC and the card when the image starts (R6 lists the steps but not the end state), and whether anything the ROM sets, such as the selected boot partition, survives until the first stage runs. | Narrowed. DCD order closed (R3). | A board, or another manual section |
| X6 | Whether the current RT1060 Flashloader can program an eMMC through an SD adapter, and which uSDHC instance it uses. A forum snippet and a community fork claim fixed instances and a failure through an adapter. Not used. | Open | Flashloader source in an SDK, or the MCU Bootloader reference manual for the release |
| X7 | Whether a signed image on SD or eMMC uses IVT offset `0x400` or `0x1000`. | Narrowed. The manual gives `0x400` for SD and eMMC (R4). Where the CSF sits relative to it is not read. | CST user guide |
| X8 | Whether the third-party-hosted Rev. 3 copy equals the `nxp.com` document and whether a later revision or an erratum changes R1 to R12. | Open | The `nxp.com` document (needs an NXP account) and the errata sheet |
