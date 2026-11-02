# Stage 5: trial bootloader and signed image cross-builds (RT1060-EVKB)

| Field | Value |
| --- | --- |
| Date | Builds run on 2 November 2026, 00:07 to 00:40 UTC (results `environment.txt`). Recorded on 2 November 2026. |
| Approval | A1, given on 1 November 2026 for two RT1060 trial builds: rank 1 (RAM load with revert) and rank 2 (swap using offset) |
| Evidence class | **Cross-compilation only (build-only).** This is not Host test, `native_sim`, CI or physical-board evidence. No image was programmed, booted or run. No probe, Flash, reset or fuse operation took place. |
| Label | **Verified by build** for statements read directly from the trial outputs. Other statements are Inference or Not verified, with the reason. |
| Provenance | Results archive `d8f53cd7-results.tar.gz`, SHA-256 `8e2e9839eae863b79a42a7a294a97e39f093a83c3f48d0598f9fb582f01cd3ed`. The archive is not committed. Small inputs and text outputs are copied to [trial-inputs/](trial-inputs/). |
| Source column | Paths are relative to the results directory of the archive. `A/` means `build-a-ramload-revert/` and `B/` means `build-b-swap-offset/`. |

## 1. Inputs and provenance

| Item | Value | Source |
| --- | --- | --- |
| crux-pilot tree used for the trial | `1d6ac4f6cd5b4e4013f2ce0c59375beb3cdad9f2`. Git status was clean at the start and at the end. | `environment.txt` |
| Relation to the PR base | The trial tree is **not** the PR #151 base `f57ff22`. `1d6ac4f` is an older ancestor of `f57ff22` (checked with `git merge-base --is-ancestor`). `git diff --stat 1d6ac4f f57ff22 -- samples/controller` lists 15 changed files (278 insertions, 72 deletions). The changes are in C sources and the sample README. `prj.conf`, `CMakeLists.txt`, `Kconfig` and `boards/` of `samples/controller/zephyr` are unchanged. The Crux module files `zephyr/Kconfig`, `zephyr/CMakeLists.txt` and `cmake/CruxConsumerBoundary.cmake` also differ. The trial `west.yml` allowlist has `cmsis`, `cmsis_6` and `hal_nxp` only, while `f57ff22` adds `littlefs` and `fatfs`. | Local `git` commands in this worktree, `west.yml.copy` |
| Consequence | The numbers below describe the controller sample at `1d6ac4f`. Whether they hold at `f57ff22` is **Not verified**: no build was run at `f57ff22`. | Inference |
| Zephyr | `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0) | `environment.txt` |
| MCUboot | `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2` (`v2.4.0-rc1-3-gee39e2d6`), equal to the pin in the Zephyr `west.yml`. Clone at `/opt/crux-cache/trial/mcuboot`, detached, working tree clean. | `environment.txt` |
| Board target | `mimxrt1060_evk@B//qspi`. The build resolves it to `mimxrt1060_evk/mimxrt1062/qspi`. | Command files, `baseline/build.log` |
| Application | `samples/controller/zephyr`, the controller sample that `scripts/test-controller.sh` builds for the EVKB. Zephyr `hello_world` was not needed. | `README.md` |
| Module route | Plan section 3.3 route 1: `-DEXTRA_ZEPHYR_MODULES=/opt/crux-cache/trial/mcuboot`, together with `-DZEPHYR_EXTRA_MODULES=<crux-pilot>` as the repository scripts do. Configure-only check exit 0. No manifest change. Route 2 (scratch workspace) was not needed and not run. | `route1-command.txt`, `environment.txt` |
| Environment | Container `crux-141-boot-trial` created from the repository Dev Container image (Ubuntu 24.04, amd64, `--memory=4g --cpus=2`), with the documented mounts `/workspaces/crux` and volume `crux-host-cache` at `/opt/crux-cache`. Zephyr SDK 1.0.1, `arm-zephyr-eabi-gcc` 14.3.0, GNU readelf 2.43.1, Python 3.12.3, west 1.4.0, CMake 3.28.4, Ninja 1.11.1. | `environment.txt` |
| Python packages | Separate virtual environment `/opt/crux-cache/venv-boot-trial`, 52 entries (for example `cryptography==50.0.2`, `intelhex==2.3.0`, `click==8.5.0`, `cbor2==6.2.0.post1`, `PyYAML==6.0.3`, `west==1.4.0`) | `venv-packages.txt` |
| imgtool | `scripts/imgtool.py` from the pinned MCUboot clone, version 2.4.0, as sysbuild calls it | `environment.txt` |
| Signing key | Development ECDSA P-256 key generated outside the repository at the container path `/opt/crux-cache/keys/dev-ecdsa-p256.pem` (directory mode 700, file mode 600). Only the public key is in the results. Public key DER (SPKI) SHA-256 `0aafb408cfe1de0f6cdb2e1f7f357a6bb171b6e53ea5b856321c2a8dcb7ea368`. See [Stage 8](stage8-reproducibility-and-signing.md). | `pubkey-sha256.txt`, `README.md` |

