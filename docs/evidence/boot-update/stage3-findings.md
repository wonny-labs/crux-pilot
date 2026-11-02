# Stage 3 findings: HAB, XIP encryption, boot media and signing tools (RT1060-EVKB)

Issue: wonny-labs/crux-pilot#141, Stage 3 (desk study). Prepared 1 November 2026. "Plan" means the [pre-M8 investigation plan](../../plans/pre-m8-investigation-plan.md).

Scope: documentation and pinned-source reading only. No tool was downloaded or installed. No licence was accepted. No build was run and no hardware was used.

## Pinned and fixed sources

| Short name | Source | Revision |
| --- | --- | --- |
| `zephyr@684c9e8` | github.com/zephyrproject-rtos/zephyr | 684c9e8f32e4373a21098559f748f06915f950c9 |
| `hal_nxp@2c2f28a` | github.com/zephyrproject-rtos/hal_nxp (pin from Zephyr `west.yml` lines 215 to 216) | 2c2f28ac333e995d7279777409817c4b4f92c1ec |
| `mcuboot@ee39e2d` | github.com/zephyrproject-rtos/mcuboot (pin from Zephyr `west.yml` lines 337 to 338) | ee39e2d694bd827ffd1bebbce2f571a9154e6ec2 |
| `crux@f67e22a` | wonny-labs/crux-pilot `origin/main` | f67e22a5bf49874aab18f2dbde7ac239fec689d8 |
| `spsdk@v3.12.0` | github.com/nxp-mcuxpresso/spsdk, tag v3.12.0 (public vendor source, not a Crux pin) | tag v3.12.0 |
| `mcuxsdk-ex@3151bc0` | github.com/nxp-mcuxpresso/mcuxsdk-examples, branch `main` (public vendor documentation) | 3151bc057b8924db54c7395d39b213c05ab94dc3 |
| `u-boot@v2026.10` | github.com/u-boot/u-boot, tag v2026.10 (i.MX6/7 input only, not RT1060) | tag v2026.10 |

Pinned sources were read with read-only blobless Git fetches into a directory outside the repository and with `raw.githubusercontent.com`. The `gh api` contents endpoint was not available for these repositories in this session. The content is the same object at the same commit.

All web pages were read on 1 November 2026. NXP documents on `www.nxp.com/docs/` could not be retrieved from this environment (proxy refusal or redirect to the home page). They are cited by title only and marked Not verified.

## Labels

- **Verified on pinned source**: checked at the cited file and line of a pinned revision above (Zephyr, `hal_nxp`, MCUboot, or crux-pilot `origin/main`). It shows what the code allows, not board behaviour.
- **Verified by public vendor document**: read from a public vendor page or public vendor repository, with URL and date read. It is the vendor's statement, not a Crux result.
- **Not verified**: not checked, inaccessible, or inferred. The reason is given.

## Findings

### A. HAB and the Zephyr boot header

