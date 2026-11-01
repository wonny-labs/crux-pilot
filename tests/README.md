# Test organisation

Crux uses GoogleTest/CTest for selected Linux Host component and controller tests, Twister with Ztest or console harnesses for Zephyr tests, and Python runners for build and scenario validation. The existing physical Time/RTC tests use standalone pytest on macOS; other physical device, network and controller tests use dedicated Python command-line runners. Twister's pytest harness is not currently configured.

Typed monotonic and Thread sleep checks include controlled common/native failure
cases, C17/C++17 header/type checks, and RTC-disabled Zephyr Ztests in
`tests/thread/zephyr`. `scripts/test-portable-execution.sh` includes those Ztests
and their EVKB cross-build. Thread-only means without Sync; its explicit Time
clock dependency is included. These checks do not operate physical hardware.

Time Busy-wait tests include common/native error handling, counter conversion and
unsupported-profile shims, and actual Linux integration under `tests/time/busy_wait/`.
`scripts/test-time-busy-wait.sh` runs normal-ISR and IRQ-delivery Ztests on native_sim and
cross-builds EVKB. Shims and compilation do not qualify a physical ISR or timing.
See the [Time Busy-wait contract](../docs/time-busy-wait.md).

| Test purpose | Runner or harness | Environment |
| --- | --- | --- |
| Portable components and controller logic | CTest with GoogleTest suites and standalone C/C++ tests | Linux Host |
| Zephyr components and integration | Twister with Ztest, console or the default harness | Zephyr `native_sim` |
| Basic periodic controller scenarios | Python runner through `scripts/test-controller.sh` | Zephyr `native_sim` |
| Build tools and validation oracles | Python `unittest` | Linux host |
| Physical Time/RTC functional tests | Standalone pytest through `scripts/test-target.sh` | macOS, EVKB and J-Link |
| Physical device, network and integrated watchdog tests | Dedicated Python CLI capture runners | EVKB and controlled Mac/peer setup |

## Host: `time/` and `controller/`

GoogleTest/CTest exercises the C17 Time implementation, calendar conversion, and portable command
handler against the controlled host backend. The C++ tests include the public C headers and call
the C17 implementation, providing the C++ consumer coverage needed for this API. The test build
does not recompile production `.c` sources as C++ and does not maintain per-header fixtures or a
separate link-only executable.
The controller Host tests also cover portable scheduling, readiness, freshness,
recovery and joint owner-health logic. CTest applies a 30-second limit to each
discovered Time host test.

At the repository root, `BUILD_TESTING` sets the initial default for `CRUX_BUILD_TESTS`. The `CRUX_BUILD_TESTS` option controls Crux's internal tests and GoogleTest/C++ setup. In an existing build directory, set `CRUX_BUILD_TESTS=OFF` explicitly to disable them. For a library-only build, also set `CRUX_BUILD_HOST_SAMPLE=OFF` and `CRUX_BUILD_REFERENCE_CONTROLLER=OFF`. See the [build guide](../docs/build.md#linux-host-build) for the supported configurations.

## Zephyr: `time/zephyr/`

Twister builds and runs the C17 Ztest application on three `native_sim/native/64`
configurations: RTC emulator, absent RTC and a controlled fake RTC driver. Tests
cover system realtime/authority/snapshot consistency and ISR rejection, explicit
RTC storage and failure isolation, the shared command handler and the registered
Zephyr Shell adapter using the dummy Shell backend. The trailing-argument case verifies that Shell passes syntax errors to the shared Crux
handler for the documented `ERR INVALID_ARGUMENT` result.

Host tests own portable behavior. Ztest covers native integration and the Shell boundary. The two
suites do not duplicate every case across frameworks.

Run the Time Zephyr integration checks from the repository root in the Linux Dev Container,
after setting up the west workspace and SDK:

```sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

This runs the `native_sim` Ztests, builds the Time Shell sample for simulation and
cross-builds its EVKB firmware image. It does not run Ztests on the EVKB, program
or reset the physical board. The macOS physical suite below records the earlier RTC-backed contract; it has
not qualified the redesigned system realtime/optional-RTC implementation.

## Build tools: `build/`

Python standard-library `unittest` checks the provenance safeguards: changed input rejection,
actual dependency HEAD/dirty-state checks, and invalidation of stale manifests even when a debug
build, clean command or native test run fails. Run
`python3 -m unittest discover -s tests/build -v` on Linux; host CI runs it too.
No additional test framework is required.

## Physical UART: `target/`

This pytest suite runs on macOS against the already flashed EVKB QSPI sample. It uses
pyserial at 115200 baud, a kernel-exclusive UART handle and J-Link Commander resets at
400 kHz. Stop VS Code debugging, J-Link GDB Server and all Serial Monitor programs first.
The suite never flashes. Its fixture validates the ELF against a provenance manifest and
requires a retained GDB download/section-comparison log for that identified image.

Prepare an isolated host environment (separate from the Linux build tools):

```sh
python3 -m venv build/tools/m4-venv
build/tools/m4-venv/bin/python -m pip install -r tests/target/requirements.txt
```

Run after the explicit, documented download/verification step, with the actual artifact paths:

```sh
scripts/test-target.sh \
  --uart /dev/cu.usbmodem11102 \
  --elf <verified-zephyr.elf> --manifest <matching-provenance.json> \
  --flash-log <gdb-download-and-compare.log> \
  --probe 602003106 --board 'mimxrt1060_evk@B//qspi' \
  --evidence build/evidence/m4/<new-run-directory> \
  -v --junitxml=build/evidence/m4/<new-run>.junit.xml
