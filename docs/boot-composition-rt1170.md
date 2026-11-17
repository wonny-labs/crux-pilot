# Boot composition for the RT1170-EVK (starting values, build only)

> **Status:** Starting values, approved as recommended by the project owner on 15 November 2026 (design note "RT1170-EVK boot composition: memory-map choices") and to be corrected by the first CI build. The composition is still **Not verified** by build. The four board files and the check script exist so that a first CI build can test the values. The check runs in its own step of the runtime shard of the Zephyr workflow.
> **Verified:** By host tests of file contents only (`tests/build/test_boot_composition_rt1170.py`): the files exist, both overlays declare the same retained region and trim OCRAM2 by exactly that region, the window values, and the script's argument set.
> **Not verified:** Everything else. The CI step exists but has not passed yet, so the composition is still not verified by build, and nothing was booted. The first run may fail on the memory map, which is expected information for correcting the starting values. That the application builds for this board under sysbuild, and every RT1170 boot ROM, XMCD, OTFAD, HAB and fuse behaviour, are open.
> **Read this first / then:** the [boot overview](boot-overview.md), then the [boot composition](boot-composition.md) page of the RT1060-EVK Rev B, which is the recipe this page copies.

This page is the RT1170-EVK counterpart of [boot composition](boot-composition.md) (work package WP1a of [issue #296](https://github.com/wonny-labs/crux-pilot/issues/296), RT1170 build-only evidence of M8). It does not change that page or the RT1060-EVK files. Statements carry the labels **Verified by build**, **Verified on source** (read in the named Zephyr source tree, as in the boot and update investigation), **Inference** and **Not verified**. No statement about the RT1170 on this page is Verified by build.

## The argument set

The composition is a set of arguments to `west build`, with the four files kept in this repository under `zephyr/boot/boards/mimxrt1170_evk_mimxrt1176_cm7_A/`. As for the RT1060, MCUboot comes from the optional west group `crux-boot` (no `-DEXTRA_ZEPHYR_MODULES`), and there is no key argument, so sysbuild uses MCUboot's published public test key. Images signed with it must never be programmed to a board. `<crux>` is the checkout of this repository, `<app>` the application directory and `<image>` its base name.

```text
west build --sysbuild -b 'mimxrt1170_evk@A//cm7' <app> -d <build> -- \
  -DZEPHYR_EXTRA_MODULES=<crux> \
  -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
  -DSB_CONFIG_MCUBOOT_MODE_RAM_LOAD_WITH_REVERT=y \
  -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y \
  -Dmcuboot_EXTRA_CONF_FILE=<crux>/zephyr/boot/boards/mimxrt1170_evk_mimxrt1176_cm7_A/mcuboot.conf \
  -Dmcuboot_EXTRA_DTC_OVERLAY_FILE=<crux>/zephyr/boot/boards/mimxrt1170_evk_mimxrt1176_cm7_A/mcuboot.overlay \
  -D<image>_EXTRA_CONF_FILE=<crux>/zephyr/boot/boards/mimxrt1170_evk_mimxrt1176_cm7_A/app.conf \
  -D<image>_EXTRA_DTC_OVERLAY_FILE=<crux>/zephyr/boot/boards/mimxrt1170_evk_mimxrt1176_cm7_A/app.overlay
```

[`scripts/test-boot-composition-rt1170.sh`](../scripts/test-boot-composition-rt1170.sh) builds this set (Linux only, build directory `build/zephyr/boot-composition-rt1170`), checks the MCUboot configuration, signs, runs `imgtool verify` with the public test key and checks that a key of another type and two modified images are refused, without a Python traceback. It never programs a board and never generates a key. It runs in CI in its own step of the `runtime` shard of the Zephyr workflow, separate from the RT1060 step. It has not passed yet: the composition is still **Not verified** by build until the first run passes, and that run may fail on the memory map, which is expected information for correcting the starting values.

## Starting values and their basis

The values are the RT1060-EVK values mapped to the RT1170 memory map, as the design note proposed them, now starting values approved as recommended by the project owner on 15 November 2026. All of them are **Inference**, to be corrected by the first CI build.

| Choice | Starting value | Basis |
| --- | --- | --- |
| Board | `mimxrt1170_evk@A//cm7` | The design note follows the Stage 7 study and the [RT1170-EVK feasibility](rt1170-evk-a1-feasibility.md) record. That the Rev A1 PCB is Zephyr revision A is **Not verified**. |
| Executable window | `0x80000000`, size `67108864` (the whole 64 MiB SDRAM) | The RT1060 mechanism, which uses the whole 32 MiB. **Verified on source (v4.4.0):** `sdram0` is 64 MiB at `0x80000000` and is the board's default RAM. |
| MCUboot's own RAM | `ocram2` (`0x202c0000`, 512 KiB), trimmed by the retained region | MCUboot must not use the SDRAM for its own RAM, because it loads the image there. **Verified on source (v4.4.0):** `nxp_rt1170.dtsi` defines `ocram2` with these values. |
| Retained region | 256 B (`0x100`) at `0x2033ff00`, the end of `ocram2`, declared identically in both overlays | Same size and role as the RT1060 region at `0x2027ff00`. The trimmed `ocram2` is `0x202c0000` size `0x7ff00`, which ends where the region begins. |
| Slots | The upstream partitions, unchanged (7 MiB each) | **Verified on source (v4.4.0)** for the sizes. **Inference**, inherited from the RT1060 work: a padded image is the full 7 MiB slot. |

Where the files differ from the RT1060 files only by these values, the construct is the RT1060 one. The overlays say in a comment that two points are **Not verified**: that overriding `ocram2` by `reg` is accepted at v4.4.2, and that `0x2033ff00` is free of any other use in the application.

## Open points

| # | Open point | State |
| --- | --- | --- |
| Starting values | Approved as recommended by the project owner on 15 November 2026, as starting values. | To be corrected by the first CI build. |
| D2 | **Application.** The controller sample has no RT1170 board configuration. | The script builds the controller sample and passes no board configuration. Whether that builds under sysbuild is Not verified. `CRUX_BOOT_RT1170_APP` replaces the application directory without a script edit, for example by a small Zephyr sample, if the controller sample needs more than a board `.conf`. |
| D3 | **CI placement.** | The script runs in its own step of the `runtime` shard of `.github/workflows/zephyr.yml`, placed after the RT1060 step, so that an RT1170 failure does not hide an RT1060 result. The change to that protected path is additive, and tests in `tests/build/test_boot_composition_rt1170.py` check the step and that no line of the workflow was removed or reordered. The guard against removed lines compares with `origin/main`, so it skips in CI (the host job checks out with depth 1, so there is no `origin/main` ref) and protects local runs only. The first run may fail on the memory map. |
| D4 | **Script.** | A second script reuses every check, so no existing protected file is edited. A parameter of `scripts/test-boot-composition.sh` was the alternative; the owner chose the second script, as recommended. |

### Memory table of the Zephyr board document (Not verified)

The Zephyr board document (`boards/nxp/mimxrt1170_evk/doc/index.rst`, Zephyr v4.4.0) lists `sdram0` at `0x80030000` and `ocram2` at `0x200C0000`, and says that only the first 16 KiB of `ocram2` has the memory attributes needed for shared memory. The devicetree files that the composition files follow (`mimxrt1170_evk_mimxrt1176_cm7.dts` and `nxp_rt1170.dtsi`) give the SDRAM at `0x80000000` and `ocram2` at `0x202c0000`. The document may describe the dual-core view or be out of date. **Not verified:** which is right for this composition. The first build must settle it, and a different retained-region location may be needed.

## Not verified

- That MCUboot and the application build for `mimxrt1170_evk@A//cm7` under sysbuild at Zephyr v4.4.2 with these files. The upstream MCUboot sample target resolves to revision B, so a revision-A build may need more than the four files.
- The memory-map facts above at v4.4.2. They were read in a v4.4.0 tree.
- Booting: the RT1170 boot ROM accepting the layout (the FCB offset, the XMCD that starts the SDRAM, boot switch settings, OTFAD in place of BEE), the jump into the application, confirm and revert.