## 2. Commands

The copies in [trial-inputs/](trial-inputs/) mark the key path in the same way
as the imgtool argument files of the results
(`<DEV-KEY-PATH:/opt/crux-cache/keys/dev-ecdsa-p256.pem>`). They are otherwise
unchanged. All paths are container paths.

Build A, RAM load with revert ([build-a-command.txt](trial-inputs/build-a-command.txt), source `A/command-build-a-ramload-revert-build.txt`):

```text
west build --sysbuild -p always -b mimxrt1060_evk@B//qspi -d /opt/crux-cache/trial/build-a-ramload-revert-build /workspaces/crux/crux-pilot/samples/controller/zephyr -- -DZEPHYR_EXTRA_MODULES=/workspaces/crux/crux-pilot -DEXTRA_ZEPHYR_MODULES=/opt/crux-cache/trial/mcuboot -DSB_CONFIG_BOOTLOADER_MCUBOOT=y -DSB_CONFIG_MCUBOOT_MODE_RAM_LOAD_WITH_REVERT=y -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"<DEV-KEY-PATH:/opt/crux-cache/keys/dev-ecdsa-p256.pem>\" -Dmcuboot_EXTRA_CONF_FILE=/opt/crux-cache/trial/build-a-files/mcuboot.conf -Dmcuboot_EXTRA_DTC_OVERLAY_FILE=/opt/crux-cache/trial/build-a-files/mcuboot.overlay -Dzephyr_EXTRA_CONF_FILE=/opt/crux-cache/trial/build-a-files/app.conf -Dzephyr_EXTRA_DTC_OVERLAY_FILE=/opt/crux-cache/trial/build-a-files/app.overlay
west build exit: 0
```

Build B, swap using offset ([build-b-command.txt](trial-inputs/build-b-command.txt), source `B/command-build-b-swap-offset-build.txt`):

```text
west build --sysbuild -p always -b mimxrt1060_evk@B//qspi -d /opt/crux-cache/trial/build-b-swap-offset-build /workspaces/crux/crux-pilot/samples/controller/zephyr -- -DZEPHYR_EXTRA_MODULES=/workspaces/crux/crux-pilot -DEXTRA_ZEPHYR_MODULES=/opt/crux-cache/trial/mcuboot -DSB_CONFIG_BOOTLOADER_MCUBOOT=y -DSB_CONFIG_MCUBOOT_MODE_SWAP_USING_OFFSET=y -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"<DEV-KEY-PATH:/opt/crux-cache/keys/dev-ecdsa-p256.pem>\"
west build exit: 0
```

