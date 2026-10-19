# M3 execution and validation

The accepted result and merged implementation are recorded under
[User acceptance and closeout](#user-acceptance-and-closeout). Earlier interim
status and failed attempts are retained as historical evidence.

The current [#10], [#11] and [#12] acceptance criteria remain authoritative.
This records execution decisions, gates and evidence; it does not replace them.
The merged starting revision is `540ea7f934987989e429ae3767352b153fb2e747`.
Its tree matches the reviewed M2 head. The planning reference remains read-only at
`crux-platform@7d2287bdb5a252ee5e1fc51c7142813ba37800d6`.

## Authority, dependencies and current limits

The [current task authorisation] permits implementation, relevant tests, commits,
pushes, PR delivery and scoped physical verification after independent review and
successful preflight. That execution-stage authority did not permit merging, auto-merge or issue/milestone
closure. The subsequent explicit user acceptance below authorises reviewed squash
merge and Issue/M3 closure. AGENTS.md and public contracts remain unchanged.

The [current SW5 physical result](#uart-remediation-and-sw5-physical-handoff) and
[qualified Ethernet hand-off](#qualified-ethernet-handoff) establish the identified
#10 and #11 component paths. The [final integrated handoff](#final-integrated-physical-handoff)
independently supports the scoped #12 physical criteria; final software CI and
PR review still govern readiness. Earlier Ethernet/stimulus holds were released by the recorded task
resumption; fresh operator availability is still required for each physical series.
The earlier failed SW8/SW5, carrier, diagnostic and timestamp attempts remain
preserved and are not retrospectively passed. A component result does not establish
simultaneous controller integration or final M3 completion.

The target remains the confirmed EVKB PCB B, i.MX RT1062, standard 8 MiB QSPI
and J-Link PLUS 602003106. Software cannot detect an unreported wiring/jumper
change. DAPLink/UART USB also supplies board power; its removal is a power-off.
The Mac management connection is excluded from the dedicated Ethernet tests.

## First integrated programming entry — stopped before qualification

The first identified `a940bb308165d55822748ef5c8bdcde7febb182f` integrated
entry passed all four applicable CI workflows, fresh USB protection, ownership,
probe/CPU/voltage checks and full comparison of the existing normal image. The
candidate programming operation then failed with a J-Link RAMCode Prepare timeout
and flash-information error. Candidate verification and its post-download start
were not reached. No integrated boot, SW5 cue, application peer, network window
or watchdog trial occurred. This attempt is a programming failure; all required
#12 functional and watchdog outcomes remain Pending.

The full raw UART capture contains an existing `fe9f54f683ee` normal boot during
the commanded resets. Its subsequent rejection by the integrated-profile grammar
is secondary evidence of the wrong profile and does not replace the primary
programming error. A separately reviewed read-only identification later matched
all 41,232 bytes of that original normal image (SHA256
`238b352fd66bbfea984c4210510dd398cfe808fcb93c5f11a142fa4ba5bd24c0`).
The CPU was halted at the query. This comparison does not establish extended
Flash integrity, continuous execution or the programming failure's cause. No
automatic programming or reset followed. The user disconnected DAPLink/UART USB,
which powers off this board, and the identified temporary guard was retired with
strict process-absence evidence. UART, probe and peers were released.

The next software procedure candidate removes the separate `r`/`h` before
`loadfile`, which already performs an implicit reset/halt in the actual log.
Integrated programming reuses the existing shared image-command generator; one
load, full verification and the subsequent reset/start remain ordered, with the
same address, pinned tools and failure stops. [SEGGER's RT1060 guidance] warns
that reset during initialisation can cause standalone programming problems.
That guidance supports this bounded procedure change, but does not prove the
observed timeout's cause or that the candidate resolves it. It is not a firmware,
FlexRAM, MPU, loader-selection, reset-strategy or tool-version change. A new
independently reviewed physical entry is required before another programming
attempt; the original failed series must remain preserved.

[SEGGER's RT1060 guidance]: https://kb.segger.com/i.MXRT1060

## Execution order and proposed composition

1. Obtain independent plan PASS before implementation. Preserve C17, base/Time,
   public usage targets and pinned dependencies/toolchain.
2. Assess GPIO/I2C/SPI/EEPROM using M1 findings. Implement sample-owned native
   input access for the `sw0` candidate: GPIO5.0, active-low/pull-up. The physical
   EVKB user button is SW5; the pinned common DTS calls this input SW8.
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

The capture tool preserves all raw bytes and receive chunks, polls, transmitted
commands, reset/programming output and rejected attempts. A reader runs during
reset/start. Known unarmed output before the first fresh reset banner is retained
as a pre-boot prefix and excluded from qualifying rows; an unexplained boot, arm
or feed in that prefix rejects the capture. The source/profile/default-off banner
must follow the native banner within 500 ms. Reader gaps above 250 ms reject the
trial. Receive/target interval disagreement above 150 ms for feeds or 250 ms for
work reports also rejects it. These are capture-validity limits, not timing
guarantees. Every observer report needs its stack report, and unhealthy recovery
reports cannot be filtered away. Revalidate the final received rows after stopping
the reader, including the last complete line.

Before arming any watchdog, the normal image must demonstrate at least 3 s of
advancing required work, healthy observation, supported watchdog reset reporting,
successful cause clearing/readback and declared stack margins. It has no Shell or
fault commands. The probe waits 2.5 s after boot before requesting arm and 12.5 s
after the actual arm report before requesting a stall; the oracle independently
requires at least 1 s pre-arm healthy evidence and at least 12 s target-clock
health before the actual stall. There are exactly three trials and no retry.
Runtime rejection stops injections and restores/verifies the normal image once;
probe/programming diagnostics stop further hardware changes, preserving the error
and current target state for review. A recovery failure is never retried silently.

Software-only preparation uses `tests/target/prepare_m3_image.py --build <build>
--inputs <m3-inputs.json> --profile probe|normal --output <manifest.json>`.
It independently checks ARM ELF load bytes against BIN, standalone QSPI headers,
generated configuration, stack objects, pinned inputs and file hashes. Keep the
probe/normal manifests and their files immutable for qualification. After exact
physical-plan PASS and ownership preflight, the explicit hardware entry point is
`tests/target/capture_m3.py --probe-manifest <probe.json> --normal-manifest
<normal.json> --recovery-bin <known-good-M2.bin> --evidence <new-directory>
--uart <verified-device-path> --uart-serial <verified-USB-serial> --program`.
It requires a unique matching DAPLink USB serial and path, clean committed runner
source and the retained M2 recovery hash. This command must not be invoked merely
because software controls pass.

## Evidence and resume state

The initial unpublished temporary checkout disappeared on interruption. Its plan
review is historical, not proof that restored bytes match. The restored concrete plan received independent PASS at `6b46eba`;
subsequent source/result reviews and physical gates retain their separate scopes.
No M3 target operation occurred during restoration. Persistent source and evidence paths are used from this point.

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


## Interface assessment and reusable software hand-off

This reuses the [M1 assessment](platform-m1-readiness.md); it does not repeat
board bring-up or convert source support into physical support.

| Interface | Intended operation and native path | Access and coexistence constraints | Current evidence and next disposition |
| --- | --- | --- | --- |
| GPIO | Read onboard EVKB SW5 (`sw0`, GPIO5.0, active-low/pull-up; common DTS label SW8) through native MCUX GPIO. The sample-owned adapter configures input only and preserves output on read failure. | Existing board input; no external drive or wiring. It avoids GPIO1.9 shared by LED/PHY reset. The user has now confirmed EVKB/SW5 and can provide stimulus. | Controlled `native_sim` transitions, missing-device and actual driver-error tests pass. Physical SW5 input qualification passed in the [identified normal composition](#uart-remediation-and-sw5-physical-handoff). Other pins remain unverified. Joint GPIO/Time/Ethernet/watchdog operation is qualified only within the [final integrated handoff](#final-integrated-physical-handoff). |
| I2C | Native LPI2C1/`arduino_i2c`, SDA AD_B1_01 and SCL AD_B1_00; candidate peripheral reads. | No confirmed external device/address, pull-ups or 3.3 V wiring; optional onboard sensor population is not established. | Unverified/unavailable fixture. Retain upstream driver; confirm actual device/routing/address before selecting a trial. No scan or pin drive was performed. |
| General SPI | Native LPSPI1/`arduino_spi`: SD_B0_01 PCS0, SD_B0_00 SCK, SD_B0_03 SDI, SD_B0_02 SDO. | USDHC sharing and R278–R281 routing/population are unresolved; no safe fixture is confirmed. LPSPI3 shares LCD pins. | Unverified. Resolve routing and ownership before use; QSPI/XIP evidence is not SPI-transaction evidence. No rework or loopback is assumed. |
| EEPROM | Existing AT24 I2C/AT25 SPI driver is a candidate. | No confirmed populated device, geometry, write-protect connection or disposable write range. | Unavailable fixture/unverified support. Do not write EEPROM or claim persistence; confirm all prerequisites before selecting it. M4 storage remains outside scope. |

The GPIO adapter lives in `samples/controller/zephyr/device_input.[ch]`. Portable
health/readiness code is sample-owned under `core`/`include`; it has no native
headers. No new public device API or production driver is added. The read-error
fixture is test-only because the pinned GPIO emulator lacks a read-error hook.
It exercises the real adapter through the native GPIO API rather than replacing
the adapter's return value.

The independent M3 composition consumes the existing portable controller helpers
and `crux::time`. `m3.conf` selects the development probe; `m3-normal.conf` removes
Shell/test commands. General memory-write, kernel-control, date and device-init
Shell modules are disabled in the probe. The remaining one-shot commands request
atomic flags and do not access Time. They are local UART development controls,
not a remotely exposed service or a production security qualification.

Use `scripts/test-m3.sh` and the controller README for software reproduction.
The first native run failed because the fault overlay omitted its base overlay;
that run is retained, and the remaining unfinished configurations were invalidated.
The correction explicitly selects each base overlay and asserts the requested
fixture at compile time. At `d07ac4c`, all three configurations and 13 executed
cases passed; a separate non-author reviewer verified their JSON/XML/raw logs.
The first EVKB compilation then exposed ARM header ordering, corrected separately.
No failed cross-build is physical evidence. Host Debug and sanitizer each passed
40/40 at `80f7205`, with unchanged portable production logic in later corrections.
The corrected format command, configured analysis, dependency checks and 18 existing
build-safeguard controls passed. The original missing-format-tool failure and the
unexplained Calendar process-exit timeout are retained; later passes do not cure it.

## Physical preflight outcome and hand-off

The separate reviewer approved the exact `fe9f54f` standalone health images and
recovery plan, conditional on successful current preflight. The first invocation
stopped at the Mac UART ownership inventory: `lsof` remained uninterruptible after
the declared timeout. Python's original timeout cleanup could wait indefinitely.
The lead stopped the runner before serial open or any J-Link invocation. No M3
image was programmed, reset or physically qualified, and Ethernet was untouched.
The prior board image remains unchanged; its current runtime was not observed.

The affected runner was revised to use a new process group, file-backed output,
a 10-second wait and at most one further second to reap after group termination.
An unreaped group is recorded as uncertain ownership and always stops target access.
Negative controls cover this case without accessing hardware. There is no automatic
retry, ownership exemption or weakened physical limit. A fresh successful ownership
inventory and independent approval are required before resuming physical work.
The original attempt, process state, exact image manifests and independent REVISE
record remain in the portable evidence; an image-plan PASS is not runtime evidence.

The last qualified local firmware source is `fe9f54f683eee41cb7700f3918d4c0123db99eae`.
Its development BIN is 58,164 bytes (SHA-256
`7b9cc541d75d03c3779d35985fe7a3cafd0de232b139784d80cc62927e1c8896`);
its normal BIN is 41,232 bytes (SHA-256
`238b352fd66bbfea984c4210510dd398cfe808fcb93c5f11a142fa4ba5bd24c0`).
These are identified cross-builds, not physically tested firmware. Later tooling
and documentation revisions are identified separately in the PR/evidence record.

## Acceptance coverage and held work

The table references AC order in the linked authoritative issues. It records
partial evidence, not replacement acceptance criteria or checked completion.

| Issue / AC | Evidence or current disposition |
| --- | --- |
| #10 / 1 | Interface assessment and actual SW5/GPIO5.0 selection; PHY reset GPIO1.9 remains separate. |
| #10 / 2 | Bounded adapter/error cases and identified fe9 SW5 stimulus/response qualified. |
| #10 / 3 | Actual five-group/45 s SW5 physical input/UART hand-off independently PASS. |
| #10 / 4 | Portable Host/native GPIO checks and qualified physical source retained; current-head CI is recorded in the PR. |
| #10 / 5 | Qualified fe9 source/configuration/fixture/build entry points retained; no other I/O path inferred. |
| #11 / 1–6 | Identified d2 seven-stage TCP/UDP/link-loss/restart series, resource return and final normal recovery independently PASS; see qualified hand-off below. |
| #12 / 1 | Three-owner native composition and full scoped combined physical coverage independently PASS; see final handoff. |
| #12 / 2 | Exact UART/source/readiness/error/reset facts and native logger evidence independently replayed across the identified integrated cases. |
| #12 / 3 | Actual five-group SW5 input with 39 completed network pairs PASS; real link loss and explicit communication recovery PASS. |
| #12 / 4 | Time remains worker-owned; applicable Host/native logic and output controls retained. Exact final source/configuration/CI is recorded in the PR. |
| #12 / 5 | Joint healthy feeding and separately bounded Time/network-owner faults independently verified on the EVKB. |
| #12 / 6 | Three Time-owner and three network-owner trials independently PASS with watchdog cause 0x10 and required fresh unarmed post-reset observations. |
| #12 / 7 | Frozen images, ABI/configuration, exact adapters, raw UART/peer events, resource observations and independent replay are retained with failed attempts. |
| #12 / 8 | Scoped physical coverage and final normal handoff independently PASS. Final software CI/PR acceptance remain separate; Issues and M3 stay Open. |

The Draft PR and issue comments identify current-head CI, independent verdicts,
physical results, archive identities and resume state. Keep all issues/M3 open for
human acceptance. Ready requires the complete original M3 success conditions;
no merge or closure is authorised. Physical series require fresh runtime identity,
ownership/protection, exact-image and actual operator-availability checks.

## Resumption and renewed preflight stop

The user released the Ethernet hold, confirmed direct isolated EVKB-to-Mac USB
Ethernet cabling and offered SW8 button actions. AX88179 en5 is configured as
192.168.77.1/24 without a router; en12 remains DHCP and the management route is
en0. No network settings were changed by this work. Carrier was still absent.
The earlier held-state and partial results above remain historical evidence.

The current DAPLink path changed to cu.usbmodem11302. The runner now requires an
explicit expected USB serial and rejects missing, duplicate, wrong-path or wrong
VID/PID matches. This is a current mapping, not proof of historical serial equality.
Fresh ownership initially passed and the affected correction received independent
PASS. The subsequent standalone attempt reached UART open and J-Link preflight,
but never reached EVKB programming or a reset/runtime capture/watchdog trial.
Installed Commander V9.86a attempted an automatic probe firmware update and then
failed to reconnect. The lead stopped the parent runner before target writes;
the child exited independently. The user subsequently reconnected the probe and
reported normal indicators. Probe operational identity is still unverified.

A separately reviewed suppression-only invocation accepted DisableAutoUpdateFW
without opening a probe. The runner now pins exact Commander and DLL hashes in
`tests/target/jlink-tools.json`, disables automatic target connection, and executes
firmware-update suppression before explicit USB selection and target connection.
It removes DYLD overrides and rejects firmware-maintenance diagnostics. The
manifest is specific to this Mac qualification setup, not a toolchain upgrade or
permission to update probe firmware. A changed tool requires affected review.
See the official [command string](https://kb.segger.com/J-Link_Command_Strings#DisableAutoUpdateFW)
and [Commander](https://kb.segger.com/J-Link_Commander) documentation.

The next identity-only check stopped before J-Link invocation: the bounded UART
ownership inventory again timed out, leaving lsof PID1778 uninterruptible and
unreaped. Hardware access remains stopped; do not bypass ownership, silently retry,
or treat probe indicators as successful preflight. Preserve both failed attempts
and verify resolution of the outstanding process before any fresh physical gate.
Ethernet software preparation can continue independently. At that stop no M3 physical result had passed. The subsequently resumed
standalone health result below supersedes that runtime status; peer listeners
have not been run.

### Standalone health result and corrected button fixture

The [resumed standalone result](https://github.com/wonny-labs/crux-pilot/issues/12#issuecomment-6247354666)
passed three physical watchdog trials, full BIN verification and final normal
observation. Independent review reconstructed UART rows from receive chunks and
reran the actual validators. Last-feed-to-boot receive intervals were
2.014542/2.017607/2.009744 s; maximum reader gap was0.07335 s. Probe minimum untouched
worker/main stacks were3744/1352 bytes; normal3776/1352. Original limits were retained.
These are standalone native health results, not combined M3 qualification.
The final normal image is original fe9, BIN SHA256
238b352fd66bbfea984c4210510dd398cfe808fcb93c5f11a142fa4ba5bd24c0,
without Shell/fault commands. UART/probe were released.

The first manual GPIO trial failed: all51 source reports remained pressed=0,
although the operator followed the requested five SW8 actions. Raw capture and
failed oracle remain preserved; none counts as a passed stimulus trial. The lead
had applied the common DTS label to the physical EVKB without checking its actual
button designation. [NXP's EVKB manual, table5](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/mcuxpresso-ide/9718/1/MIMXRT1060EVKBUM%20%282%29.pdf)
identifies SW5 as GPIO5-00 User and SW8 as ON/OFF. The user subsequently confirmed
the actual MIMXRT1060-EVKB model and presence of SW5. Do not repeat SW8 instructions.

The corrective proposal changes only the physical button designation to EVKB SW5;
firmware GPIO5.0 input, full BIN identity and acceptance limits stay unchanged.
After affected independent PASS and fresh preflight, one trial verifies the current
normal BIN, resets/starts it and captures a fresh released baseline for5 s. Only
then may GPIO_READY cue five press3s/release3s pairs within a45 s action window.
Acceptance requires exactly five observed press groups, at least two1s reports
for each press/intermediate release, at least3s final released observation, fresh
source/normal health/stack checks and continuous receive/target timing. Total
capture is bounded60s. No flash, watchdog arm, network or external pin drive is
included. Failure stops, preserves raw records and leaves the identified unarmed
normal image; no automatic retry or loosened limits. `capture_m3_gpio.py` retains
the final rows, failed attempts and action-window timestamps.

The first corrected SW5 capture (`physical-gpio-fe9f54f-03`) observed five press
groups but failed the unchanged oracle: one press spanned938ms and one intermediate
release had only one report. Independent raw reconstruction confirmed both
failures. The preceding attempt02 stopped at ownership preflight and is recorded
separately as blocked, not a functional failure. The blocked lookup ended after
the user reconnected DAPLink USB; fresh serial identity and ownership then passed.

The reviewed corrective stimulus uses spoken Korean press/release cues every4s
(five pairs within the same45s window), with final release at36s. It retains the
original firmware, observation minima and60s capture bound. Cue schedules, actual
launch/completion times and failures are retained; they describe instructions,
not measured physical button timings. Speech has bounded child termination/reap;
late, failed or uncertain speech stops the trial. The user confirmed an actual
local voice check was audible. Prior failures remain failures; the next trial
requires fresh preflight and independent raw-result review.

Paced trial04 retained ten successful spoken cues but all45 action reports were
released. The user reports performing later actions; this mismatch remains
unexplained. A separate static-state diagnostic does not replace qualification.
Its first attempt was blocked before target access. Attempt02 passed full normal
BIN/probe verification without reset, then aborted on the12s acknowledgement
limit: the held confirmation was recorded18.54s after the request and6.52s after
capture ended. Its17 released reports cannot establish the later held state.
The user confirmed release; both procedural failures remain retained.

The reviewed diagnostic correction allows a30s held acknowledgement,1s settling
and5s observation, then an explicit release acknowledgement and observation.
Whole-capture checks and the90s total remain unchanged. The confirmed SW5 User
GPIO input, with power management disabled, may be held for at most60s during
this diagnostic; this reasoning does not apply to SW8 ON/OFF. Instructions require
release at60s even if communication fails. Actual acknowledgement timestamps are
not physical edges. Exact source review and fresh preflight are still required.
An observed mismatch during confirmed hold stops further hardware attempts.


### UART remediation and SW5 physical handoff

The manual Shell observation prompted re-identification rather than an assumed
M3 continuation. The mapped firmware matched the complete 65,964-byte Time sample,
not either M3 image. J-Link 602003106 reset then produced one fresh Time boot and
READY on the identified DAPLink UART after 1.291543 s. Independent raw review
confirmed operational UART/J-Link target association for that session.

A bounded mount-attribute call reproduced the host lookup hang on the exact
DAPLink virtual disk. Normal unmount also timed out. After USB disconnection
cleared the blocked children, a temporary process-local mount veto for that exact
USB serial/VID/PID kept the virtual disk unmounted; the original strict UART owner
inventory then passed. This is a session workaround, not a permanent OS repair
or permission to skip ownership checks. No persistent system/interface policy changed.

The original fe9 normal image was programmed and all 41,232 bytes verified, BIN
SHA256 238b352fd66bbfea984c4210510dd398cfe808fcb93c5f11a142fa4ba5bd24c0.
Its first observation remains FAILED because the final stack line was truncated;
`RESTORED_CAPTURE_TRUNCATED` records image restoration without passing that capture.
The next GPIO preflight stopped on a reported idle halt, before UART/reset/cues.
A reviewed state-aware preflight then observed running before BIN verification and
an idle halt afterwards at PC 0x60006416 inside the exact image's `arch_cpu_idle`.
This bounds the observed transition to that verification stage; it does not prove
permanent post-detach halt or a firmware fault. No reflash followed.

One explicit reset/start and the unchanged fresh normal baseline preceded the
original 45 s five-group SW5 qualification. `physical-gpio-reidentified-fe9f54f-02`
passed: action 45.017456 s; press reports 3/4/4/4/4 spanning 2000/3062/3000/3049/3000 ms;
intermediate releases each 4 reports/3000 ms; final released 7 reports/6000 ms. Minimum
untouched worker/main stacks were 3744/1352 bytes; maximum reader gap 47.234 ms.
Independent reviewer `/root/m3_resume_review` reconstructed raw bytes and reran
`fresh_rows`/`validate_gpio`: PASS for EVKB SW5/GPIO5.0 input handoff only.
Previous failures remain retained and unexplained where applicable. Ethernet,
other device paths and combined #12 qualification are not established by this run.

The normal image has Shell/fault commands disabled; no watchdog arm, network
operation or external pin drive occurred. The user disconnected DAPLink/UART
before the temporary veto process was terminated. Last observed work advanced
with SW5 released; power/execution after USB removal was not observed.

Portable supplement `crux-m3-uart-gpio-fe9f54f-20261018.tar.gz` contains 307 files,
including original images/configuration, complete source-history bundle through
a9a6d3a, frozen helpers/oracles, raw failures/results and independent reviews.
Archive SHA256 6f9b6283bc27a767456cfd90abbf475339dea958acb59dc1a861ad1c7cf9ac5a;
the archive was reopened and every relative manifest hash verified. Dirty,
unfinished integrated-controller changes were neither built nor programmed.

## Ethernet and combined-controller qualification preparation

The next scoped configuration reuses the qualified SW5 input and the native
network client in an integrated development composition. It has a 100 ms Time/
GPIO owner (priority 5), one network owner (priority 6), and a main health observer.
Each has 4096 usable stack bytes, with no application heap. The network owner
starts no application traffic automatically. One explicit command admits a 60 s
window; each complete TCP/UDP pair has a 14 s exchange/cleanup budget and a gap of
at least 1 s after completion. Pair failures or window expiry publish inactive,
unhealthy state. The same protocol, fixed peer and native resource fence are
used by the independent network probe and the combined controller.

Joint readiness and watchdog feeding require both actual owners' successful,
fresh progress and carrier availability. Time freshness remains 500 ms; network
freshness is 16000 ms, covering the bounded complete pair. Arming requires at
least 1 s continuous joint health and 40 s remaining in the window. Owner-local
fault requests expire after 15 s and require at least 25 s remaining when the
owner handles them. Rejected requests cannot inject a stall. Native watchdog
configuration remains 2000 ms with debug-halt pause. These are application test
policies, not new Crux interfaces or platform timing guarantees.

`tests/controller_integrated` executes the actual composition source with
controlled clock, thread creation and driver/socket-owner seams. It tests the
state machine; it cannot prove real scheduling, sockets or watchdog action.
Native socket mock and actual stack fixtures remain distinct. The standalone
normal profile preserves its previous behaviour. Integrated normal disables
Shell/fault commands and remains unarmed without an automatic network window.

The prospective #11 physical sequence uses seven explicit logical stages: echo,
silence, echo, TCP closure with UDP echo, echo, physical link loss, then an explicit
restart followed by echo after cable restoration. Each full round attempts ten
TCP and ten UDP frames, with a 150 s absolute round budget and 2 s per exchange.
Native context settling is capped at 5 s and clamped to the round deadline.
Owned sockets must be zero; immutable idle contexts zero; RX/TX packet slabs
16/16 free and data-buffer pools 32/32 free at declared idle checkpoints. Require
three consecutive approximately 1 s idle reports within 5 s and at least 1024
untouched main-stack bytes. A link-loss cleanup fence is retained as failed
in-place reclamation; the preselected one restart is the recovery mechanism.
It does not establish automatic recovery or leak-freedom across resets.

The isolated peer binds only 192.168.77.1 on the dedicated AX88179/en5; the EVKB
uses 192.168.77.2. Management networking is excluded. Ordinary peer lifetime is
180 s, with 300 s for the physical-unplug stage to cover human response and the
whole round. The complete series is bounded to 1800 s. The capture retains raw
UART and both-end frame records; the pure oracle rejects identity, framing,
resource, readiness, deadline and post-fence work discrepancies. USB mount/owner,
probe, peer or transport uncertainty stops further target changes. Only a known
functional-oracle failure permits the separately identified normal restoration.
Physical #11 acceptance and integrated #12 trials remain Pending until actually
executed and independently reviewed; successful software tests/builds do not
satisfy those physical gates. No issue closure or PR merge is authorised here.

Image qualification found a four-byte alignment hole in the network ELF load
segment. The pinned build uses `objcopy --gap-fill 0xFF`, so those bytes are FF in
the BIN while ELF segment padding is zero. The corrected checker maps every
allocated file-backed section through its unique bounded load segment and
reconstructs the entire BIN, including the expected FF holes. Code, hole,
mapping, overlapping-section and duplicate-load tampering are rejected. Firmware
bytes were not changed to accommodate the checker. New manifests distinguish
section-byte verification from the historical whole-segment claim; historical
manifests are accepted only after a fresh actual-image check.

A separate preflight reproduced process-relative `time.monotonic()` origins in
the Mac's Python 3.9.6. The M3 capture, peer and temporary-guard tools now explicitly
share `clock_gettime(CLOCK_MONOTONIC)`; UTC is not used for test deadlines. A real
child-process test brackets its timestamp inside the parent interval, and the
capture/peer records identify the clock. No Ethernet trial had run with mixed
origins. Earlier single-process GPIO/health timing remains within its original
scope. The fresh guard's original invalid-origin record is retained; its remaining
lifetime was conservatively bounded using the same process/start identity and
reported elapsed age, with the complete bounded query duration plus floor/launch
margin subtracted. No guard lifetime was extended and no second mount was tried.

The first physical network attempt used source `1fda8d291e89`, BIN 110476 bytes,
SHA256 da76d36e86a1b28c8a502ff1142fc16becb4edd463da0de1237933cc8aedcaef.
Its first healthy stage passed all twenty both-end exchanges and the declared
full idle/stack checkpoint. The silence stage completed twenty withheld requests
and target timeouts at 2000/2001 ms; all twenty results were -116, while the former
oracle incorrectly expected Linux -110. It therefore remains **FAIL** under its
original criteria; stages 3–7 were not executed. Twenty-one cleanup observations
returned contexts to zero, with maximum 1748 ms, and final packet/data pools were
full. These facts diagnose the failed oracle assumption; they do not retroactively
qualify the rejected stage or complete #11.

Exactly one allowed normal restoration followed: complete original 41232-byte
fe9 BIN verification, reset/start and fresh normal observation independently
passed (untouched worker/main 3776/1352 bytes, commands off, watchdog unarmed).
The failed attempt, all raw bytes and restoration are retained. No live criteria
were changed and no uncontrolled retry followed.

The actual pinned Picolibc compiler, with the saved network compile flags, defines
EIO 5, ENETDOWN 115, ENETUNREACH 114, ECONNRESET 104, ENOTCONN 128, ETIMEDOUT 116,
ECONNREFUSED 111 and EHOSTUNREACH 118. Linux numbers are not interchangeable:
Picolibc 110 is ESHUTDOWN. `prepare_m3_errno.py` freezes actual preprocessor output,
selected headers, compiler/command identity and exact source/BIN binding. The
capture requires this verified sidecar; the oracle uses symbolic classes from
that one map, with no Linux fallback or union. Wrong ABI/image/source/header/
macro/command controls reject. A fresh series needs independent renewed review
of that correction and the preserved failure/restoration before execution.

Attempt 02 then stopped before any application round: a following state/stack
report arrived 24.077 ms after its resource line, and the former checkpoint cut
its observation at the resource receive timestamp. Raw bytes show full resources
and correct link/address/default readiness; the initial checkpoint nevertheless
remains **FAIL** under that runner. No peer or socket round started and no
subsequent automatic restoration/reset was attempted. The identified network
image remained unarmed and idle.

The corrected checkpoint associates each of the first three resource reports
with exactly one following state and stack report, in row order before the next
resource report. It retains the 250 ms association and 5 s checkpoint bounds,
full pool/context/stack criteria and consecutive approximately 1 s cadence.
Missing, duplicate, wrong, late and orphan records reject. Diagnostic replay of
old raw captures is explicitly separate from qualification and does not change
either failed attempt. A separately reviewed resumption may verify the complete
existing network BIN and issue one fresh reset/start without reflashing it;
entry mismatch stops all further target changes. The final normal restoration
and seven-stage physical criteria remain unchanged.

## Ethernet carrier failure and revised reference candidate

The later controlled `physical-network-1fda8d2-04` attempt independently passed
stages 1–5: healthy TCP/UDP, withheld responses, healthy recovery, TCP close with
UDP echo, and another healthy recovery. Each stage retained twenty correlated
exchanges and the declared full native idle baseline; the minimum unused main
stack was 2992 bytes. These are bounded component results, not full #11 acceptance.

After the operator removed only the EVKB RJ45 cable, Mac en5 became inactive,
but every target report retained carrier/service-ready throughout the subsequent
15-second observation. The series stopped before requesting stage 6 traffic:
zero stage-6 socket rounds/peer frames, sockets/contexts zero and full pools.
The complete attempt remains **FAIL**. No automatic restoration or retry followed;
the identified network image stayed unarmed/idle, and the operator reconnected
the cable. Earlier attempts 02 and 03 also remain failed initial checkpoints;
03 exposed queued pre-reset idle output being mistaken for a fresh-boot boundary.
The corrected runner restricts initial verdicts to exactly one identified new
boot, while retaining and checking the pre-reset prefix separately.

The pinned KSZ8081 driver uses the optional devicetree interrupt route instead of
periodic monitoring once initial work completes. The observed stale carrier
justifies selecting its existing 500 ms polling path in this reference overlay:
remove only `int-gpios`, retain PHY reset GPIO1.9, SW5 GPIO5.0 and fixed MAC/IP.
The interrupt-polarity/path hypothesis is not a confirmed hardware root cause.
Neither upstream code, Crux interfaces nor the original 15-second criterion changes.
A new identified image and a fresh reviewed seven-stage physical series are required.

Required probe/controller records use one synchronous `printk` call per record.
The validation profiles disable Shell input echo and use an empty prompt; the
runner sends only exact nonempty supported commands. This does not serialise
arbitrary help/error Shell output or establish a real-time guarantee. A network
request is reserved before its acknowledgement and becomes runnable only after
that record completes; a controlled native preemption test covers the boundary.
New candidate manifests and capture entry checks enforce the output configuration.

The combined physical runner/oracle covers the original window, operator receipt
versus physical-edge uncertainty, contiguous health, per-owner watchdog faults,
both-end frames, paired stack/resource observations and final frozen-capture
validation. It requires an independently reviewed complete #11 physical hand-off.
Its software controls and compiled images are not #12 physical acceptance.
Unknown transport/probe/peer/evidence state prohibits automatic programming;
a defined functional failure allows only the preselected single normal restoration.
Physical #11/#12 qualification, publication review and final support coverage remain
Pending; no PR merge or issue/milestone closure is authorised by this work.

### PHY and MAC diagnostic image

The polling candidate subsequently reproduced a brief reported carrier loss
followed by failed TCP/UDP exchanges. A separate opt-in configuration,
`samples/controller/zephyr/m3-phy-diagnostic.conf`, supplements the network
configuration for investigation only. It adds read-only RT1062 MAC observations
around an explicit round and native PHY register reads after the round finishes.
It performs no register writes, driver replacement or automatic application traffic.
The ordinary network, controller and normal recovery profiles leave it disabled.

Use this image only with `capture_m3_network.py --diagnostic-echo-only`; the runner
rejects it for seven-stage qualification. Preserve the identified image, exact ABI
map, raw records and peer/packet captures. Missing or implausible diagnostic
completion stops further target operations. The five-second host observation limit
does not cancel a blocked target driver call.

PHY register reads can clear latched status and interleave with the native monitor.
The two BMSR reads are not an atomic pair. A successful native return and plausible
identity do not prove MDIO transaction completion in the pinned driver. These
post-round observations cannot establish the cause of an earlier transient link
event, qualify a driver correction or replace the original failure/recovery tests.


### Native MDIO completion correction candidate

The e6b545f diagnostic completed ten TCP and seven UDP exchanges before native
`-ENETDOWN` results. Its eight subsequent PHY reads all returned success with
zero data, including implausible PHY identifiers. The attempt stopped and remains
unqualified. Normal fe9 firmware was subsequently fully verified and freshly
booted. The intervening failed read-only recovery inspection requested `13`
words, which J-Link Commander interpreted as hexadecimal (19 words); strict
length validation rejected that attempt before programming. The corrective
normal-only recovery passed. Neither diagnostic data nor external SDRAM backing
reads establish a coherent CPU snapshot or the original cause.

An exact-source host model independently reproduced discarded wait errors and
premature reads with an explicitly injected old completion token. A maintained,
opt-in [native patch](../patches/zephyr/README.md) now provides a candidate for
bounded completion/error handling without modifying the dependency checkout.
Its source-level controls and target compilation do not establish a physical
correction. The original seven-stage Ethernet acceptance and dependent integrated
physical cases remain Pending; a new identified candidate and affected independent
source/physical review are required before another target trial.

The identified 7ea68ff candidate passed full binary verification and booted once,
but all sixteen startup observations kept link and service readiness at zero.
The fifteen-second readiness limit expired before any peer or exchange started;
no post-round PHY diagnostic was performed. This is a failed startup trial, not
Ethernet acceptance. Its passive capture closed cleanly; normal fe9 firmware was
then fully verified and freshly booted with commands disabled. That idle hand-off
does not assert Time or network service readiness.

The default-off, private first-failure trace was physically observed once at
`4fa6f14`, followed by full-verified fresh normal fe9 recovery.
It retains the first failed wait in CPU context, including the actual wait branch,
modular cycle delta and sequential register observations. The application adds no
output until a retained failure is available, then emits one complete line after
copy protection ends. It adds no MDIO transaction, timeout extension or background
thread. Logical models check retention and the actual observer's quiet-before-failure
behaviour; they do not qualify physical timing, establish causation or approve a
new image for target use. Seven-stage Ethernet and integrated physical acceptance
remain Pending.

The first retained failure was an interrupt-wait PHY0 BMSR read at 3,607 ms,
`-ETIMEDOUT`116, with semaphore count1 observed afterwards. The 256880-cycle
interval at 600 MHz is 428.133 us; native 100-us ticks and timeout rounding apply.
This is consistent with a late completion token, without proving a physical
cause or overlap with UART output. It is valid diagnostic evidence, not a
corrected Ethernet or integrated qualification result.

The native direct `PRINTK_SYNC` path holds IRQ exclusion over complete polling
UART records. A separate default-off `m3-deferred-output.conf` evaluates native
`LOG_PRINTK` deferred UART output in the first-failure diagnostic only. It adds a
2048-byte native logger stack at priority 14 and 4096-byte finite buffer; producers
do not wait for buffer space. One UART backend emits raw records, with Shell
logging disabled. Lost-message reports invalidate observations. Logger allocation,
usable stack and observed unused space must be distinguished; the prospective
short-run minimum unused is 1024 bytes, without a worst-case claim. Target timestamps
remain distinct from delayed receive times. Existing timing limits are unchanged.
This diagnostic did not adopt a general output profile. The later network-only
16 KiB composition is qualified below; integrated adoption has its separate gate.

The deferred diagnostic emits a START timestamp and at most one FINAL snapshot
when its existing main-loop polling reaches 15000 ms. Getter timestamps bracket the
CPU copy; a valid-zero observation is limited to that snapshot. It does not mean
that every MDIO request succeeded or that no later failure can occur. Missing,
late, duplicate or lossy window records are not evidence of absence. Native
completion controls include a failure that appears after the final getter.

### Native output candidate for full Ethernet qualification

The separate `m3-native-output.conf` composes native deferred `LOG_PRINTK` and the
polling UART backend with the network probe and maintained MDIO correction. It
excludes PHY/startup diagnostic observers. It is a sample-owned, unqualified
composition; the diagnostic flag remains rejected by the seven-stage runner.
Logger resources are fixed at priority 14, 2048 usable stack bytes (2112 allocated
including guard) and a 16384-byte drop-new/nonblocking buffer. The minimum observed
unused logger stack is 1024 bytes, a functional limit rather than a worst-case claim.

Authoritative verdicts replay raw UART event bytes without stripping ANSI or spaces;
only CR/LF is normalised. Any native dropped-message marker invalidates evidence.
Periodic native resource groups are ordered resource/state/main-stack/logger and
paired within 250 ms. Queue count is sampled before that group's records are enqueued.
The first three consecutive idle resource reports inside the existing five-second
checkpoint must have queue 0, with existing 750..1250 ms cadence and span >=1800 ms.
Later favourable samples cannot replace an initial nonempty queue. Traffic-time
queue counts remain bounded observations. ACK must be published before work and
received within 1 second of the command; raw replay checks both ordering and time.
All original seven-stage network, peer, full-baseline, real-link-loss and recovery
criteria remain required. No physical qualification of this composition is claimed.

All present logger/main-stack values are checked across the authoritative full
stage capture, including before ACK. Native periodic main-stack records must each
have exactly one paired logger record. Only an exact ACK may interleave that group.
An initial partial group may be a single suffix ending in LOGGER before the first
RESOURCE; its present values are still checked. This capture-boundary exception
cannot apply after the first RESOURCE. Functional round slicing remains at ACK;
it does not discard the separate full-capture resource validation.

The finite production-format native_sim comparison retained the rapid 4 KiB failure
(56 delivered, 74 dropped) and rapid 8 KiB failure (112 delivered, 18 dropped). At
16 KiB, all 130 records arrived with exact content/order and no loss; the pre-drain
snapshot held 9,496 bytes. A 50 ms paced 4 KiB contrast also delivered 130; that delay
is not a production guarantee. Printed fixture stack/queue fields are synthetic
payload. The native ring API reserves one 32-bit word: usable capacities are 4,092,
8,188 and 16,380 bytes, distinct from the configured allocations. The network-only
16 KiB choice adds 12,288 static RAM bytes and leaves the startup diagnostic at
4 KiB. It adds no producer wait or changed exchange/round deadline. These are volume
observations, not target service-rate, WCET or physical qualification evidence.

The first full-series native-output attempt at `e6b4f03` passed echo and silence
stages, then failed the unchanged timeline oracle during stage 3 recovery. UDP
sequence 1 ended at 82903 ms; sequence 2 reported end 82904 ms and elapsed 2 ms,
which reconstructed a start before the previous end. The production report read
uptime separately for completion and duration in arguments whose C evaluation
order is unspecified. Both values now use one completion snapshot; no timing
allowance or acceptance criterion changes. An advancing-clock test extracts the
actual production function, rejects the original two-read expression, and checks
consistent TCP/UDP reports above the 32-bit uptime range. The failed physical
series remains FAIL, and its full-verified `fe9f54f` normal restoration passed
(commands off, fault injection off, worker/main unused 3776/1352 bytes). Stages
4–7 were not attempted in that failed series. The later d2 full-series result is
recorded below; integrated physical acceptance remains Pending.


## Qualified Ethernet hand-off

Source `d2bee283e0cd1adbebc87e980f7a56faa3764355` passed all seven original
physical Ethernet stages and independent raw/peer/oracle review: echo, silence,
echo recovery, TCP close/UDP echo, echo recovery, actual RJ45 loss and actual
reconnection plus explicit reset/recovery. Every stage recorded 20 bounded
transactions. Full capture resource/stack/logger and unchanged timing checks
passed. Three idle samples span 2002 ms; minimum observed main/logger unused
values were 2928/1560 bytes. These are short observations, not worst-case limits.
The final full-verified fe9 normal restoration passed, commands/fault injection
off, worker/main unused 3776/1352 bytes. No integration acceptance is inferred.

Qualified BIN: 115484 bytes, SHA256
`8161960fb64643b7c0be580d81d1147bf545d093554ff826ec84442fcd4563e1`.
All four exact-head CI runs succeeded; the preserved Zephyr push attempt-1 SDK
403 rate-limit failure occurred before tests, and its single infrastructure retry
passed. The [reviewed result](https://github.com/wonny-labs/crux-pilot/issues/11#issuecomment-6252853065)
identifies the fixed peer/addresses, exact native configuration and component
hand-off. Archive `crux-m3-network-d2bee28-actual-20261018.tar.gz`, 227 payloads,
SHA256 `9f00c77f51d90182b3a23baa386301458d95295917271b8c155d65dac6541019`,
references the exact-image/compiler-ABI/source/failure preparation supplement
`crux-m3-network-timestamp-checkpoint-d2bee28-20261018.tar.gz`, SHA256
`aae3c8a857cba134ce4122e50fea450c4b18a0cf87f4b6bbbf242482ce01a521`.
All payload hashes were independently checked. Earlier failures remain retained;
there is no universal MDIO hardware-cause, automatic recovery or M5 claim.

## Integrated native-output preparation

The reviewed #12 composition reuses the exact qualified client/header/overlay,
maintained native MDIO patch and pinned dependencies. The private default-off
native-output option now admits the integrated probe/normal roles, retaining the
16 KiB nonblocking drop-new buffer, one UART backend, 2048-byte usable logger
stack and priority 14. Existing worker/network/main priorities, health/window/
watchdog policy and strict physical limits are unchanged. Mode-specific image
checks bind this role without admitting diagnostics, alternate backends or
blocking logging. Integrated native transport/PHY/output settings and generated
patch identity of the probe must match the #11 hand-off. The normal role has four
pinned Shell-disablement differences: runtime filtering y→n, runtime default level
0→absent, and disabled LOG_CMDS/MDIO_SHELL symbols becoming absent. These are recorded
explicitly, with no other native-setting difference allowed. Application role,
worker stacks and development-command exposure are intentional composition differences.

Every integrated observer/stack group gains a paired logger query, with queue
count sampled before that group is enqueued. Closed-capture exact raw replay
precedes full logger/stack and original functional/watchdog verdicts. Loss,
malformed/duplicate/orphan/cross-reset records, insufficient short-run margins,
and the first three consecutive idle queue snapshots fail conservatively. Raw
pre-reset output is retained with its explicit prior role and excluded from new
qualification; arbitrary repair or prefix skipping is forbidden. Target event
timestamps remain distinct from delayed receive times.

The finite native output fixture extracts production formats and runs actual
main0/worker5/network6 producers, 10 Hz feed payloads, 1 Hz observer/stack/logger
and pair bursts through native LOG_PRINTK. It delivered all 179 representative
records in per-producer order with no loss; the separate 502-record rapid case
delivered 227 and exposed 275 native drops. Actual-handler corruption controls
reject spacing, ANSI, duplicates, truncation, sequence and phase-boundary errors.
Functional/stack/fault/boot values are fixture payloads except the actual logger
query; no real Time, GPIO, network or watchdog action is performed by this fixture.
These output results do not satisfy the required integrated physical trials.

The a940bb3 target compositions cross-built with owned-code warnings treated as
errors. Their probe BIN/RAM were 124192/63616 bytes; normal BIN/RAM are 107860/59712 bytes,
with 13936 DTCM bytes in each. Worker/network/main stack objects are each 4160
bytes for 4096 usable; logger allocation/usable are 2112/2048 bytes. These are
configured build allocations, not measured high-water use. Clean committed images,
compiler ABI and applicable CI must still pass their exact-image gate before
physical use. The original combined short-run untouched minimum is 1024 bytes
for each observed worker/network/main/logger stack; no acceptance limit is lowered.


## Initial idle logger observation correction

The a940bb3 integrated probe was fully programmed, byte-verified and booted in
procedure02 using the corrected programming command sequence. It then **failed**
the unchanged initial idle checkpoint: the first three eligible observations at
1504/2505/3507 ms each reported queued=1. The later 4508 ms zero was retained,
never substituted. No functional or watchdog case ran. In the pinned Zephyr
log core, the buffered count includes a record until backend processing and free
complete. Worker reports preceded the failing samples by 1–4 ms; these data do
not establish persistent accumulation. The earlier RAMCode programming failure
and read-only identification remain retained; later success does not establish
its universal cause or cure.

One separately reviewed recovery fully verified the fe9 normal image and fresh
normal behaviour. Its original helper still failed because the preceding native
thread-analyser output included `rx_q[0]`. The narrowly corrected prefix grammar
admits that single configured RX thread name, retaining rejection of other
bracketed names, partial records and escapes. Independent replay of all 77 raw
rows established a separate limited normal hand-off; it did not change the
original helper failure or qualify #12. The user then removed the board-power/
UART USB, and the exact owned guard was retired. The reviewed 117-payload archive
`crux-m3-integrated-queue-actual-6bca80a-20261018.tar.gz` has SHA256
`aa315f697c4bf8db78910fd6a994839eda5051dd320b84e333b96f1eb737eb5c`.

The integrated native-output composition now introduces one fixed 50 ms initial
main-thread offset, after creating the unchanged worker and network owners. It
retains the relative 100 ms loop, all priorities, logging configuration, native
client/MDIO settings and health/window/watchdog policy. It never waits for queue
zero or changes which samples qualify. This offset is not a recurring phase
lock, continuous-zero promise or platform scheduling guarantee. Main may enqueue
a watchdog-feed report before an armed sample; zero is required only at the
original initial idle checkpoint.

The focused [native phase fixture](../tests/native/logger_phase/README.md) tests
the actual in-flight count mechanism and fixed samples with recurring producer
and analyser volume under a controlled serial-delay model. Production formats
and the production phase constant are extracted at build time. The shared
first-three queue predicate retains the original time/cadence and zero limits;
full physical grammar, resource margins and functional criteria still apply.
Native model results, target cross-builds and physical results are separate.
New clean images, compiler ABI, independent review, applicable successful CI and
a new physical-plan/runtime gate remain required before target use. At that software-only checkpoint, integrated physical acceptance and PR readiness
were Pending; the later final handoff below supersedes the physical disposition.

The controlled native experiment observed one in-flight message and zero after
backend completion. With the fixed phase, the first three eligible stamps were
1051/2052/3054 ms, all queue zero. The unshifted control at 1001/2002/3004 ms
observed one each and was rejected by the same predicate. Eight mutations of the
actual stream were rejected. The recurring pre-report records occupied 234–244
bytes; the explicit idle-field bound through 90 s is 248 bytes, or 21.528 ms at
115200/8N1. This serialization budget excludes arbitrary startup/active output and
CPU/driver delays. Model drift of 0/1/2/4 ms is imposed, not a measured Cortex
bound. Physical phase and every complete raw observation still require review.
The first native model used the default 100 Hz tick and failed the oracle: its
per-message delay rounded to coarse ticks and the fixed samples were displaced.
That failed capture is retained; the corrected model explicitly uses the frozen
EVKB's 10000 Hz tick. No production tick or acceptance limit changed.

The subsequent ec94fbe physical series 06 passed the five actual SW5 groups with
39 completed network pairs. Its F2 operator receipt arrived after the observation
deadline, so that series remains stopped. The separately reviewed F2-forward
series 07 received the real cable acknowledgement within the original window,
but closed its capture before a fresh post-receipt observer row. The unchanged
oracle correctly rejected the incomplete evidence. The capture now waits within
the existing receipt-plus-15-second bound for that observation; it does not
substitute an earlier row or extend the network window.

Series 07's planned normal restoration fully verified the normal BIN, but its
helper failed because the reset interrupted an old Shell analyser line and the
literal new boot banner followed that fragment on the same UART line. Independent
offline replay established normal behaviour from the preserved new epoch. The
capture recognises only that exact observed fragment at the first planned boot,
records its exclusion, and preserves every raw byte. Unknown fragments and later
joined epochs remain rejected. Original failures remain failures; these capture
corrections do not qualify F2/F3, the six watchdog trials or final integrated
acceptance. The board was powered off and its temporary mount guard retired.


## Final integrated physical handoff

Independent replay supports the aggregate of separately gated cases on the frozen
`ec94fbe` firmware: series06 F1 (39 completed network pairs and five actual SW5
groups), series08 F2/F3 (actual RJ45 loss and explicit restart/recovery), series09
Time watchdog trials 1/2, and continuation10 Time trial 3 plus network trials
1–3. Each watchdog case used a fresh planned boot, the original healthy checkpoint,
one bounded owner fault and a real watchdog reboot with cause `0x10`. Last-feed
to boot observations span 2.499694–2.523135 seconds; all required post-reset
unarmed/default-off observations passed. These observations are bounded test
results, not worst-case reset or scheduling guarantees.

The original series08 and series09 helper failures remain failures. Independent
replay separately qualified complete fresh epochs; no missing observations or
field values were invented. The capture now selects the second observed boot
for the post-watchdog interval. Only a final old periodic record with complete
ordered numeric fields and a proper partial next field/value may be excluded at
a planned reset; prior complete rows and every fresh epoch retain strict parsing.
Raw bytes and the exact excluded fragment/time/index/schema remain recorded.
Future captures also persist body metadata, including the actual host link-down
observation time. Series08 did not persist that exact time: its host observation
is conservatively bounded between the actual acknowledgement and the next
recorded reconnect request, less than 0.476 seconds apart. The independent replay
records that interval limitation rather than fabricating a timestamp.

The final normal image was fully verified (107,872 bytes, SHA256
`ad729684749181f0c26853828550187a2c3e60dbb28cf3ee2194a7984141e02f`),
started and independently replayed with commands/faults/automatic traffic off,
unarmed status, idle resource return and the original first-three queue-zero
checkpoint. Observed unused worker/main/network stack was 3688/3232/3624 bytes;
logger unused was 1568 bytes. Probe minima were 3680/3232/2952 and logger 1560.
The user then disconnected DAPLink/UART USB, powering off the board; the exact
owned guard11 process was retired and process/USB absence verified. No further
hardware operation is required for the scoped acceptance evidence.

The capture corrections are host tooling only and do not change the frozen
firmware, native dependencies, original windows, watchdog criteria or resource
limits. Final canonical-tooling CI, portable archive identities and publication
review are recorded against PR #13 before Ready. The user subsequently accepted the scoped result and authorised independently
reviewed squash merge and Issue/M3 closure, as recorded below. Other I/O devices, long-run/load/M5, physical POSIX
and worst-case stack/jitter behaviour remain outside this bounded qualification.

## User acceptance and closeout

The user accepted the final scoped M3 result and authorised record corrections,
independent review, squash merge of PR #13 and closure of #10, #11, #12 and M3.
At the time of this acceptance record, merge and issue closure remained pending.
The completed integration is recorded below. M4 implementation remains
unstarted and outside this authorisation. Earlier Pending/Draft and execution
authority statements in the chronological evidence describe their historical
stage; original failed attempts remain failures.

All five #10, six #11 and eight #12 acceptance criteria have supporting evidence.
The final aggregate above provides the combined physical outcomes. I2C, general
SPI, EEPROM and other GPIO pins remain unverified or unavailable; at least one
representative non-UART path, not every bus, is required by the approved scope.

Physical firmware remains attributed to `ec94fbe`, with reviewed canonical host
tooling at `00aaba3`. These closeout changes are documentation only; they do not
claim a newly built latest-head image was physically tested or byte-identical.
The final review rehashed the 545-payload physical archive and 16-payload
publication archive successfully. No hardware or build/test rerun was performed
for that read-only review. Documentation accuracy, consistency and latest-head CI
are checked for this closeout; prior physical evidence is retained unchanged.

**Integration status:** PR [#13](https://github.com/wonny-labs/crux-pilot/pull/13)
was squash-merged into `main` as
[`ce0b40f`](https://github.com/wonny-labs/crux-pilot/commit/ce0b40f8f9bca0076a28d0dd37bbfa4e0fa57962)
on 18 October 2026. Issues [#10](https://github.com/wonny-labs/crux-pilot/issues/10),
[#11](https://github.com/wonny-labs/crux-pilot/issues/11) and
[#12](https://github.com/wonny-labs/crux-pilot/issues/12) were closed and M3 was
accepted. The original physical firmware and capture identities remain as recorded
above; the physically tested integrated firmware remains identified by `ec94fbe`.
This documentation update does not change its source identity, previous failures
or qualification limits. Earlier Pending statements document their respective
historical stages.
