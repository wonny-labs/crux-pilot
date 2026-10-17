# Early reference controller

This product-neutral M2 consumer reports Time availability/UTC and cycle progress. Its
portable core contains no OS headers. The host and Zephyr compositions own scheduling,
initialisation and all Time access; neither enables a concurrent Time command writer.
The sample-owned `controller.h` and helpers are not new Crux platform interfaces.
The core's private Time header path lets tests select the existing instrumented Time
composition; final applications consume `crux::time`. Base/Time production code and
public contracts are unchanged.

## Scenario and verification conditions

Conditions defined before the first controller run: 20 observations on a 100 ms monotonic
phase grid, normal unloaded composition, one execution owner. Cycles 0–1 observe unset
Time. Cycle 2 explicitly sets `1792238400` UTC seconds and cycle 6 explicitly steps UTC
back to `1792234800`. These are deterministic scenario inputs, not an implicit date or
synchronisation to the host. Unavailable/error results never display a numeric UTC.
The host Time store remains deterministic; the target RTC may progress between cycles.

The composition selects the first future phase after work, skipping elapsed releases
rather than generating catch-up bursts. The portable schedule tests include deliberate
overrun, invalid input and overflow rejection; Time integration tests include failed
initialisation/retry, unset, read failure and recovery. No latency/jitter upper limit is
claimed. The 10-second runner and native join limits are test liveness bounds, not
real-time guarantees. The [execution investigation](../../docs/execution-profile.md)
records the independent decision and native/POSIX limitations.

Zephyr uses one pre-emptive priority 5 worker with a static 4096-byte stack and no configured
kernel heap. Linux invokes the same portable behaviour in its main thread with
`CLOCK_MONOTONIC` absolute sleeps. Clock/sleep errors stop the host scenario; Time init/set
failure stops either composition. `native_sim` missing-RTC validation must report failed
initialisation and no cycles. The simulation runner terminates the idle kernel after the
final success/failure marker; it does not claim a Zephyr kernel shutdown API. The finite
host process must exit successfully, allowing sanitizer shutdown diagnostics to fail it.

## Build and run without hardware

In the documented Linux environment, `scripts/check-host.sh` builds/tests the controller
alongside existing tests. Run `build/linux/debug/samples/controller/host/crux-controller-host`
for its output. `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh` exercises the same
scenario with ASan/UBSan. `CRUX_BUILD_REFERENCE_CONTROLLER` controls the host executable;
its default follows `CRUX_BUILD_HOST_SAMPLE`, preserving the library-only setup.

`scripts/test-controller.sh` builds/runs normal and missing-RTC `native_sim` compositions
and cross-builds `mimxrt1060_evk@B//qspi`. It performs no hardware operations. Scenario logs
are under `build/zephyr/controller-{normal,missing-rtc}/scenario.log`; the EVKB build is
under `build/zephyr/controller-evkb/`. Existing host/Zephyr workflows run these paths and
retain reports/configuration/artefacts for 14 days.

## Physical verification — Pending

The new controller has not run on an EVKB. Actual period/lateness under the stated
scenario, worker stack use/sufficiency, runtime resource/failure behaviour and the
identified firmware's success/failure cases require a separately authorised physical
run. A cross-build or simulated stack measurement cannot satisfy those criteria. M2
and issue #7 remain open. Later concurrent network/storage-load qualification remains M5.