| Other command | Content | Source |
| --- | --- | --- |
| Stage 4 baseline rebuild | `west build -p always -b mimxrt1060_evk@B//qspi -d /opt/crux-cache/trial/baseline-build /workspaces/crux/crux-pilot/samples/controller/zephyr -- -DZEPHYR_EXTRA_MODULES=/workspaces/crux/crux-pilot`, exit 0. The same command as the EVKB step of `scripts/test-controller.sh`, with the build directory moved out of the repository. | `baseline/command.txt` |
| Reproducibility rebuilds | Identical to the Build A and Build B commands except for the build directory (`repro-a-build`, `repro-b-build`). Checked by text comparison with the directory name masked. Both exit 0. | `A/command-repro-a-build.txt`, `B/command-repro-b-build.txt` |
| Route 1 check | `west build --sysbuild --cmake-only` with the Build A sysbuild options, exit 0 | `route1-command.txt` |

## 3. Plan Stage 5 verification

| Plan check | Build A | Build B | Source |
| --- | --- | --- | --- |
| MCUboot `.config` has the expected mode | `CONFIG_BOOT_RAM_LOAD=y`, `CONFIG_BOOT_RAM_LOAD_REVERT=y` | `CONFIG_BOOT_SWAP_USING_OFFSET=y`, `# CONFIG_BOOT_RAM_LOAD is not set` | `A/checks.txt`, `B/checks.txt` section 1 |
| `CONFIG_BOOT_VALIDATE_SLOT0=y` | Yes | Yes | Same |
| Tinycrypt backend | `CONFIG_BOOT_USE_TINYCRYPT=y`, `CONFIG_BOOT_ECDSA_TINYCRYPT=y`, `CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y` | Same | Same |
| Application `CONFIG_BOOTLOADER_MCUBOOT=y` | Yes, with `CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD_WITH_REVERT=y` | Yes, with `CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET=y` | `checks.txt` section 2 |
| Application has no NXP boot header | `NXP_IMXRT_BOOT_HEADER` occurs 0 times in the application `.config` | Same | `checks.txt` section 2 |
| Bootloader image has the boot header at `0x60000000` | `FCFB` tag at binary offset 0. IVT bytes `d1 00 20 41` at offset `0x1000`. Symbols `image_vector_table` `0x60001000`, `boot_data` `0x60001020`, `dcd_data` `0x60001030`. Vectors at `0x60002000`. | Same addresses | `checks.txt` section 3, `A/mcuboot-boot-header.txt`, `B/mcuboot-boot-header.txt`, `*/mcuboot/nm-n.txt` |

Labelling error in the helper output: `mcuboot-boot-header.txt` has a line
"FlexSPI config block @0x400 (expect 'FCFB' tag)" that shows zeros. That label
is wrong for RT1060. On RT1060 the FCB starts at offset 0
(`CONFIG_FLEXSPI_CONFIG_BLOCK_OFFSET` is `0x0` in `*/mcuboot/nm-n.txt`), and
the `FCFB` tag is present there. The zeros at `0x400` are not a missing FCB.
The same helper expected IVT bytes `d1 00 20 40`. The observed bytes are
`d1 00 20 41`, and the standalone baseline image has the same bytes at
`0x1000` (author check of `baseline/zephyr.bin`). The meaning of the last byte
was not analysed.

Stop conditions of plan Stage 5: none was triggered. No upstream source patch
was needed (the MCUboot clone stayed clean, `environment.txt`). No Zephyr crypto
module was needed (tinycrypt). The key stayed outside the repository. The
container did not run out of memory or disk (end-of-run snapshot in
`environment.txt`).

## 4. Builds side by side