| # | Claim | Source | Label | Note |
| --- | --- | --- | --- | --- |
| A1 | Zephyr images for RT10xx carry an IVT whose `csf` field is set to `CSF_ADDRESS`. | `zephyr@684c9e8` `soc/nxp/imxrt/imxrt10xx/soc.c` lines 63 to 76 | Verified on pinned source | The IVT is defined by Zephyr. The SDK copy is removed (A4). |
| A2 | `CSF_ADDRESS` is defined as 0. A Zephyr-built image therefore has no CSF, so the boot ROM has nothing to authenticate. | `hal_nxp@2c2f28a` `mcux/mcux-sdk-ng/devices/RT/RT1050/MIMXRT1052/xip/fsl_flexspi_nor_boot.h` line 91 | Verified on pinned source | RT1062 reuses the RT1050 xip directory: `devices/RT/RT1060/MIMXRT1062/xip/CMakeLists.txt` line 1. |
| A3 | `IVT_SIZE` is 0x2000 for the FlexSPI NOR boot header. | Same header, lines 63 to 67 | Verified on pinned source | Matches SPSDK `initial_load_size: 0x2000` for `flexspi_nor` (D4). |
| A4 | Zephyr removes the SDK `fsl_flexspi_nor_boot.c` and uses its own boot header for RT10xx and RT11xx. | `zephyr@684c9e8` `modules/hal_nxp/mcux/mcux-sdk-ng/device/device.cmake` lines 53 to 55 and 133 to 147 | Verified on pinned source | |
| A5 | Boot data `size` is `_flash_used` for XIP images. | `soc/nxp/imxrt/imxrt10xx/soc.c` lines 51 to 61 | Verified on pinned source | A HAB signer must cover the image the ROM loads. How a signer treats the Zephyr IVT is Not verified (see A14). |
| A6 | The NXP boot header is built only when the image is not an MCUboot application. | `soc/nxp/imxrt/Kconfig` lines 24 to 30 (`depends on !(BOOTLOADER_MCUBOOT \|\| SECOND_CORE_MCUX)`) | Verified on pinned source | The MCUboot image keeps the IVT, FCB and DCD. The application has none. Only the bootloader can be a HAB target. |
| A7 | No HAB-related source file exists in the pinned Zephyr, `hal_nxp` or MCUboot trees (file-name search). | Tree listings at the three pins | Verified on pinned source | File-name search only, not a content search. |
| A8 | The pinned RT1062 ROM API driver exposes the bootloader tree and FlexSPI NOR functions. It has no HAB API entry. | `hal_nxp@2c2f28a` `devices/RT/RT1060/MIMXRT1062/drivers/romapi/fsl_romapi.c` lines 42 to 167, `fsl_romapi.h` lines 18 to 21 | Verified on pinned source | Calling HAB `report_event` or `report_status` from Zephyr would need new code and the ROM vector address from the security reference manual. |
| A9 | `west sign` supports only `imgtool`, `picotool`, `rimage` and `silabs_commander`. It has no HAB signer. | `zephyr@684c9e8` `scripts/west_commands/sign.py` line 127 | Verified on pinned source | |
| A10 | HAB authenticates the SRK using the SRK hash (`SRK_HASH`) fuses. Install SRK takes an SRK table and an SRK index. | https://spsdk.readthedocs.io/en/latest/images/hab.html, read 1 Nov 2026 | Verified by public vendor document | Generic HABv4 statement. Not RT1060-specific. |
| A11 | In the i.MX6/7 U-Boot flow, SRK hash programming (section 1.5) comes before HAB event checking (section 1.6). The guide reads events with `hab_status`, which calls the HAB `report_event` and `report_status` API. Closing the device (1.7) programs `SEC_CONFIG[1]`. | `u-boot@v2026.10` `doc/imx/habv4/guides/mx6_mx7_secure_boot.txt` lines 136, 193 to 214, 240 to 247 | Verified by public vendor document (open-source project document) | Input only. It describes i.MX6/7, not RT1060. |
| A12 | On RT1020 the vendor states that production needs HAB closed mode through `SEC_CONFIG[1]` and that the SRK hash "must be programmed to fuses to enable secure boot mode". | https://mcuxpresso.nxp.com/mcuxsdk/25.09.00-pvw1/html/middleware/mcu_bootloader/docs/iMXRT1020_Manufacturing_User_Guide/topics/manufacturing_process_in_production_phase.html, read 1 Nov 2026 | Verified by public vendor document | Input only. Not an RT1060 result. |
| A13 | RT1060 open-configuration behaviour with unprogrammed SRK hash fuses: whether the ROM performs authentication, which events it logs, and whether it continues to boot. | i.MX RT1060 Security Reference Manual (title only), AN12079 "Secure Boot on i.MX RT10xx Series" (title only) | Not verified | Documents not retrievable from this environment. The security reference manual is normally behind an NXP account. Expected, not confirmed: the SRK table check against blank fuses cannot succeed, so open configuration can at best show that the CSF was parsed, not that authentication passed. |
| A14 | Whether SPSDK or SEC accept a Zephyr image that already contains an FCB and IVT, or need a raw image and generate the IVT themselves. | SEC user guide section "Preparing source image for RT10xx/RT115x/RT116x/RT117x devices" (body not retrievable) | Not verified | Stage 5 or a later approved check. |
| A15 | `sdphost` provides `error-status` ("Reads the error code from the device") over the serial downloader protocol. The page does not link it to HAB events. | https://spsdk.readthedocs.io/en/latest/apps/sdphost.html, read 1 Nov 2026 | Verified by public vendor document | Whether `error-status` returns HAB status on RT1060 is Not verified. Any use needs a board and is out of scope. |
| A16 | Vendor support answer on RT1020: the Secure Provisioning Tool "does not support writing to shadow registers for RT devices". | https://community.nxp.com/t5/Other-NXP-Products/HAB-testing/m-p/1876736, read 1 Nov 2026 | Verified by public vendor document | Input only (RT1020, forum). It removes shadow-register SRK simulation through SEC as an assumed path. |