```

The output directory must be new so failed evidence is not overwritten. Keep the pytest
console log as well. Each run retains raw received bytes, timestamped RX/TX bytes and
transaction/assertion results, firmware/source identity and reset output. Command timeout is
2 seconds, readiness timeout is 10 seconds, and the reset tool timeout is 20 seconds. Each
read-after-set bound uses the monotonic interval from the start of set to the end of get,
plus one RTC tick (1 second). Progression is sampled four times at approximately 1-second
intervals within 5 seconds; this verifies liveness, not oscillator drift.

The final successful reset case leaves the sample running at the safe test date
`2026-10-17T12:00:00` UTC (advancing), not synchronised to the host wall clock. The retained
`reset.jlink` script only resets, starts and disconnects; no erase, flash or fuse operation
is hidden in a pytest fixture. The suite currently supports only the verified EVKB QSPI
setup; other boards need their reset/transport procedure reviewed explicitly.


## Execution and reference controller

`tests/execution` compares the pinned native Zephyr/POSIX subset; its POSIX cases also run
on Linux. `tests/controller` verifies sample-owned Time observations/recovery and monotonic
phase logic. The maintained boundary-negative and host-runner failure checks are under
`tests/build`. Host CTest includes the finite controller scenario, including sanitizer exit
status; `scripts/test-controller.sh` validates normal/RTC-absent native_sim system-realtime compositions
and cross-builds EVKB without hardware access. See the [controller conditions and limits](../samples/controller/README.md).

Run `scripts/test-controller-integration.sh` in the configured Linux/west
environment for device, networking and integrated-controller Zephyr scenarios
and EVKB cross-builds. Twister executes Ztest, console-harness and other
`native_sim` application scenarios. The existing `scripts/test-m3.sh`
remains a compatibility entry point for historical M3 instructions.
Additional Python validators inspect the captured simulation output. Neither this
script nor CI programs or resets the physical EVKB.

Physical device, network and integrated-controller evidence is gathered by the
reviewed `tests/target/capture_m3*.py` command-line runners on the identified Mac/EVKB setup. These are not pytest
test functions and are not automatically executed by GitHub Actions. Consult the
[controller validation record](../docs/m3-validation.md) for authorised fixtures,
commands, exact firmware identities, final results and limits.


## Native output fixtures

The native deferred-output evaluation is under `tests/native/deferred_output`.
It runs the pinned Zephyr raw printk queue and UART backend on native_sim, including
long signed/unsigned 64-bit output and a deliberately overflowing finite buffer.
`verify_output.py` checks the actual handler logs and requires the shared physical
loss validator to reject the real overflow marker. This is software evidence; it
does not establish EVKB UART/IRQ timing or logger stack sufficiency.

The native_sim board requires its fallback POSIX console. The fixture retains that
compiled fallback but disables the separate POSIX logging backend; `LOG_PRINTK`
returns through the deferred logger before the fallback character hook. Actual
configuration and exact output counts must confirm that UART is the sole logging
backend. Required record comparisons remove CR/LF endings only, retaining ANSI,
whitespace, duplication and partial-line errors as failures.

## External application build boundary

Run inside the Linux development environment:

```sh
python3 -m unittest discover -s tests/build -p test_external_application.py -v
```

The test copies `tests/external_application/` into fresh temporary directories
outside the Crux source tree, then configures, builds and runs independent C17/C++17
Base/Time, Thread-only, Sync-only, Serial-only, Logging-only, Queue/Time/Thread and Base-only parent projects.
Parent C99/C++20 targets and CTest tests before/after inclusion remain functional;
a separate C-only parent retains its enabled-language and testing policy ON/OFF.
Actual compile commands and final ELF symbols check component selection and private
requirements. Focused negatives cover missing/unsupported/multiple Time selection,
incompatible Time/Serial/Logging providers and missing the Queue Linux composition target. Existing
source/build guards retain the detailed native-dependency negatives. A separately
copied Serial consumer runs all five PTY/policy scenarios beneath a parent project
with its own tests enabled, preserving both parent tests. The independent Logging
fixture similarly runs its actual-provider and policy cases under a testing-ON parent.

See the [external application example](../docs/build.md#external-linux-application-using-crux-source).
The fixture uses public capabilities in portable targets and declares platform
composition separately. It does not establish an installed SDK, production Linux,
physical qualification or support for additional toolchains.

## Portable execution capability

Run `scripts/test-portable-execution.sh --host-only` for separately configured C/C++
Thread-only and Sync-only consumers, firmware and independent runtime-shaped consumers,
and their execution boundary checks. Standalone configurations build only the selected
component; portable C/C++ translation units cannot see the opposite public header or
private headers. Final ELF symbol and compile-command checks reject the unselected
provider. Sync-only ownership is exercised from a native OS-created worker.
The full `scripts/test-portable-execution.sh` also runs Zephyr `native_sim` and EVKB
cross-builds for the combined and two standalone component configurations. Common conformance and Host failure/poll regressions run in the standard
Host debug/sanitizer checks. Private admission tests prove lifecycle races without
adding production test hooks. See the [current Thread/Sync contract](../docs/thread-sync.md) for semantics and
provider limits, and the [historical validation record](../docs/history/execution-validation.md)
for its recorded source identities, results and unresolved gates at those revisions.

## Portable networking software slice

The [networking contract](../docs/networking-contract.md) defines the approved
IPv4 client-only profile. `scripts/check-host.sh` includes deterministic and real
Linux loopback tests. Run `scripts/bootstrap-networking-tests.sh` once to fetch
Zephyr's pinned Picolibc test module into the external cache, then
`scripts/test-networking.sh` for independent C/C++ consumers, real Zephyr loopback
qualification and an RT1060 cross-build. These commands never operate hardware.
The native fixture requires embedded libc to avoid silently testing Linux sockets.
Fixture servers use standard APIs outside the public client profile; they are
not consumer examples. Physical acceptance remains pending.

## M5 physical qualification preparation

`scripts/test-networking-physical.sh` runs private M5 runner/oracle tests,
embedded-libc Zephyr native instrumentation tests and a dedicated RT1060
cross-build. `--freeze` requires clean committed inputs and creates
commit-specific image/provenance/ABI manifests. Neither mode accesses hardware.
See [the preparation and approval procedure](../docs/networking-physical-preparation.md)
for fixed criteria, M5-only wire identity, bounded injected-adapter runner,
raw-evidence replay and separate physical operation gates. The concrete adapter is tested with fake devices. Its physical capture/reset
path requires separate exact-operation approval and current M3 safety gates;
software preparation does not establish physical acceptance or authorisation.

## Portable Configuration software checks

The standard Host debug/sanitizer CTests include `Configuration.Conformance` and
`Configuration.ControllerScenario`. Existing build-tool unittest discovery runs
the independent C17/C++17 consumer build without private consumer dependencies.
See [Configuration qualification](../docs/configuration-validation.md) for the
focused manual `native_sim` and pinned Settings source-wrapper probe commands.
These checks do not operate Flash or qualify an adopted persistence backend.

## Portable File software slice

`scripts/test-storage-simulators.sh` runs the actual VFS/LittleFS and FatFs File suites on
32/64-bit `native_sim`, the Settings/NVS Configuration suite on 64-bit `native_sim`,
and the Configuration source-staging regressions. It requires the pinned west
workspace with LittleFS and FatFs and a Host compiler with 32-bit development libraries.
The Zephyr CI workflow runs this software-only gate; no physical storage is used.

The [File contract](../docs/file-contract.md) and
[qualification/handoff record](../docs/file-validation.md) cover the Linux Host
provider and private deterministic failure tests. Normal Host CTest includes
`File.file_fault` and `File.file_host`; the latter uses an isolated temporary
root and no hardware. Separate public-only C17/C++17 consumers are built with:

```sh
cmake -S tests/file_consumer -B build/file-consumers -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/file-consumers
ctest --test-dir build/file-consumers --output-on-failure
```

The portable consumer objects link `crux::file`; root selection belongs to a
separate trusted native fixture. No Zephyr File runtime or production storage
backend is enabled by these checks.

### FatFs File on `native_sim`

`tests/native/file_fatfs` runs the File contract on real Zephyr FatFs over a RAM disk,
including READ_WRITE, EOF and partial writes, storage full and reclamation, corrupted media
that is never formatted, remount, and a torn-sync model. A remount inside one process is not
evidence of persistence.

`scripts/test-file-persistence.sh` builds `tests/native/file_fatfs_persist`, where the disk is a
`zephyr,flash-disk` on the flash simulator backed by a host file, and runs it as separate
processes: files acknowledged by one process are intact in the next, blank media is never
mounted or modified, and a process killed between updates (`SIGKILL`, right after the child
reports that it is resting) leaves every acknowledged file intact. A sample is kept only if the
runner saw the report within 80 ms and only one such report, and the check then finds the next
update not yet begun; any other sample is discarded and redrawn (at most 5 times). The same run executes a
negative control: a deliberate defect that acknowledges an update before it is durable (before
both sync and close) must be reported as a lost acknowledged file in every iteration
(`--mutant ack_before_durable` runs only that control). It shows that the check can fail; it does
not show that `sync` alone works, which the in-process torn-sync test covers. A kill at a random
point is only recorded: where it lands is not controlled, and on this erase-then-program medium an
update in flight can destroy neighbouring sectors. That says nothing about SD cards and is not a
power-loss or physical-media claim.

`tests/zephyr/file_fatfs_compile` compiles the File implementation and the RT1060 FatFs/SDHC
composition for `mimxrt1060_evk@B//qspi` as an unlinked object. It does not mount, format or run
anything on a board. `scripts/test-file-rt1060-compile.sh` runs it and fails if the testcase is
filtered out instead of built. `scripts/test-storage-simulators.sh` and this script are in the
Zephyr CI workflow; `scripts/test-file-persistence.sh` is not.