| Item | Baseline (no MCUboot) | Build A: RAM load with revert | Build B: swap using offset | Source |
| --- | --- | --- | --- | --- |
| MCUboot Flash use against the 128 KiB `mcuboot` partition | n/a | 51,760 B of 131,072 B (39.49 %), headroom 79,312 B | 53,940 B of 131,072 B (41.15 %), headroom 77,132 B | `checks.txt` section 4 |
| MCUboot RAM | n/a | 14,656 B in OCRAM2, `0x20200000` to `0x20203940` | 27,904 B in SDRAM from `0x80000000` | `checks.txt` section 4, `A/mcuboot/nm-key-symbols.txt`, `B/mcuboot/readelf-l.txt` |
| MCUboot ITCM | n/a | 8,544 B | 8,544 B | `checks.txt` section 4 |
| Application Flash | 37,536 B at `0x60000000` | 0 B (FLASH region of 3,145,530 B unused) | 30,688 B from `0x60020000` (FLASH region 3,141,482 B) | `baseline/linker-usage.txt`, `checks.txt` section 4 |
| Application RAM | 8,704 B in SDRAM | 41,536 B in SDRAM, `0x80000000` to `0x8000a240`. Code, data and stack are all in this range. | 8,704 B in SDRAM | Same, `A/app/nm-key-symbols.txt` |
| Application link and execution | XIP from `0x60000000`, vectors `0x60002000` | Linked and loaded at `0x80000000`, vectors `0x80000400` | XIP from `0x60020000`, vectors `0x60020400` | `baseline/nm-n.txt`, `A/app/nm-key-symbols.txt`, `B/app-flexspi-window-report.txt` |
| `CONFIG_ROM_START_OFFSET` (image header size) | `0x2000` (NXP boot header) | `0x400` | `0x400` | `*/dot-config.txt` |
| Unsigned application binary | 37,536 B | 32,880 B (includes the `0x400` header space) | 30,688 B | File sizes in the results |
| Signed application image | n/a | 33,032 B. Header `load_addr` `0x80000000`, flags `RAM_LOAD (0x20)`, `img_size` `0x7c70`, TLV area `0x98` B | 30,838 B. `load_addr` `0x0`, flags `0x0`, `img_size` `0x73e0`, TLV area `0x96` B | `*/imgtool-dumpinfo-app.txt` |

Note on the application FLASH region: its size is the 3 MiB slot minus the
MCUboot image footer estimate (3,145,728 − 198 = 3,145,530 B for Build A and
3,145,728 − 4,246 = 3,141,482 B for Build B). The arithmetic matches exactly. It
shows that sysbuild limits the application link to the slot less that
estimate (Inference from the numbers, the CMake code was not re-read).

### 4.1 Baseline against Build B application size

Build B is 6,848 B smaller than the baseline (37,536 − 30,688). The section
tables explain the difference (author comparison of `baseline/readelf-S.txt`
and `B/app/readelf-S.txt`):

| Difference | Bytes | Explanation | Label |
| --- | --- | --- | --- |
| `rom_start` section | −7,168 (`0x22c0` to `0x6c0`) | The baseline reserves `0x2000` for the NXP boot header (FCB, IVT, boot data, DCD, padding). Build B reserves `0x400` for the MCUboot image header. The vector table (`0x2c0`) is in both. The baseline has the symbols `qspi_flash_config`, `image_vector_table`, `boot_data` and `dcd_data`. Build B has none of them. | Verified by build |
| `text` section | +320 (`0x4a90` to `0x4bd0`) | Build B adds `z_arm_clear_arm_mpu_config` (36 B) and `z_arm_init_arch_hw_at_boot` (272 B), from `CONFIG_INIT_ARCH_HW_AT_BOOT=y`, which Build B sets and the baseline does not. Together 308 B. The remaining 12 B are **Not verified** (alignment is likely). | Symbols and sizes Verified by build (from `B/app/nm-n.txt` addresses). The last 12 B Not verified. |
| All other allocated sections | 0 | Same sizes (`initlevel`, `device_area`, `sw_isr_table`, driver API areas, `rodata`, RAM sections) | Verified by build |

## 5. Build A: the two known problems and their configuration-only solutions

