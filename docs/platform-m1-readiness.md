# Platform M1 target and maintenance readiness

This record closes the assessment scope of [issue #2](https://github.com/wonny-labs/crux-pilot/issues/2)
for **Engineering and target foundation** in the current Platform Development Plan, reviewed at
`crux-platform` commit `7d2287bdb5a252ee5e1fc51c7142813ba37800d6`.
It is separate from the historical Time Pilot M0–M5. Assessment date: 2026-10-17.
No hardware was operated for this assessment.

## Assessed configuration and evidence

The established reference is MIMXRT1060-EVKB PCB revision B, MIMXRT1062, standard 8 MiB
QSPI NOR, SW4 OFF–OFF–ON–OFF, without HyperFlash rework. Inputs remain Zephyr v4.4.0
`684c9e8f32e4373a21098559f748f06915f950c9`, SDK 1.0.1 (ARM GCC 14.3.0), west 1.4.0,
CMake 3.28.4, Ninja 1.11.1 and the existing pinned CMSIS/NXP HAL manifest.
See [baseline input identities](baseline.md#selected-pinned-inputs).

Clean `main` source `bd0def0e92274be2d7e744d97ce16aef7c757aef` passed
[Host CI](https://github.com/wonny-labs/crux-pilot/actions/runs/39067354154) (27/27) and
[Zephyr CI](https://github.com/wonny-labs/crux-pilot/actions/runs/39067353907) (4/4,
native_sim and EVKB sample builds). The downloaded manifest's 19 artefact/report hashes
and sizes matched. ELF `362ccac5…a9ab889` and BIN `4f8632b2…de764a` match the image in
[physical Time evidence](baseline.md#m4-physical-uart-and-rtc-acceptance-2026-10-17).
Boot/download/debug, UART and RTC are verified only in that identified configuration.

Authoritative hardware reference: [NXP EVKB user manual, Rev. 0](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/mcuxpresso-ide/9718/1/MIMXRT1060EVKBUM%20%282%29.pdf),
sections 2.2, 2.8, 2.9 and 2.12. Connector names from another EVK/revision must not be
applied without checking the actual board. This record uses source inspection to assess
feasibility; it does not turn a devicetree node into physical acceptance evidence.

## Native support and physical access

The following paths are relative to the pinned [Zephyr tree](https://github.com/zephyrproject-rtos/zephyr/tree/684c9e8f32e4373a21098559f748f06915f950c9).
The [board DTS](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/boards/nxp/mimxrt1060_evk/mimxrt1060_evk.dtsi)
and [pinctrl](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/boards/nxp/mimxrt1060_evk/mimxrt1060_evk-pinctrl.dtsi)
identify the intended controller, driver and pin routing.

| Path | Available native mechanism and constraint | Physical access / disposition |
| --- | --- | --- |
| Ethernet/PHY | Enabled ENET MAC/MDIO nodes, RMII, `microchip,ksz8081` at MDIO address 0, reset GPIO1.9 and interrupt GPIO1.10. Drivers: `drivers/ethernet/eth_nxp_enet.c`, `drivers/ethernet/phy/phy_microchip_ksz8081.c`. DTS requests a random MAC address; this is not a deployed MAC provisioning policy. | Onboard PHY/RJ45 path is a plausible M3 target. Cable, peer/switch and isolated test-network access are not confirmed. Before M3 qualification, confirm actual PHY/routing, link, addressing and MAC policy, then TCP/UDP and link-loss results. No new driver is currently justified. |
| I2C | `arduino_i2c` is LPI2C1; native `drivers/i2c/i2c_mcux_lpi2c.c`. SDA AD_B1_01 and SCL AD_B1_00. | Arduino SDA/SCL access is documented. Confirm 3.3 V wiring, pull-ups and address conflicts before an authorised physical run. External device/fixture availability is unknown. The optional onboard sensor cannot be assumed populated. M3 device qualification remains pending. |
| General SPI | `arduino_spi` is LPSPI1, native `drivers/spi/spi_nxp_lpspi/spi_nxp_lpspi.c`. Selected pins are SD_B0_01 PCS0, SD_B0_00 SCK, SD_B0_03 SDI, SD_B0_02 SDO. Pinctrl explicitly notes USDHC sharing and routing resistors R278–R281. LPSPI3 also exists but shares LCD pins. | Arduino access is documented, but the actual routing/resistor population is unverified. Resolve this before selecting a physical SPI fixture; do not use the bus alongside conflicting SD/display functions. A loopback or external peripheral needs confirmed wiring and later M3 evidence. No board rework is authorised by this assessment. |
| EEPROM | No populated EEPROM is established by the board DTS or current evidence. Native `drivers/eeprom/eeprom_at2x.c` supports AT24 I2C and AT25 SPI families. | A compatible 3.3 V external device with known geometry/write-protect wiring is a candidate; none is confirmed available. Existing `eeprom_at2x_emul.c` can provide controlled software tests, not physical persistence evidence. Device selection/access is bounded M3 work, not an M1 hardware purchase requirement. |
| Watchdog | Board alias `watchdog0` selects enabled `wdog0`, compatible `nxp,imx-wdog`; `drivers/watchdog/wdt_mcux_imx_wdog.c`. The driver handles debug/sleep pause options with documented sleep caveats. | No external fixture is inherently needed for a later reset scenario. Authorised reset access and console/reset-cause observation are required. Timeout behaviour, halt interaction and meaningful health supervision remain M3 verification; no runtime guarantee is inferred from source availability. |
| UART/RTC | Existing LPUART1 console and SNVS Counter/RTC composition. | Already physically verified in the retained Pilot evidence. No repeat is required for this readiness record. |

The current Time image has `CONFIG_NETWORKING`, `CONFIG_I2C`, `CONFIG_SPI`,
`CONFIG_WATCHDOG` and `CONFIG_FLASH` disabled. Availability in upstream source does not
mean these services are enabled or supported by the current image. Continue with the
existing target; resolve the listed access constraints before the affected M3 work.

## ROM, image placement and writable storage

Pinned source references are
[QSPI DTS](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/boards/nxp/mimxrt1060_evk/mimxrt1060_evk_mimxrt1062_qspi.dts),
[boot-header linker placement](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/soc/nxp/imxrt/boot_header.ld),
[SoC boot data/IVT](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/soc/nxp/imxrt/imxrt10xx/soc.c) and
[FlexSPI NOR driver](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/drivers/flash/flash_mcux_flexspi_nor.c).

The selected boot switches choose QSPI. The ROM consumes the FlexSPI configuration,
IVT, boot data and DCD memory configuration and hands execution to the Zephyr vector
entry. Zephyr startup precedes the sample's explicit Time initialisation. The existing
map places the configuration block at `0x60000000`, IVT at `0x60001000`, boot data at
`0x60001020`, and DCD at `0x60001030`. This supports the observed boot and debugger
path; a debugger download alone is not proof of every ROM recovery or authentication path.

The generated configuration has `CONFIG_XIP=y`, `CONFIG_NXP_IMXRT_BOOT_HEADER=y`,
`CONFIG_FLASH_LOAD_OFFSET=0`, `CONFIG_USE_DT_CODE_PARTITION=n` and
`CONFIG_BOOTLOADER_MCUBOOT=n`. Thus the current standalone image starts at
`0x60000000`, despite the DTS naming `slot0_partition` as its code partition.
The linker allocates 65,964 bytes of Flash and 8,448 bytes of RAM. Do not treat an
unused interval after this image as an approved storage allocation.
The current linker exposes the whole 8 MiB Flash region; the declared partitions
do not fence standalone image growth away from a future data area.

The upstream 8 MiB DTS declares these **relative** fixed partitions:

| Name | Start | End, exclusive | Meaning for current standalone image |
| --- | --- | --- | --- |
| mcuboot | `0x000000` | `0x020000` | Current image occupies part of this range; no MCUboot is installed by this build. |
| image-0 | `0x020000` | `0x320000` | Future slot declaration, not the current link address. |
| image-1 | `0x320000` | `0x620000` | Future slot declaration; no update/rollback claim. |
| storage | `0x620000` | `0x800000` | Candidate 1,920 KiB data area at CPU address `0x60620000`; not yet writable/qualified. |

The DTS specifies 4 KiB erase blocks and a one-byte write block. Runtime Flash access
is currently disabled even though boot/XIP configuration is present. The FlexSPI driver
requires code and data used during write/erase to avoid Flash fetches; its XIP path
uses RAM/ITCM relocation, prefetch/cache handling and interrupt locking. Its explicit
logging warning describes read-while-write hazards. A separate data partition on the
same chip does not remove those execution or latency hazards.

Before M4 storage integration, verify the actual NOR geometry, selected driver/linker
relocation, bounds, shared-bus/cache/interrupt effects, write/erase duration and restart/
interruption behaviour. Review whether LittleFS and Settings share or separate the
candidate data region. Do not erase the full chip or assume this DTS establishes a safe
update layout. A microSD/VFS alternative exists in upstream board support but card
availability, pin conflicts and persistence behaviour are unverified.

Future image verification/update/recovery must preserve the ROM headers and DCD, agree
on standalone versus bootloader offsets, protect writable data from image downloads,
and define verification boundaries, interruption recovery and rollback before making
those claims. Current ELF checksums and debugger section comparisons establish identity,
not an authenticated boot chain. Fuse/lock state is not assessed. Choosing secure boot,
a bootloader or a new partition layout is a later reviewed scope/design decision.

## Upstream support and upgrade disposition

On 2026-10-17, the official [supported-release table](https://docs.zephyrproject.org/latest/releases/index.html)
lists Zephyr 4.4.0 as latest stable, released 2026-04-14, with EOL 2027-04-12. It is
not an LTS line. Retain the verified pin for this development baseline, with a support
review before that EOL or earlier if relevant fixes require it. This is not a promise
of indefinite upstream support or that a fixed tag receives later fixes automatically.

The [4.5 migration guide](https://docs.zephyrproject.org/latest/releases/migration-guide-4.5.html)
is a working draft, not an adopted upgrade. Its C17 and CMake 3.28 minimums are met by
this baseline. Its NXP counter alarm error change concerns an alarm API not used by
Time. It still requires review of relevant driver, Kconfig, devicetree and build changes.
For an upgrade, resolve the Zephyr manifest, CMSIS/CMSIS 6, NXP HAL and SDK as one tested
input set; check Python/tool requirements, regenerate board configuration and compile
databases, and repeat affected Host/Zephyr and physical RTC/boot/debug checks. Preserve
old evidence under its old identity. Do not assume revision-C board, newer SDK or native
simulation results apply to this EVKB B. No upgrade or architecture change is needed to
close M1; subsequent adoption requires the repository's review gate.

## M1 readiness disposition

The early assessment is complete with constraints: existing build/boot/debug/Time
foundation can continue, and no evidenced requirement for a replacement BSP, driver or
target has been found. Peripheral fixture/routing uncertainty is bounded before M3;
Flash write/XIP, partition and persistence qualification is bounded before M4. Upstream
support review is required before the recorded EOL. These are explicit limits on later
claims, not missing peripheral implementation required to complete M1.
