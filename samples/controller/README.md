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

## Physical verification — bounded evidence

[Authorised physical verification](../../docs/m2-physical-verification.md) ran the identified
controller three times on EVKB B/QSPI: 20 cycles each, expected unset/valid Time and backward
UTC step, independent monotonic phase, joined completion. Observed intervals were 100 ms at
uptime resolution, with zero recorded millisecond lateness and no skipped phases in this
normal unloaded scenario. Native execution probes separately exercise deliberate overrun,
lifecycle, synchronisation, timeouts and bounded messages on the target.

The requested usable worker stack is 4096 bytes; this ARM configuration allocates 4160
including a 64-byte MPU guard. Physical controller high-water accounting observed 408 used /
3688 untouched bytes, exceeding the predeclared 1024-byte short-run headroom objective.
Simulator accounting is not used as a Cortex-M measurement. Configured heap zero and static
Flash/RAM allocation are distinct from measured worker use or system worst-case sufficiency.

The evidence retains the first rejected capture, observer correction and programmer
post-disconnect diagnostic. The earlier sanitizer exit-timeout cause is still unresolved.
Physical POSIX, long-duration/concurrent-load behaviour and worst-case timing/resource
qualification are not established. PR #9 remains Draft and M2/issues #6/#7 open for human
review and integration; passing this bounded physical plan does not authorise merging.