Both problems were predicted by the desk record (B53 to B56 and B64). Both
were solved with Kconfig fragments and devicetree overlays only. No source
patch was used.

| Problem | Solution | Result | Source |
| --- | --- | --- | --- |
| 1. SDRAM clash. MCUboot `zephyr,sram` and the executable RAM window both default to SDRAM at `0x80000000` (32 MiB), so loading the application would overwrite the bootloader's live RAM. | MCUboot overlay: `zephyr,sram = &ocram2`, with OCRAM2 trimmed to `0x20200000` size `0x7ff00`. MCUboot Kconfig: `CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_START=0x80000000` and `CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_SIZE=33554432` set explicitly, because they default from `zephyr,sram`. | MCUboot RAM `0x20200000` to `0x20203940`. Application from `0x80000000` to `0x8000a240`. No overlap. The MCUboot NXP boot header still carries the DCD (`dcd_data` at `0x60001030`), which is the only SDRAM initialisation path. | `A/checks.txt` section 1, `A/mcuboot/dts-key-nodes.txt`, `A/*/nm-key-symbols.txt`, `A/mcuboot-boot-header.txt` |
| 2. Confirm path. The application finds its running slot through MCUboot shared data, which needs a retained memory region and a `zephyr,bootloader-info` chosen node that the board does not have. | Both images: a `zephyr,memory-region` node `RetainedMem` at `0x2027ff00`, size `0x100` (top of OCRAM2), with a `zephyr,retention` child, chosen as `zephyr,bootloader-info`. OCRAM2 trimmed by 256 B in both images, so no image RAM covers it. MCUboot: `BOOT_SHARE_DATA`, `BOOT_SHARE_DATA_BOOTINFO`, `BOOT_SHARE_BACKEND_RETENTION`. Application: `RETENTION_BOOTLOADER_INFO`, `RETENTION_BOOTLOADER_INFO_TYPE_MCUBOOT`. | Both images configure and link. MCUboot links `boot_save_shared_data` and `retention_write`. The application links `blinfo_init` and `retention_read`. | `A/checks.txt` sections 1 and 2, `A/*/dts-key-nodes.txt`, `A/*/nm-n.txt` |

The overlays and fragments are in [trial-inputs/](trial-inputs/):
[build-a-mcuboot.conf](trial-inputs/build-a-mcuboot.conf),
[build-a-mcuboot.overlay](trial-inputs/build-a-mcuboot.overlay),
[build-a-app.conf](trial-inputs/build-a-app.conf) and
[build-a-app.overlay](trial-inputs/build-a-app.overlay). The results also hold
`retained-ocram2.overlay.inc`, which is byte-identical to `app.overlay` and to
the first part of `mcuboot.overlay`. It is not copied again. The MCUboot
overlay adds only this to the shared part:

```dts
/ {
	chosen {
		zephyr,sram = &ocram2;
	};
};
```

The MCUboot Kconfig fragment:

```text
CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_START=0x80000000
CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_SIZE=33554432
CONFIG_RETAINED_MEM=y
CONFIG_RETENTION=y
CONFIG_BOOT_SHARE_DATA=y
CONFIG_BOOT_SHARE_DATA_BOOTINFO=y
CONFIG_BOOT_SHARE_BACKEND_RETENTION=y
```

**What the confirm path result does not show.** The controller sample never
calls `boot_fetch_active_slot()`, `boot_request_upgrade()` or
`boot_write_img_confirmed()`. The linker therefore dropped `blinfo_lookup`, and
only `retention_read` remains in the application ELF (`README.md`, and no such
symbol in `A/app/nm-n.txt`). The application `.config` also has
`# CONFIG_FLASH is not set` and `# CONFIG_STREAM_FLASH is not set`, so the
trial application contains no Flash driver and cannot write NOR at all. The
confirm path and the test request path were configured, not exercised.
Whether the shared data survives the jump into the application is unknown and
needs hardware.