## Portable queue and monotonic timing

Queue owns its native ports under `components/queue/ports/<os>/`. Portable
consumers use `crux::queue`; the final Linux executable adds `crux::queue_linux`
(`crux_queue_linux` implementation). Direct and transitive native links are
rejected for portable consumers. The old Monotonic capability/targets/header
were removed by the typed Time redesign; absence is checked explicitly.

Time owns typed monotonic observation/deadlines and Thread owns sleep. Their
common/Linux error, rounding, overflow and early-wake tests run with Host checks.
The former real Linux C++ wait scenario is now `Thread.TypedLinuxIntegration`.
Native 128-Hz exact/fractional deadline and ISR checks are retained in
`tests/thread/zephyr_fractional`; `scripts/test-messaging.sh` runs these with Queue
and independent messaging consumers. RTC-disabled Time/Thread tests also remain
in `tests/thread/zephyr`. Historical measurements and old test names are retained
in [the original evidence](../docs/messaging-validation.md), not new qualification.

## GPIO

The Host CTest suite includes `GPIO.Host`, which exercises the same native-free
input consumer against a deterministic in-memory provider that has no polarity model,
covering logical values, invalid arguments, unavailable state and injected provider
failure, and `GPIO.NativeStatus`, which maps native driver results to Crux statuses
without Zephyr. The native_sim Ztest uses Zephyr GPIO emulation for logical state,
including the active-low inversion taken from the Devicetree flag, argument errors,
rejection from interrupt context and reading before configuration.
`scripts/test-gpio.sh` also rejects missing, disabled and malformed Devicetree
selections, a disabled GPIO controller and an unsupported provider, and cross-builds
the same portable consumer for RT1060-EVKB and for RT1170-EVK board revision A
(Cortex-M7), each with its own alias overlay under `tests/gpio/evkb/boards`. Two further
native_sim apps under `tests/gpio/zephyr_failure` reach native driver failures through
the emulator: an open-drain flag gives `-ENOTSUP` and a pin the emulated controller does
not have gives `-EINVAL`. They run with Zephyr assertions disabled, because the generic
GPIO layer asserts on both requests when assertions are enabled. A controller that is
not ready cannot be induced with the emulator, and the mapping of other results is
covered on the Host only. No hardware operation is performed; the cross-build results
are compile/link evidence only.