### B. Signing tools

| # | Claim | Source | Label | Note |
| --- | --- | --- | --- | --- |
| B1 | `scripts/requirements-extras.txt` with `spsdk >= 3.0.0` is a Zephyr file, not a crux-pilot file. It is at lines 21 to 22 with the comment "used by NXP platform to generate/flash firmware images". | `zephyr@684c9e8` `scripts/requirements-extras.txt` lines 21 to 22 | Verified on pinned source | The plan's Stage 3 input row should say "Zephyr `scripts/requirements-extras.txt`". |
| B2 | crux-pilot `origin/main` has no `scripts/requirements-extras.txt`. No file in the tree mentions `spsdk`. | `git show origin/main:scripts/requirements-extras.txt` fails. `git grep -i spsdk origin/main` has no result. | Verified on pinned source | `crux@f67e22a`. |
| B3 | The Crux bootstrap installs Zephyr `requirements-base.txt`, `requirements-build-test.txt` and `requirements-run-test.txt`, not `requirements-extras.txt`. SPSDK is therefore not installed by the documented setup. | `crux@f67e22a` `scripts/bootstrap-zephyr.sh` lines 24 to 28 | Verified on pinned source | |
| B4 | The Zephyr `spsdk` runner is a flashing runner ("Runner for flashing with SPSDK"), not a signer. | `zephyr@684c9e8` `scripts/west_commands/runners/spsdk.py` lines 5 and 15 to 16 | Verified on pinned source | |
| B5 | SPSDK latest release is 3.12.0 (uploaded 13 Oct 2026). Licence BSD-3-Clause. `requires_python >=3.10`, classifiers 3.10 to 3.14. It has 34 non-optional dependency entries, including `cryptography>=48.0.1,<50`, `libusbsio`, `libuuu`, `spsdk-mcu-link`, `spsdk-pyocd`. | https://pypi.org/pypi/spsdk/json, read 1 Nov 2026. `spsdk@v3.12.0` `LICENSE` | Verified by public vendor document | Licences of binary dependencies (`libusbsio`, `libuuu`, `spsdk-mcu-link`) are Not verified. Part of A2. |
| B6 | SPSDK `nxpimage hab` is described as "an actual successor of legacy elftosb and CST tools". It exports HAB images from YAML (`nxpimage hab export -c <yaml> -o <bin>`, `get-template`). It supports plain, signed and encrypted images, DCD and XMCD input, and a signer as a key file or a signature-provider string. | https://spsdk.readthedocs.io/en/latest/images/hab.html, read 1 Nov 2026 | Verified by public vendor document | A scripted CLI path without a GUI exists for image generation. Key type support for RT1060 HAB (RSA or ECC) is Not verified. |
| B7 | SPSDK device data for `mimxrt1060` is an alias of `mimxrt1064`, which is an alias of `mimxrt1024`. `mimxrt1064` names `spsdk_predecessor_name: rt106x`. | `spsdk@v3.12.0` `spsdk/data/devices/mimxrt1060/database.yaml` line 5, `mimxrt1064/database.yaml` lines 5 and 9 | Verified by public vendor document | The exact alias inheritance rules are inferred from the files. Not verified in SPSDK code. |
| B8 | The inherited `mimxrt1024` data lists features `cert_block` (`rot_type: srk_table_hab`), `hab`, `fcb`, `bee` and `bootable_image`. | `spsdk@v3.12.0` `spsdk/data/devices/mimxrt1024/database.yaml` lines 83 to 146 | Verified by public vendor document | Tool support, not a ROM specification. |
| B9 | SPSDK release notes: RT106x support since 1.0.0, BEE support in 1.8.0, encrypted HAB in 1.10.1, an HSM signature provider example for i.MX RT10xx HAB devices in 3.9.0. | `spsdk@v3.12.0` `docs/release_notes.rst` lines 1777, 1352, 1223, 194 | Verified by public vendor document | |
| B10 | SEC latest version is 26.09 (September 2026). It "provides a unified GUI front-end over SPSDK command-line tools" and integrates SPSDK v3.12.0. Hosts: Windows 11, macOS 26, Ubuntu 22.04, 24.04 and 26.04. RT106x is supported. BEE boot for RT10xx with OTPMK and SW-GP2/GP4 key sources was added in 2.0. CST was removed from legacy tools in 8.0 ("fully replaced by spsdk tools"). | https://docs.mcuxpresso.nxp.com/secure/latest/release_notes.html, read 1 Nov 2026 | Verified by public vendor document | |
| B11 | SEC has a CLI (`securep`) that can generate build and write scripts without running them. The documented build example assumes the GUI already prepared the workspace. Some commands open the GUI. The page says nothing on deterministic output. | https://docs.mcuxpresso.nxp.com/secure/latest/08_command_line_operations.html, read 1 Nov 2026 | Verified by public vendor document | SEC is not a reproducibility path. Fully headless use is Not verified. |
| B12 | SEC downloads are marked "Account Required". The licence name is not shown on the product page. | https://www.nxp.com/design/design-center/software/development-software/mcuxpresso-software-and-tools-/mcuxpresso-secure-provisioning-tool:MCUXPRESSO-SECURE-PROVISIONING, read 1 Nov 2026 | Verified by public vendor document | Licence terms Not verified. |
| B13 | NXP distributes CST under a click-through "SOFTWARE LICENSE AGREEMENT" for the "i.MX CODE SIGNING TOOL", labelled "LA_OPT36 v2 August 2018". Download needs acceptance. | https://www.nxp.com/webapp/sps/download/license.jsp?colCode=IMX_CST_TOOL, read 1 Nov 2026 | Verified by public vendor document | Licence text not reviewed. Whether an account is needed is Not verified. |
| B14 | Debian packages `imx-code-signing-tool` 4.0.1+dfsg-3 in archive area `main`. | https://tracker.debian.org/pkg/imx-code-signing-tool, read 1 Nov 2026 | Verified by public vendor document (distribution record) | `main` implies a DFSG-free licence, but the exact licence was not read. Not verified. |
| B15 | MCUboot `imgtool` on PyPI is 2.5.0 (22 Oct 2026), Apache-2.0, Python >=3.8. The pinned MCUboot carries `imgtool_version = "2.4.0"`. | https://pypi.org/pypi/imgtool/json, read 1 Nov 2026. `mcuboot@ee39e2d` `scripts/imgtool/__init__.py` line 17 | Verified by public vendor document and Verified on pinned source | Use the pinned in-tree `imgtool` for the trial (A1), not PyPI. |
| B16 | Pinned MCUboot `scripts/requirements.txt` has seven entries: `cryptography>=40.0.0`, `intelhex`, `click`, `cbor2`, `setuptools`, `pyyaml`, `pytest`. | `mcuboot@ee39e2d` `scripts/requirements.txt` lines 1 to 7 | Verified on pinned source | The plan section 3.2 lists six. `pytest` is test-only. |
| B17 | The Crux Dev Container is based on Ubuntu 24.04. | `crux@f67e22a` `.devcontainer/Dockerfile` line 1 | Verified on pinned source | The Python version inside it (expected 3.12) was not checked. Not verified. |