Other MCUboot board files: there is no RT1060 file under
`boot/zephyr/boards/` at the pin. There is an RT1050 HyperFlash RAM load overlay
and an RT1170 `.conf` file (`README.md`). This agrees with Stage 3 D11.

## 6. Further configuration checks requested by the record

| Check | Build A | Build B | Source |
| --- | --- | --- | --- |
| Application `CONFIG_XIP`, `CONFIG_FLASH_SIZE`, `CONFIG_USE_DT_CODE_PARTITION` | `# CONFIG_XIP is not set`, `CONFIG_FLASH_SIZE=0`, `CONFIG_USE_DT_CODE_PARTITION=y`. This is what B68a predicted. | `CONFIG_XIP=y`, `CONFIG_FLASH_SIZE=8192`, `CONFIG_USE_DT_CODE_PARTITION=y` | `*/app/dot-config.txt` |
| Application `CONFIG_FLASH_LOAD_OFFSET`, `CONFIG_FLASH_LOAD_SIZE` | `0x20000`, `0x300000` | `0x20000`, `0x300000` | Same |
| Application `CONFIG_INIT_ARCH_HW_AT_BOOT` | `y` | `y` | Same |
| MCUboot `BOOT_DISABLE_CACHES`, `MCUBOOT_CLEANUP_ARM_CORE` | Both `y` | Both `y` | `*/mcuboot/dot-config.txt` |
| MCUboot watchdog | `BOOT_WATCHDOG_SETUP_AT_BOOT` does not appear in the `.config`. `# CONFIG_BOOT_WATCHDOG_FEED is not set`. `# CONFIG_WATCHDOG is not set`. | Same | Same |
| MCUboot downgrade prevention | `MCUBOOT_DOWNGRADE_PREVENTION` does not appear in the `.config` | Same | Same |
| MCUboot other options | `BOOT_FIH_PROFILE_OFF=y`, `MCUBOOT_LOG_LEVEL=3`, `# CONFIG_MCUBOOT_SERIAL is not set`, `# CONFIG_BOOT_ENCRYPT_IMAGE is not set`, `UPDATEABLE_IMAGE_NUMBER=1`, `BOOT_MAX_IMG_SECTORS_AUTO=y` | Same | Same |
| Application `MCUBOOT_GENERATE_CONFIRMED_IMAGE` | `# ... is not set` | `# ... is not set` | `*/app/dot-config.txt` |
| Application `MCUBOOT_BOOTLOADER_NO_DOWNGRADE` | `y` | `# ... is not set` | `*/app/dot-config.txt` |
| How the rank 1 trial marks the inactive slot for test | It does not. The sample has no update code (see section 5). | Not applicable | Section 5 |

`MCUBOOT_BOOTLOADER_NO_DOWNGRADE` is an application-side symbol. The Stage 2
matrix (section 2.4) already says Zephyr sets it for RAM load modes because
the bootloader boots the highest version. Its effect on the application was not
checked against the pinned source. It does not change the finding that the RAM
load bootloader rejects no older image (B14, B21, B60).

## 7. FlexSPI window check of the Build A application

Window: `0x60000000` to `0x6FFFFFFF`. Source: `A/app-flexspi-window-report.txt`.

| Item | Result |
| --- | --- |
| Allocated sections with a run address in the window | 0 |
| LOAD segments | 2. Run and load address `0x80000000` (file size `0x7c90`, memory size `0x8000`) and `0x80008000` (file size `0x70`, memory size `0x2240`). Neither run nor load address is in the window. |
| Symbols with an address in the window | 1: the absolute constant `CONFIG_FLASH_BASE_ADDRESS` (`0x60000000`), which is a Kconfig value, not code or data |

For comparison, the Build B application has 14 allocated sections and 3 of its
4 LOAD segments in the window, and 611 symbols there
(`B/app-flexspi-window-report.txt`). It executes in place.

