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

The [current SW5 physical result](#uart-remediation-and-sw5-physical-handoff) supersedes
the earlier physical-input Pending status within its stated scope. Ethernet and
combined-controller physical qualification remain Pending.

The earlier [user steering] held Ethernet work and confirmed that real I/O
stimulus cannot currently be supplied. Therefore #11 implementation and all peer
or network manipulation are held. #10 software and controlled native integration
tests can proceed, but physical stimulus/response acceptance remains Pending.
#12 independent diagnostics, readiness and health/watchdog work can proceed;
final integration still requires qualified real #10/#11 hand-offs. The user has
subsequently requested resumption and offered physical stimulus. Software work
may resume after its applicable review; current topology, stimulus availability
and affected physical gates still require confirmation. Held-state descriptions
below preserve the preceding partial delivery rather than new completion claims. Independent
watchdog evidence cannot substitute for that combined controller result.

The expected target is the previously confirmed EVKB PCB B, i.MX RT1062, standard
8 MiB QSPI and J-Link PLUS 602003106. Existing setup evidence is reused within its
scope; software cannot detect an unreported wiring/jumper change. At plan authoring no physical
operation had been performed for M3; current outcomes remain in the issue/PR records. The Mac management connection is excluded.

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
| GPIO | Read onboard EVKB SW5 (`sw0`, GPIO5.0, active-low/pull-up; common DTS label SW8) through native MCUX GPIO. The sample-owned adapter configures input only and preserves output on read failure. | Existing board input; no external drive or wiring. It avoids GPIO1.9 shared by LED/PHY reset. The user has now confirmed EVKB/SW5 and can provide stimulus. | Controlled `native_sim` transitions, missing-device and actual driver-error tests pass. Physical SW5 input qualification passed in the [identified normal composition](#uart-remediation-and-sw5-physical-handoff). Other pins and joint Ethernet operation remain unverified. |
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
| #10 / 1 | Interface assessment above; selected real-input stimulus/access remains unresolved. |
| #10 / 2 | Bounded software adapter/normal/error cases implemented; physical stimulus scenario remains Pending. |
| #10 / 3 | Pending real non-UART repeatable stimulus/response. Emulation cannot satisfy it. |
| #10 / 4 | Portable Host checks and native GPIO cases pass; current-head target/CI results remain in PR evidence. |
| #10 / 5 | Source/configuration/build entry points retained; physical firmware/fixture hand-off is not yet qualified. |
| #11 / 1–6 | Held by the user. No M3 Ethernet implementation, peer preparation or network manipulation. |
| #12 / 1 | Independent health composition defined; combined #10/#11 composition still depends on qualified hand-offs. |
| #12 / 2 | Stable source/progress/readiness/reset/error diagnostics implemented; physical results require the separate gate and records. |
| #12 / 3 | Pending qualified real-I/O, TCP/UDP and combined controller runs. |
| #12 / 4 | Time remains worker-owned; portable Host and native logic pass. Exact current-head CI/configuration identity is recorded in the PR. |
| #12 / 5 | Progress-conditioned helper and bounded arm/stall design implemented/reviewed; physical workload coupling still requires actual trials. |
| #12 / 6 | Independent watchdog trials require exact-image/runner/recovery PASS and preflight. They cannot substitute for combined M3 qualification. |
| #12 / 7 | Reproduction, frozen inputs and raw evidence are retained; exact physical footprint/stack/timeout results must come from identified trials. |
| #12 / 8 | This coverage record keeps missing physical/network/integration outcomes explicit. Full M3 remains incomplete. |

The Draft PR and issue comments record current-head CI, review verdicts, later
physical outcomes (if executed), archive identity and precise resume state.
No issue/milestone closure, merge or Ready transition is authorised while these
required outcomes are missing. Resume real I/O only when stimulus is available;
resume Ethernet only after the user releases its hold and an isolated peer/link
control path is confirmed. Preserve the failed attempts and re-review affected
observers/configurations before retrying an invalid physical result.

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