### C. BEE (XIP encryption)

| # | Claim | Source | Label | Note |
| --- | --- | --- | --- | --- |
| C1 | `hal_nxp` has a BEE driver and an RT1060 BEE peripheral header. | `hal_nxp@2c2f28a` `mcux/mcux-sdk-ng/drivers/bee/fsl_bee.h`, `devices/RT/RT1060/periph/PERI_BEE.h` | Verified on pinned source | |
| C2 | The BEE driver states that the `BEE_KEYx_SEL` eFuse "must be set accordingly" for a key loaded in BEE registers to be used. "Otherwise, key cannot loaded and BEE will use key from OTPMK or SW_GP2." | `hal_nxp@2c2f28a` `fsl_bee.h` lines 199 to 212 (note at 204 to 205) | Verified on pinned source | This conflicts in wording with C4, which says the software key is the default `BEE_KEYn_SEL` value. Resolution needs the RT1060 security reference manual. Not verified. |
| C3 | Zephyr does not wire the BEE driver (no BEE entry in the driver list). MCUboot and the imxrt SoC Kconfig have no BEE reference. | `zephyr@684c9e8` `modules/hal_nxp/mcux/mcux-sdk-ng/drivers/drivers.cmake` (search), plan Appendix A | Verified on pinned source | Correction added when the record was assembled: for MCUboot this rests on plan Appendix A and the Stage 1 re-check, which verified only the listed files ([stage1-pinned-source-findings.md](stage1-pinned-source-findings.md) A8). Whole-tree absence in MCUboot is Not verified. |
| C4 | BEE is specific to RT10xx (except RT1010). It has two regions with two AES keys. The EKIB is decrypted with a key selected by `BEE_KEYn_SEL`: software key (default, "evaluating BEE without fusing the device"), SW-GP2 (user-fused, limited by errata), or OTPMK (NXP-provisioned, unique per device, recommended). | `mcuxsdk-ex@3151bc0` `ota_examples/_doc/encrypted_xip_bee.md` lines 7 and 11 to 25 | Verified by public vendor document | MCUXpresso SDK, not Zephyr. |
| C5 | The PRDB region has 4 KiB-aligned start and end addresses. AES-CTR uses a nonce for the upper 96 bits of the counter, with the "address field supplied by HW". Ciphertext is therefore bound to the address. | Same file, lines 80 to 84 | Verified by public vendor document | |
| C6 | The ROM configures on-the-fly decryption from structures in the bootable image header (the bootloader). For updates, NXP moves application configuration out of the header and lets the bootloader configure the engine. | `mcuxsdk-ex@3151bc0` `ota_examples/_doc/encrypted_xip.md` lines 39 to 49 | Verified by public vendor document | |
| C7 | NXP's MCUboot extension supports BEE only with a modified OVERWRITE_ONLY mode. MCUboot re-encrypts the image into the execution slot. Revert is not possible. The re-encryption is in "customized MCUboot code and in MCUboot hooks". | Same file, lines 92, 109 and 169 | Verified by public vendor document | Not upstream Zephyr MCUboot. Swap modes are not supported with BEE in this extension. |
| C8 | MIMXRT1060-EVKB is a listed BEE board for the SDK OTA examples. Its encrypted XIP build uses modified `MCUBOOT_OVERWRITE_ONLY`, ECDH-P256 for the image AES key, a 4 KiB metadata sector at 0x60440000, and BEE on the primary slot. | `mcuxsdk-ex@3151bc0` `encrypted_xip.md` line 182, `_boards/evkbmimxrt1060/ota_examples/mcuboot_opensource/example_board_readme.md` lines 41 to 57 | Verified by public vendor document | MCUXpresso SDK layout. Not a Crux layout proposal. |
| C9 | Whether the RT1060 ROM needs a fuse (for example an encrypted XIP boot fuse) before it applies BEE to the bootloader region. | AN12852 and AN12800 and AN12901 (titles only), RT1060 security reference manual | Not verified | Documents not retrievable. |
| C10 | Sysbuild allows MCUboot image encryption with RAM load. It excludes it for direct-XIP and firmware updater modes. | `zephyr@684c9e8` `share/sysbuild/images/bootloader/Kconfig` lines 106 to 115 and 206 to 209 | Verified on pinned source | Allowed by Kconfig only. Not verified by build. |