Limits of the check: it reads section run addresses, segment run and load
addresses and the symbol table. It does not follow literal pointers or other
indirect references in code and data. The #22 matrix warns that a symbol table
misses such edges. The controller sample also uses no ITCM or DTCM (0 B in the
baseline and in both applications), so it does not show how a composition that
relocates code to ITCM would link for RAM load. Both points are **Not verified**.

## 8. Flash driver linked by MCUboot

| Item | Build A and Build B | Source |
| --- | --- | --- |
| Kconfig | `CONFIG_FLASH_MCUX_FLEXSPI_NOR=y`, `CONFIG_FLASH_MCUX_FLEXSPI_XIP=y`, `CONFIG_FLASH_MCUX_FLEXSPI_XIP_MEM="ITCM"`, `CONFIG_FLASH_MCUX_FLEXSPI_NOR_WRITE_BUFFER=y` | `*/mcuboot-flash-driver.txt` |
| Linked object | `flash_mcux_flexspi_nor.c.obj` from `libdrivers__flash.a`, with `flexspi.c.obj`. The `hal_nxp` FlexSPI functions (for example `FLEXSPI_TransferBlocking`) are linked in both builds. | `*/mcuboot-flash-driver.txt`, `*/mcuboot/nm-n.txt` |
| Where the driver runs | `.itcm_text_reloc`, run address `0x0` (ITCM), load address `0x600022c0` in NOR, size `0x2160` (8,544 B, the ITCM figure in section 4). It holds `flash_flexspi_nor_write`, `flash_flexspi_nor_erase`, `memc_flexspi_transfer`, `memc_flexspi_wait_bus_idle` and `FLEXSPI_TransferBlocking`. | `*/mcuboot/readelf-l.txt`, `*/mcuboot/nm-n.txt` |
| What stays in NOR | `memc_flexspi_config_0` (`0x6000bfd8` in A, `0x6000c650` in B) and `flash_flexspi_nor_config_0` in `rodata`. The vector table (`0x60002000`), `z_arm_fault`, `z_arm_nmi` and all MCUboot logic, including `flash_area_write` and `flash_area_erase`, execute in place. | `*/mcuboot/nm-n.txt` |

[Stage 6](stage6-layout-and-write-analysis.md) relates this to the #22 phase
matrix.

## 9. Trailer, alignment and signing arguments

| Item | Build A | Build B | Source |
| --- | --- | --- | --- |
| Devicetree NOR `write-block-size` and `erase-block-size` | `0x1`, `0x1000` | `0x1`, `0x1000` | `checks.txt` section 5 |
| `CONFIG_MCUBOOT_BOOT_MAX_ALIGN` | 1 | 1 | Same |
| imgtool arguments for the application | `sign --version 0.0.0+0 --header-size 0x400 --slot-size 3145728 --align 1 --load-addr 0x80000000 --key <DEV-KEY-PATH> --hex-addr 0x20000` | `sign --version 0.0.0+0 --header-size 0x400 --slot-size 3145728 --align 1 --key <DEV-KEY-PATH>` | `*/imgtool-args-app.txt` (extracted from `build.ninja` by the trial script) |
| `--pad` or `--confirm` | **Not passed** | **Not passed** | Same |
| MCUboot image signed | No (no imgtool command in the MCUboot `build.ninja`) | No | `*/imgtool-args-mcuboot.txt` (empty) |
| MCUboot CMake trailer estimates, not passed to imgtool | Image footer 198 B, upgrade footer 48 B | Image footer 4,246 B, upgrade footer 4,096 B | `checks.txt` section 5 |
| Application Kconfig warning | `MCUBOOT_UPDATE_FOOTER_SIZE` assigned `0x30` but ignored, because `MCUBOOT_IMG_MANAGER=n` and `IMG_MANAGER=n` | Same warning | `A/build-build-a-ramload-revert-build.log` line 83, `B/build-build-b-swap-offset-build.log` line 80 |

