# Tutorial: your first build and sample

This tutorial takes you from a working Dev Container to running Crux samples, changing a build, and running a sample on
your board. It is one path: Part A runs on the Linux Host, Part B on Zephyr's `native_sim`, and Part C on a board. Parts
A and B need no board. Each step gives the command and what you should see; the
[Build and Integration Guide](build.md) goes deeper.

## Prerequisites and the Dev Container

Complete the prerequisites and the clone in [Getting started](getting-started.md), then open the repository folder in VS Code. If your network's proxy re-signs TLS (the symptom is in [network and proxy prerequisites](getting-started.md#4-network-and-proxy-prerequisites)), put your organisation's root CA as a `.crt` file (PEM or DER) in `.devcontainer/certs/` of the repository folder you opened; the [folder README](../.devcontainer/certs/README.md) gives the steps. Then choose **Dev Containers: Reopen in Container**.

All commands in this tutorial run from `/workspace/crux`, where the VS Code terminal opens.

## Part A. Linux Host

### 1. Check that the Host build is ready

The Dev Container builds and tests the Linux Host when it is created ([The Dev Container sets itself up](getting-started.md#6-the-dev-container-sets-itself-up)):

```sh
scripts/bootstrap.sh status
```

It shows `host    ok`. Otherwise run the command it prints, `scripts/bootstrap.sh host`; its output shows the CTest summary, `100% tests passed`, and ends with `bootstrap: host ok`.

### 2. Run Hello Crux on the Linux Host

`samples/hello_crux` is the smallest application that uses Crux. Its Host program is its own CMake
project:

```sh
cmake -S samples/hello_crux/linux -B build/samples/hello_crux_host -G Ninja -DCRUX_SOURCE_DIR="$PWD"
cmake --build build/samples/hello_crux_host --target run_hello_crux
```

The `run_hello_crux` target builds the program and runs it in your terminal.

It prints `Hello from Crux 0.1.0-dev on host` (the version of your checkout) and `CRUX_HELLO_READY composition=host`. [Its README](../samples/hello_crux/README.md) explains what the sample shows.

### 3. Run the networking sample on the Host

`samples/networking/echo_client` exchanges one TCP and one UDP echo; the `run_echo_client` target starts a local peer
and runs it once:

```sh
cmake --build --preset host-debug --target run_echo_client
```

### 4. Run every Host sample with CTest

CTest runs each Host sample of the root build once and checks its output:

```sh
ctest --preset host-debug -L sample
```

It runs `Sample.TimeShellHost` and `Sample.EchoClientHost`; `hello_crux` is its own CMake project (step 2).

To build, test and run the Host samples from VS Code instead, see [Linux Host in VS Code](build.md#linux-host-in-vs-code).

### 5. Change a Host build

CMake options configure the build itself. The Linux Host build selects Crux components and samples with `CRUX_BUILD_*` options:

```sh
cmake -LH build/linux/debug | grep -B1 '^CRUX_'                       # list the options and their help
cmake --preset host-debug -DCRUX_BUILD_TIME_PERIODIC_SCENARIO=OFF
cmake --build --preset host-debug
ctest --preset host-debug -R TimePeriodic -N                           # the scenario test is gone
cmake --preset host-debug -DCRUX_BUILD_TIME_PERIODIC_SCENARIO=ON       # back to the default
```

The [capability and provider selection](build.md#capability-and-provider-selection) section lists the options that
matter for products.

## Part B. Zephyr

### 6. Check that the Zephyr workspace is ready

The Dev Container also creates the west workspace and installs the Zephyr SDK when it is created. `scripts/bootstrap.sh status` shows `zephyr  ok`; otherwise run the command it prints, `scripts/bootstrap.sh zephyr`.

### 7. Run Hello Crux

This is the Zephyr form of step 2:

```sh
west build -p always -b native_sim/native/64 -d build/hello_crux samples/hello_crux/zephyr -t run
```

You should see, after the boot banner:

```text
Hello from Crux 0.1.0-dev on zephyr
CRUX_HELLO_READY composition=zephyr
```

Stop it with Ctrl+C. The sample's [README](../samples/hello_crux/README.md) explains how it uses Crux.

### 8. Run the Time shell

The Time sample is a Zephyr shell with Crux Time commands:

```sh
west build -p always -b native_sim/native/64 -d build/time samples/time/shell/zephyr -t run
```

It prints `CRUX_TIME_READY composition=zephyr` and then waits. Its shell is on a pseudo-terminal, as the first output
line says. To type commands in your own terminal, stop it with Ctrl+C and start the built program with its UART on
stdin and stdout:

```sh
build/time/zephyr/zephyr.exe -uart_stdinout
```

Type `time get` (`ERR NOT_SET`), then `time set 2026-10-16T14:30:00` (`OK`), then `time get` again. Stop with Ctrl+C.

### 9. Select a provider by configuration

`samples/file/basic` writes a file through the Crux File API and reads it back. The application source does not
know where the file lives: the Kconfig fragment and the devicetree overlay of the build choose the File provider. The
default build uses LittleFS on the flash simulator:

```sh
west build -p always -b native_sim/native/64 -d build/basic samples/file/basic/zephyr -t run
```

It ends with `CRUX_FILE_WRITE_READ_OK provider=simulator`. The same source, built with another fragment and overlay,
uses FatFs on a RAM disk:

```sh
west build -p always -b native_sim/native/64 -d build/basic_fatfs \
    samples/file/basic/zephyr -t run -- \
    -DEXTRA_CONF_FILE=fatfs_ram_disk.conf -DEXTRA_DTC_OVERLAY_FILE=fatfs_ram_disk.overlay
```

and ends with `CRUX_FILE_WRITE_READ_OK provider=fatfs_ram_disk`. Step 11 explains the two options after `--`.

### 10. Run every Zephyr sample with Twister

Twister builds each runnable Zephyr sample, runs it, and checks its output against the regular expression in its
`sample.yaml`:

```sh
west twister -T samples -p native_sim/native/64 -t sample
```

Its summary line reports how many configurations passed; output and logs are under `twister-out/`. `-t sample`
selects the runnable samples.

### 11. Change a Zephyr build

Every change in this step is a standard Zephyr or CMake mechanism. Options after `--` on a `west build` command line
go to CMake. They stay in the build directory's cache, so use `-p always` when you drop one again.

#### Kconfig

Kconfig options (`CONFIG_...`) select features and values. A build merges them from, in order:

| Source | Where | Use it for |
| --- | --- | --- |
| `prj.conf` | The application folder | The application's own settings |
| Board file | `boards/<board>.conf` in the application folder, for example `samples/time/shell/zephyr/boards/native_sim.conf` | Settings for one board |
| Extra files | `-DEXTRA_CONF_FILE=<file>[;<file>...]` | A variant without editing the application, for example a debug profile |
| One option | `-DCONFIG_<NAME>=<value>` | A quick experiment |

For example, turn off Zephyr's boot banner for one build:

```sh
west build -p always -b native_sim/native/64 -d build/time samples/time/shell/zephyr -- -DCONFIG_BOOT_BANNER=n
grep CONFIG_BOOT_BANNER build/time/zephyr/.config
build/time/zephyr/zephyr.exe -uart_stdinout
```

The `grep` prints `# CONFIG_BOOT_BANNER is not set`, and the program starts without the `*** Booting Zephyr OS ***`
line. The same setting in a file has the same effect:

```sh
printf 'CONFIG_BOOT_BANNER=n\n' > build/no-banner.conf
west build -p always -b native_sim/native/64 -d build/time samples/time/shell/zephyr -- -DEXTRA_CONF_FILE=$PWD/build/no-banner.conf
```

`build/<dir>/zephyr/.config` is the final, merged result: check it whenever an option seems to have no effect,
because Kconfig drops a value whose dependencies are not met and only warns. To browse and change options
interactively, run `west build -d build/time -t menuconfig` (or `-t guiconfig`). It saves into that `.config`, which
the next pristine build replaces, so copy a setting you want to keep into `prj.conf` or a `.conf` file.

#### Devicetree overlays

Devicetree describes the hardware: which devices exist and how they are wired. An overlay changes it for one build.
Zephyr picks up `app.overlay` or `boards/<board>.overlay` from the application folder by itself, and
`-DEXTRA_DTC_OVERLAY_FILE=<file>` adds one from anywhere. For example, build the Time sample with the simulated RTC
disabled:

```sh
west build -p always -b native_sim/native/64 -d build/time samples/time/shell/zephyr \
  -- -DEXTRA_DTC_OVERLAY_FILE=$PWD/tests/time/zephyr/boards/native_sim_no_rtc.overlay
grep -A3 'rtc: rtc' build/time/zephyr/zephyr.dts
```

`build/<dir>/zephyr/zephyr.dts` is the final Devicetree, with the file and line that set each property: it shows
`status = "disabled"` from the overlay.

## Part C. Run on your board

No board? Parts A and B run every sample and test.

The board is a parameter: the same build takes the board's `-b` target, and each board has a guide with its switches,
probe, connections and recovery. Pick your board:

| Board | `-b` target | Probe | Board guide |
| --- | --- | --- | --- |
| NXP MIMXRT1060-EVKB, revision B | `mimxrt1060_evk@B//qspi` | SEGGER J-Link on J2; console on the DAPLink USB serial port | [MIMXRT1060-EVKB](boards/mimxrt1060_evkb.md) |

Which components have evidence on each board is recorded in [Component readiness](component-readiness.md). Set the
board up as its guide says ([switches and jumpers](boards/mimxrt1060_evkb.md#switches-and-jumpers)) before you start.

The steps below use the J-Link software in the Dev Container (Linux and WSL2); the route is in the
[board guide](boards/mimxrt1060_evkb.md#flash-and-debug), and its [known limits](boards/mimxrt1060_evkb.md#known-limits) cover other hosts.

### 12. Connect the probe to the Dev Container

The J-Link software runs in the Dev Container, so the probe must reach it first. On Windows with WSL2, run `usbipd list` in PowerShell to find the probe's `BUSID` (the J-Link has vendor ID `1366`), run `usbipd bind --busid <BUSID>` once in an administrator PowerShell only if its STATE is `Not shared`, then `usbipd attach --wsl --busid <BUSID>`. On native Linux there is no attach; the Linux-side set-up is in the same place. The procedure is in [Getting started, section 7](getting-started.md#7-if-you-use-a-board). Without it, step 13 fails.

### 13. Check the J-Link connection

In the Dev Container, with the switches and jumpers of the board guide:

```sh
printf 'exit\n' | JLinkExe -nogui 1 -if swd -speed 100 -device MIMXRT1062xxx6A -autoconnect 1
```

It should show a `VTref` of about 3.3 V and `Found Cortex-M7`. If it reports `Cannot connect to the probe/programmer` or finds no J-Link, the probe is not attached
to the container: repeat step 12. For `VTref=0.000V` or another failure, use the board guide's
[recovery section](boards/mimxrt1060_evkb.md#recovery) before you go on. Then open the board's console, the DAPLink serial port, at 115200 baud, 8-N-1, in a serial terminal
on the computer the board's USB cable is plugged into (on Windows, the COM port that Device Manager lists under Ports).

### 14. Flash Hello Crux

The first Crux image on the board is the one from step 7. Zephyr's own `west build` and `west flash` build and flash
it:

```sh
west build -p always -b 'mimxrt1060_evk@B//qspi' -d build/hello_crux_evkb samples/hello_crux/zephyr
west flash -d build/hello_crux_evkb -r jlink
```

`-r jlink` selects the J-Link runner, because Zephyr's default runner for this board is a different one. If the download fails, run `west -v flash -d build/hello_crux_evkb -r jlink` for more
output; the [J-Link board route](board-jlink-usbipd.md#if-a-step-fails) lists the speeds to try and the usual
failures.

### 15. See the console line

Once the image is running, the console of step 13 shows the Zephyr boot banner and then the two lines of step 7,
ending with `CRUX_HELLO_READY composition=zephyr`. If the board does nothing, [Troubleshooting](#troubleshooting)
checks board and probe with Zephyr's `blinky`.

### 16. Debug on the board

In VS Code, press F5 (**Run and Debug**) and pick the profile **Board: Debug**. VS Code asks for the sample directory to debug: press Enter to accept `samples/hello_crux/zephyr`, or type another sample directory, such as `samples/time/shell/zephyr`. The profile builds that sample with `scripts/build-debug-target.sh`, flashes it to the board in place of the image of step 14, and stops at `main`. [VS Code debug profiles](boards/mimxrt1060_evkb.md#vs-code-debug-profiles) describes the profiles and the build they debug.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Downloads in the image build or the container fail with a certificate error (for example `curl: (60) SSL certificate problem`, or the same from pip, Git or west), behind a proxy that re-signs TLS | The image does not trust your organisation's root CA. Add it to `.devcontainer/certs/` and rebuild the container; the [folder README](../.devcontainer/certs/README.md) gives the steps |
| `hello_crux` fails at configure or build on `native_sim` | Build Zephyr's own `hello_world`: `west build -p always -b native_sim/native/64 -d build/hello ../zephyr/samples/hello_world -t run`. You should see `Hello World! native_sim/native/64`. If this fails too, the Zephyr install is the problem (rerun the Zephyr bootstrap of step 6); if only `hello_crux` fails, the problem is in the Crux module or its configuration |
| **Board: Flash** reports `required program JLinkExe not found`, or **Board: Debug** reports `spawn JLinkGDBServer ENOENT` | The image has no J-Link software; `scripts/bootstrap.sh status` says so. The usual cause is a download blocked by a proxy that re-signs TLS (the certificate row above); `CRUX_JLINK=0` and a missing placed package are the others. Fixes: [J-Link board route, step 3](board-jlink-usbipd.md#steps) |
| `No J-Link probe found`; `JLinkExe` or `west flash` reports `Cannot connect to the probe/programmer`; `west flash` stops with `command exited with status 1: ... JLinkExe`; or **Board: Debug** or **Board: Attach** shows `jlink: GDB Server Quit Unexpectedly` | The probe is not attached to the Dev Container: `usbipd list` on Windows does not show it as `Attached`, and `lsusb -d 1366:` in the container lists nothing. Repeat step 12; the attach is lost when the probe re-enumerates or WSL2 restarts. [If a step fails](board-jlink-usbipd.md#if-a-step-fails) lists the other causes |
| The board does nothing after a download | Build Zephyr's `blinky` for the board: `west build -p always -b 'mimxrt1060_evk@B//qspi' -d build/blinky ../zephyr/samples/basic/blinky`, then flash it with the J-Link runner as in step 14: `west flash -d build/blinky -r jlink`. The user LED should blink. If it does, the board and probe work and the problem is in the image; if not, use the board guide's recovery section |

## Next steps

- [Samples](../samples/README.md): what each sample folder is for.
- [Boards](boards/README.md): every board page.
- [J-Link board route](board-jlink-usbipd.md): the other ways to debug, `west debug`, `west attach` and the VS Code profile **Board: Attach**.
- [Choosing an API](choosing-an-api.md): where each kind of product code goes.
- [Test guide](../tests/README.md): every suite and the checks to run before a pull request.
- [Documentation map](README.md): every other page, by task.