### D. Boot media, DCD and SDRAM

| # | Claim | Source | Label | Note |
| --- | --- | --- | --- | --- |
| D1 | Zephyr's imxrt boot-device choice offers FlexSPI NOR, FlexSPI NAND, SEMC NOR, SEMC NAND and XSPI NOR. It has no SD or eMMC option. The IVT offset defaults to 0x1000 for NOR and 0x400 for NAND. | `zephyr@684c9e8` `soc/nxp/imxrt/Kconfig` lines 36 to 65 and 94 to 101 | Verified on pinned source | An SD or eMMC boot image for the bootloader is not produced by the pinned Zephyr build. |
| D2 | The only ROM-loader alternative in Zephyr is FlexSPI-to-RAM loading. | `soc/nxp/common/Kconfig.rom_loader` lines 4 to 11 | Verified on pinned source | |
| D3 | The EVK board doc lists a TF socket for SD card and uSDHC1 pins. SW7 `OFF-OFF-ON-OFF` boots QSPI. `ON-OFF-ON-OFF` prevents QSPI boot. No SD boot setting is given. | `zephyr@684c9e8` `boards/nxp/mimxrt1060_evk/doc/index.rst` lines 23 to 26, 233 to 243, 482 and 489 | Verified on pinned source | |
| D4 | SPSDK data for the RT1024-derived RT1060 entry lists HAB memory types `serial_downloader`, `flexspi_nor` (0x2000), `flexspi_nand`, `semc_nand`, `sd` and `mmc` (0x1000). Bootable image layouts exist for `sd`, `mmc` and `emmc` with `hab_container` at 0x400. FlexSPI NOR has FCB at 0, BEE headers at 0x400 and 0x800, HAB container at 0x1000. | `spsdk@v3.12.0` `spsdk/data/devices/mimxrt1024/database.yaml` lines 88 to 146 | Verified by public vendor document | Indicates that the vendor tool expects ROM boot from SD and eMMC on this family. The RT1060 ROM behaviour is Not verified. |
| D5 | SD boot on MIMXRT1060-EVKB without fuse writes, using SW7 and boot GPIOs. | MIMXRT1060-EVKB Board User Manual (title only), IMXRT1060RM "System Boot" chapter (title only), AN12107 (title only, RT1050) | Not verified | Documents not retrievable. The SEGGER and NXP getting-started pages give only the QSPI setting. |
| D6 | The EVK board sets `DEVICE_CONFIGURATION_DATA` and `NXP_IMX_EXTERNAL_SDRAM` to `y` by default. The board CMake adds `dcd/dcd.c` when DCD is enabled and warns if RAM is SDRAM without DCD. | `zephyr@684c9e8` `boards/nxp/mimxrt1060_evk/Kconfig.defconfig` lines 8 to 12, `CMakeLists.txt` lines 43 to 52 | Verified on pinned source | |
| D7 | The DCD is a ROM command table in section `.boot_hdr.dcd_data` that programs clocks and the SEMC (for example `SEMC_MCR`, `SEMC_BR0`). It is linked after the IVT. | `boards/nxp/mimxrt1060_evk/dcd/dcd.c` lines 10 to 31 and 469 to 485. `soc/nxp/imxrt/boot_header.ld` lines 31 to 38 | Verified on pinned source | |
| D8 | The EVK chooses `zephyr,sram = &sdram0` (32 MiB at 0x80000000). | `boards/nxp/mimxrt1060_evk/mimxrt1060_evk.dtsi` lines 28 to 29 and 38 to 42 | Verified on pinned source | Default Zephyr RAM on this board is SDRAM. |
| D9 | Historical Crux EVKB build configurations have `CONFIG_DEVICE_CONFIGURATION_DATA=y` and `CONFIG_NXP_IMX_EXTERNAL_SDRAM=y`. | `crux@f67e22a` `docs/evidence/m5-system/selected.config` lines 79 to 80 | Verified on pinned source | Historical evidence only. Not a current build result. |
| D10 | Pinned MCUboot's Zephyr port does not change DCD or SDRAM settings (no reference in `boot/zephyr/Kconfig`, `prj.conf`, `CMakeLists.txt`, `main.c`). | `mcuboot@ee39e2d` search of those four files | Verified on pinned source | So the MCUboot image inherits the board default: boot header with DCD (A6, D6). The ROM runs the DCD before MCUboot, and SDRAM is ready for the application. Not verified by build. |
| D11 | Pinned MCUboot has a RAM-load overlay for RT1050 hyperflash only. None exists for RT1060. RT1060 QSPI is a sample target. | `mcuboot@ee39e2d` `boot/zephyr/boards/mimxrt1050_evk_mimxrt1052_hyperflash_ram_load.overlay` lines 1 to 35, `boot/zephyr/sample.yaml` line 26 | Verified on pinned source | RAM load on RT1060 needs a Crux overlay. The RT1050 overlay places retained boot info in SDRAM at 0x80007f00. |