The Build B trailer estimate rounds the swap status up to one 4 KiB sector,
which matches the Stage 2 arithmetic for swap using offset (S2-10). Whether the
4,246 B estimate also covers the one-sector offset of the update in the
secondary slot (S2-9) is **Not verified**.

**Default signed image and rank 1.** Both builds produce
`zephyr.signed.bin` without trailer magic and without the confirmed flag. The
desk record found that under RAM load with revert the bootloader erases the
whole slot of a selected image whose trailer magic is not GOOD (B62 as
corrected, MCUboot `loader.c`:2199-2211), and that the stock test request always
targets slot 1 (B63a). Applied to this build output, a Build A signed image
programmed or downloaded without further steps would be erased when the
bootloader first selects it (Inference, not run). This is build evidence of the
default only. It gives owner decision 13 a measured baseline: the padded
alternative would be a 3 MiB file per update (the signed application is
33,032 B unpadded). It is not a hardware result.

## 10. Build warnings

Both logs show the devicetree warning `unique_unit_address_if_enabled` for
`/soc/usbd@402e0000` and `/soc/usbh@402e0000` in both images, and the
`MCUBOOT_UPDATE_FOOTER_SIZE` warning above (build logs lines 37, 81 and 83 for
Build A, 36, 78 and 80 for Build B). No other warning is in the logs.

## 11. Deviations from the plan

| Deviation | Effect | Source |
| --- | --- | --- |
| Two unrelated containers were already running (one running a networking diagnostic script from another worktree, one mounting a different workspace). They were not used or touched. | None on the builds. Recorded because they share the volume below. | `README.md` deviation 1 |
| The development private key sits on the Docker volume `crux-host-cache`, which those containers also mount (one runs as root). File mode 600, directory mode 700. | The key is readable by those containers. It is a development key only. Recommendation: delete it after the trial, or keep it only if a fixed development key for later board tests is wanted (see Stage 8). | `README.md` deviation 2 |
| imgtool ran from the pinned MCUboot clone (2.4.0), not from a PyPI install | Matches what sysbuild calls and the pinned revision | `README.md` deviation 3 |
| The baseline build directory was moved out of the repository | Avoids collision with other sessions. `scripts/record-build-provenance.py` was not run. | `README.md` deviation 4 |
| `EXTRA_ZEPHYR_MODULES` and `ZEPHYR_EXTRA_MODULES` were both used | Zephyr merges both variables | `README.md` deviation 5 |
| Route 2 and Zephyr `hello_world` were not needed | None | `README.md` |
| No provenance JSON files (`provenance-mcuboot.json`, `provenance-app.json`) as listed in the plan | Hashes, `.config` extracts and revisions are in [hashes.txt](trial-inputs/hashes.txt) and the check files instead | This record |
| The trial tree `1d6ac4f` is older than the PR base `f57ff22` | See section 1 | Section 1 |

## 12. Failure record

The first Build A attempt failed at Kconfig. The MCUboot fragment set
`CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_SIZE=0x2000000`. That option has type `int`
and rejects a hexadecimal value: "the value '0x2000000' is invalid for
BOOT_IMAGE_EXECUTABLE_RAM_SIZE ... which has type int -- assignment ignored",
followed by "Aborting due to Kconfig warnings" (`A/failed-attempt1-build.log`
lines 39 and 46). The command was identical to the successful one
(`A/failed-attempt1-command.txt`, exit 1). The value was changed to `33554432`
and the build passed. No other failure is recorded.

## 13. What this stage does not show

No boot, revert, recovery, interrupted update, SDRAM retention across the jump,
shared-data survival, cache or MPU behaviour, watchdog behaviour, copy time,
timing or thermal behaviour. None of the images was run. A configuration that
builds is not a configuration that boots.