## I2C

The Host CTest suite covers the shipped loopback provider (`I2c.HostContract`), the portable layer
against a test-only provider that models a register device and injects failures
(`I2c.PortableFaults`: argument rejection before any provider call, exact argument hand-off and
verbatim status propagation), and the native-result mapping (`I2c.NativeStatus`). A `native_sim`
Ztest uses Zephyr's I2C emulation controller for repeated-start message shape, native error
mapping (a missing acknowledgement, `-ENOTSUP` and `-ENOSYS`) and rejection from interrupt
context, and `tests/build/test_i2c_composition.py` rejects a missing alias, a disabled selection
and an unsupported provider. The emulated controller is always ready, so the not-ready status is
not exercised on Zephyr. `scripts/test-i2c.sh` runs these checks without hardware. They do not
qualify a physical bus or driver.

A first portable consumer, `tests/i2c/consumer/tmp117.c`, reads a TI TMP117 temperature sensor
(device identification and temperature, each as one combined write-then-read transfer) using only
the public header. `I2c.Tmp117Consumer` runs it against a test-only provider that models the
sensor's 16-bit registers: the conversion including negative and truncated values, the
no-conversion marker (`CRUX_ERR_NO_DATA`), the device-ID check, verbatim transfer statuses and
rejection of invalid arguments before any transfer. `tests/i2c_consumer_zephyr` runs the same
source on `native_sim` against a software TMP117 behind Zephyr's I2C emulation controller and
checks that each operation reaches the driver as a restarted write-read pair. The sensor model
is written for these tests from the data sheet; no physical TMP117 or board has been used, so
the temperature scaling and the pointer-register protocol are not confirmed against a part.

## SPI

