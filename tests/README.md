# Test organisation

Crux uses different test tools for different validation goals. A simulated build or a passing CI check does not replace physical-target evidence.

| Test purpose | Runner or harness | Environment |
| --- | --- | --- |
| Portable Time and controller logic | GoogleTest with CTest | Linux host |
| Zephyr components and integration | Twister with Ztest, console or the default harness | Zephyr `native_sim` |
| Reference-controller functional scenarios | Python runner through `scripts/test-controller.sh` | Zephyr `native_sim` |
| Build tools and validation oracles | Python `unittest` | Linux host |
| Time/RTC physical functional tests | Standalone pytest through `scripts/test-target.sh` | macOS, EVKB and J-Link |
| M3 I/O, network and integrated watchdog tests | Dedicated Python CLI capture runners | Physical EVKB and controlled Mac/peer setup |

No current Twister test uses its pytest harness. Standalone pytest and the M3 physical CLI runners are not Twister-managed hardware tests.

## Host: `time/`

GoogleTest/CTest exercises the C17 Time implementation, calendar conversion, and portable command
handler against the controlled host backend. The C++ tests include the public C headers and call
the C17 implementation, providing the C++ consumer coverage needed for this API. The test build
does not recompile production `.c` sources as C++ and does not maintain per-header fixtures or a
separate link-only executable.
CTest applies a 30-second limit to each discovered host test.

The root CMake option `BUILD_TESTING=OFF` skips GoogleTest and C++ configuration. Set
`CRUX_BUILD_HOST_SAMPLE=OFF` as well to configure and build only the portable and host library
targets.

## Zephyr: `zephyr_time/`

Twister builds and runs the C17 Ztest application on two `native_sim/native/64` configurations: one
with an RTC alias and one without it. The Time sample is separately cross-built for EVKB, and actual RTC/UART behaviour is checked by the macOS physical suite. Tests cover Zephyr error mapping, RTC backend behaviour, the
shared command handler, and the actual registered Zephyr Shell adapter using Zephyr's dummy Shell
backend. The trailing-argument case verifies that Shell passes syntax errors to the shared Crux
handler for the documented `ERR INVALID_ARGUMENT` result.

Host tests own portable behavior. Ztest covers native integration and the Shell boundary. The two
suites do not duplicate every case across frameworks.

Run the Time Zephyr integration checks from the repository root in the Linux Dev Container,
after setting up the west workspace and SDK:

```sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

This runs `native_sim` Ztests, builds the native simulation sample and cross-builds the
EVKB image. It does not program or reset the physical board.

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
status; `scripts/test-controller.sh` validates normal/missing-RTC native_sim compositions
and cross-builds EVKB without hardware access. See the [controller conditions and limits](../samples/controller/README.md).

## M3 integration and physical verification

`scripts/test-m3.sh` runs the M3 `native_sim` suites through Twister, checks native output and network behaviour, and cross-builds selected EVKB configurations. It does not flash or reset the board. GPIO, Ethernet and combined health/watchdog target validation is recorded in [M3 validation](../docs/m3-validation.md).

The physical M3 procedures use separately authorised, interactive Python CLI capture runners under `tests/target/capture_m3*.py`. They are not pytest tests and are not run by GitHub Actions. Use the recorded hardware setup, source and image identities, operator gates and recovery procedures before any physical operation.

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
