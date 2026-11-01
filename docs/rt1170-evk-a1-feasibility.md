# RT1170-EVK Rev A1 Cortex-M7 software feasibility

## Scope and evidence boundary

This is a source and build-configuration review for Issue #33. No RT1170 board,
probe, serial adapter, flash, reset, or other physical hardware was accessed.
The issue maps the PCB marked Rev A1 to Zephyr's board revision `A`; the board
marking was not independently checked in this task.

The project pins Zephyr at
[`684c9e8f32e4373a21098559f748f06915f950c9`](https://github.com/zephyrproject-rtos/zephyr/tree/684c9e8f32e4373a21098559f748f06915f950c9),
and pins `hal_nxp` at
`2c2f28ac333e995d7279777409817c4b4f92c1ec` in `west.yml`. At that Zephyr
revision, `boards/nxp/mimxrt1170_evk/board.yml` declares revisions A and B and
defaults to B. The exact target for the requested software configuration is
`mimxrt1170_evk@A/mimxrt1176/cm7`: revision A, MIMXRT1176 SoC, Cortex-M7.
Rev B / EVKB and the Cortex-M4 targets are separate configurations.

## Rev A board defaults

The pinned Rev A CM7 DTS selects:

- `lpuart1` for the console and shell at 115200 baud;
- the board's 64 MiB SDRAM node at `0x80000000` as `zephyr,sram`, plus the
  SoC-defined ITCM and DTCM;
- `ext_flash_ctrl`, `is25wp128`, and the board-defined `slot0_partition` as the
  Flash controller, Flash, and code partition;
- `wdog1` as the board `watchdog0` alias.

The Rev A board inherits its Flash layout from the shared `mimxrt1170_evk.dtsi`:
a 128 KiB boot partition, two 7 MiB image slots, and a storage partition in the
16 MiB device. The Rev B overlay replaces it. These are upstream source
settings and were not changed here. They are not copied
from the RT1060 target and do not prove the fitted device or qualify erase,
program, or persistence behavior on a physical board.

The board Kconfig defaults external memory configuration data (XMCD) and NXP
external SDRAM support for the M7. The board CMake file selects
`evkmimxrt1170_flexspi_nor_config.c` for revision A when the NXP boot header and
FlexSPI NOR options are enabled. This is the Rev A boot configuration source;
it differs from the EVKB-specific
`evkbmimxrt1170_flexspi_nor_config.c`.

Rev B's overlay replaces the flash with a 64 MiB W25Q512NW and defines a
separate Mcuboot/image-0/image-1/storage layout. The pinned Zephyr feature
table says “XIP only” for the EVKB's W25Q512, not for the Rev A IS25WP128. That
overlay and qualification note are not Rev A configuration. This assessment
does not copy the EVKB overlay or the RT1060 QSPI layout into the Rev A target.

The board's source configuration is evidence for a candidate software
composition; it does not prove the fitted flash part, board straps, boot mode,
debug path, persistence behavior, or physical operation of a particular PCB.
NXP's [RT1170 documentation page](https://www.nxp.com/products/i.MX-RT1170?linkline=Data-Sheet&tab=Documentation_Tab)
lists the Rev 3.0 EVK hardware guide. It was not available through the browser
session because NXP redirects the guide to account sign-in.

## Crux integration gaps

The current Crux tree qualifies the RT1060-EVKB M3 composition in several
places:

| Area | Current assumption | RT1170 consequence |
| --- | --- | --- |
| System/watchdog | The opt-in `CRUX_SYSTEM` provider accepts native_sim or `CONFIG_BOARD_MIMXRT1060_EVK`; the RT1060 branch requires `crux-watchdog0` to be `wdog0` at `0x400b8000` and reads its running state through `WDOG_GetStatusFlags`. | A separate System-enabled RT1170 app needs an explicit binding for Rev A's `wdog1` at `0x40030000`. The pinned Zephyr `nxp,imx-wdog` driver also uses `WDOG_GetStatusFlags`, but the Crux System provider rejects every board other than RT1060-EVK and native_sim outright with a build error, before any watchdog address check. This is outside the M3-only candidate below. |
| Serial | `serial_binding.c` only allows the qualified RT1060 LPUART3 resource or native_sim. | A Rev A UART resource and any external connector route need an explicit composition decision. The board console is LPUART1, already owned by console output. |
| Logging | `logging_binding.c` only allows the qualified RT1060 LPUART1 resource or native_sim, with RT1060-specific console sharing constraints. | The Rev A default console also uses LPUART1. A compatible logging selection and owner-conflict policy have not been established for this board. |
| GPIO | The portable GPIO capability's software configurations do not include RT1170; see its contract for the current ones. | RT1170 is not yet an accepted GPIO provider target. A PR cross-build alone would not establish physical pin behavior. |
| Network | Crux NetIf uses Zephyr's default interface. | Upstream board support lists Ethernet, but PHY/link and fixture suitability are unverified for this Rev A board. No Crux network qualification is claimed. |

The M3-only profile in this assessment enables `CONFIG_CRUX_CONTROLLER_M3`
and uses the sample's Zephyr watchdog and hardware-info APIs. It does not
enable the opt-in Crux System, Serial, Logging, or NetIf components. Its
`device_input.c` wrapper uses Zephyr GPIO directly; the controller core and
Crux Time consumer are common project sources. The System binding therefore
does not block this M3-only build candidate. A separate System-enabled app
still needs a reviewed RT1170 binding, and System's CMake composition rejects
coexistence with the native M3 watchdog owner.

## Reproducible build target and current limit

The upstream board target to use for a future clean Crux build is:

```sh
repo="$(git rev-parse --show-toplevel)"
: "${CRUX_WORKSPACE:?Set this to the root of the pinned west workspace containing zephyr/}"
workspace="$CRUX_WORKSPACE"
export ZEPHYR_BASE="$workspace/zephyr"
cd "$workspace"
west build -p always \
  -b 'mimxrt1170_evk@A/mimxrt1176/cm7' \
  -d "$repo/build/zephyr/rt1170-evk-a1-m3" \
  "$repo/samples/controller/zephyr" -- \
  -DZEPHYR_EXTRA_MODULES="$repo" \
  -DEXTRA_CONF_FILE="$repo/samples/controller/zephyr/m3.conf" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

This is a software build target only. It does not invoke flash, reset, or debug
commands. It uses the upstream Rev A board Flash, memory, and boot
configuration without an application overlay. Set `CRUX_WORKSPACE` to the
west workspace root when Crux is a sibling worktree; the usual `dirname(repo)`
default only applies when Crux is nested directly in that workspace.

`scripts/test-m3-rt1170.sh` contains this build, and the Zephyr GitHub workflow
now invokes it and uploads the generated configuration, DTS, map, ELF, binary,
and compile commands. A focused Host-side composition test checks the target
and guards against explicit or automatically discovered RT1170 application
overlays replacing the upstream Rev A defaults.

The direct cross-build command and the final runner were attempted locally;
both stopped before configuration because `west` is not installed. CMake and
an Arm Zephyr toolchain are also absent from the host, so nothing was built
locally. Local checks passed for the composition test, runner shell syntax,
and Python syntax; these do not replace the target cross-build.

At head `baf2ae1ae45e2e23d0f06e4c64fdfd880a6f6531` the Zephyr workflow step
"Cross-build RT1170-EVK Rev A CM7 composition (software-only)" succeeded
(GitHub Actions run 41118849856), so the M3-only candidate configures and links
for this target. This record does not capture the SoC, toolchain or image
details: the generated `.config`, DTS, map, ELF and binary are kept only as
expiring CI artifacts and were not inspected for this assessment, and no ROM/RAM
figures are recorded here. Later commits change only this document, its Host
composition test and an environment guard in the runner; the step runs again on
every push.

## Remaining evidence

- Record the SoC, board revision, toolchain and image configuration (ROM/RAM
  use and load addresses) from the CI-generated `.config`, map and ELF; the
  cross-build itself has succeeded in CI. Review a watchdog binding only if a
  separate System-enabled application becomes part of the target scope; it does
  not block this M3-only build.
- Verify the selected Rev A hardware revision and fitted components before any
  physical test. Confirm boot mode, image format/address, debug path, and
  programming/erase risks before requesting any board operation.
- Establish which UART, GPIO, and network resources are routed and safe for
  the intended consumer; do not infer this from SoC peripherals or EVKB data.
- After separate authorization, record boot, serial, network, GPIO, and timing
  evidence on the physical board. Simulator and cross-build results are
  software evidence only.
- Keep RT1060 and RT1170 consumers on common portable source and rerun the
  native-interface checks after the RT1170 port is added.

No Issue #33 acceptance criterion is marked complete by this assessment.