The Host CTest suite covers the shipped loopback provider (`Spi.HostContract`), the portable layer
against a test-only provider that records each call and injects failures (`Spi.PortableFaults`:
argument rejection before any provider call, exact argument hand-off and verbatim status
propagation), and the native-result mapping (`Spi.NativeStatus`). A `native_sim` Ztest uses
Zephyr's SPI emulation controller for full-duplex data, Devicetree-selected transfer settings,
native error mapping (an I/O error, `-ENOTSUP` and `-ENOSYS`) and rejection from interrupt context,
and `tests/build/test_spi_composition.py`
rejects a missing alias, a disabled selection, a peripheral that holds chip select, selects
half-duplex or selects TI frame format, and an
unsupported provider. The emulated controller and its chip select are always ready, so the
not-ready status is not exercised on Zephyr. `scripts/test-spi.sh` runs these checks without
hardware. They do not
qualify a physical peripheral. Native-free consumption is checked only by the public header
compile checks in the script and by the common source checker; SPI has no external-application or
consumer-boundary profile yet because no application consumer exists.

A first portable consumer, `tests/spi/consumer/spi_flash_id.c`, reads the JEDEC identification of a
serial NOR flash (command `0x9F`) using only the public header, as one four-byte full-duplex
exchange so chip select spans the command and the response. `Spi.FlashIdConsumer` runs it against a
test-only provider that models the command and an idle line: decoding, the ignored first byte,
rejection of an all-0x00 or all-0xFF answer as `CRUX_ERR_IO`, verbatim provider statuses with the
output untouched, null arguments and the capacity code. `tests/spi_consumer_zephyr` runs the same
source on `native_sim` against a software flash behind Zephyr's SPI emulation controller, and
checks the single exchange, the composed mode (mode 0, most significant bit first, 8-bit words)
and native error mapping. The flash model follows the common `0x9F` convention from data sheets;
no physical flash or board has been used, so it is not confirmed against a part. The consumer's
own chip-select assumption (one exchange is one chip-select assertion) is the Crux contract
and is not observable on `native_sim`.

## Portable Serial

`scripts/test-serial.sh --host-only` runs independently authored public C17/C++17
consumers and the controller against disposable PTYs. The default command also
runs focused UART-emulator/native ownership suites and cross-builds the EVKB
fixture. It does not access hardware. See [Serial qualification](../docs/serial-validation.md)
for the contract, actual evidence, unresolved observations and separate physical
proposal. Root Host/ASan checks include the common and native PTY Serial cases.
The independent Host consumer selects `CRUX_BUILD_SERIAL` and `CRUX_BUILD_TIME` with an explicit Time provider
through the root entry point, keeps parent `BUILD_TESTING` intact and composes
the public `crux::time` clock contract. Static Zephyr composition and
UART ownership checks remain part of the full Serial script.

## Portable Logging

`scripts/test-logging.sh --host-only` runs the independent C17/C++17 consumer and
Controller checks. Full `scripts/test-logging.sh` also checks rejected native
compositions, actual UART-emulator output and the revision-B EVKB cross-build.
Root Host tests include `Logging.*`; the normal sanitizer preset covers them.
See [Logging validation](../docs/logging-validation.md) for evidence and limits.

The independent Host Logging fixture selects only `CRUX_BUILD_LOGGING` through
the root entry point, preserving parent testing and language settings. Logging-only
external profiles verify public C17/C++17 usage, native-boundary checks and exclusion
of unrelated components. Provider validation and native UART ownership stay unchanged.

## Portable System Health and Reset

System Health and Reset (Issue #30): the root Host suite includes focused common
lifecycle and real Linux monotonic-provider tests.
`scripts/test-system.sh --host-only` runs independent C17/C++17 consumers and
Controller policy checks.
`scripts/test-system.sh` additionally checks native ownership/configuration,
Zephyr `native_sim` mappings and the revision-B EVKB cross-build. None operates
hardware; normal Host reset/reset-cause services are explicitly unsupported.
See [System validation](../docs/system-validation.md) for evidence and limits.

The independent Host System fixture selects only `CRUX_BUILD_SYSTEM` through the
root entry point. External System profiles preserve parent testing/languages,
exercise C17/C++17 public usage, reject conflicting providers and exclude
unrelated components. The existing System contract and native ownership checks
remain unchanged.
