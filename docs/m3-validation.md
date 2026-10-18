# M3 execution and validation

The current [#10], [#11] and [#12] acceptance criteria remain authoritative.
This records execution decisions, gates and evidence; it does not replace them.
The merged starting revision is `540ea7f934987989e429ae3767352b153fb2e747`.
Its tree matches the reviewed M2 head. The planning reference remains read-only at
`crux-platform@7d2287bdb5a252ee5e1fc51c7142813ba37800d6`.

## Authority, dependencies and current limits

The [current task authorisation] permits implementation, relevant tests, commits,
pushes, PR delivery and scoped physical verification after independent review and
successful preflight. It does not permit merging, auto-merge or issue/milestone
closure. AGENTS.md and public contracts remain unchanged.

The subsequent [user steering] holds Ethernet work and confirms that real I/O
stimulus cannot currently be supplied. Therefore #11 implementation and all peer
or network manipulation are held. #10 software and controlled native integration
tests can proceed, but physical stimulus/response acceptance remains Pending.
#12 independent diagnostics, readiness and health/watchdog work can proceed;
final integration still requires qualified real #10/#11 hand-offs. Independent
watchdog evidence cannot substitute for that combined controller result.

The expected target is the previously confirmed EVKB PCB B, i.MX RT1062, standard
8 MiB QSPI and J-Link PLUS 602003106. Existing setup evidence is reused within its
scope; software cannot detect an unreported wiring/jumper change. No physical
operation has been performed for M3. The Mac management connection is excluded.

## Execution order and proposed composition

1. Obtain independent plan PASS before implementation. Preserve C17, base/Time,
   public usage targets and pinned dependencies/toolchain.
2. Assess GPIO/I2C/SPI/EEPROM using M1 findings. Implement sample-owned native
   input access for the `sw0` candidate: onboard SW8, GPIO5.0, active-low/pull-up.
   This avoids the common DTS GPIO1.9 LED/PHY-reset ownership concern. Do not
   drive external pins, assume external devices, change wiring or write EEPROM.
   Actual real-I/O qualification still needs confirmed stimulus and observation.
3. Add small portable sample-owned readiness and health logic with Host tests;
   test native GPIO configuration, active-low transitions and missing-device/error
   outcomes using upstream GPIO emulation on `native_sim`. Emulation is not real I/O.
4. Add a separate M3 probe composition alongside the existing M2 composition.
   Reuse `controller_observe` and `controller_schedule_advance`; retain native
   types outside portable code. Extend maintained quality, CI and dependency paths.
5. Independently review actual source/software evidence and the exact physical
   and recovery plan before target use. Pin firmware, runner, generated config/DTS,
   map, ELF/BIN hashes and recovery image; review observer corrections before retry.
6. Deliver one Draft integration PR and portable verified evidence. Keep all
   required missing AC explicit and #10/#11/#12/M3 open. No narrower M3 success.

The proposed independent health composition has two native contexts. A priority-5
worker owns every Time init/get operation and GPIO read on a 100 ms monotonic phase
grid. The main context observes completed work at 100 ms and owns watchdog setup
and feed. Shell commands request actions, never call Time. Separating observation
allows critical work to stall while unrelated code remains visibly alive.

Publish a consistent progress snapshot: completion timestamp, sequence and result
must describe the same completed iteration. A short native critical section may
protect the snapshot; no Time/GPIO/blocking operation occurs inside it. Command
requests are atomic. The portable observer uses the actual completion timestamp,
not the time it first notices a counter change. It permits feeding only after
successful required work while its age is at most 500 ms. Failed/future/backward
observations cannot renew freshness. New valid progress is required after error.

Worker/main proposed usable stacks are 4096/2048 bytes. Independently check object,
reserved guard and usable size. Predeclared short-run untouched minima are
1024/512 bytes respectively. Record relevant native threads, RAM/Flash and actual
allocation. No application heap allocation is proposed; native subsystem use is
separate. These are probe conditions, not inherited M2 measurements or worst-case
budgets. No universal OSAL, health/lifecycle framework or Crux public API is added.

Development-only status/one-shot arm/stall commands are removable by configuration.
Normal startup never arms or injects a fault. Overall M3 readiness always remains
not ready while real I/O and required networking are missing. Component health is
reported separately. Unset Time is explicit; no implicit UTC is written.

## Software verification

Host cases cover required-service failure versus optional degradation, first work,
actual freshness/expiry, delayed observation, errors/recovery, counter wrap, invalid
and backward time. Native tests cover logical active-low GPIO transitions, missing
device and error output preservation. No test claims physical SW8 stimulus.

Use existing Host Debug/sanitizer, format, configured static analysis/warnings,
dependency controls and applicable Time/controller regression. Add focused runner
negative controls against false-ready, incomplete, repeated-boot and invalid
health/reset evidence. Extend CI with applicable `native_sim` and EVKB probe/normal
builds and retained reports/firmware. New commands must be implemented before being
reported as available. CI never accesses hardware. Existing M2 capture modes are
not relabelled as M3 validators.

## Physical health proposal — separate exact-input PASS still required

The lead author is the sole hardware owner; reviewer performs no hardware action.
Before target use, review the actual commands, runner/oracles and abort/recovery
inputs. Verify probe/CPU, 3.0–3.6 V VTref, retained board/QSPI setup, current UART
mapping and exclusive ownership. Unexpected identity/voltage/ownership stops work.
Keep the known-good M2 controller image and verified hash. Program only the
ELF/BIN-verified firmware range at `0x60000000`, then full-BIN readback. No full-chip
erase, boot layout/fuse/lock operation or unconfirmed pin drive is permitted.

Proposed watchdog settings: 2000 ms, minimum zero, no callback, pinned
`nxp,imx-wdog` driver, pause-on-debug for bounded recovery. Verify actual HAL value,
effective resolution, debug/sleep behaviour and startup/reset semantics from the
compiled source/configuration before use. A disable callback alone does not prove
live hardware disable/restart. No debugger is attached during qualifying runtime.

Each of three separately initiated trials must:

1. Start identified firmware, capture one fresh boot and normal worker progress;
   record reset supported/raw/clear/read results. Arm/fault defaults are off.
2. Arm after at least 1 s of continuous successful work. Observe at least 12 s
   (six nominal timeout intervals), successful progress-conditioned feeds and no
   unintended reset. Record monotonic diagnostics and stack use.
3. Request one critical-worker stall; observe the actual stall and continuing
   observer activity. No successful feed later than 750 ms after actual stall.
4. Observe the first fresh boot 1.0–5.0 s after the last successful-feed receive
   observation. This envelope includes 500 ms granularity, transport/scheduling
   uncertainty and boot time; it is not direct reset-pin timing. Buffered or
   discontinuous capture that cannot support this relationship is invalid.
   Supported reset-cause evidence must distinguish the watchdog event; unsupported,
   accumulating, ambiguous or uncleared information remains explicit.
5. Observe at least 3 s normal post-reset progress without automatic rearm, repeat
   injection or reset loop. Manual/debug reset cannot substitute for watchdog action.

Each trial has a 30 s capture bound. A failure stops the series; preserve it and
independently review corrections rather than retry until a pass. No post-result
widening of acceptance limits. Verify pause/recovery and boot-default behaviour
before watchdog use with a bounded normal preflight. Recovery is one identified
reset/start or established verified normal-image programming operation, not an
assumption about application `wdt_disable`. Stop hardware changes if recovery fails.
Final hand-off restores an identified known-good normal image/state, fault off,
UART/probe released. No network resource is touched while Ethernet is held.

## Evidence and resume state

The initial unpublished temporary checkout disappeared on interruption. Its plan
review is historical, not proof that restored bytes match. This restored concrete
plan requires independent review before implementation resumes. No M3 target
operation occurred. Persistent source and evidence paths are used from this point.

Separate source, runner and documentation identities; retain generated config/DTS,
maps, ELF/BIN, raw program/reset/UART logs, checksums, failures and actual review
records outside disposable build outputs/CI retention. Record tested revisions,
pending AC, target state, blockers and next useful action. Retain unresolved M2
sanitizer exit-timeout and debugger post-quit observations without claiming a cure.

The full goal still requires real #10 I/O, #11 TCP/UDP/link-failure recovery and
combined #12 integration. Those remain unresolved rather than replaced by the
independent health probe. M4 persistence and M5 qualification remain outside scope.

[#10]: https://github.com/wonny-labs/crux-pilot/issues/10
[#11]: https://github.com/wonny-labs/crux-pilot/issues/11
[#12]: https://github.com/wonny-labs/crux-pilot/issues/12
[current task authorisation]: https://github.com/wonny-labs/crux-pilot/issues/12#issuecomment-6243536217
[user steering]: https://github.com/wonny-labs/crux-pilot/issues/12#issuecomment-6243570872
