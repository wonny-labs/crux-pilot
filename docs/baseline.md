# Crux implementation and readiness evidence

Recorded from 2026-10-16 (Europe/London). Primary repo: `/Users/jiwonkim/Projects/crux-workspace/crux-pilot`.

This log contains dated snapshots; historical failures remain as evidence. The latest dated section
at the end records current results and supersedes older status summaries.

## Repository and host state

| Check | Command / source | Actual result |
| --- | --- | --- |
| Repository location/state | `pwd && git status --short --branch` | Correct primary repo; initial branch `main`, no commits; no remote. At inspection, only starter files and `.DS_Store` were untracked. Existing files were read before edits. |
| Parent workspace | `ls -la ..` | `/Users/jiwonkim/Projects/crux-workspace` contains only `crux-pilot/`; no `.west/`, `zephyr/`, or module siblings exist. It is outside the Crux Git root. |
| OS/CPU | `uname -a; uname -m; sw_vers` | macOS 15.7.4, Apple M3, `arm64`. |
| Memory | `sysctl hw.memsize` and `system_profiler SPHardwareDataType` | 8 GiB RAM. |
| Free storage at initial inspection | `df -h .` | 11 GiB free (95% volume use), below the plan's 30–40 GiB working headroom; no files were deleted. |
| Tools | `command -v` / `--version` | Present: Git 2.39.5 (Apple Git), Apple Clang 17.0.0.13.0. Absent: CMake, Ninja, clang-format, clang-tidy, Python 3.12+, uv, Docker/Podman, VS Code CLI, west, Homebrew, MacPorts. System Python is 3.9.6. |
| Docker architecture/daemon | `docker version`; `docker info` | User installed and started Docker Desktop. Client 29.8.2, Desktop 4.95.0, Engine 29.8.2; client `darwin/arm64`, engine `linux/arm64`; 8 CPUs and 4,106,604,544 bytes reported. |
| Attached USB devices | `system_profiler SPUSBDataType` | Two USB buses listed; no device entries. Does not identify or prove the presence of an EVK/probe/UART. |
| Network/dependency access | `git ls-remote` for Zephyr, example-application and GoogleTest; HTTPS reads of official upstream raw files and GitHub release API | Succeeded. Small metadata/source requests and the later pinned west fetch work. Docker was installed and started after the initial inspection. |

The initial inspection commands were:

```sh
pwd
git status --short --branch
ls -la
ls -la ..
uname -a
uname -m
sw_vers
df -h .
sysctl hw.memsize
system_profiler SPHardwareDataType
system_profiler SPUSBDataType
docker version
```

Tool inventory used `command -v` and version commands for `git cmake ninja ctest clang clang-format clang-tidy python3 uv docker podman code west brew`. No authentication action was attempted. The pinned CMake/Ninja/clang-format versions were installed into `/tmp/crux-m1-tools`; no privileged/system package changes were made. Storage later measured 41 GiB free before west bootstrap and 40 GiB afterward. After the Docker image build and SDK download, `df -h .` reported 32 GiB free; it later measured 33 GiB after SDK setup and 31 GiB after Zephyr run-test dependencies and smoke. After the Linux sanitizer runtime/image rebuild, it measured 26 GiB free, below the plan's advisory 30–40 GiB headroom; no files or images were deleted.

## Selected pinned inputs

| Input | Selection and identity | Evidence / limit |
| --- | --- | --- |
| Zephyr | `v4.4.0`, commit `684c9e8f32e4373a21098559f748f06915f950c9` | `git ls-remote https://github.com/zephyrproject-rtos/zephyr.git refs/tags/v4.4.0 'refs/tags/v4.4.0^{}'`; manifest pins the peeled commit. The project release page lists v4.4.0 as latest stable on the record date. |
| CMSIS 6 | commit `30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74` | Imported from Zephyr v4.4.0's pinned `cmsis_6` project; required by its Cortex-M build (`cmsis_core.h`). Added to this repository's west import allowlist after the EVKB compile exposed the missing dependency. |
| Example-application reference | `v4.4.0`, commit `aaff110f0d5656e8346ca43eddd1c874bf8a2f93` | Tag resolved with `git ls-remote`; reference only, no code copied. |
| GoogleTest | `v1.15.2`, commit `b514bdc898e2951020cbdca1304b75f5950d1f59`; archive SHA-256 `9257316d65d6259f6596bc4fa5464092305ab3f3d9dc6d7e5780ab462d8baa64` | Tag resolved with `git ls-remote`; the archive was streamed and hashed, 873,745 bytes. Root CMake uses the immutable commit archive and hash. |
| Zephyr SDK | `v1.0.1`, `arm-zephyr-eabi` toolchain | Zephyr v4.4.0's `SDK_VERSION` file selects 1.0.1; the migration guide sets 1.0.0 as its minimum. SDK, ARM compiler and host-tools are installed in the Docker named volume. `ZEPHYR_SDK_INSTALL_DIR` points at that persistent path. |
| west | `1.4.0` | Pinned in the Dev Container's persistent-volume venv and later installed in `/Users/jiwonkim/Projects/crux-workspace/.crux-cache/zephyr-venv`; `west topdir`, `west list`, and pinned workspace bootstrap passed. |
| Host image | `mcr.microsoft.com/devcontainers/base:ubuntu-24.04@sha256:d94c97dd9cacf183d0a6fd12a8e87b526e9e928307674ae9c94139139c0c6eae`, `linux/amd64` | Built successfully. Latest local image ID `sha256:dbbf1293a2032554bd9174c44059859e3cf8bc4f1b05b30e2fb093858b8dbbd7`. The Dockerfile includes pinned Clang 18 sanitizer runtime `libclang-rt-18-dev=1:18.1.3-1ubuntu1` for Linux ASan/UBSan builds. |
| Host build tools | CMake `3.28.4`, Ninja `1.11.1`, clang-format `18.1.8`, Clang/clang-tidy `18.1.3`, C17/C++17 | Installed identities checked inside the amd64 container. Host CMake/Ninja/format versions were also used successfully on macOS. |
| Python | Zephyr 4.4 requires Python 3.12 minimum; Ubuntu 24.04 provides the selected platform baseline | Container reports Python 3.12.3. The macOS Python 3.9.6 was not used for Zephyr requirements. |

Zephyr's v4.4 migration guide records minimum Python 3.12 and SDK 1.0.0. Exact resolved source tags and commits are in this file and `west.yml`. Apt tool versions are exact-pinned because the base-image digest does not pin mutable apt repositories; capture the actual installed version strings after the first container build.

## Hardware and range assessment