### E. Closed configuration and board availability

| # | Claim | Source | Label | Note |
| --- | --- | --- | --- | --- |
| E1 | Closing HAB in the i.MX6/7 reference flow needs a signed image that boots without events after SRK hash programming, then `SEC_CONFIG[1]`. After that "the chip does not load an image that has not been signed using the correct PKI tree". | `u-boot@v2026.10` `doc/imx/habv4/guides/mx6_mx7_secure_boot.txt` lines 240 to 247 | Verified by public vendor document (open-source project document) | Input only. RT1060 procedure Not verified. |
| E2 | NXP lists the i.MX RT1060 Evaluation Kit (MIMXRT1060-EVKB) as "No Longer Manufactured". | SEC product page (B12 URL), read 1 Nov 2026 | Verified by public vendor document | Relevant to sourcing a dedicated board for closed-configuration work. |
| E3 | RT1060 fuse map, fuse irreversibility statements, and lock behaviour. | IMXRT1060RM fuse map chapter, RT1060 security reference manual (titles only) | Not verified | eFuses are one-time programmable by nature, but the RT1060 statement was not read. |

## Documents cited by title only (Not verified)

- i.MX RT1060 Processor Reference Manual (IMXRT1060RM). Not retrievable from this environment.
- i.MX RT1060 Security Reference Manual. Normally behind an NXP account.
- AN12079 "Secure Boot on i.MX RT10xx Series".
- AN12852, AN12800, AN12901 (BEE application notes named in `encrypted_xip_bee.md` line 29).
- AN4581 "i.MX Secure Boot on HABv4 Supported Devices" (named by the U-Boot guide).
- AN12107 (SD card and Octal flash boot, RT1050).
- MIMXRT1060-EVKB Board User Manual.
- MCUXpresso Secure Provisioning Tool User Guide, RT10xx workflow chapter (page body not retrievable).
