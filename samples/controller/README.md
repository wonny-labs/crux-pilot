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
qualification are not established. The scoped M2 result was accepted and merged through
[PR #9](https://github.com/wonny-labs/crux-pilot/pull/9) as commit
[`540ea7f`](https://github.com/wonny-labs/crux-pilot/commit/540ea7f934987989e429ae3767352b153fb2e747),
and M2 issues [#6](https://github.com/wonny-labs/crux-pilot/issues/6) and
[#7](https://github.com/wonny-labs/crux-pilot/issues/7) are closed. Its bounded evidence does not establish M3 acceptance.

## Independent M3 GPIO and health probe

M3 is in progress. This separate composition uses native GPIO for the onboard
`sw0` input and observes actual critical-worker completion before allowing a
watchdog feed. It adds no Crux public interface. The existing M2 composition and
its finite UTC-step scenario remain available.

From the documented Linux workspace, run `scripts/test-m3.sh` for the three
`native_sim` GPIO/health scenarios, native-client socket controls and EVKB
health probe/normal/network cross-builds. Outputs are
under `build/zephyr/twister-m3`, `m3-probe-evkb`, `m3-normal-evkb` and
`m3-network-evkb`; frozen inputs
are retained at `build/evidence/m3-inputs.json`. This command performs no hardware
operation. Host checks also exercise the portable readiness/freshness helper.

For a manual build in that environment:

```sh
west build -p always -b 'mimxrt1060_evk@B//qspi' \
  -d build/zephyr/m3-probe-evkb samples/controller/zephyr -- \
  -DZEPHYR_EXTRA_MODULES="$PWD" \
  -DEXTRA_CONF_FILE="$PWD/samples/controller/zephyr/m3.conf"
```

The development probe exposes `m3 status`, `m3 arm` and `m3 stall`. Arm is an
explicit physical watchdog operation and stall deliberately stops critical work;
use them only under the independently reviewed physical/recovery plan. Ordinary
boot does neither. Add `m3-normal.conf` after `m3.conf` (semicolon-separated in
`EXTRA_CONF_FILE`) for a normal image without Shell/test commands.

`M3_WORK` reports input read status and an unset/valid Time status. `M3_PENDING`
remains not ready: actual I/O stimulus and combined Ethernet integration are
unqualified. The earlier Ethernet hold has been released; the standalone health
image still contains the historical held-state diagnostic. Emulated GPIO transitions and the independent health probe cannot establish
#10 real-I/O, #11 networking or #12 combined-integration acceptance. Refer to
[the M3 execution record](../../docs/m3-validation.md) and the linked issue records
for actual results, support limits and physical gates.

## Isolated M3 network probe — software preparation

`m3-network.conf` and `m3-network.overlay` select a separate native IPv4 client.
Do not combine them with the independent health configuration. The overlay removes
LED ownership of GPIO1.9 so PHY reset retains that pin, and selects local test MAC
02:43:52:55:58:03. It uses EVKB 192.168.77.2/24 and Mac 192.168.77.1/24, with no
DHCP, gateway or DNS. This configuration is for a direct isolated cable only.
Confirm actual interface, MAC, cabling and current physical gate before execution.
Successful cross-builds and instrumented native tests do not qualify real Ethernet.

The Shell command `m3net` requests one round: ten framed TCP exchanges to port 42420,
then ten UDP datagrams to port 42421. Each exchange has one monotonic 2 s deadline.
Partial TCP transfers retain that deadline; UDP requires an exact 32-byte response
from the connected peer, including trial/sequence and fixed payload. The same
owner closes each socket before the next transaction. A close failure retains
uncertain ownership and prevents further rounds. The probe accepts at most seven
rounds: one baseline and three failure/recovery pairs. It sends no traffic at boot.

For the Mac peer, after the separate reviewed physical/network gate:

```sh
python3 tests/target/m3_network_peer.py --mode echo --duration 60 --output peer.jsonl
```

The peer binds only 192.168.77.1, accepts only 192.168.77.2, and closes its sockets
at its declared duration or 200 events. `--mode silence` withholds responses;
`--mode close` closes TCP after receiving its frame and withholds UDP. Output files
must be new. Logs identify receive/response facts and correlated trial/sequence.
No listener should be exposed to shared networks; payloads contain no credentials
or sensitive data. Remove the validation configuration to remove the Shell/client
entry point. This is a test boundary, not production protocol/security approval.

`M3_NET_STATE` distinguishes carrier, configured address and service success.
Restored carrier alone cannot restore service readiness: a complete successful
explicit round is required. `M3_NET_RESOURCE` reports application ownership and
native context/pkt/buffer availability. These are observations, not a leak-free
claim; physical cleanup acceptance still needs predeclared idle return bounds and
both-end repeated failure/recovery evidence. The standalone network probe does
not establish combined controller health/watchdog acceptance.