- Selected board target: NXP MIMXRT1060-EVKB. The user confirmed the physical board is revision B, SW4 is OFF–OFF–ON–OFF, and no HyperFlash rework was performed; this agrees with the selected `mimxrt1060_evk@B//qspi` target. The explicit target compiled and was later programmed; see [QSPI download evidence](#m4-qspi-download-and-breakpoint-verification-2026-10-17). The earlier generic DAPLink volume name alone did not establish these details.
- User-provided NXP product-page specifications for MIMXRT1060-EVKB list the MIMXRT1062DVL6B processor, 64 Mbit QSPI flash, and 512 Mbit HyperFlash. The user-confirmed switch/rework state identifies the current board setup as QSPI without HyperFlash rework. The J-Link identity and UART device path are recorded in the M4 host preflight and J-Link check sections below. Source: [NXP MIMXRT1060-EVKB product page](https://www.nxp.com/design/design-center/development-boards-and-designs/MIMXRT1060-EVKB?tab=Documentation_Tab).
- The user plans to use an external SEGGER J-Link. NXP's EVKB-specific guidance connects the probe to the 20-pin J2 header, removes J9/J10 to isolate the on-board SWD circuit, and uses J1 for board power and the OpenSDA UART/USB bridge. The EVKB user manual identifies J11/J13 as UART routing jumpers, shorted by default. NXP documents the default J40 power selection as pins 5-6. These are reference instructions only; verify the physical board revision and jumper labels before changing anything. Sources: [NXP external J-Link instructions](https://community.nxp.com/t5/i-MX-RT-Crossover-MCUs-Knowledge/Using-J-Link-with-MIMXRT1060-EVKB-or-MIMXRT1040-EVK/ta-p/1452717) and [MIMXRT1060-EVKB user manual](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/mcuxpresso-ide/9718/1/MIMXRT1060EVKBUM%20%282%29.pdf).
- Connector discrepancy: the [Zephyr v4.4.0 EVKB guide](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/boards/nxp/mimxrt1060_evk/doc/index.rst) identifies J41 and J45/J46 for its documented console path, while the NXP EVKB manual and NXP's EVKB-specific external J-Link instructions identify J1 as the OpenSDA USB/UART connection and J11/J13 as UART routing. NXP SDK documentation also uses J41 for the on-board audio codec. The previous claim that J41 is the user's console USB connector is therefore withdrawn. For physical setup, follow the actual board revision's silkscreen/manual and first establish which USB connector enumerates as OpenSDA; do not assume the conflicting board-guide numbering applies to this EVKB.
- User plans to use a 115200 baud, 8-N-1 serial terminal, consistent with NXP SDK examples. The current UART path is `/dev/cu.usbmodem11102`; successful console output and reset/session behavior remain unverified.
- The v4.4 RT1060 EVK devicetree identifies `nxp,mimxrt1062`; its `rtc` alias points to an enabled `zephyr,rtc-counter` child nested under the SNVS RTC node (`nxp,imx-snvs-rtc`). The MCUX SNVS Counter driver consumes that RTC-compatible node, reports 1 Hz and `UINT32_MAX` top.
- Zephyr `struct rtc_time` stores each calendar member, including `tm_year`, as `int`; it is not a `time_t` API.
- The generic `rtc_counter` path uses `timeutil_timegm64()` for input but converts output through `time_t` and `gmtime_r()`. It rejects derived seconds above `UINT32_MAX`. Calendar seconds from 2000 through 2099 fit that unsigned 1 Hz counter range, but target `time_t`/libc behavior still needs a compile and test. The v4.4 DTS and counter driver identify the actual candidate SNVS path; a board build must confirm the selected Kconfig/devicetree configuration. Source inspection is not target acceptance.
- The selected baseline's RTC sample sets `2024-11-17 04:19:00` on startup and its test expects `04:19:01`; it cannot be used as an unchanged Crux behavior or test oracle.

## M4 host preflight (2026-10-17)

The user authorized physical M4 verification on the MIMXRT1060-EVKB using SEGGER J-Link. At this
initial pre-install check, a read-only USB/UART preflight found a DAPLink interface attached, but
the physical PCB revision was unconfirmed and no external J-Link was detected. This probe status
was superseded by the later installation check below. No firmware was written, and no reset was
issued.

Reproducible checks and actual results:

| Command | Result |
| --- | --- |
| `system_profiler SPUSBDataType` | Found `DAPLink CMSIS-DAP`, ARM VID `0x0d28`, PID `0x0204`, plus unrelated hub/storage devices. No SEGGER/J-Link USB device appeared. |
| `diskutil info /dev/disk5` | DAPLink mass-storage volume mounted as `/Volumes/RT1060-EVK`, 67.2 MB. This name suggests the RT1060 EVK family but does not confirm the PCB's EVKB marking or revision. |
| `cat /Volumes/RT1060-EVK/DETAILS.TXT` | DAPLink Interface v0246, Bootloader v0244, interfaces MSD/CDC/HID/WebUSB. Read only. |
| `ls -l /dev/cu.*` | Found `/dev/cu.usbmodem1402` in addition to Bluetooth/printer/debug-console devices. |
| `python3 -c 'import importlib.util; print(bool(importlib.util.find_spec("serial")))'` | `False`; host Python has no pyserial module. A one-off standard-library `termios`/`select` reader was used instead; no package was installed. |
| Standard-library read-only UART capture on `/dev/cu.usbmodem1402`, 115200 8-N-1, 8 seconds | 0 bytes received. It did not transmit, reset, or change the target. This alone does not show whether firmware is running or console routing is correct. The exact capture command is below. |
| `command -v JLinkExe`; `find /Applications -maxdepth 4 -iname '*jlink*' -print` | No executable or matching application found in those locations. J-Link host tools are not currently available to this shell. |
| `stat -f ... build/zephyr/mimxrt1060_evk_B_qspi/zephyr/zephyr.bin .../zephyr.elf`; `shasum -a 256 ...` | Existing target artifacts are dated 2026-10-17 09:54:03 BST: BIN 65,976 bytes, SHA-256 `904f5a3480cc8a689d4710ef99787f0c03dcb178ae66f1581ce424e19a61dda6`; ELF 1,272,928 bytes, SHA-256 `20f6c93d2d0e30ff11fa310d127b2afda5a7e8e598b3ef9aa21dbe7b6e40a4fc`. |

Exact passive UART capture command used:

```sh
python3 -c 'import os,termios,select,time; p="/dev/cu.usbmodem1402"; fd=os.open(p,os.O_RDONLY|os.O_NOCTTY|os.O_NONBLOCK); a=termios.tcgetattr(fd); a[0]=0; a[1]=0; a[2]=termios.CS8|termios.CREAD|termios.CLOCAL; a[3]=0; a[4]=termios.B115200; a[5]=termios.B115200; a[6][termios.VMIN]=0; a[6][termios.VTIME]=0; termios.tcsetattr(fd,termios.TCSANOW,a); end=time.time()+8; data=bytearray();
while time.time()<end:
 r,_,_=select.select([fd],[],[],min(.25,end-time.time()));
 if r:
  try: data.extend(os.read(fd,4096))
  except BlockingIOError: pass
print("Read-only 115200 8N1 capture, 8s, bytes:",len(data)); print(data.decode("utf-8",errors="replace"))'
```

NXP's EVKB-specific instructions connect an external J-Link to J2, remove J9/J10 to isolate the
on-board SWD interface, and use J1 for power and the OpenSDA UART/USB bridge. The user manual lists
J11/J13 as UART routing jumpers and J40 pins 5-6 as the default power selection. Keep that
configuration subject to checking the actual PCB silkscreen/manual before changing jumpers. The
board enumerates as generic `RT1060-EVK` DAPLink, which does not identify the PCB revision or flash
rework. The first read-only SWD attempt failed, then connected after the J9/J10 correction at 100
kHz; see [initial J-Link check](#m4-segger-and-swd-check-2026-10-17) and [successful SWD
connection](#m4-swd-connection-after-j9j10-correction-2026-10-17). SEGGER tooling runs on macOS
because the probe is attached to the Mac; the Dev Container has no direct USB access by default. Do
not program until the physical board/revision and QSPI configuration are confirmed. Sources: [NXP
external J-Link instructions](https://community.nxp.com/t5/i-MX-RT-Crossover-MCUs-Knowledge/Using-J-Link-with-MIMXRT1060-EVKB-or-MIMXRT1040-EVK/ta-p/1452717), [NXP EVKB user manual](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/mcuxpresso-ide/9718/1/MIMXRT1060EVKBUM%20%282%29.pdf), and [Docker Desktop USB/IP overview](https://docs.docker.com/desktop/features/usbip/).

## M4 SEGGER and SWD check (2026-10-17)

After the user installed SEGGER software and connected the probe, a host-side check confirmed the
software and USB probe. The board's DAPLink interface remained connected as well. The first SWD
attempt at 1000 kHz failed, as recorded below. A later attempt after the user corrected J9/J10
succeeded at 100 kHz; see the follow-up section. Neither attempt issued a flash, erase, reset, or
memory-write command.

| Command / check | Actual result |
| --- | --- |
| `command -v JLinkExe`; `find /Applications /opt /usr/local/bin /opt/homebrew/bin -maxdepth 5 -iname 'JLinkExe*' -print` | `/usr/local/bin/JLinkExe` resolves to `/Applications/SEGGER/JLink_V986a/JLinkExe`. |
| `JLinkExe -version` | Commander printed `V9.86a` and DLL `V9.86a`, then reported `Unknown command line option -version`; the banner is the version evidence, not a successful `-version` option. |
| `system_profiler SPUSBDataType` | Found SEGGER J-Link VID `0x1366`, PID `0x1020`, USB serial `000602003106`, and ARM DAPLink CMSIS-DAP VID `0x0d28`, PID `0x0204`. The DAPLink serial port is currently `/dev/cu.usbmodem11102`. |
| `printf 'ShowEmuList USB\nq\n' \| JLinkExe -NoGui 1` | Commander listed a J-Link PLUS with S/N `602003106`, hardware V12.00, firmware V12 built 2026-09-24, and `VTref=3.328V`. During this probe-list command, Commander automatically updated probe firmware from a build dated 2025-04-01; it reported `New firmware booted successfully`. This changed the probe firmware, not the MCU. |
| `printf 'connect\nq\n' \| JLinkExe -USB 602003106 -Device MIMXRT1062xxx6A -If SWD -Speed 1000 -NoGui 1 -ExitOnError 1` | **Failed**, exit code 1: after selecting the requested device and initializing SWD, Commander reported `Could not connect to the target device.` It read `VTref=3.322V`. No reset/write/flash command was sent. |

The initial failure was not evidence that the MCU or board was defective: the probe was enumerated
and saw target voltage, while SWD did not respond. The user then corrected J9/J10, and the follow-up
connection succeeded. The [SEGGER Commander reference](https://kb.segger.com/J-Link_Commander)
documents probe selection and target connection; the [NXP EVKB J-Link
instructions](https://community.nxp.com/t5/i-MX-RT-Crossover-MCUs-Knowledge/Using-J-Link-with-MIMXRT1060-EVKB-or-MIMXRT1040-EVK/ta-p/1452717)
document this board's J2/J9/J10 setup.

## M4 SWD connection after J9/J10 correction (2026-10-17)

After the user corrected the J9/J10 setup, the host-side, read-only target-identification command
succeeded at 100 kHz SWD. It established communication with the selected `MIMXRT1062xxx6A` device
definition and identified an ARM Cortex-M7 core. No reset, erase, flash, or memory write was
requested.

```sh
printf 'connect\nq\n' | JLinkExe -USB 602003106 -Device MIMXRT1062xxx6A -If SWD -Speed 100 -NoGui 1 -ExitOnError 1
```

Actual result: exit code `0`; Commander reported `VTref=3.322V`, SW-DP ID `0x0BD11477`, AHB-AP IDR
`0x04770041`, Cortex-M7 r1p1, and CPUID `0x411FC271`. The preceding 1000 kHz attempt failed with
`Could not connect to the target device`; the successful follow-up used the corrected J9/J10 setup
and a conservative 100 kHz SWD speed. This confirms the current probe-to-target wiring/configuration
and target core identification, but does not isolate which change was necessary. It does not
establish the exact PCB revision, the active QSPI/HyperFlash wiring, firmware state, UART behavior,
or RTC behavior. Preserve the generated firmware checksum and confirm the board marking and flash
configuration before programming.

## M4 J-Link GDB transport check (2026-10-17)

Started the repository's macOS J-Link GDB Server launcher with the connected EVKB and J-Link PLUS.
The server remained at `Waiting for GDB connection...`; it reported J-Link S/N `602003106`, target
`MIMXRT1062xxx6A`, SWD at 400 kHz, target voltage `3.34 V`, and `Connected to target`. From the
running Dev Container, connected the SDK GDB client to the Mac host and read core registers, without
loading an ELF, resetting the board, or downloading firmware:

```sh
# macOS terminal
./scripts/macos/start-jlink-gdbserver.command

# Dev Container terminal
"$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb" --batch \
  -ex "set remotetimeout 10" \
  -ex "target remote host.docker.internal:2331" \
  -ex "info registers pc sp" -ex "detach" -ex "quit"
```

Actual result: the container GDB connected through `host.docker.internal:2331`, read PC `0x00217398`
and SP `0x20200fa0`, then detached. J-Link GDB Server logged the client connection and register
reads, then returned to waiting. The server was stopped with Ctrl-C. This verifies the probe-to-target
SWD link at the requested 400 kHz and the container-to-Mac GDB transport for this session. It does
not verify VS Code's `cppdbg` launch sequence, execute or identify the firmware, or validate the
physical RTC. GDB warned that no executable was specified, as this read-only connection intentionally
did not load the ELF. No reset or flash command was sent. The earlier target-connection-loss log was not
reproduced in this run; its cause remains unknown. The board marking and active QSPI/HyperFlash
configuration remain unconfirmed, so programming stays pending.

## M4 manual GDB attach and image comparison (2026-10-17)

To manually investigate the VS Code launch failure, started the macOS J-Link GDB Server using
`scripts/macos/start-jlink-gdbserver.command`. It reported J-Link GDB Server V9.86a, probe S/N
`602003106`, target `MIMXRT1062xxx6A`, SWD at 400 kHz, target voltage `3.34 V`, and `Connected to
target`. From the running Dev Container, GDB attached to the target and read registers without
issuing `load`, `monitor reset`, erase, or an explicit memory-write command:

```sh
"$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb" --batch -q \
  build/zephyr/mimxrt1060_evk_B_qspi/zephyr/zephyr.elf \
  -ex 'set remotetimeout 60' \
  -ex 'target remote host.docker.internal:2331' \
  -ex 'info registers pc sp' -ex 'detach' -ex 'quit'
```

Actual result: exit code `0`; GDB read PC `0x6000231c` and SP `0x81e00000`. The ELF maps the PC
to `__text_region_start + 92` in its external-flash `text` section. A read-only content comparison
was then run against the same ELF:

```sh
"$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb" --batch -q \
  build/zephyr/mimxrt1060_evk_B_qspi/zephyr/zephyr.elf \
  -ex 'set remotetimeout 60' \
  -ex 'target remote host.docker.internal:2331' \
  -ex 'compare-sections rom_start' -ex 'compare-sections text' \
  -ex 'detach' -ex 'quit'
```

Actual result: GDB reported both `rom_start` (`0x60000000–0x600022c0`) and `text`
(`0x600022c0–0x6000c1d8`) as `MIS-MATCHED` against the current ELF. This shows the contents at
those target addresses do not match the current build; it does not establish whether the existing
image is stale, from another build, or invalid. The generated ELF SHA-256 is
`20f6c93d2d0e30ff11fa310d127b2afda5a7e8e598b3ef9aa21dbe7b6e40a4fc`; BIN SHA-256 is
`904f5a3480cc8a689d4710ef99787f0c03dcb178ae66f1581ce424e19a61dda6`. The GDB client detached and
the server was stopped with Ctrl-C. No firmware was downloaded, and no explicit reset, erase, or
write command was issued. The mismatch supports investigating the existing QSPI image, but the
physical PCB marking, SW4 setting, and any HyperFlash rework have not been visually confirmed; do
not program until they are confirmed. This manual attach did not reproduce the VS Code
`monitor reset`/disconnect sequence, so that launch failure remains unexplained.

## M4 QSPI download and breakpoint verification (2026-10-17)

The user confirmed the physical board is MIMXRT1060-EVKB revision B, SW4 is OFF–OFF–ON–OFF,
and the board has not been modified for HyperFlash. This resolved the flash-selection precondition
for the selected `mimxrt1060_evk@B//qspi` image. The external J-Link PLUS is S/N `602003106`;
J-Link GDB Server ran on macOS at 400 kHz and reported `VTref=3.34 V`. GDB ran in the Dev
Container and reached the macOS server through `host.docker.internal:2331`.

The first Docker CLI build attempt did not inherit the Dev Container's configured PATH and failed
before building with `FATAL ERROR: CMake is not installed or cannot be found; cannot build.`
Adding the container's pinned tool venv to PATH fixed this environment issue. The reproducible
container-terminal form is:

```sh
PATH=/opt/crux-cache/venv/bin:$PATH ./scripts/build-debug-target.sh 'mimxrt1060_evk@B//qspi'
```

Actual result: exit code `0`. CMake reconfiguration took 64.4 seconds and the incremental build
completed in 7 steps. Reported static usage was 65,976 bytes FLASH and 8,448 bytes RAM. Artifact
SHA-256 values were:

```text
zephyr.elf  20f6c93d2d0e30ff11fa310d127b2afda5a7e8e598b3ef9aa21dbe7b6e40a4fc
zephyr.bin  904f5a3480cc8a689d4710ef99787f0c03dcb178ae66f1581ce424e19a61dda6
```

The GDB client then ran this sequence against the running host-side J-Link GDB Server:

```sh
/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb --batch -q \
  build/zephyr/mimxrt1060_evk_B_qspi/zephyr/zephyr.elf \
  -ex 'set remotetimeout 60' \
  -ex 'target remote host.docker.internal:2331' \
  -ex 'monitor reset' -ex 'load' -ex 'break main' -ex 'continue' \
  -ex 'info registers pc sp' -ex 'bt' -ex 'detach' -ex 'quit'
```

`monitor reset` was explicitly issued before `load`; SEGGER documents this as a normal GDB setup
sequence and defines the monitor reset operation as resetting and halting the target CPU. Reset
changes target execution state; it is distinct from `monitor flash erase`. The subsequent `load`
did write the identified QSPI image. GDB exited `0`, reported programming sections in the
`0x60000000` flash range (65,972 bytes loaded; start address `0x600058f0`), set a breakpoint at
`main` (`0x60002768`, `samples/time/src/main.c:7`), continued, and stopped at that breakpoint.
At the stop, PC was `0x60002768`, SP was `0x80001ec0`, and the backtrace's top frame was `main`.
The QSPI section programming and breakpoint entry demonstrate manual download-and-debug success;
they do not verify serial output, RTC behavior, session-unset behavior after reset, or VS Code F5.

After detaching, a separate read-only GDB `compare-sections` check reported both `rom_start`
(`0x60000000–0x600022c0`) and `text` (`0x600022c0–0x6000c1d8`) as `matched`. The earlier
read-only comparison recorded in the preceding section was performed before this programming and
found mismatches. The J-Link GDB Server was stopped with Ctrl-C after verification. No further
board operation was performed. SEGGER's documented GDB sequence and reset semantics:
[J-Link GDB Server](https://kb.segger.com/J-Link_GDB_Server).

## M0–M1 work sequence

1. Build the pinned amd64 Dev Container; record `docker image inspect` identity, resource allocation, and `cmake`, compiler, Ninja, Python, LLVM, west versions. (Done; see latest execution log.)
2. Run `scripts/bootstrap-host.sh`, then `scripts/check-host.sh` with GoogleTest already cached and CMake FetchContent disconnected.
3. Run `scripts/check-format.sh`, `scripts/check-dependencies.sh`, and `scripts/analyze-host.sh`.
4. Run `scripts/bootstrap-zephyr.sh` from the repo with the pinned Python 3.12 environment active (or set `CRUX_PYTHON`); it fetches west projects and installs Zephyr base, build/test and run/test requirements plus pinned west 1.4.0. (Done.) Install SDK v1.0.1 with `scripts/bootstrap-zephyr.sh --install-sdk`. (Done; see execution log.)
5. Run `scripts/check-zephyr-smoke.sh`; verify `west topdir`, module listing, generated host/native artifacts and Twister report. (Done: native_sim Hello World and C++ Hello World built; Twister passed 1/1; Crux module discovery was reported for native and EVKB configurations; `mimxrt1060_evk@B//qspi` Hello World cross-compiled. Do not flash.)
6. Update AC status with command outputs and actual results. Stop before M2.

## Latest resumed M0–M1 execution (2026-10-16)

| Command / check | Actual result |
| --- | --- |
| `df -h .` | 41 GiB available at start of resumed work; 40 GiB after west bootstrap. The 30–40 GiB plan headroom is met. |
| `docker version`; `docker info` | Pass after user installed and started Docker Desktop: Client 29.8.2 / `darwin/arm64`, Desktop 4.95.0, Engine 29.8.2 / `linux/arm64`, 8 CPUs and 4,106,604,544 bytes memory. |
| `python3 -m venv ../.crux-cache/zephyr-venv && ../.crux-cache/zephyr-venv/bin/python -m pip install --disable-pip-version-check --no-cache-dir west==1.4.0 && ../.crux-cache/zephyr-venv/bin/west --version` | Pass. Installed pinned west v1.4.0 in the external workspace cache; no system or repo installation. |
| `PATH="/Users/jiwonkim/Projects/crux-workspace/.crux-cache/zephyr-venv/bin:/tmp/crux-m1-tools/bin:$PATH" CRUX_CACHE_DIR="/Users/jiwonkim/Projects/crux-workspace/.crux-cache" scripts/bootstrap-zephyr.sh` (host) | Pass for west workspace initialization/fetch. This host-side run predates the container requirements installation; it is not evidence that host Python 3.9 can install Zephyr requirements. |
| `/Users/jiwonkim/Projects/crux-workspace/.crux-cache/zephyr-venv/bin/west topdir && /Users/jiwonkim/Projects/crux-workspace/.crux-cache/zephyr-venv/bin/west list` (from parent workspace) | Pass. `west topdir` is `/Users/jiwonkim/Projects/crux-workspace`; manifest, Zephyr, CMSIS, and NXP HAL resolve to expected siblings and pinned revisions. |
| `PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/check-host.sh` | Pass on resumed run with CMake FetchContent disconnected; CTest 1/1 passed. |
| `PATH=/tmp/crux-m1-tools/bin:$PATH CLANG_FORMAT=/tmp/crux-m1-tools/bin/clang-format scripts/check-format.sh`; `scripts/check-dependencies.sh`; `bash -n scripts/*.sh` | Pass. Formatting, host/Zephyr build boundary, and shell syntax pass after the bootstrap update. |
| `python3 -m json.tool .devcontainer/devcontainer.json`; `python3 -m json.tool CMakePresets.json`; Ruby YAML parse for `west.yml`, host workflow, and `zephyr/module.yml` | Pass; all JSON and YAML parsed. |
| Hardware command scan `rg -n 'west[[:space:]]+flash|nrfjprog|JLinkExe|pyocd|openocd' scripts/check-zephyr-smoke.sh scripts/bootstrap-zephyr.sh` | No matches; bootstrap and smoke scripts do not invoke flash/debug probe commands. |
| `west sdk install --help` (after west update) | Blocked loading Zephyr's SDK extension: `ModuleNotFoundError: No module named 'patoolib'`. |
| `docker build --platform linux/amd64 -f .devcontainer/Dockerfile -t crux-pilot-dev .` (re-run after image lookup error) | Pass. Docker reported `naming to docker.io/library/crux-pilot-dev:latest`; image inspect reports the ID above and `linux/amd64`. Dockerfile includes Zephyr v4.4 Linux development prerequisites. |
| User's `docker run ... crux-pilot-dev ...` diagnostic | First attempt failed before container start: `Unable to find image 'crux-pilot-dev:latest' locally` / `pull access denied for crux-pilot-dev`. `docker context show` returned `desktop-linux`; `docker image inspect crux-pilot-dev` confirmed the image was absent in that engine. After rebuilding, the same diagnostic passed: `/opt/crux-cache/venv/bin/cmake` version 3.28.4, `/opt/crux-cache/venv/bin/west` version 1.4.0, and executable `/opt/crux-cache/sdk/zephyr-sdk-1.0.1/setup.sh` present. The existing named volume retained its SDK files. |
| Read-only SDK host-tools inspection (Docker volume mounted `:ro`) | User's pasted diagnostic output matches. `setup.sh` checks CMake and then invokes `hosttools/zephyr-sdk-x86_64-hosttools-standalone-0.10.sh -y -d .`; its output is redirected to `/dev/null`, and a nonzero exit becomes the generic `Host tools installation failed` message (script lines 297–304). The 68 MiB installer and partial host-tools files existed; this inspection alone did not prove successful installation. The next row records the explicit successful invocation. |
| `docker run --rm --platform linux/amd64 --memory=4g --cpus=2 -v /Users/jiwonkim/Projects/crux-workspace:/workspaces/crux -v crux-host-cache:/opt/crux-cache -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev <check-command>` | Pass after fixing `/opt/crux-cache` ownership. Replaced `<check-command>` with the in-container identity/mount inspection, host-check, Zephyr bootstrap and smoke commands below. `arch=x86_64`; Git root `/workspaces/crux/crux-pilot`; `west topdir=/workspaces/crux`; `.west`, Zephyr, CMSIS, NXP HAL siblings visible. CMake 3.28.4, clang/clang++ 18.1.3, Ninja 1.11.1, Python 3.12.3, west 1.4.0, clang-format 18.1.8, clang-tidy 18.1.3. Only the documented parent bind and named cache volume were mounted; no home, credentials or Docker socket. |
| Container `scripts/bootstrap-zephyr.sh` | Pass. Installed Zephyr `requirements-base.txt`, `requirements-build-test.txt`, `requirements-run-test.txt`, and pinned west==1.4.0 into the persistent cache venv. |
| Initial container `scripts/bootstrap-zephyr.sh --install-sdk` | Failed with `ERROR: Host tools installation failed` / `FATAL ERROR: command "/opt/crux-cache/sdk/zephyr-sdk-1.0.1/setup.sh -t arm-zephyr-eabi -h" failed`. A direct `setup.sh -t arm-zephyr-eabi -h` diagnostic outside the venv PATH exited 90 with `cmake` not found; that diagnostic did not reproduce the wrapper's environment. The first generic error's underlying child failure was hidden by the SDK script's output redirection. Relevant repo files: `scripts/bootstrap-zephyr.sh`, `.devcontainer/devcontainer.json`; installer state is in the external Docker named volume. |
| SDK host-tools recovery and bootstrap | The user authorized continuing after the earlier report. Direct command `cd /opt/crux-cache/sdk/zephyr-sdk-1.0.1/hosttools && ./zephyr-sdk-x86_64-hosttools-standalone-0.10.sh -y -d .` completed with exit 0 and `SDK has been successfully set up and is ready to be used.` Then `scripts/bootstrap-zephyr.sh --install-sdk` completed with exit 0, installed SDK v1.0.1 and ARM toolchain, and printed `All done.` Initial generic output was suppressed by `setup.sh`; its source shows the failing child installer and redirection at lines 297–304. The underlying first failure cause was not recovered from that suppressed run. |
| First `scripts/bootstrap-zephyr.sh --install-sdk` rerun | Failed before SDK work with `fatal: detected dubious ownership in repository at '/workspaces/crux/crux-pilot'`. Retried with per-process Git `safe.directory` entries for the four exact checked-out repos; no host/global Git settings were changed. Wrapper then completed successfully. `.devcontainer/devcontainer.json` post-create now adds those exact safe-directory entries inside the container. |
| Initial post-install `scripts/check-zephyr-smoke.sh` in a fresh `docker run --rm` container | Failed before native_sim configure because CMake's SDK package registration was written under ephemeral `/home/vscode/.cmake/packages/Zephyr-sdk`; that container's home disappears after exit. Zephyr v4.4 `FindZephyr-sdk.cmake` supports `ZEPHYR_SDK_INSTALL_DIR`, so the Dev Container now sets it to `/opt/crux-cache/sdk/zephyr-sdk-1.0.1`. |
| Smoke after setting `ZEPHYR_SDK_INSTALL_DIR` | native_sim configured and built; CMake printed `Crux Pilot Zephyr module discovered`. Twister first reported missing `natsort`, then after run-test requirements were added reported missing `junitparser`. These were missing Zephyr `requirements-run-test.txt` and `requirements-build-test.txt` dependencies, not SDK errors. `scripts/bootstrap-zephyr.sh` was amended to install both sets in addition to `requirements-base.txt`. |
| Initial final `scripts/check-zephyr-smoke.sh` before EVKB selection | Pass, exit 0. native_sim Hello World configured and built with host GCC; Crux module discovery confirmed. Twister ran 1 test configuration / 1 test case and passed (no warnings). C++ Hello World configured and built; Crux module discovery confirmed again. EVK compile was skipped at that time because `CRUX_BOARD_TARGET` was unset and the board/revision had not yet been identified. No flash or hardware action occurred. |
| Generated artifacts | Verified from the host after container exit: `../build/zephyr/zephyr.elf` (579 KiB) and `../build/zephyr/zephyr.dts` exist; `../twister-out/twister.json`, `twister.xml`, and `twister_report.xml` exist. These outputs are outside the `crux-pilot` Git root in the parent workspace. |
| Final config/shell validation | `python3 -m json.tool .devcontainer/devcontainer.json` and `bash -n scripts/*.sh` passed after the SDK path, Git safe directories, and Zephyr build/test dependency updates. Container `git rev-parse` passed for Crux and all three west repositories after the exact in-container safe-directory config. |
| Container host foundation (historical preset name): `CRUX_HOST_PRESET=host-debug-container scripts/bootstrap-host.sh`; then `scripts/check-host.sh && scripts/check-format.sh && scripts/check-dependencies.sh && scripts/analyze-host.sh` | Pass. This was the original separate container preset, before the Linux-only preset consolidation recorded below. CTest 1/1 passed; offline host check, format, dependency boundary and clang-tidy passed. |
| `df -h .` after image and SDK work | 31 GiB available after Zephyr run-test dependency installation and final smoke. This remains within the planned 30–40 GiB headroom range. |

Container invocation and check commands (run from the primary repo; Docker CLI was used to reproduce the Dev Container settings):

```sh
docker build --platform linux/amd64 -f .devcontainer/Dockerfile -t crux-pilot-dev .
docker volume create crux-host-cache
docker run --rm --user root -v crux-host-cache:/opt/crux-cache crux-pilot-dev \
  chown -R vscode:vscode /opt/crux-cache
docker run --rm --platform linux/amd64 --memory=4g --cpus=2 \
  -v "$PWD/..:/workspaces/crux" \
  -v crux-host-cache:/opt/crux-cache \
  -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev \
  bash -lc 'uname -m; git rev-parse --show-toplevel; west topdir; west list; cmake --version; clang --version; ninja --version; python3 --version; west --version; clang-format --version; clang-tidy --version'
docker run --rm --platform linux/amd64 --memory=4g --cpus=2 \
  -v "$PWD/..:/workspaces/crux" \
  -v crux-host-cache:/opt/crux-cache \
  -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev \
  bash -lc 'export PATH=/opt/crux-cache/venv/bin:$PATH; CRUX_HOST_PRESET=host-debug scripts/bootstrap-host.sh; scripts/check-host.sh; scripts/check-format.sh; scripts/check-dependencies.sh; scripts/analyze-host.sh'
docker run --rm --platform linux/amd64 --memory=4g --cpus=2 \
  -v "$PWD/..:/workspaces/crux" \
  -v crux-host-cache:/opt/crux-cache \
  -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev \
  bash -lc 'git config --global --add safe.directory /workspaces/crux/crux-pilot; git config --global --add safe.directory /workspaces/crux/zephyr; git config --global --add safe.directory /workspaces/crux/modules/hal/cmsis; git config --global --add safe.directory /workspaces/crux/modules/hal/nxp; export PATH=/opt/crux-cache/venv/bin:$PATH; export CRUX_PYTHON=/opt/crux-cache/venv/bin/python; export CRUX_WORKSPACE=/workspaces/crux; export CRUX_CACHE_DIR=/opt/crux-cache; export ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1; scripts/bootstrap-zephyr.sh'
docker run --rm --platform linux/amd64 --memory=4g --cpus=2 \
  -v "$PWD/..:/workspaces/crux" \
  -v crux-host-cache:/opt/crux-cache \
  -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev \
  bash -lc 'git config --global --add safe.directory /workspaces/crux/crux-pilot; git config --global --add safe.directory /workspaces/crux/zephyr; git config --global --add safe.directory /workspaces/crux/modules/hal/cmsis; git config --global --add safe.directory /workspaces/crux/modules/hal/cmsis_6; git config --global --add safe.directory /workspaces/crux/modules/hal/nxp; export PATH=/opt/crux-cache/venv/bin:$PATH; export CRUX_WORKSPACE=/workspaces/crux; export CRUX_CACHE_DIR=/opt/crux-cache; export ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1; export CRUX_BOARD_TARGET="mimxrt1060_evk@B//qspi"; scripts/check-zephyr-smoke.sh'
```

For the checks above, the persistent cache venv had the Dev Container `postCreateCommand` packages and Zephyr requirements installed. SDK host-tools setup and full SDK bootstrap completed after the user explicitly asked to continue. CMake package registration under a disposable Docker CLI container was not persistent, so `ZEPHYR_SDK_INSTALL_DIR` now points directly to the named-volume SDK path. No hardware was attached or operated.

The first resumed bootstrap invocation used a relative PATH and exited 127 with `west: command not found`; the absolute-path invocation succeeded. Host Python 3.9 could not satisfy Zephyr v4.4 requirements, while container Python 3.12.3 installed base, build/test and run/test requirements. The initial SDK host-tools call failed with suppressed child output; a direct host-tools invocation then succeeded, followed by successful SDK bootstrap. The first fresh-container smoke missed the ephemeral CMake package registration; setting the persistent SDK path fixed that. Twister exposed two missing requirement sets, which were added to bootstrap; the final smoke passed. Docker CLI reproduced the documented resource limits, parent bind, primary folder and cache volume, but the VS Code Dev Containers extension UI itself was not launched. No hardware operation or flash command was run.

## Current build policy (2026-10-16)

Per the user's direction, all CMake host builds and Zephyr smoke builds now run on Linux in the Dev Container. The M1-prescribed CMake presets are named `host-debug` and optional `host-sanitize`; their conditions expose them only when CMake runs on Linux. This removes the Mac/container duplicate while preserving the plan's preset names. `scripts/bootstrap-host.sh`, `scripts/check-host.sh`, `scripts/analyze-host.sh`, and `scripts/check-zephyr-smoke.sh` also stop with exit 2 on non-Linux hosts. Historical macOS build results above remain evidence of the earlier M1 verification; they are not the current supported build path.

Current commands, from `/workspaces/crux/crux-pilot` inside the Dev Container:

```sh
# Standard C17/C++17 smoke configure, build, and test
scripts/bootstrap-host.sh

# Offline repeat of configure, build, and test
scripts/check-host.sh

# Optional Linux ASan/UBSan configure, build, and test
cmake --preset host-sanitize -S . -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build --preset host-sanitize
ctest --preset host-sanitize

# Zephyr native_sim, Twister, C++ smoke; no flash
scripts/check-zephyr-smoke.sh
```

The CMake Tools extension should select `host-debug` for both configure and build presets while attached to the Dev Container. The preset conditions and script guards prevent the documented build flow from running on macOS.

| Current Linux-only check | Command | Actual result |
| --- | --- | --- |
| Host preset discovery | `/tmp/crux-m1-tools/bin/cmake --list-presets` on macOS | No Linux presets were listed. `scripts/bootstrap-host.sh` exited 2 with `Host CMake builds are configured for Linux only; reopen this repository in the Dev Container.` |
| Linux preset discovery | Docker CLI with the documented `linux/amd64` image and cache volume: `/opt/crux-cache/venv/bin/cmake --list-presets` | Listed only `host-debug` and `host-sanitize`. |
| Linux debug CMake/CTest | Docker CLI, then `scripts/check-host.sh` | Pass. CMake 3.28.4/Clang 18.1.3 configured and built in `build/linux/debug`; CTest 1/1 passed. |
| Initial Linux sanitizer configure | `cmake --preset linux-sanitize -S . -DFETCHCONTENT_FULLY_DISCONNECTED=ON` inside Docker, before preset names were aligned to the M1 plan | Failed because `/usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.asan_static-x86_64.a` and `libclang_rt.asan-x86_64.a` were absent. |
| Sanitizer runtime fix | Added `libclang-rt-18-dev=1:18.1.3-1ubuntu1` to `.devcontainer/Dockerfile` and rebuilt `crux-pilot-dev` | Image build passed; latest image is `sha256:dbbf1293a2032554bd9174c44059859e3cf8bc4f1b05b30e2fb093858b8dbbd7`, `linux/amd64`. |
| Initial Linux sanitizer configure/build/test (temporary preset name) | `cmake --preset linux-sanitize -S . -DFETCHCONTENT_FULLY_DISCONNECTED=ON && cmake --build --preset linux-sanitize && ctest --preset linux-sanitize` inside the rebuilt Docker image | Pass after adding the Clang runtime; CTest 1/1 passed in `build/linux/sanitize`. It was rerun below with the M1-prescribed `host-sanitize` name. |
| Linux format, dependency, analysis, and initial Zephyr smoke before EVKB selection | Container command with `CLANG_FORMAT=clang-format`: `scripts/check-format.sh; scripts/check-dependencies.sh; scripts/analyze-host.sh; scripts/check-zephyr-smoke.sh` | Pass, exit 0. Clang-tidy emitted and suppressed third-party warnings; Zephyr native_sim C and C++ smoke builds and Twister completed. EVK compile was skipped at that time because no target had been selected. |
| Docker storage after image rebuild | `df -h .` | 26 GiB free; no cleanup was performed. |

The CLI format check initially used the script's default `clang-format-18` name, which is not provided by the Python formatter package in the cache venv. Re-running with the Dev Container's configured `CLANG_FORMAT=clang-format` passed. The Dev Container now declares the CMake Tools extension so it is installed in the Linux VS Code environment.

| Final M1 preset-name verification | Command | Actual result |
| --- | --- | --- |
| Final Linux preset discovery | Docker CLI: `/opt/crux-cache/venv/bin/cmake --list-presets` | Pass; only `host-debug` and `host-sanitize` are available. |
| Final Linux debug build/check | Docker CLI: `scripts/check-host.sh` | Pass; CMake/CTest succeeded, 1/1 test passed in `build/linux/debug`. |
| Final Linux sanitizer build/check | Docker CLI: `cmake --preset host-sanitize -S . -DFETCHCONTENT_FULLY_DISCONNECTED=ON && cmake --build --preset host-sanitize && ctest --preset host-sanitize` | Pass; CTest 1/1 passed in `build/linux/sanitize`. |
| Initial EVKB cross-build without Dev Container SDK environment | `west build -p always -b 'mimxrt1060_evk@B//qspi' /workspaces/crux/zephyr/samples/hello_world -- -DZEPHYR_EXTRA_MODULES=/workspaces/crux/crux-pilot` | Failed during CMake configure because `Zephyr-sdkConfig.cmake` was not found. The Docker CLI invocation did not inherit Dev Container `containerEnv`; rerun with `ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1`. |
| EVKB cross-build with SDK path | Same `west build` command with `ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1` | Board resolved as revision B, qualifiers `mimxrt1062/qspi`, then failed compiling `offsets.c`: `cmsis_core.h: No such file or directory`. `west.yml` imported `cmsis` but omitted Zephyr v4.4.0's separate pinned `cmsis_6` project. |
| CMSIS 6 manifest and container fix | Added `cmsis_6` to `west.yml`'s Zephyr import allowlist and `/workspaces/crux/modules/hal/cmsis_6` to `.devcontainer/devcontainer.json`'s `safe.directory` setup; reran `scripts/bootstrap-zephyr.sh` from the documented container mount | Pass after adding the new path to the one-off Git safe-directory list. Imported CMSIS 6 revision `30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74`; Zephyr requirement packages were already satisfied. The first update attempt stopped because the new checkout was not yet a Git safe directory; its safe-directory entry is now part of Dev Container setup. |
| Full Zephyr smoke with EVKB target | Docker CLI using the documented parent bind and cache volume; `CRUX_BOARD_TARGET=mimxrt1060_evk@B//qspi scripts/check-zephyr-smoke.sh` | Pass, exit 0. native_sim Hello World compiled; Twister passed 1/1 in 80.43 s; C++ native_sim Hello World compiled; EVKB Hello World compiled and linked all 157 steps. Flash/reset was not invoked. Zephyr DTS emitted one `unique_unit_address_if_enabled` warning for upstream `usbd@402e0000`/`usbh@402e0000`; build succeeded. |

## M2 Time host implementation verification (2026-10-16)

All M2 work stayed in the Linux Dev Container and used the documented parent workspace bind, named cache volume and `crux-pilot` as the working directory. No board, J-Link, UART or flash operation was used. The reproducible debug and sanitizer runs were:

```sh
docker run --rm --platform linux/amd64 --memory=4g --cpus=2 \
  -v "$PWD/..:/workspaces/crux" -v crux-host-cache:/opt/crux-cache \
  -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev \
  bash -lc 'set -e
    git config --global --add safe.directory /workspaces/crux/crux-pilot
    export PATH=/opt/crux-cache/venv/bin:$PATH
    export CRUX_WORKSPACE=/workspaces/crux
    export CRUX_CACHE_DIR=/opt/crux-cache
    export ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1
    scripts/check-host.sh
    CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh'
```

Both configurations completed CMake configure, Ninja build and CTest with 28/28 tests passing. Each also passed `scripts/check-host-sample.sh` with a deterministic stdin/stdout scenario. The test set includes the M1 C17/C++17 smoke and M2 Time API, calendar, command and production-link tests.

Additional container checks, after setting `CLANG_FORMAT=clang-format`, passed `scripts/check-format.sh`, `scripts/check-dependencies.sh` and `scripts/analyze-host.sh`. Clang-Tidy suppressed third-party header warnings; no owned-code diagnostic remained. The deliberate M2-07 boundary test temporarily added `#include <zephyr/kernel.h>` to `components/time/src/time.c`; `scripts/check-dependencies.sh` failed as expected with exit 1 and the native/host dependency diagnostic. The source was restored and the ordinary dependency check then passed.

| Initial result | Resolution / final status |
| --- | --- |
| First M2 build reported missing public include paths and undeclared `NULL` in common C sources. | Added target include requirements and standard `<stddef.h>` includes. A subsequent build found the same missing include in the host backend; fixed it. Final debug and sanitizer builds passed. |
| First CTest run showed `crux_time_tests_NOT_BUILT` placeholders although binaries linked. | `include(GoogleTest)` existed only in the sibling smoke-test directory, leaving the M2 discovery script path empty. Added it to `tests/time/CMakeLists.txt`; final discovery ran all 28 tests. |
| Initial formatter invocation used its default `clang-format-18`, which was not found in the venv. The first check with the correct executable found formatting differences. | Set `CLANG_FORMAT=clang-format`, formatted owned C/C++ files and reran successfully. |
| Clang-Tidy flagged duplicate adjacent branches in the CLI status mapping. | Removed the duplicate `CRUX_ERR_IO` branch; the default branch preserves `ERR IO_ERROR`. Analysis then passed. |
| First deliberate dependency mutation check was blocked by Git's dubious-ownership guard in the temporary container. | Added the exact `safe.directory` entry inside that container, repeated the negative test successfully, restored the source and confirmed the normal dependency check passed. |

At the time of this 2026-10-16 snapshot, M2-08 was failing focused analysis after the C command-handler refactor; see the later 2026-10-17 section for the revised analyzer result. M0-05 and M4 physical evidence remain pending because no board was operated. M1-06 passes from local failure-propagation evidence and the non-flashing build path; the GitHub Actions workflows have not been executed, so M5-03 remains pending external CI validation because no remote or push was authorized.

## M3 Zephyr integration and M5 software-only evidence (2026-10-16)

All work used the running Linux Dev Container (`linux/amd64`, image ID `sha256:dbbf1293a2032554bd9174c44059859e3cf8bc4f1b05b30e2fb093858b8dbbd7`), mounted parent workspace `/workspaces/crux`, and `crux-pilot` Git root. Pinned worktree revisions were Zephyr `v4.4.0` commit `684c9e8f32e4373a21098559f748f06915f950c9`, CMSIS 6 commit `30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74`, and NXP HAL commit `2c2f28ac333e995d7279777409817c4b4f92c1ec`. Tools: west `1.4.0`, Zephyr SDK `1.0.1` (`arm-zephyr-eabi-gcc` 14.3.0), CMake `3.28.4`, Ninja `1.11.1`, and Clang `18.1.3`. Build output and raw reports remain in ignored `build/` paths; no upstream files were copied into the repository.

The initial M3 implementation shared the C17 Time sources and C++17 command handler between host and Zephyr. The C17 command-handler refactor and its replacement build evidence are recorded later in this section. The Zephyr backend binds the standard `rtc` alias, uses `rtc_get_time()`/`rtc_set_time()`, maps native errors to Crux status values, and keeps Zephyr types private. The Zephyr sample explicitly initializes Time before printing `CRUX_TIME_READY`; Zephyr RTC Shell commands are disabled. `CONFIG_RTC_INIT_PRIORITY=61` is set because the pinned upstream RTC-counter wrapper asserts that its initialization priority is greater than the SNVS counter driver's priority 60.

The native_sim Ztest commands run from `/workspaces/crux` inside the container:

```sh
west twister -p native_sim/native/64 \
  -T /workspaces/crux/crux-pilot/tests/zephyr_time \
  -O /workspaces/crux/crux-pilot/build/zephyr/m5-final-twister --inline-logs

west build -p always -b native_sim/native/64 \
  -d /workspaces/crux/crux-pilot/build/zephyr/m5-final-native-sample \
  /workspaces/crux/crux-pilot/samples/time -- \
  -DZEPHYR_EXTRA_MODULES=/workspaces/crux/crux-pilot \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

/workspaces/crux/crux-pilot/scripts/build-target.sh 'mimxrt1060_evk@B//qspi'
```

The final clean Twister run completed in 234.33 seconds with no warnings: 2/2 configurations and 4/4 Ztest cases passed. In addition to the real native_sim RTC no-data/set/get path and RTC-disabled NOT_READY case, tests cover Crux mapping of `-ENODATA`, `-EINVAL`, `-ERANGE`, `-ENODEV`, `-ENXIO`, and fallback `-EIO`. This is simulator/backend evidence, not silicon behavior. Twister reports and raw logs are in `build/zephyr/m5-final-twister/`. The final native_sim Shell application build passed all 140 steps and generated `build/zephyr/m5-final-native-sample/zephyr/zephyr.elf`.

The pristine EVKB target build `scripts/build-target.sh 'mimxrt1060_evk@B//qspi'` completed successfully (204/204 compile/link steps; ROM and RAM reports exited 0). Generated `.config` says revision B and `mimxrt1062/qspi`. Generated `zephyr.dts` maps `rtc = &counter_rtc` to `nxp,imx-snvs-rtc` and its `zephyr,rtc-counter` child. At that time the sample enabled C++17 for its command handler; the later C17 sample rebuild below supersedes that configuration. `CONFIG_RTC_INIT_PRIORITY=61` is set, and `CONFIG_RTC_SHELL` is disabled; upstream counter priority is 60. Zephyr's linker report gives 65,772 B FLASH / 8 MiB and 8,448 B RAM / 32 MiB. `rom.json` totals 65,764 bytes and `ram.json` totals 8,426 bytes; these are static build reports, not measured board memory use.

The final `zephyr.elf` SHA-256 is `3b395af12960b006b5815ce216bafa15549a1b951644b38dc4ee95035f274342`; `zephyr.bin` is `bf5b7d8d85e066b83a2dbf3af1c6dcaaa63e0bc2bc3b4b5e97e50b91e5aebce6`. The BIN checksum matches the preserved pre-mapping image; the ELF changed after making the RTC error mapper directly testable, and its final map includes `crux_time_zephyr_status_from_rtc_error`. Pre-mapping artifacts remain in `build/evidence/m5-pre-mapping-target/zephyr/` and `build/evidence/m5-prior-target/zephyr/`. Current `.config`, generated DTS, `.map`, `.stat`, `rom.json`, and `ram.json` remain under `build/zephyr/mimxrt1060_evk_B_qspi/`; full final clean-build output is `build/evidence/m5-evkb-build.log`.

Clean host reproduction used separate output directories so earlier evidence remained available:

```sh
cmake --preset host-debug -S . -B build/linux/m5-repro-debug \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build/linux/m5-repro-debug
ctest --test-dir build/linux/m5-repro-debug --output-on-failure
ctest --test-dir build/linux/m5-repro-debug --output-junit \
  build/linux/m5-repro-debug/ctest-junit.xml
scripts/check-host-sample.sh host-debug build/linux/m5-repro-debug

cmake --preset host-sanitize -S . -B build/linux/m5-repro-sanitize \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build/linux/m5-repro-sanitize
ctest --test-dir build/linux/m5-repro-sanitize --output-on-failure
ctest --test-dir build/linux/m5-repro-sanitize --output-junit \
  build/linux/m5-repro-sanitize/ctest-junit.xml
```

Both clean host presets built from the pinned cached GoogleTest dependency and passed 28/28 CTest cases. The fresh debug sample stdin/stdout scenario passed. `LastTest.log` and JUnit XML for both clean runs are retained in their build directories. The updated `scripts/check-host.sh` was run in both `host-debug` and `host-sanitize`; each regenerated CTest JUnit output and passed all 28 tests plus the host sample scenario. `scripts/check-format.sh`, `scripts/check-dependencies.sh`, and `scripts/analyze-host.sh` passed; Clang-Tidy suppressed third-party diagnostics and emitted no owned-code diagnostic. A first clean-format check exposed formatting in `time_port_zephyr.c`; the file was formatted and the check passed. A first sample invocation found its old output path; `check-host-sample.sh` and README now use `samples/time/host/crux-time-host`, and the scenario passed from the clean output.

M5-02 negative check: wrote the temporary string `int main(){return 0;}` to `tests/time/src/m5_format_failure_probe.cpp` and ran `scripts/check-format.sh`. It failed as expected with exit 1 and line diagnostics; the source was removed and the normal formatter check passed. The raw negative output is `build/evidence/m5-format-negative.log`.

`.github/workflows/host.yml` covers format, dependency boundaries, host configure/build/tests and analysis; the host check scripts emit CTest JUnit XML and raw logs. `.github/workflows/zephyr.yml` installs pinned Python tools and SDK, fetches the pinned west manifest, runs `scripts/test-zephyr-time.sh`, and uploads Twister raw/JUnit-compatible reports, target configuration/DTS/size reports and firmware. Both workflows use 14-day artifact retention, and the Zephyr upload explicitly includes hidden `.config`. Ruby's standard YAML parser accepted both workflow files. GitHub Actions itself was not run: there is no remote, and the user prohibited remote creation and push. CI status therefore remains pending. Repo state is still the initial `main` branch with no commits and all project files untracked; there is no remote.

### C17 sample and shared command refactor (2026-10-16)

The Time sample and shared command handler now use C17. `samples/time/src/main.c`, `time_shell.c`, `time_command.c`, and `samples/time/host/main.c` are compiled as C. The command header has C++ linkage guards. Host GoogleTests and the Zephyr Ztest application were C++17 consumers at this checkpoint, including `PublicApiLink.CppConsumerCallsC17ProductionLibrary`; the later test-language simplification below converts the Zephyr Ztest source to C17. The Zephyr sample configuration sets `CONFIG_STD_C17=y` and leaves `CONFIG_CPP` disabled.

Commands ran in the existing Linux/amd64 Dev Container at `/workspaces/crux/crux-pilot` with the pinned `/opt/crux-cache` tools:

```sh
scripts/check-format.sh
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
scripts/check-dependencies.sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

Format and dependency checks passed. Debug and ASan/UBSan host builds each passed all 28 CTest cases and the host sample stdin/stdout scenario. `PublicApiLink.CppConsumerCallsC17ProductionLibrary` passed in both runs. Twister selected 2/2 `native_sim/native/64` scenarios and passed 4/4 Ztest cases in 143.05 seconds. The native_sim Shell sample built in 136/136 steps, and the EVKB QSPI firmware compiled and linked in 199/199 steps. No hardware command ran.

The final sample `.config` selects `CONFIG_STD_C17=y`, leaves `CONFIG_CPP` disabled, uses serial Shell and RTC init priority 61, and disables RTC Shell. The target's static size report is 65,972 B FLASH / 8 MiB and 8,448 B RAM / 32 MiB. Final ELF SHA-256: `c122247ea634c6bda8ffcf6570f54c4cab5dad9f069dbbe97c47db1b6c250cce`; BIN SHA-256: `0cea307a02e772c2e60227042fe65e71653a4b985b300ba3deaf0e22ccb5e120`. Current configuration, DTS, map, size reports and firmware are in ignored `build/zephyr/mimxrt1060_evk_B_qspi/`; Twister reports are in `build/zephyr/twister-time/`.

Clang-Tidy remains a recorded failure after two focused attempts. In the first attempt, the command sequence `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh; scripts/check-format.sh; scripts/check-dependencies.sh; scripts/analyze-host.sh` stopped in the final command with exit 1. It reported warnings-as-errors at `samples/time/src/time_command.c:86` and `:130`: `snprintf` triggered `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling`. The fix replaced those calls with a length-checked copy and manual date formatting. The second command, `clang-tidy-18 -p build/linux/debug samples/time/src/time_command.c`, also returned exit 1 because the same rule flagged the length-checked `memcpy` calls at lines 90 and 150. Further retries are paused per instruction; no suppression or workaround was added.

### M5-05 architecture exit review

| Area | Classification | Evidence and constraint |
| --- | --- | --- |
| Independent host use | supported | Root CMake configures host-only libraries/tests/sample without loading Zephyr; Linux presets remain separate from west builds. |
| C/C++ interoperability | supported with constraints | C++17 GoogleTests include public headers and call the C17 Time implementation in the host test composition. The production `crux::time` target is not independently linked from C++ after removal of its dedicated smoke test; Zephyr production and integration tests use C17. |
| Source reuse | supported with constraints | Shared C17 Time and command sources are referenced by each composition. Host controlled-clock and Zephyr RTC backends remain separate. |
| Visible composition | supported | CMake links explicit common, backend and command targets. Zephyr Shell delegates to the portable handler. No backend registry or weak-symbol discovery exists. |
| Native-type isolation | supported | `struct rtc_time`, `struct device`, errno and RTC APIs remain in the Zephyr backend. Public and common code use Crux types. |
| Proportional complexity | supported with constraints | One Time module, one private port per platform, two Ztest scenarios/four test cases and thin scripts/CI workflows; no general HAL/service framework was introduced. Init priority and RTC semantics are Zephyr/board-specific. |

### M5-06 unresolved material issues and next actions

1. M4 physical acceptance is partial. Board/revision/flash setup is confirmed by the user, and M4-01 passed after QSPI download and breakpoint verification. UART output, RTC behavior, reset/session semantics and physical timing still need verification. The earlier J41 serial-path assumption was corrected in the hardware assessment above.
2. SNVS RTC read/write, no-data on silicon, progression, range behavior, reset semantics and serial Shell response have only compile/simulator evidence. Keep all hardware ACs pending until measured on the identified board.
3. M5-03 still needs a successful GitHub Actions run for external CI validation. Both workflows parse and define the intended coverage; `origin` is configured, but authenticated remote access and a successful Actions run have not been verified. M1-06's failure-propagation and no-implicit-hardware criteria are met locally.
4. At this 2026-10-16 checkpoint, M2-08 focused host analysis failed on Clang-Tidy's `DeprecatedOrUnsafeBufferHandling` rule for checked standard C calls. The 2026-10-17 analyzer configuration and result are recorded below.
5. Free host storage is now 20 GiB, below the plan's advisory 30–40 GiB headroom. No files were deleted. Avoid more large downloads/builds until space is reviewed.
6. The user confirmed QSPI switch settings and no HyperFlash rework; the current `mimxrt1060_evk@B//qspi` image was programmed and verified at `main`. Any future flash operation should still use the recorded board setup and deliberate reset/download sequence.

| Initial result | Resolution / current evidence |
| --- | --- |
| First Zephyr application configure failed because public Crux include directories were not propagated to the sample target. | Added a small public-header INTERFACE target in the Zephyr module; native_sim sample then built. |
| The first Zephyr C++ build could not find `<cstring>` in Zephyr's minimal C++ library. | Replaced standard C++ wrappers with equivalent C headers/functions supported by the configured runtime; native builds passed. |
| A C++ Ztest could not link `crux_time_port_read()` because the private port header lacked C linkage guards. | Added `extern "C"` guards; both native_sim scenarios and all four Ztest cases passed. |
| The first EVKB target compile hit the upstream assertion that RTC priority must be greater than SNVS Counter priority 60. | Set `CONFIG_RTC_INIT_PRIORITY=61`; EVKB QSPI application build passed. |
| Zephyr printed an upstream `unique_unit_address_if_enabled` DTS warning for `usbd@402e0000` and `usbh@402e0000`. | Build completed; no Crux DTS change was made because the conflict is in pinned upstream board description. |
| Initial C++ formatter check rejected `time_port_zephyr.c`. | Applied the repository's clang-format and reran the full owned-code format check successfully. |
| The first host sample verification used its pre-M3 path. | Moved the host target beneath `samples/time/host/`, updated the helper and README, and verified the sample in the clean debug directory. |

At that earlier checkpoint, no physical board, J-Link, UART, flash or reset operation had been performed. Thus that build verified target compilation and DTS integration, not SNVS hardware operation or persistence. Later J-Link/QSPI evidence is recorded in [M4 QSPI download and breakpoint verification](#m4-qspi-download-and-breakpoint-verification-2026-10-17). The free-space check after the M5 builds reported 20 GiB. No files or images were deleted.

## Current blockers

1. M0-05 and M4-01 pass. M4-02..07 remain pending for UART/RTC/reset behavior and physical test execution; M4-08 remains pending for timing bounds and limitations. The user-confirmed board/revision/SW4/rework state and QSPI download/debug result are recorded in [M4 QSPI download evidence](#m4-qspi-download-and-breakpoint-verification-2026-10-17).
2. M5-03's GitHub Actions execution remains pending because no remote or push was created as instructed. Local workflow parsing and M1-06 failure-propagation verification are recorded above.
3. Storage is 20 GiB free after the clean M5 build outputs. This is below the plan's advisory 30–40 GiB working-headroom target, not an official minimum. No files or images were deleted.
4. Dev Container image, mount layout, workspace identity, SDK path, tool identities, host checks and clang-tidy were verified via Docker CLI. The CMake Tools extension is declared in the Dev Container; its preset selection and build actions in the VS Code UI were not exercised here.

## Scaffold verification performed

The commands and results in this section record the initial M0–M1 validation, including macOS runs before the Linux-only build decision. The active preset names are still `host-debug` and `host-sanitize`, but their current conditions restrict them to Linux; use the current commands in [Current build policy](#current-build-policy-2026-10-16).

| Command | Actual result |
| --- | --- |
| `bash -n scripts/*.sh` | Pass, including after changing `check-format.sh` from Bash 4 `mapfile` to a Bash 3-compatible array loop. |
| `python3 -m json.tool .devcontainer/devcontainer.json` and `python3 -m json.tool CMakePresets.json` | Both parsed successfully. |
| Ruby standard-library YAML parse | `west.yml`, `.github/workflows/host.yml`, and `zephyr/module.yml` parsed successfully. |
| `scripts/check-dependencies.sh` | Pass: root CMake remains host-only and the module CMake entry is `zephyr/CMakeLists.txt`. |
| Apple Clang C17 compile of `tests/build_smoke/src/smoke_value.c`, followed by Apple Clang++ C++17 compile/link/run of a temporary consumer | Pass: C object linked into and called from a C++17 executable. Temporary files are under `/tmp/crux-m1-smoke`, outside the repo. This does not substitute for CMake/GoogleTest acceptance. |
| `python3 -m venv /tmp/crux-m1-tools && /tmp/crux-m1-tools/bin/python -m pip install --disable-pip-version-check --no-cache-dir cmake==3.28.4 ninja==1.11.1` | Pass; CMake/Ninja installed in a task-local virtual environment, outside the repo. |
| `/tmp/crux-m1-tools/bin/pip install --disable-pip-version-check --no-cache-dir clang-format==18.1.8` | Pass; pinned formatter installed in the same task-local virtual environment. No system package changes. |
| `PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/bootstrap-host.sh` | Pass: CMake configure, Ninja build, and CTest; `BuildSmoke.C17LibraryIsCallableFromCpp17` passed (1/1). GoogleTest fetched to external cache with pinned SHA-256. |
| `PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/check-host.sh` | Pass: CMake configured with `FETCHCONTENT_FULLY_DISCONNECTED=ON` and CTest passed (1/1) in both the initial offline run and final rerun after formatting. The final run rebuilt the changed host targets. Zephyr, west and SDK were absent from this workspace/run. |
| ASan/UBSan CMake preset configure/build/CTest, with `FETCHCONTENT_FULLY_DISCONNECTED=ON` | Pass on macOS AppleClang 17; 1/1 CTest passed. |
| `CLANG_FORMAT=/tmp/crux-m1-tools/bin/clang-format scripts/check-format.sh` after installing `clang-format==18.1.8` | Pass; reported `clang-format version 18.1.8`. The first formatter run exposed style differences; `.clang-format` was tightened and the smoke fixture formatted before the passing run. |
| Apple Clang `--analyze` for the C17 library and C++17 GoogleTest consumer | Pass with `-Wall -Wextra -Wpedantic -Werror`; no diagnostics. `clang-tidy` was absent on macOS but passed in the container. |
| Initial `CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/bootstrap-host.sh` | Blocked, exit 127 at `cmake: command not found`; resolved by installing pinned CMake/Ninja into the task-local venv above. |
| Initial `scripts/check-format.sh` | First attempt exposed Bash 3's lack of `mapfile`; after fixing the script it stopped with exit 127 (`clang-format-18: command not found`). The pinned formatter was then installed in the task-local venv and the check passed. |
| Initial `scripts/bootstrap-zephyr.sh` attempt before west was installed | Blocked, exit 2 with `west is required. Install the pinned version with: python3 -m pip install west==1.4.0`; resolved during resumed setup above. |
| Initial `scripts/check-zephyr-smoke.sh` attempt before west bootstrap | Blocked, exit 2 with `Run scripts/bootstrap-zephyr.sh before the Zephyr smoke.`; workspace bootstrap later completed. No build or hardware action occurred in that initial attempt. |
| Failure propagation probe | With `/tmp/crux-m1-fake-bin/cmake` returning 37, `CRUX_CACHE_DIR=/tmp/crux-m1-fake-cache PATH=/tmp/crux-m1-fake-bin:/usr/bin:/bin scripts/bootstrap-host.sh` returned 37. This checks shell failure propagation only; the fake tool is outside the repo. |
| Hardware command scan | `rg -n 'west[[:space:]]+flash|nrfjprog|JLinkExe|pyocd|openocd' scripts/check-zephyr-smoke.sh scripts/bootstrap-zephyr.sh` had no matches. |
| `git diff --check` | Pass for tracked diff; at this starter-repo state all implementation files are untracked, so whitespace was additionally scanned with `rg -n '[\\t ]+$' ...` and no matches were found. |

YAML parser command used:

```sh
ruby -e 'require "yaml"; %w[west.yml .github/workflows/host.yml zephyr/module.yml].each { |f| YAML.load_file(f); puts "parsed #{f}" }'
```

Historical host tool installation and verification commands (these ran before the Linux-only build decision; use the current Linux commands in [Current build policy](#current-build-policy-2026-10-16)):

```sh
python3 -m venv /tmp/crux-m1-tools
/tmp/crux-m1-tools/bin/python -m pip install --disable-pip-version-check --no-cache-dir cmake==3.28.4 ninja==1.11.1
/tmp/crux-m1-tools/bin/pip install --disable-pip-version-check --no-cache-dir clang-format==18.1.8
PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/bootstrap-host.sh
PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/check-host.sh
PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache cmake --preset host-sanitize -S "$PWD" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
PATH=/tmp/crux-m1-tools/bin:$PATH cmake --build --preset host-sanitize
PATH=/tmp/crux-m1-tools/bin:$PATH ctest --preset host-sanitize
CLANG_FORMAT=/tmp/crux-m1-tools/bin/clang-format scripts/check-format.sh
clang --analyze -std=c17 -Wall -Wextra -Wpedantic -Werror -I tests/build_smoke/include tests/build_smoke/src/smoke_value.c
clang++ --analyze -std=c++17 -Wall -Wextra -Wpedantic -Werror -I tests/build_smoke/include -isystem /tmp/crux-m1-cache/cmake/googletest-src/googletest/include tests/build_smoke/src/smoke_test.cpp
```

Historical macOS host CMake/CTest, sanitizer and clang-format checks passed before the Linux-only decision. Current Linux container CMake/CTest, ASan/UBSan, clang-format, dependency checks, clang-tidy and Zephyr smoke passed. The final Zephyr smoke built native_sim and C++ Hello World, Twister passed 1/1, and Crux module discovery was reported twice. Only the board-specific compile is pending because the exact EVK target is unknown; CI remains unrun.

### Public-header compile fixture simplification (2026-10-17)

Removed the six `tests/time/public_headers/{status,time,time_calendar}.{c,cpp}` compile-only fixtures and their two CMake object-library targets. Zephyr documents C++ applications consuming its C API and organizes Ztest coverage around APIs/features ([C++ synchronization sample](https://docs.zephyrproject.org/latest/samples/cpp/cpp_synchronization/README.html), [Ztest guide](https://docs.zephyrproject.org/latest/develop/test/ztest.html)); its guidance does not require a C/C++ compile-only fixture for every public header. At this checkpoint, Crux retained `PublicApiLink.CppConsumerCallsC17ProductionLibrary` alongside the C++ `TimeCalendar.*` tests. The later decision below removes that separate production-target smoke; public API C++ guards and C++ GoogleTests remain.

Commands ran in the existing Linux/amd64 Dev Container at `/workspaces/crux/crux-pilot`:

```sh
PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-host.sh
GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/workspaces/crux/crux-pilot PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-format.sh
GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/workspaces/crux/crux-pilot PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-dependencies.sh
```

Results: host Debug configured and built successfully; all 28/28 CTest cases passed, including `PublicApiLink.CppConsumerCallsC17ProductionLibrary` and all seven `TimeCalendar.*` cases. The host sample stdin/stdout scenario passed. Formatting and dependency-boundary checks passed. The initial direct `check-host.sh` invocation lacked the cached tool directory in `PATH` and exited 127 (`cmake: command not found`); rerunning with the pinned cache path passed. The first format/dependency invocations stopped on Git's dubious-ownership check; setting `safe.directory` in each command's temporary Git configuration resolved it without changing persistent Git configuration.

### Test-language simplification (2026-10-17)

The test directories have distinct jobs: `tests/time/` runs fast host behavior/API tests with GoogleTest and a deterministic backend; `tests/zephyr_time/` runs native Zephyr backend/integration scenarios with Twister and Ztest. Host GoogleTest sources remain C++ because GoogleTest is a C++ framework; those tests exercise public C API calls against the C17 host test composition. The then-present `PublicApiLink.CppConsumerCallsC17ProductionLibrary` additionally linked the production `crux::time` target and is removed in the later decision below. Zephyr Ztest does not need to be C++ for API-consumption coverage: its sources are now C17. This is a judgment about test purpose and maintenance cost, not a rule that Ztest cannot be used from C++.

Changes: renamed `tests/zephyr_time/src/main.cpp` to `src/main.c`; removed the test application's C++ language options from its CMake file and `CONFIG_CPP`/`CONFIG_STD_CPP17` settings from `prj.conf`; removed `extern "C"` wrappers from the private Zephyr port headers because no C++ consumer includes them. Public API C++ guards remain. `tests/README.md`, the README, readiness review and implementation status describe the split and C++ API coverage through host GoogleTests. The earlier per-public-header compile-only fixtures remain removed.

Commands ran in the existing Linux/amd64 Dev Container at `/workspaces/crux/crux-pilot`:

```sh
PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-host.sh
GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/workspaces/crux/crux-pilot PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-format.sh
GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/workspaces/crux/crux-pilot PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-dependencies.sh
ZEPHYR_BASE=/workspaces/crux/zephyr GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/workspaces/crux/crux-pilot PATH=/opt/crux-cache/venv/bin:$PATH scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

Results: host Debug build and all 28/28 CTest cases passed; the deterministic host sample scenario passed; formatting and dependency-boundary checks passed. The first Twister invocation lacked `ZEPHYR_BASE` and west could not discover the Twister extension; setting `ZEPHYR_BASE=/workspaces/crux/zephyr` fixed that setup issue. With the variable set, Twister started but both native_sim configurations stopped during CMake configuration, before source compilation or test execution:

```text
ERROR: project zephyr (zephyr): cannot import contents of west.yml; do you
need to run "west update"?
```

This occurred in `zephyr/cmake/modules/zephyr_module.cmake` while running `zephyr/scripts/zephyr_module.py`, at its `Manifest.from_file()` workspace-manifest load. Twister reported 0/2 configurations built, 2 errors, 0 tests executed. The recorded build log is `build/zephyr/twister-time/native_sim_native_64/host_gnu/crux-pilot/tests/zephyr_time/crux.time.zephyr/build.log` (the no-RTC scenario has its corresponding log). `west -vv list` and direct west manifest parsing from the workspace succeeded, so the cause is not yet isolated. No `west update` was run and no speculative retry was made. The previous C++ Ztest revision had passed 4/4 cases in 2/2 scenarios; that historical result does not verify the new C17 test source. M3-05 is therefore pending until the current C17 suite builds and passes.

### Removal of the dedicated production-target C++ smoke (2026-10-17)

Removed `tests/time/src/public_api_link_test.cpp` and the `crux_time_api_link_tests` CMake target. The existing host GoogleTests are themselves C++ consumers: `TimeApiTest.*` and `TimeCalendar.*` include public C headers and call the C17 common implementation, compiled into the deterministic test composition. They already exercise C/C++ linkage for the APIs they call. The dedicated smoke's only distinct coverage was linking those calls through the production `crux::time` target rather than the test composition. The user chose to drop that additional target-specific check; this narrows M2-02 accordingly and does not remove the public headers' C++ linkage guards or C++ support from the API.

The last host run before removal passed 28/28 CTest cases and the host sample scenario; it included the now-removed smoke case. The remaining GoogleTests in that run passed, but no build or test command was run after deleting the separate target. Therefore this record does not claim a post-removal CTest run or a C++ link against the production `crux::time` target. The status table describes the revised M2-02 contract and points to the last run as historical evidence.

## Architecture-boundary cleanup and current verification (2026-10-17)

The platform review resulted in these bounded changes:

- Root CMake starts as a C-only project. `BUILD_TESTING=OFF` disables C++ and GoogleTest; setting `CRUX_BUILD_HOST_SAMPLE=OFF` also omits sample code. This provides a C-only library configure/build path.
- Removed the generic `tests/build_smoke/` fixture because the real C++ Time GoogleTests already compile and call the public C17 API. The per-header compile fixtures and the separate production-target Time link smoke remain removed.
- Added `components/time/sources.cmake` so the Time component owns its short common-source list. Host, host tests and Zephyr include it.
- Kept `components/base` independent. The Zephyr module's `crux_base` exports the status include path only; Zephyr usage requirements enter at the Time target.
- Moved command-handler ownership out of `zephyr/CMakeLists.txt`. The host and Zephyr applications compile the same sample-owned C17 command source.
- Added CMake compatible-interface provider identity to host and Zephyr backend targets. A temporary consumer linking `crux::time` and a second provider configured with a conflicting identity failed CMake generation: `INTERFACE_CRUX_TIME_BACKEND ... does not agree with ... CRUX_TIME_BACKEND`.
- Expanded public Time API comments to state UTC units/range, session initialization, output behavior, status conditions and serialization/ISR limits. The Shell adapter lets argument counts reach the shared handler; Ztest now invokes the registered Shell command through Zephyr's dummy backend in both RTC and no-RTC configurations.
- Standardized the CLI spelling to `ERR NOT_SET`. Readiness output now says `composition=host-simulation` or `composition=zephyr`; these are composition labels, not unique build identities. A source/configuration identity and firmware checksum manifest remain unimplemented, so M5-04 is pending.
- Removed the stale deleted-source reference from `scripts/analyze-host.sh`. `.clang-tidy` now analyzes owned component/sample/test headers. It disables the checker that flags bounded `memcpy`/`snprintf` use and the enum-size check that cannot be applied to a C17 enum without changing its representation; other selected checks remain errors.

The following checks ran inside the existing Linux/amd64 Dev Container at
`/workspaces/crux/crux-pilot`, using CMake 3.28.4, Ninja 1.11.1, Clang 18.1.3, west 1.4.0,
Zephyr v4.4.0 and the pinned cache at `/opt/crux-cache`.

| Command | Actual result |
| --- | --- |
| `scripts/check-host.sh` | Pass. Debug configured/rebuilt, CTest passed 26/26, and the host stdin/stdout scenario passed with `ERR NOT_SET` and the composition marker. |
| `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh` | Pass. ASan/UBSan configured/rebuilt, CTest passed 26/26, and the host sample scenario passed. |
| `cmake -S . -B build/linux/library-only-final -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF -DCRUX_BUILD_HOST_SAMPLE=OFF` | Pass. Configure identified only Clang C; it did not configure C++, GoogleTest or the sample. |
| `cmake --build build/linux/library-only-final --target crux_time_common crux_time_host` | Pass; both C17 static libraries built. |
| `scripts/check-format.sh` with `CLANG_FORMAT=clang-format` | Pass. |
| `scripts/check-dependencies.sh` | Pass. It checks base independence, native-header isolation, private test controls, backend identity declarations, shared source-list use and sample/module ownership. |
| `scripts/analyze-host.sh` | Pass. Clang-Tidy processed host implementation, command code and host C++ tests; no enabled owned-code diagnostic remained. It reported 83,066 generated warnings, mostly suppressed dependency/header diagnostics. |
| `ctest --show-only=json-v1` in `build/linux/debug` | Pass. All 26 discovered host tests have `TIMEOUT=30` seconds; this is execution timeout, separate from GoogleTest discovery timeout. |
| CMake negative consumer probe for backend selection | Pass. CMake generation rejected two different `CRUX_TIME_BACKEND` interface values before linking. The probe lived under `/tmp` and is not a repository test target. |

### Zephyr verification blocker

Historical failure: the root cause was subsequently isolated to Git trust configuration under the
manual build user. See [the resolution below](#zephyr-manifest-blocker-resolution-2026-10-17).

The updated Twister tests and Shell application were not compiled because west manifest loading failed
before Crux module loading. The exact command was:

```sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

It ran with `ZEPHYR_BASE=/workspaces/crux/zephyr`, `CRUX_WORKSPACE=/workspaces/crux`,
`CRUX_CACHE_DIR=/opt/crux-cache`, and
`ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1`. The first run used Twister's default
eight jobs. To match the container's configured two CPUs, the script was changed to run
`west twister -j 2` and executed again. Both attempts failed before source compilation:

```text
CMake Error at /workspaces/crux/zephyr/cmake/modules/zephyr_module.cmake:73 (message):
  ERROR: project zephyr (zephyr): cannot import contents of west.yml; do you
  need to run "west update"?
```

Twister reported 0/2 configurations built and 0 tests executed; six test-case records were marked
errored. Logs are under `build/zephyr/twister-time/native_sim_native_64/host_gnu/crux-pilot/tests/zephyr_time/`
in `crux.time.zephyr/` and `crux.time.zephyr_missing_rtc/`.

The Zephyr source involved is `zephyr/cmake/modules/zephyr_module.cmake:73`, which invokes
`zephyr/scripts/zephyr_module.py:735` and `Manifest.from_file()`. Workspace inputs are
`../.west/config` and this repository's `west.yml`; the Crux caller is
`tests/zephyr_time/CMakeLists.txt`. Read-only diagnostics passed: `west topdir` returned
`/workspaces/crux`; `west list` showed Zephyr and the three pinned modules; `west manifest --resolve`
loaded the same manifest; and direct `zephyr_module.py` execution with
`--zephyr-base=/workspaces/crux/zephyr` and `--extra-modules=/workspaces/crux/crux-pilot` returned
0. Reducing Twister from eight jobs to two did not resolve the CMake failure. No `west update` or
further speculative retry was run. The cause remains unresolved, so M3-01, M3-03, M3-04, M3-05,
M3-07 and M5-01 remain pending for current Zephyr source. Earlier EVKB and native_sim binaries are
historical and stale after these source changes.

The checkout still has no commit and all project files are untracked. The readiness marker identifies
only composition, and no content-addressed source/configuration manifest is tied to a firmware
checksum. M5-04 remains pending. No board, probe, UART, flash or reset was used; no remote was
created and nothing was pushed.

## Zephyr manifest blocker resolution (2026-10-17)

The apparent CMake-only manifest error was an execution-user/Git trust problem. The earlier manual
commands used root with a per-process `safe.directory` entry for `/workspaces/crux/crux-pilot` only.
The Python child process reading the imported Zephyr manifest failed Git's ownership check for
`/workspaces/crux/zephyr`. West's `Manifest.from_file()` surfaced that as the generic
`cannot import contents of west.yml` error; the manifest and pinned revision were present.

A controlled diagnostic ran `subprocess.run(['git', 'show', 'manifest-rev:west.yml'],
capture_output=True, text=True)` and then `Manifest.from_file()` from `/workspaces/crux/zephyr`.
With the earlier one-entry Git environment, Git returned 128 with:

```text
fatal: detected dubious ownership in repository at '/workspaces/crux/zephyr'
```

Adding the exact Zephyr path as a second per-process `safe.directory` entry made the same Git call
return 0 and the same manifest load succeed. The before/after transcripts are retained in ignored
`build/evidence/zephyr-manifest-before.log` and `build/evidence/zephyr-manifest-after.log`.
The previous successful CLI/direct probes were insufficient to establish that the failing Python
child had the same effective Git trust setup. Reducing Twister concurrency was unrelated.

The supported Dev Container already sets `remoteUser` to `vscode` and its post-create command
configures the exact Crux/Zephyr/CMSIS/CMSIS 6/NXP Git paths for that user. No wildcard trust,
upstream source patch, manifest revision change, dependency fetch, or new installation was needed.
The resolution was to execute verification as that configured user. README now documents this
requirement for manual Docker execution. The current validation command is:

```sh
docker exec --user vscode --workdir /workspaces/crux/crux-pilot \
  --env PATH=/opt/crux-cache/venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  --env ZEPHYR_BASE=/workspaces/crux/zephyr \
  --env CRUX_WORKSPACE=/workspaces/crux --env CRUX_CACHE_DIR=/opt/crux-cache \
  1d7f83b19856 scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

The inherited SDK setting is `/opt/crux-cache/sdk/zephyr-sdk-1.0.1`. Both Twister configurations
passed manifest loading and CMake generation under `vscode`, demonstrating that the blocker is
resolved. Twister then passed 2/2 configurations and 4/4 executed test cases, with zero failures,
errors or runtime filters. Reports are `build/zephyr/twister-time/twister_report.xml` and
`twister.json`; each scenario's `handler.log` records the executed Ztests, including actual Shell
argument rejection in the RTC and no-RTC cases. The native_sim Shell sample also built successfully.

An additional native sample execution used:

```sh
build/zephyr/native_sim_time/zephyr/zephyr.exe -uart_stdinout -rt
```

It received `time get`, `time set 2024-01-01T00:00:00 extra`,
`time set 2038-01-19T03:14:08`, then `time get` over stdin. The transcript in
`build/evidence/zephyr-native-shell.log` shows the readiness marker, `ERR NOT_SET`,
`ERR INVALID_ARGUMENT`, `OK`, then `2038-01-19T03:14:09 UTC`. The simulator stopped at 1.210 s;
the one-second RTC advancement is expected. An initial ad hoc check incorrectly expected the
read-back timestamp to equal the set value exactly and exited 1. Rechecking the same retained log
with elapsed time allowed passed; the sample was not rerun and production code was not changed.
An earlier guessed argument `-uart0_stdinout` was rejected; `--help` identified the actual
`-uart_stdinout` option used above. These were diagnostic invocation/oracle errors, not Zephyr
manifest or Crux runtime failures.

The full `scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'` invocation completed with exit 0.
Its Twister output directory was new (the prior output was retained as `twister-time.9`); the
sample and target builds used `west build -p always`.

| Check | Actual result |
| --- | --- |
| C17 native_sim Ztests, RTC and missing RTC | Pass: 2/2 configurations, 4/4 executed cases; no failures, errors or skipped cases. |
| native_sim Shell sample build | Pass; current sample/module composition compiled and linked. |
| native_sim sample runtime | Pass from the retained transcript: ready, initial unset, syntax rejection, set and progressing read-back. |
| EVKB QSPI build | Pass: `mimxrt1060_evk@B//qspi`, revision B, `mimxrt1062/qspi`, current sample compiled and linked. |
| C17 compilation policy | Both sample compilation databases contain six owned `.c` translation units, all with `-std=c17`. |
| Target configuration | `CONFIG_STD_C17=y`, `CONFIG_RTC_INIT_PRIORITY=61`, `CONFIG_RTC_SHELL` disabled. |
| Target size | Linker usage: FLASH 65,976 B; RAM 8,448 B. Symbol-based `rom_report` and `ram_report` totals are 65,964 B and 8,416 B respectively; these are distinct accounting views. |
| Dependency boundary check | `scripts/check-dependencies.sh` passed under `vscode`. |

The existing target script generated firmware/configuration/DTS/map/stat files and checksums under
`build/zephyr/mimxrt1060_evk_B_qspi/zephyr/`. Current SHA-256 values are:

```text
zephyr.bin  904f5a3480cc8a689d4710ef99787f0c03dcb178ae66f1581ce424e19a61dda6
zephyr.elf  20f6c93d2d0e30ff11fa310d127b2afda5a7e8e598b3ef9aa21dbe7b6e40a4fc
```

For complete report retention, the report-only targets were repeated after the successful build
as `vscode`, without source changes:

```sh
cmake --build build/zephyr/mimxrt1060_evk_B_qspi --target rom_report > build/evidence/zephyr-rom-report.log 2>&1
cmake --build build/zephyr/mimxrt1060_evk_B_qspi --target ram_report > build/evidence/zephyr-ram-report.log 2>&1
```

`build/evidence/zephyr-build-output-tail.log` retains partial captured build stdout; the two report
logs above retain the complete report output. Twister retains its full build/runtime logs.
At the time this Zephyr blocker resolution was recorded, external CI (M5-03) and build provenance
(M5-04) were deferred. The current state and subsequent evidence for those items are recorded in
[CI and build-artifact provenance](#ci-and-build-artifact-provenance-2026-10-17). M4 physical work
remains pending. No remote was created or pushed, and no board, debugger, UART device, flash or reset
operation was used.

## CI and build-artifact provenance (2026-10-17)

### CI workflows and limits

`.github/workflows/host.yml` (`host-foundation`) runs on push, pull request, or manual dispatch. It
installs pinned host tools and runs host configure/build/CTest, format, dependency-boundary, and
Clang-Tidy checks. Host jobs do not set up Zephyr or its SDK. Its CTest JUnit XML and raw test log
are uploaded for 14 days, including after a failed check when those files exist.

`.github/workflows/zephyr.yml` (`zephyr-software`) has the same triggers and separately installs
the pinned west/Python tools, Zephyr workspace inputs and SDK. It runs the two native_sim Twister
scenarios, builds the native_sim Shell sample, then cross-builds `mimxrt1060_evk@B//qspi`. It does
not flash or reset a board. The workflow uploads the Twister reports and per-scenario raw logs, the
EVKB `.config`, DTS, map/stat and ROM/RAM reports, firmware ELF/BIN, build logs and
`provenance.json`, with 14-day retention. Hidden files are enabled for the generated `.config`.
Both workflows request only `contents: read`; no repository secrets are used by these jobs.

Both workflows pass `actionlint` v1.7.12, as well as JSON, shell-syntax and Python-syntax checks.
Actionlint initially rejected `runner.temp` in job-level `env` blocks. Those paths now use the
workspace parent, outside the checked-out repository; the Zephyr Python tool venv uses the same
location. The official macOS ARM64 actionlint release archive was SHA-256 verified before use.
The workflow lint command was `actionlint .github/workflows/host.yml .github/workflows/zephyr.yml`;
it exited 0 after that correction.
Local equivalents for host debug/sanitizer builds and tests, format,
dependency checks, analysis, native_sim/Twister and EVKB builds passed as recorded in the preceding
sections. GitHub Actions has **not** been verified in this evidence set. The current checkout has a
configured `origin` and local `main` commit, but no remote Actions run has been verified here.
M5-03 remains pending until the current workflow runs and artifacts are reviewed. M1-06's local
failure-propagation and no-implicit-hardware criteria remain independently verified.

To trigger the workflows, push the reviewed `main` branch to the configured GitHub `origin`. A push
starts both workflows; later runs can be started from **Actions → host-foundation/zephyr-software →
Run workflow**. GitHub CLI authentication (`gh auth login`) or the user's configured SSH/Git
credential flow must be completed on the Mac. Credentials should remain local; no token needs to be
added as a workflow secret. If Actions are disabled by repository policy, enable them in the
repository's Actions settings.

### Target build provenance

`scripts/build-target.sh 'mimxrt1060_evk@B//qspi'` now captures the full EVKB build, ROM report and
RAM report logs under ignored `build/evidence/`, then writes
`build/zephyr/mimxrt1060_evk_B_qspi/provenance.json`. It records:

- a SHA-256 snapshot over repository file paths, modes and contents, plus Git commit/branch/dirty
  state and a hash of `west.yml`;
- all resolved west project revisions and `.west/config` hash;
- CMake, Ninja, west, compiler and Python identity, Dev Container recipe/package inventory, the
  runtime image ID when available, and GitHub runner/run URL metadata when running in Actions;
- the selected target, generated `.config` and DTS hashes, ROM/RAM reports, ELF/BIN and all
  required configuration/report files with sizes and SHA-256 values;
- Twister test totals and hashes/sizes for the reports and per-test build/handler logs.

The deterministic `build_id` is derived from the source/build identity and firmware/configuration
outputs; timestamp, CI run number and raw test-log contents are retained as metadata but do not
change that identity. The manifest itself is a CI artifact rather than a repository file, so its
own changing digest does not create a recursive source fingerprint.

The fresh local validation used the Linux Dev Container as user `vscode`, with the documented
parent workspace mounted at `/workspaces/crux` and repository at
`/workspaces/crux/crux-pilot`:

```sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

That run completed the fresh EVKB target build at 198/198 steps, emitted ROM/RAM reports, and
generated the provenance manifest. The existing clean Twister run for the same production/test
sources had 2/2 configurations and 4/4 test cases, with zero failures, errors, or skips. The final
manifest reports 11 target/build evidence files and 8 Twister report/log files; independent local
verification recomputed every recorded file size and SHA-256 with no mismatch. It records the
container image ID `sha256:c42936535e1b5a253afbeba80f1708208f7014aec63d76ff7a29e834fc785654`, all
west revisions, and the local `HEAD` at build time. Exact ELF/BIN hashes and sizes are in the
generated manifest. Later documentation edits make the current worktree dirty; they do not change
the recorded image, but the manifest continues to describe the exact source snapshot at build time.
No hardware flash was performed.

| Acceptance criterion | Status | Evidence / remaining action |
| --- | --- | --- |
| M1-06: local commands and CI fail on actual check failures; target build does not invoke hardware | pass locally; GitHub CI pending | `set -euo pipefail`, a synthetic CMake exit-37 propagation probe, non-flashing target script, local workflow parsing and the local checks above. External CI execution is tracked separately as M5-03. |
| M5-03: CI covers format, forbidden dependencies, host build/tests/analysis, native_sim tests and target build | pending | Workflow definitions and local equivalents are verified. `origin` is configured; authenticated access to it was unavailable to `git ls-remote` (`could not read Username for 'https://github.com': Device not configured`). Review both remote workflow results and artifacts when authentication is available. |
| M5-04: record source/module/tool/image/config/size/checksum/report/log provenance | pass | `build/zephyr/mimxrt1060_evk_B_qspi/provenance.json`; all 19 referenced artifact and test-report file hashes/sizes independently matched. A future clean GitHub run will add its commit SHA and runner/run metadata. |

## M4 VS Code download failure and fix (2026-10-17)

### Reproduced failure and cause

Used the already confirmed EVKB revision B, SW4 OFF–OFF–ON–OFF, no HyperFlash rework,
J-Link PLUS S/N `602003106`, SWD 400 kHz, and 3.34 V target voltage. No switch or jumper was
changed. Started `scripts/macos/start-jlink-gdbserver.command` on macOS and replayed the repository's
launch configuration through the installed Microsoft C/C++ 1.34.4 `OpenDebugAD7` adapter inside the
Dev Container as `vscode`. The adapter used Zephyr SDK 1.0.1 GDB 16.2. A temporary DAP client was
used only to capture diagnostic requests/responses; no test framework was added to the repository.

The original profile's `customLaunchSetupCommands` replaced the normal ELF loading and architecture
detection. Its `program` property therefore did not cause GDB to load the file. The actual MI trace
in `build/evidence/dap-before.jsonl` and `dap-before-console.log` shows:

```text
-target-select remote host.docker.internal:2331
warning: No executable has been specified ...
-interpreter-exec console "monitor reset"
Resetting target
-target-download
^error,msg="No executable file specified.\nUse the \"file\" or \"exec-file\" command."
-gdb-exit
```

The DAP `launch` response was `success:false`. There were no section-download messages in the
server log for that failed attempt. Reset succeeded; the next download command failed locally in
GDB because no executable had been selected. GDB then exited and closed the client socket. The
J-Link server remained alive and accepted later clients without restarting. Thus this reproduced
failure was a debugger setup error, rather than an observed QSPI programming failure.

Microsoft documents that custom launch commands replace the default launch sequence; its
[MIEngine source](https://github.com/microsoft/MIEngine/blob/main/src/MIDebugEngine/Engine.Impl/DebuggedProcess.cs)
also shows the default `-file-exec-and-symbols`, architecture detection, remote connection and
post-connection commands. Reference: [C/C++ launch configuration](https://code.visualstudio.com/docs/cpp/launch-json-reference).

### Fix and actual verification

The firmware profile now uses `miDebuggerServerAddress` and `postRemoteConnectCommands`, preserving
default ELF loading and architecture detection. Only reset and download are added after the remote
connection. `stopAtEntry:true` pauses at `z_arm_reset` after programming, before application code.
Set a normal source breakpoint at `samples/time/src/main.c:7`, then continue to reach `main`.
The deprecated architecture override is no longer needed for this profile. The attach profile
explicitly loads the selected ELF's symbols and uses `stopAtConnect:true`; it sends no reset or
image-download command. A focused attach check without `stopAtConnect` auto-continued and timed
out waiting for a stop; the corrected attach check stopped and returned a valid stack trace.

| Check | Actual result |
| --- | --- |
| First focused ELF-loading correction | Adapter download succeeded: MI `^done` with `load-size="65972"`; source breakpoint at `main.c:7` was reached. Logs: `build/evidence/dap-after.jsonl` and `dap-after-console.log`. |
| Final standard remote profile, without an injected source breakpoint | Adapter launch succeeded and paused at `z_arm_reset`, `reset.S:147`, PC `0x600058f0`; stack trace succeeded. Logs: `dap-standard.jsonl` and `dap-standard-console.log`. |
| Corrected attach profile | Launch stopped on connection and returned a stack trace through `arch_cpu_idle`; clean detach succeeded. Logs: `dap-attach-fixed.jsonl` and `dap-attach-fixed-console.log`. The earlier auto-continue/timeout is retained in `dap-attach.jsonl`. |
| Actual VS Code F5, final profile | `Build firmware` completed with `ninja: no work to do`; download completed and the editor paused at `reset.S:147`. F5 continued to the real source breakpoint at `main.c:7`, displayed as **Paused on breakpoint**. F10 advanced to `main.c:9`, displayed as **Paused on step**. This was observed in the actual VS Code UI, not inferred from the CLI probe. |
| Read-only comparison through the active VS Code Debug Console | `-exec compare-sections rom_start` reported `0x60000000 -- 0x600022c0: matched`; `-exec compare-sections text` reported `0x600022c0 -- 0x6000c1d8: matched`. |
| Server lifetime | The same macOS server process accepted the failed launch, successful downloads, detach and attach checks, and the final UI session. No server restart was needed between them. |

The final UI session remains paused at `main.c:9`, with the host J-Link GDB Server running so the
user can continue debugging. No RTC timestamp was set and no UART acceptance sequence was run.
The earlier screenshot's fatal `Failed to open remote socket` server exit was not reproduced.
Its cause remains unknown; it must not be equated with the client-close message reproduced above.
The historical `exited with code 42` message was also not reproduced and is not evidence that the
firmware returned 42. The actual reproduced launch failure and successful final path are recorded
independently of those older symptoms.

### Reproduction, build delay and retained evidence

From macOS, start the launcher above. In the Dev Container, use the firmware debug profile in
VS Code, set the source breakpoint at `main.c:7`, press F5 to download, then F5 again from the
startup entry point. Press F10 at `main` to step. For a direct build-only check:

```sh
PATH=/opt/crux-cache/venv/bin:$PATH scripts/build-debug-target.sh 'mimxrt1060_evk@B//qspi'
```

During UI verification, the previous script forced a CMake reconfiguration on every invocation
because it always passed extra CMake arguments to west. It now supplies those arguments only when
creating the Ninja build system. Subsequent builds rely on Ninja's dependency tracking. The
unchanged build returned `ninja: no work to do` in 2.065 seconds, with no reconfiguration; the same
result was then observed in the actual F5 task. Raw output: `build/evidence/debug-incremental-build.log`.
The first-build configuration command is unchanged. Shell syntax, launch JSON parsing and
`git diff --check` passed. Production C sources were unchanged, so component unit tests were not
repeated for this debugger/script correction.

The combined host server transcript is retained as `build/evidence/jlink-download-session.log`;
`jlink-server-before.log` is the original live capture spanning all attempts, despite its filename.
The exact original launch request is in `launch-before.json`, the verified configuration in
`launch-verified.json`, and the temporary protocol client in `replay-cppdbg.py`, all beneath ignored
`build/evidence/`. Adapter diagnostics can be repeated from the repository on macOS with:

```sh
python3 build/evidence/replay-cppdbg.py standard .vscode/launch.json
python3 build/evidence/replay-cppdbg.py attach-fixed .vscode/launch.json
```

These diagnostic commands connect to hardware; run them only after ending any active VS Code
session, with the server already listening. The retained client names the current container and
installed extension path; update those for a new container/extension installation. The original
failed request can be replayed with `launch-before.json`, but it resets the target and reproduces
the invalid download, so normal use should keep the corrected profile.

Firmware checksums remained unchanged through the build and UI verification:

```text
zephyr.elf  20f6c93d2d0e30ff11fa310d127b2afda5a7e8e598b3ef9aa21dbe7b6e40a4fc
zephyr.bin  904f5a3480cc8a689d4710ef99787f0c03dcb178ae66f1581ce424e19a61dda6
```

M4-01 remains **pass** with additional real VS Code evidence. M4-02 through M4-08 remain **pending**
for their UART/RTC/timing/reset-session criteria; source stepping and flash comparison do not
substitute for those tests.


## First-push baseline preparation (2026-10-17)

The architecture review found that F5 rebuilt the ELF/BIN while leaving the older
`provenance.json` beside them. The reviewed ELF changed from `20f6c93d…` to `52f9c6ea…`;
the manifest still recorded the former. This was an actual hash mismatch, not a firmware
execution failure. It supersedes the earlier assumption that one successful manifest generation
ensures later development outputs remain associated with that verification.

The corrected development build and VS Code clean task invalidate prior provenance before
changing outputs, including on failure. Pristine verification captures source/workspace inputs
before building and refuses changed inputs afterwards. Dependency records contain actual Git
HEADs and clean state as well as pinned revisions; Finder `.DS_Store` metadata is excluded without
deleting it. Unrelated dirty source fingerprints are labelled not recoverable from Git. Accepted
source baselines use a clean local commit. An input fingerprint is not a stored source archive.
Test reports are linked only when an explicitly supplied successful test-input record matches
those build inputs; a standalone target build does not silently attach an old Twister report.

The shared command handler no longer owns the host-only banner. The host process owns its
simulation explanation; the Zephyr sample prints `Crux Time sample`. The SNVS RTC priority is
in `samples/time/boards/mimxrt1060_evk_mimxrt1062_qspi.conf`, rather than the common application
configuration. Public APIs and capability semantics are unchanged. Repository/module identities
remain appropriate to the current checkout; these changes add no platform-wide framework.

The user reported that all guided UART steps worked: help, initial unset, valid set/get,
progression, invalid-date rejection and subsequent reads. This is user-reported manual evidence,
not a captured transaction report. Precise timing, post-2038/upper-range cases, reset-session
behavior and the planned exclusive-UART pytest suite remain pending. Historical logs above are
preserved; current summaries now acknowledge both successful download/debugging and this manual
report. No additional board operation is needed to verify these software fixes.

Initial safeguard regression checks passed before the full verification run. Commands and final
results for the committed baseline will be recorded below. Adding a new board fragment to an
existing Zephyr cache does not update its cached `CONF_FILE` list; a pristine configure is required
once. Subsequent F5 launches reuse that configuration and remain incremental.

The cached incremental diagnostic was stopped after confirming that `.config` still contained
`CONFIG_RTC_INIT_PRIORITY=50` and the log merged only `prj.conf`. It was not a completed build or
a hardware failure. The full verification run below uses a pristine configure and must show the
board fragment merged with priority 61. Safeguard regression tests passed 4/4, including failed
debug and clean commands returning the injected exit status 37 with the old manifest removed.


### Committed software verification results

The full run used clean source commit `bbf3d08af51c2c2e1fa0ae18d7315bac4da06a75`
(76 tracked files), source fingerprint
`cf549c68d36bc1de7422426dc73ad3168037414cb24d5ec5184ec7062b405bed`.
The runtime image was inspected from Docker and recorded as
`sha256:16f67fc22a7900e393c0cb2787d79c83f43c8aa505e911540d4c8e7c94c9eed5`.
Host builds were recreated from removed generated `build/linux/debug` and
`build/linux/sanitize` directories; upstream dependency caches were retained.
Twister created fresh outputs, and both sample builds used pristine configurations.
All commands ran as `vscode` with the parent workspace at `/workspaces/crux`.

Reproducible commands inside the pinned Linux container:

```sh
export PATH=/opt/crux-cache/venv/bin:$PATH
export CMAKE_BUILD_PARALLEL_LEVEL=2
python3 -m unittest discover -s tests/build -v
scripts/check-format.sh
scripts/check-dependencies.sh
# Remove only generated host outputs to reproduce this clean run.
rm -rf build/linux/debug build/linux/sanitize
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
scripts/analyze-host.sh
cmake -S . -B build/verification-libraries -G Ninja \
  -DBUILD_TESTING=OFF -DCRUX_BUILD_HOST_SAMPLE=OFF
cmake --build build/verification-libraries
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

| Check | Actual result | Retained output (ignored `build/evidence/`) |
| --- | --- | --- |
| Build safeguards | 4/4 tests passed; injected exit 37 propagated and stale manifest removed | `first-push-host.log`, final expanded coverage in `first-push-final-safeguards.log` |
| Host Debug | 26/26 CTest cases and stdin/stdout sample passed | `first-push-host.log` |
| ASan/UBSan | 26/26 CTest cases and sample passed | `first-push-host.log` |
| Format and dependency boundaries | Passed | `first-push-host.log` |
| Configured clang-tidy checks | Exit 0; non-owned diagnostics and existing NOLINT suppressions reported | `first-push-host.log` |
| C-only libraries | Fresh configure/build passed with tests and host sample disabled; no C++ or GoogleTest configured | `first-push-host.log` |
| Twister native_sim | 2/2 configurations, 4/4 cases passed | `first-push-zephyr.log`, `build/zephyr/twister-time/` |
| native_sim sample | Pristine build passed; startup, unset, post-2038 set/read/progression, invalid-date rejection and preserved time passed | `first-push-native-check.log`, `first-push-native-shell.log` |
| EVKB QSPI | Pristine configure/build passed; new board fragment merged and priority 61 confirmed | `first-push-zephyr.log` |
| Workflow lint | Official actionlint 1.7.12 archive checksum verified; both workflows exited 0 | `first-push-workflows.log` |
| Input rejection | Mismatched source and test records each failed and left no manifest | `first-push-provenance-check.log` |
| Incremental reuse | Repeated F5 build reported `ninja: no work to do`; ELF unchanged and prior manifest absent | `first-push-incremental.log` |
| Bundle consistency | All 19 artifact/report sizes and hashes matched; 4 actual dependency HEADs matched clean pinned checkouts | `first-push-hash-verification.log` |

The first post-report incremental diagnostic expected the exact text `no work to do` and failed
that output assertion. It was an oracle limitation: the ELF hash stayed unchanged, and the next
incremental invocation explicitly reported no work. No production fix or timing allowance was
introduced to hide it. The original diagnostic output is retained. Valid provenance was then
regenerated with the matching original input records and the inspected runtime image ID.

The native_sim runtime check launched
`build/zephyr/native_sim_time/zephyr/zephyr.exe -uart_stdinout -rt`, waited up to 10 seconds for
readiness/responses, issued `time get`, set `2038-01-19T03:14:08`, read twice across a 1.2-second
host wait, rejected `2024-02-30T12:00:00`, and confirmed the last date still advanced. Raw output is
retained. This is simulated RTC behavior, not physical SNVS timing acceptance.

For an independent bundle check from the repository root:

```sh
python3 - <<'CHECK'
import hashlib, json
from pathlib import Path
m = json.loads(Path('build/zephyr/mimxrt1060_evk_B_qspi/provenance.json').read_text())
for entry in m['artifacts'] + m['test_artifacts']:
    p = Path(entry['path'])
    assert p.stat().st_size == entry['size_bytes']
    assert hashlib.sha256(p.read_bytes()).hexdigest() == entry['sha256'], p
assert m['source']['recoverable_from_git'] and not m['source']['dirty']
print(m['source']['git_commit'], m['test_summary'])
CHECK
```

The accepted software bundle is tied to the recorded commit above. Later changes only invalidate
old provenance before a full test run replaces reports and expand that failure-path regression,
plus update documentation. Component, sample, module and CMake source contents remain identical
to the fully built commit. That control-flow safeguard is checked independently; its regression
uses a temporary workspace and fake failing tools and never connects to hardware.

Current firmware identity:

```text
ELF 3b75f71bf8b6909a93b6f13b07fdb52e1e217052f31499afc666ae66c68aa84a
BIN 4f8632b29db35cb268bcdc48644da9127ed8d4ed3e09bf10551f14a8dfde764a
```

Linker allocation is FLASH **65,964 B**, RAM **8,448 B**. The symbol reports sum to
65,952 B and 8,416 B respectively; those sums exclude alignment/padding and must not be presented
as the allocated regions. No firmware from this preparation run was flashed or reset on the
physical board. The earlier M4-01 image and user-reported UART checks remain distinct evidence.

### First-push readiness and remaining acceptance

The GitHub connector confirmed `wonny-labs/crux-pilot` is private and empty (the contents API
explicitly returned `This repository is empty`). Local commit history was preserved rather than
rewritten. Newly required build/debug helpers and editor settings are tracked; generated builds,
SDK/cache contents, logs and Finder files remain ignored. Tracked JSON parsed successfully and a
focused private-key/GitHub-token pattern scan found no matches; this is not a general security
certification. First-push preparation observed 19 GiB free, with no user-file cleanup.

M5-01 and corrected M5-04 pass for the software evidence above. Hosted Actions execution (M5-03)
requires the first push and inspection of both workflow runs/artifacts. Formal M4 timing bounds,
range/reset cases and exclusive-UART pytest execution remain pending. These do not prevent an
initial implementation push, and the repository does not claim overall Pilot completion.

Final recorder regeneration used the inspected image identity (supply the actual image ID for a
different container):

```sh
CRUX_BUILD_IMAGE_ID=sha256:16f67fc22a7900e393c0cb2787d79c83f43c8aa505e911540d4c8e7c94c9eed5 \
python3 scripts/record-build-provenance.py \
  --build-dir build/zephyr/mimxrt1060_evk_B_qspi --target 'mimxrt1060_evk@B//qspi' \
  --workspace /workspaces/crux --inputs build/evidence/target-inputs.json \
  --test-inputs build/evidence/zephyr-test-inputs.json
```

The final expanded safeguard run passed 4/4 tests (including both failing build and failing
Twister subcases). `git diff bbf3d08 -- components samples zephyr CMakeLists.txt CMakePresets.json`
was empty: later edits do not change compiled platform/sample sources or their configuration.
The accepted manifest continues to identify `bbf3d08`, and is not relabelled with a later commit.
Local follow-up commits include the control-flow safeguard and this evidence; no remote push
or hosted workflow execution was performed in this preparation task.


## Shared consumer build target (2026-10-17)

The Host and Zephyr application build interfaces now both use
`target_link_libraries(<application> PRIVATE crux::time)`. The existing Host target is
unchanged. The Zephyr module adds the equivalent INTERFACE target and alias, composing
its existing API requirements, common implementation and RTC backend. The Zephyr sample
and Ztests consume it; Ztests retain private includes only for their internal test hooks.
No runtime dispatch, new backend framework, installed package or API behaviour was added.
README and the Time contract describe consumer ownership and migration boundaries.

Verification ran in the existing Linux amd64 Dev Container `46325e2f6fcc`, as `vscode`,
from `/workspaces/crux/crux-pilot`, with `/opt/crux-cache/venv/bin` prepended to PATH and
`CMAKE_BUILD_PARALLEL_LEVEL=2`. Reproduction commands:

```sh
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
scripts/check-format.sh
scripts/check-dependencies.sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
git diff --check
```

| Check | Actual result / retained log under ignored `build/evidence/` |
| --- | --- |
| Host Debug | Pass, 26/26 CTest cases and stdin/stdout sample; `build-interface-host.log` |
| Host ASan/UBSan | Pass, 26/26 and sample; `build-interface-sanitizers-corrected.log` |
| Format / dependency boundaries | Both exit 0; `build-interface-format.log`, `build-interface-dependencies.log` |
| Twister | 2/2 configurations and 4/4 cases, no failures/errors; 155.98 seconds; `build-interface-zephyr.log` |
| native_sim sample | Pristine build passed through `crux::time`; same Zephyr log |
| EVKB QSPI sample | Pristine build, ROM/RAM reports and provenance generation passed; same Zephyr log |
| Separate C application | Configure/build/run exit 0, with only `crux::time` as its link dependency; `build-interface-consumer.log` |
| Whitespace | `git diff --check` exit 0 |

An initial sanitizer invocation used nonexistent preset `host-asan-ubsan` and exited 1
with `CMake Error: No such preset`. The existing preset is `host-sanitize`; correcting
that command produced the passing run above. Original error: `build-interface-sanitizers.log`.
No code change or speculative retry was needed.

The separate C consumer lives only in ignored `build/interface-consumer/`. Its CMake
project sets C17, disables BUILD_TESTING and CRUX_BUILD_HOST_SAMPLE, adds the repository
with `add_subdirectory(../.. crux)`, creates a C executable and links only `crux::time`.
Its source includes `<crux/time.h>` and checks init, set and read-back. Reproduce the
retained temporary fixture with:

```sh
cmake -S build/interface-consumer -B build/interface-consumer/out -G Ninja
cmake --build build/interface-consumer/out -j 2
build/interface-consumer/out/consumer
```

This is a one-time consumer integration probe, not a new maintained smoke target.
The committed Host sample and Zephyr sample exercise the maintained consumer paths.
The probe uses no GoogleTest, C++ compiler or manual Crux include directory.

EVKB ELF SHA-256 remains
`3b75f71bf8b6909a93b6f13b07fdb52e1e217052f31499afc666ae66c68aa84a`;
BIN remains `4f8632b29db35cb268bcdc48644da9127ed8d4ed3e09bf10551f14a8dfde764a`.
Both match the earlier software baseline. This verification used an uncommitted source
snapshot (build identity `839d5f9fde59cfbdfc014abc0de282d734d628e00e95f0af06a092010b1fba11`);
its manifest correctly reports `recoverable_from_git=false`. Subsequent evidence-document
edits are not claimed as inputs to that build. No hardware, reset, flash or remote push
was performed. M4 and hosted CI acceptance remain unchanged; no new blocker was observed.


## Module header and dependency review (2026-10-17)

Retained component-local public headers; adopted contributor rules are in
[CONTRIBUTING](../CONTRIBUTING.md#public-headers-and-module-dependencies), with a concrete
[Time ownership mapping](time-contract.md#header-and-build-ownership). Inspection covered root,
base, Time, sample, test and Zephyr CMake definitions and all owned header/include sites.
Public platform headers are component-owned; private ports, adapter helpers and test controls
are outside public roots. Applications use `crux::time`. No directory/global
`include_directories()` or `zephyr_include_directories()` calls were found in the owned build
files. Private implementation paths are target-scoped. Base remains independent of Time/Zephyr.
The Host command helper's direct private Time public-header path and integration tests' explicit
private paths are internal composition/test wiring; the contract records why they exist.
No implementation violation requiring a code change was identified in this bounded review.

`scripts/check-dependencies.sh` and `git diff --check` both exited 0 on the macOS host.
This follow-up changes documentation only; firmware/Host builds were not repeated. The previous
shared-consumer-target verification remains the build/test evidence, with its recorded dirty-input
limitation. This source review is not proof of all future dependency boundaries; the existing
script checks selected rules, not every possible include or CMake graph.

## Private helper filename review (2026-10-17)

The Zephyr-private helper header was renamed from
`components/time/zephyr/time_port_zephyr_internal.h` to
`components/time/zephyr/time_port_internal.h`. Its `zephyr` directory already defines
its backend scope; retaining `zephyr` in this private helper's basename was redundant.
The include guard and the Zephyr adapter/test references were updated together.
The Zephyr-required application board configuration name
`samples/time/boards/mimxrt1060_evk_mimxrt1062_qspi.conf` was retained because its board,
SoC qualifier and flash variant identify the configuration Zephyr automatically selects.

From the Linux Dev Container, `scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'`
passed: Twister 2/2 configurations and 4/4 cases, native_sim sample build, and EVKB QSPI
sample cross-build with ROM/RAM reports. The ELF and BIN hashes match the prior software
baseline. Full output: ignored `build/evidence/final-zephyr-rename.log`. The run used dirty
working-tree inputs and its provenance correctly does not claim Git recoverability. No
hardware operation occurred. `scripts/check-dependencies.sh` and `git diff --check` also
passed after the rename.

The post-rename formatting check could not run. Exact command:

```sh
docker exec -u vscode -w /workspaces/crux/crux-pilot 46325e2f6fcc \
  bash -lc 'export PATH=/opt/crux-cache/venv/bin:$PATH; scripts/check-format.sh'
```

Actual result: exit 1, `No such file or directory`. A second focused invocation checked
`command -v clang-format-18` before running the same script and produced the same error;
the failure was initially attributed to a missing formatter. That diagnosis was incorrect;
see the resolution below. No further retry was made at that point. The affected
owned sources are `components/time/zephyr/time_port_zephyr.c`,
`components/time/zephyr/time_port_internal.h`, and `tests/zephyr_time/src/main.c`; the changed
C/C++ lines only update the private header include/guard. `git diff --check` passes. The
repository format script passed before this filename-only C/C++ edit; the post-rename format
result was unavailable at that point and is resolved below.


## Format check after unstaged renames (2026-10-17)

The missing-tool diagnosis above is corrected. The Dev Container intentionally installs
`clang-format==18.1.8` in `/opt/crux-cache/venv`, sets `CLANG_FORMAT=clang-format`, and
prepends that environment to the VS Code remote PATH. CI uses the same pinned Python
package. `clang-format-18` is not the configured executable. No installation change is needed.
`/opt/crux-cache/venv/bin/clang-format --version` reports `clang-format version 18.1.8`.

The actual failure was an unstaged header rename: `git ls-files --cached --others` returned
both the tracked old pathname (already absent on disk) and the new untracked header. The
formatter received a nonexistent input and returned exit 1, `No such file or directory`.
This was reproduced with the previous script in a temporary Git repository. The check passed
on the committed repository even before the script fix, confirming the tool was installed.

`scripts/check-format.sh` now filters out nonexistent file paths while retaining existing
tracked and untracked sources. In a temporary Git repository, the corrected script passed an
existing tracked header, an unstaged header rename and a deleted-only header; it rejected a
malformed untracked replacement with exit 1. No permanent test fixture was added. Actual log:
ignored `build/evidence/format-rename-fix.log`.

The exact Dev Container command recorded above now exits 0 for the whole repository.
`bash -n scripts/check-format.sh` and `git diff --check` also pass. The environment remains
pinned to clang-format 18.1.8; the earlier format-check blocker is resolved. Firmware and
application code did not change in this follow-up, so their builds were not repeated.


## Final source review before publication (2026-10-17)

The review covered the public C headers, Time state/error transitions, bounded Gregorian
conversion, host/Zephyr backends, shared command parser, stdin/Shell adapters, CMake targets,
Dev Container, build/provenance scripts, test cases, and both CI workflows against the adopted
M0–M3 and software M5 goals. It did not operate hardware or generalise the Time constraints.

One correctness issue was fixed in test-only code: injecting `CRUX_OK` as a one-shot host read
status returned success without writing the output, allowing common code to consume an
uninitialised value. Successful status injection now follows the normal stored-value read;
error injection and production backend behaviour are unchanged. A focused GoogleTest checks
that a successful injected read returns the stored value. The Time contract's state count
was also corrected from two to three; its semantics did not change.

Reproduction in the existing Linux amd64 Dev Container, as `vscode`, from the repository root,
with `/opt/crux-cache/venv/bin` on PATH and `CMAKE_BUILD_PARALLEL_LEVEL=2`:

```sh
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
scripts/check-format.sh
scripts/check-dependencies.sh
python3 -m unittest discover -s tests/build -v
scripts/analyze-host.sh
```

Both host configurations passed 27/27 cases and the stdin/stdout sample. All other commands
exited 0; provenance checks passed 4/4. Clang-tidy passed the configured owned-code policy;
its log still reports suppressed upstream/non-owned warnings and existing suppressions.
Logs are retained under ignored `build/evidence/final-review-*.log`.
The preceding private-header rename verification passed native_sim 4/4 and pristine native_sim/
EVKB sample builds. This follow-up changes the Host test injection path only, so Zephyr was
not rebuilt again. It does not claim a newly generated clean-commit firmware bundle.
`git diff --check`, Git object integrity and focused credential-pattern scans passed; no
tracked build/cache/binary output or matching credential material was identified. Such a
pattern scan is not an exhaustive secret audit.

The reviewed structure meets the current portable Time slice: independent C17 implementation,
C++ consumers through actual tests, private native types, shared command logic and public
`crux::time` build usage. No unresolved source finding blocks an initial private push.
Single-instance operation, caller serialisation, UTC seconds/range and session-only validity
remain explicit limitations. Physical M4 evidence and actual hosted CI execution remain
pending; passing software tests do not replace them.

Publication transport was checked independently. The connected GitHub app confirms private
`wonny-labs/crux-pilot` and push permission, but local HTTPS Git initially failed with
`could not read Username ... terminal prompts disabled`. SSH, after using GitHub-published
host keys in an ignored temporary known-hosts file, failed with `Permission denied (publickey)`.
The official GitHub CLI 2.102.0 macOS archive was downloaded into ignored `build/tools/` and
verified against its release checksum to support browser/device authentication without a
system package installation. Authentication and push results are recorded separately when
actually observed; no token or private key is added to this repository.


## First publication and hosted CI (2026-10-17)

After explicit user authorization and completed browser authentication, the original local
Git history was pushed to private `https://github.com/wonny-labs/crux-pilot.git` as `main`.
The remote was empty beforehand. Push succeeded without force and set `origin/main` as
upstream. Both local and GitHub API reported source SHA
`df48e61fe96e120d1e911c594b13214053fcd6c1`. Authentication uses the macOS keyring; no secret
was printed or committed. A command-scoped Git credential helper used the temporary official
GitHub CLI; no global helper path pointing into ignored build output was installed.

Both initial workflows completed successfully for exactly that source SHA:

| Run | Actual result | Artifact |
| --- | --- | --- |
| [host-foundation 39064900805](https://github.com/wonny-labs/crux-pilot/actions/runs/39064900805) | Success: build, 27/27 CTest and sample, dependency-disabled host repeat, 4 provenance checks, format, configured analysis, boundaries | `host-39064900805-1`, 4,431 bytes |
| [zephyr-software 39064901047](https://github.com/wonny-labs/crux-pilot/actions/runs/39064901047) | Success: pinned workspace/SDK installation, native_sim 2/2 configurations and 4/4 cases, native_sim and EVKB sample builds, provenance/reports | `zephyr-39064901047-1`, 669,970 bytes |

Using `build/tools/github-cli/gh`, commands were `run view <run-id> --repo
wonny-labs/crux-pilot --json status,conclusion,headSha,url` and `run download <run-id>
--repo wonny-labs/crux-pilot --dir <ignored-evidence-directory>`. Retained metadata and
unpacked bundles are under ignored `build/evidence/github-df48e61/`.

The downloaded Host JUnit reports 27 tests and zero failures/skips. The downloaded Zephyr
manifest identifies clean source `df48e61`, `recoverable_from_git=true`, four pinned/clean
west dependencies, Ubuntu runner image `ubuntu24` / `20261012.334.1`, and four passing tests
with zero failures/errors/skips. All 19 recorded artifact/report sizes and SHA-256 values
were recomputed from the downloaded files and matched. The artifact path prefix `build/`
is removed when resolving paths within the uploaded bundle's common root.

Hosted build identity:
`3b7d3602bb972f374a91912940421892260772c61cbd67db58d6886b65280ceb`.
Hosted ELF SHA-256:
`362ccac5f2d2e964eeec1abab9bb01a828ab6a495e1795c92ce4d6666a9ab889`.
Hosted BIN SHA-256:
`4f8632b29db35cb268bcdc48644da9127ed8d4ed3e09bf10551f14a8dfde764a`.
The BIN matches the earlier local build; the ELF differs, so this is not a claim of
bit-identical ELF reproduction. Linker allocation remains FLASH 65,964 B / RAM 8,448 B.
Neither hosted firmware nor this review was flashed to hardware.

M5-03 is now pass. M4 physical timing/range/reset/raw-log acceptance remains pending.
This publication-evidence follow-up changes documentation only; its later commit is not
claimed as the source of the already identified firmware bundle above.

## M4 physical UART and RTC acceptance (2026-10-17)

Used the previously confirmed MIMXRT1060-EVKB PCB revision B, SW4 OFF–OFF–ON–OFF,
without HyperFlash rework; external J-Link PLUS S/N 602003106 on J2, with J9/J10 isolation
as previously confirmed. USB discovery again identified that probe and the DAPLink device.
UART was `/dev/cu.usbmodem11102` via J41, 115200 baud. Commander V9.86a identified the
Cortex-M7 at 400 kHz SWD and VTref 3.322 V; the GDB Server reported about 3.33 V.
No fuse, lock, power-control or unsupported erase operation was performed.

### Identified production sample and explicit download

The image came from the successful hosted `df48e61` bundle, not from an unidentified
local F5 build. Its manifest identifies clean source commit
`df48e61fe96e120d1e911c594b13214053fcd6c1` and build ID
`3b7d3602bb972f374a91912940421892260772c61cbd67db58d6886b65280ceb`.
ELF SHA-256: `362ccac5f2d2e964eeec1abab9bb01a828ab6a495e1795c92ce4d6666a9ab889`.
BIN SHA-256: `4f8632b29db35cb268bcdc48644da9127ed8d4ed3e09bf10551f14a8dfde764a`.
The pytest fixture recomputed the ELF hash against that manifest before opening UART.

Started `scripts/macos/start-jlink-gdbserver.command` on macOS and used SDK 1.0.1 GDB
in the existing Linux container to execute the following against the host-visible ELF:

```text
set remotetimeout 60
target remote host.docker.internal:2331
monitor reset
load
compare-sections rom_start
compare-sections text
compare-sections rodata
detach
```

Full reproduction uses `docker exec -u vscode -w /workspaces/crux/crux-pilot 46325e2f6fcc
/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb --batch -q
<ELF> -ex '<command>' ...`. Initial download compared rom_start/text; the final restoration
also compared rodata. All requested sections matched. `load` reported 65,960 section bytes;
BIN size is 65,964 bytes including section alignment. These section comparisons are not a
claim of a full raw-flash checksum. Logs: `build/evidence/m4/flash.log` and
`restore-flash.log`, with their J-Link Server logs beside them.

After programming, stopped the GDB Server. The host pytest fixture exclusively opened UART,
issued explicit native Commander `r; g; q` commands using `tests/target/reset.jlink`, and
captured the new boot/readiness output. The readiness marker is
`CRUX_TIME_READY composition=zephyr`; its association with this image is established by
manifest/ELF validation and the actual download/section-comparison log, not by treating the
generic marker as a unique build ID. The plan now states this adopted contract consistently.

### Maintained pytest UART suite and actual results

Added `tests/target/` and the non-flashing `scripts/test-target.sh` entry point. The host
venv pins pytest 8.3.5 and pyserial 3.5; actual host Python was 3.9.6 on macOS 15.7.4 arm64.
The fixture requests pyserial exclusivity and kernel `TIOCEXCL`, so another normal UART
program cannot join the run. Stop existing monitors/debug sessions before running it.
Disconnects, timeouts and mismatches fail; output directories must be new so failed runs
are not overwritten. Tests retain raw RX bytes, exact timestamped RX/TX bytes, results,
monotonic timing/assertions, image inputs and reset logs. No flash runs in a pytest fixture.

The exact final command, from the repository root:

```sh
scripts/test-target.sh \
  --uart /dev/cu.usbmodem11102 \
  --elf build/evidence/github-df48e61/zephyr/zephyr-39064901047-1/zephyr/mimxrt1060_evk_B_qspi/zephyr/zephyr.elf \
  --manifest build/evidence/github-df48e61/zephyr/zephyr-39064901047-1/zephyr/mimxrt1060_evk_B_qspi/provenance.json \
  --flash-log build/evidence/m4/restore-flash.log \
  --probe 602003106 --board 'mimxrt1060_evk@B//qspi' \
  --evidence build/evidence/m4/uart-run-02 \
  -v --junitxml=build/evidence/m4/uart-run-02.junit.xml
```

Actual final result: **5 passed in 5.56 seconds**, no warnings. Pytest's JUnit timestamp is
2026-10-17 21:20:21 BST; inputs record 20:20:21 UTC. Machine-generated summary:
`build/evidence/m4/summary.json`; full console: `pytest-run-02.log`; raw data:
`uart-run-02/uart-rx.bin`, `transactions.jsonl`, `inputs.json`, `reset.log`.

| Check | Observed result |
| --- | --- |
| Fresh readiness / unset | Marker captured after reset; first get returned `ERR NOT_SET` |
| Set/read bounds | Seven measured set-to-get intervals 0.101427–0.110739 s; all read deltas 0 s, within measured interval plus one 1-second RTC tick |
| Progression | Four samples `1792238400, 1792238401, 1792238402, 1792238403` over 3.056239 monotonic host seconds; non-decreasing and advancing, within the 5-second limit |
| Invalid syntax/calendar | Bad leap date, month 13, incomplete time, trailing Z and excess argument rejected; valid time continued |
| Range | 1999-12-31 and 2100-01-01 rejected; post-2038 `2038-01-19T03:14:08` and near-upper `2099-12-31T23:59:50` set/read succeeded |
| Reset session | Known valid time established, reset/go performed, new readiness captured, get returned `ERR NOT_SET`; safe date restored |
| Transaction bounds | 34 commands, 0.050255–0.055399 s observed response intervals; 2-second command timeout, 10-second readiness timeout, 20-second reset-process timeout |

The response timings include host serial polling/buffering and are not CPU execution-time
measurements. The one-tick allowance was fixed before execution, not widened after failure.
Progression is liveness evidence, not oscillator drift/accuracy. Final state: the production
sample is running, UART/probe are released, and RTC advances from the safe test value
`2026-10-17T12:00:00` UTC. No backup-power or persistent-retention behaviour is claimed.

### Repeated-init target Ztest and preserved diagnostic failures

Built the existing C17 Zephyr test application for the actual board, using the sample's
board-specific RTC priority configuration rather than duplicating it:

```sh
west build -p always -b 'mimxrt1060_evk@B//qspi' \
  -d crux-pilot/build/zephyr/m4-ztest crux-pilot/tests/zephyr_time -- \
  -DZEPHYR_EXTRA_MODULES=/workspaces/crux/crux-pilot \
  -DEXTRA_CONF_FILE=/workspaces/crux/crux-pilot/samples/time/boards/mimxrt1060_evk_mimxrt1062_qspi.conf
```

Added repeated-init-after-valid-set assertions to the existing RTC/command test; no public
init command or production hook was added. The physical target suite passed **2/2**, with
`PROJECT EXECUTION SUCCESSFUL`. Image SHA-256:
`93cdb8edabe5290dd9a0c446a1f0d8b48c8644a109398ad54862ab295cc9180b`.
The build used base commit `9304378` plus the reviewed test-source change; exact source-file
SHA-256 is in `build/evidence/m4/ztest-inputs.json`. It is not claimed as a clean-source
production bundle. Successful raw output is `ztest-uart-03.bin` / `ztest-uart-03.txt` and
`ztest-result-03.log`; flash/reset/build logs are retained alongside them.

Earlier results are retained, not replaced by a success-only report:

- First UART suite: 5/5 passed; pytest selected `/` as root and warned about its cache.
  The wrapper now explicitly supplies the repository root. Logs: `pytest-run-01.log`,
  `uart-run-01/`, and `uart-run-01.junit.xml`.
- First Ztest observation timed out with empty UART. GDB showed PC at `z_arm_reset`:
  programming/detach left the core halted. Explicit Commander reset/go started execution.
  Logs: `ztest-result.log`, `ztest-uart.bin`, `ztest-after-load-state.log`.
- The first executed Ztest failed its private-backend `NO_DATA` assumption. Upstream
  `rtc_counter_get_time()` reads counter+offset without requiring `epoch_valid`; unlike
  the emulated RTC, it does not promise native `NO_DATA` before set. The public Crux API
  correctly remained unset, as the UART suite had already verified. Tests now check that
  public session contract; the unused private-port include path was removed. Host tests
  continue to own no-backend-write fault/count checks. No production contract/driver was
  relaxed. Logs: `ztest-result-02.log`, `ztest-uart-02.bin`, `ztest-reset-02.log`.

After the target Ztest, the identified production sample was reflashed/compared and the
final UART suite above passed. M4-01 through M4-08 are supported by this physical evidence.

Post-correction software regression: `west twister -j 2 -p native_sim/native/64
-T crux-pilot/tests/zephyr_time -O crux-pilot/build/zephyr/twister-m4 --inline-logs`
passed 2/2 configurations and 4/4 cases, no failures/errors/warnings (306.93 s).
Log: `build/evidence/m4/twister.log`. The owned-source format check, dependency-boundary
check, `bash -n scripts/test-target.sh` and `git diff --check` passed. Host production code
was unchanged, so the previously passing 27/27 Host runs were not repeated.

## Platform M1 exit verification (2026-10-17)

This section concerns current **Platform M1 — Engineering and target foundation**,
not the historical Time Pilot milestones. Planning source is `crux-platform`
`7d2287bdb5a252ee5e1fc51c7142813ba37800d6`; implementation base is clean `bd0def0`.
The [readiness assessment](platform-m1-readiness.md) supplies issue #2's native
support, access/fixture, ROM/image, QSPI/XIP and maintenance dispositions. No physical
operation, new capability, partition change or upstream upgrade was performed.

Issue #3 is addressed by private `-Wall -Wextra -Werror` on `crux_time_common`,
`crux_time_zephyr`, and the owned sample/test `app` targets. Host policies are unchanged.
The [contributor warning policy](../CONTRIBUTING.md#testing-and-validation) records the
native warning-selection limits. Zephyr-wide `CONFIG_COMPILER_WARNINGS_AS_ERRORS` may
remain disabled for samples; the owned-target command lines enforce this policy.
Twister separately enables warnings-as-errors in its own test configurations.

Reproduction used the existing Linux amd64 Dev Container as `vscode`, with
`PATH=/opt/crux-cache/venv/bin:$PATH`, `CLANG_FORMAT=clang-format` and
`CMAKE_BUILD_PARALLEL_LEVEL=2`:

```sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
scripts/check-format.sh
scripts/check-dependencies.sh
```

The local discovery run passed 2/2 native_sim configurations and 4/4 cases, with no
warnings (124.88 seconds), and pristine native_sim/EVKB sample compilation and link.
The full wrapper exited 1 at provenance recording: the assessment documents were
edited while that run was in progress. `record-build-provenance.py` correctly rejected
it with `ValueError: Source or workspace inputs changed during verification; rerun the checks`.
No local provenance manifest from that mixed-input run is accepted as a final bundle.
The complete clean-checkout hosted workflow is required for the final source revision;
its outcome and exact commit/run identity are recorded in the associated PR.
The original discovery output remains in ignored `build/evidence/m1-zephyr.log`.

`m1-warning-flags.json` records actual compile commands: each of the native_sim sample,
EVKB sample and two Twister configurations contains six owned translation units with
`-Wall -Wextra -Werror`. Sample upstream/generated units (103 native_sim / 154 EVKB)
do not acquire general `-Werror`. Twister's upstream units retain its independent test
policy. An initial inspection incorrectly required no upstream `-Werror` in Twister;
the generated commands identified that oracle error. The corrected inspection checks
sample isolation and records Twister's separate policy. No build option was changed to
hide that result. Inspection code and results are retained in `build/evidence/`.

For the negative control, temporarily append an unused static function named
`crux_m1_warning_probe` to `components/time/src/time.c`, then run:

```sh
cmake --build build/zephyr/native_sim_time --parallel 2
cmake --build build/zephyr/mimxrt1060_evk_B_qspi --parallel 2
```

Restore the original source bytes in a `finally` block and rebuild each configuration.
The retained `m1-warning-probe.py` performs these steps and checks the original source
hash, non-zero negative result with the named diagnostic, and zero restored-build result.
Temporary source is not committed. The probe invalidates the older target manifest
before changing inputs. Logs and machine-readable results are under ignored
`build/evidence/m1-warning-*.log` and `m1-warning-probe.json`.

| Configuration | Named unused-function probe | Original source restored |
| --- | --- | --- |
| native_sim sample | Exit 1, `-Werror=unused-function` | Exit 0 |
| EVKB QSPI sample | Exit 1, `-Werror=unused-function` | Exit 0 |

Both restoration hashes are `dbe0f87dd6b5ca01d490dfc43ba4b1356c3571b6d657c2e3ef86efb4f2213426`;
Git confirms no runtime-source difference from the accepted baseline.

The existing EVKB Ztest build also reconfigured and cross-compiled with the owned
application warning policy: `cmake --build build/zephyr/m4-ztest --parallel 2`, exit 0,
FLASH 85,812 B / RAM 9,920 B. Its inherited board/sample configuration is described in
the preceding physical M4 section. Log: `build/evidence/m1-target-ztest-build.log`.
This was a compile/link check, not another on-target test. The known upstream USB
devicetree unit-address warning remains outside the owned C compiler policy.
The Zephyr workflow now retains all four compilation databases with the usual
reports so reviewers can inspect the actual warning flags independently.

Format and dependency checks exited 0; retained output is
`build/evidence/m1-format-dependencies.log`. Existing Host Debug/sanitizer 27/27 and
physical UART 5/5 / target Ztest 2/2 evidence remains applicable to unchanged runtime
source; this change does not claim a new hardware test. The rebuilt BIN SHA-256 is
`4f8632b29db35cb268bcdc48644da9127ed8d4ed3e09bf10551f14a8dfde764a`, matching the
physically verified image. Host tests and configured analysis also run in hosted CI.

M1's target/maintenance assessment now permits continued development with explicit
constraints before M3 peripheral and M4 storage qualification. M2 execution-profile and
reference-controller implementation has not started. PR review/integration and a green
hosted run for the final committed source are the remaining acceptance steps; source
revisions and hosted results must not be inferred from the uncommitted discovery run.

### Clean-source acceptance and exit criteria

Implementation revision `5c7bcd4740dbfea442bcc09505dfb57ddd602633` passed clean-checkout
[Host CI](https://github.com/wonny-labs/crux-pilot/actions/runs/39070553995) and
[Zephyr CI](https://github.com/wonny-labs/crux-pilot/actions/runs/39070553855).
This resolves the discovery run's mixed-input provenance rejection. The evidence-publication
follow-up adds compilation-database upload paths and these results; it changes neither
runtime code nor the owned-target warning options. Final-head CI remains a prerequisite
for integration and is visible on [PR #4](https://github.com/wonny-labs/crux-pilot/pull/4).

| Current platform M1 exit criterion | Technical result | Evidence |
| --- | --- | --- |
| Clean checkout reproduces Host/Zephyr builds and applicable tests | Satisfied | Above hosted runs; Host and Zephyr test reports/provenance. Existing applicable sanitizer results remain under their recorded source identity. |
| EVKB boot, download and debugging are recorded | Satisfied | Identified Pilot image, section comparisons, breakpoint and physical UART/RTC results; unchanged runtime source and matching rebuilt BIN. |
| Initial GitHub Actions jobs succeed | Satisfied | Original main runs and the above clean implementation-revision runs. |
| Owned formatting, compiler warnings and selected static analysis have controls/baseline | Satisfied | Host format/tidy/dependency gates, private owned Zephyr warning flags, four inspected compilation databases and both negative/restored controls. |
| Hardware, boot-image and upstream-maintenance risks are documented | Satisfied | Platform M1 readiness record, with bounded fixture/routing, XIP/write and support-horizon dispositions. |

The existing Time code, independent build interfaces, host/Zephyr tests and board
configuration are preserved. No new hardware qualification is asserted. A separate AI
reviewer context found no material readiness or warning-control finding; it is not a
human approval or external audit. Technical M1 closure is supported once the final
head stays green and PR review/integration completes. Issues #2/#3 are linked for closure
on merge; they must not be closed based solely on an unmerged local implementation.
