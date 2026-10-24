# Portable System Health and Reset — proposal and TDD outline

Status: proposed public contract, not yet approved for implementation. The user
selected start/feed/status without stop or reconfiguration on 24 October 2026.
The detailed declarations, limits and choices below are the author's proposal.
No product implementation, hardware operation, merge or Issue closure is approved
by this document.

## Intent and baseline

Issue #30 needs portable controller lifecycle mechanisms and an independent
runtime-shaped consumer. Start from main `bf048c1`, the squash of Logging #61;
its Git tree equals the software-qualified `bf6706f` tree. Reuse the component
architecture and the M5 Pilot validation allocation, which the current Goal
explicitly adopts despite the historical plan's retained Proposed heading.

Existing `controller_health_observe`, joint freshness and `controller_readiness`
are sample-owned policy. Preserve their 100 ms observation, 500 ms Time freshness,
16 s network freshness and 2 s watchdog scenario when reusing that scenario. Do
not move those rules, recovery thresholds or READY/DEGRADED decisions into Crux.
M3 physical evidence on `ec94fbe` observed watchdog native Zephyr cause 0x10 and reset envelopes;
it informs this design but does not qualify a new portable image or grant board
permission. The existing M3 native image and its approval/evidence remain intact.

## Approaches and decision

1. Keep watchdog/reset entirely native in composition. Suitable for existing M3,
   but cannot provide portable lifecycle/feed/reset operations to runtime logic.
2. **Selected direction:** narrow Crux contract using native Zephyr facilities,
   one watchdog activation per boot, application-owned supervision. The user
   chose start/feed/status with no stop/reconfigure. Recommend one fixed System
   component with Base-only public usage; no registry, global health setter,
   monitoring worker, automatic feeder or general OS abstraction.
3. Expose stop/restart, multi-channel callbacks and automatic supervision. Rejected
   for this scope: the pinned RT1060 driver cannot safely promise stop, and no
   independent multi-client/timer requirement justifies additional machinery.

`components/system`, `<crux/system.h>`, `crux::system` own only these fixed lifecycle
mechanisms. Private Linux/Zephyr ports keep native declarations out of consumers.
This does not turn System into a container for unrelated future services.

## Proposed public surface

All calls return existing `crux_status_t`, with C17 declarations and C++17 linkage.
Reports and value tokens contain only ISO C/Crux values. The six operations are:

```c
typedef struct { uint32_t reserved; } crux_watchdog_t; /* zero invalid */
typedef enum {
    CRUX_WATCHDOG_UNAVAILABLE, CRUX_WATCHDOG_IDLE, CRUX_WATCHDOG_ACTIVE,
    CRUX_WATCHDOG_FAULTED, CRUX_WATCHDOG_EXPIRED
} crux_watchdog_state_t;

typedef struct {
    crux_status_t watchdog_status;
    crux_watchdog_state_t watchdog_state;
    bool watchdog_emulated;
    uint32_t watchdog_timeout_ms; /* zero before a successful start */
    crux_status_t reset_status;
    crux_status_t reset_cause_status;
} crux_system_status_t;

typedef struct {
    uint32_t supported;
    uint32_t causes;
} crux_reset_cause_t;

crux_status_t crux_system_status_get(crux_system_status_t *out);
crux_status_t crux_system_reset_cause_get(crux_reset_cause_t *out);
crux_status_t crux_system_reset_cause_clear(void);
crux_status_t crux_system_reset(void);
crux_status_t crux_watchdog_start(uint32_t timeout_ms, crux_watchdog_t *out);
crux_status_t crux_watchdog_feed(crux_watchdog_t watchdog);
```

Reset-cause bits are Crux-owned POR, PIN, SOFTWARE, WATCHDOG, DEBUG, CPU_LOCKUP,
HARDWARE, SECURITY, USER and TEMPERATURE (bits 0..9 respectively); OTHER is bit 10
for native supported/reported reasons outside that mapping. Native constants and
register values never escape. Multiple bits may coexist; zero means no recognised
reported flag, not proof that no reset happened. A cause may contain multiple
possible reasons, not a disambiguated diagnosis or ordered history.

## Status and readiness

`status_get` supplies a consistent component snapshot, not a product health vote.
Its OK means the report is valid, not that all services are ready. Each service
field is OK when available, NOT_SUPPORTED when absent/unimplemented, NOT_READY
for a known unavailable device, or IO for an observed provider failure. A native
failure must never be represented as a healthy/confirmed-missing result.

The watchdog tuple reports state, emulation identity and the active requested
nominal timeout. FAULTED accompanies IO; Host EXPIRED accompanies TIMEOUT. A
successful native readiness check is not physical watchdog/reset qualification.
`reset_status=OK` says the reset mechanism is available, not that a future call
is guaranteed to reset successfully. `reset_cause_status` reflects the current
supported-query/read probe; a later call may still fail. Reports on failure are
unchanged. Null output is INVALID_ARGUMENT. There is no global READY/HEALTHY
setter, registry of subsystems or implication that network/storage/application
work is healthy. Consumers combine these mechanics with their own required work.

## Watchdog lifecycle and ownership

One composition binds one exclusive watchdog. Only a successful `start` issues a
boot-local opaque token. Copying that value delegates feed access; it does not
create another watchdog, stop it, extend its lifetime or establish a security
boundary. The application owns one supervision policy and controls token sharing.
No dependency on Crux Thread and no fragile native thread-ID lifetime binding.

- Inputs must be validated before native effects. Null output is INVALID_ARGUMENT.
  Initial qualified timeout range: 500..64000 ms, exact multiples of 500; outside
  range is OUT_OF_RANGE, unsupported resolution is NOT_SUPPORTED. No silent
  rounding. The initial scenario uses 2000 ms. These nominal configuration bounds
  are not expiry/WCET promises or production sizing derived from a real product.
- Output is zero on failed creation; it must not already contain a live token.
  Invalid inputs/unready device do not consume activation. Starting after a
  successful activation or a quarantined native activation attempt returns
  INVALID_STATE. Concurrent calls serialize; a later starter observes the
  completed first transition and cannot acquire a second owner.
- IDLE means Crux has not activated its qualified, observed-unarmed binding.
  An already running unowned native watchdog must not be silently adopted. The
  RT1060 port uses the native HAL running-status observation privately alongside
  the Zephyr API; unexpected pre-arming yields FAULTED/IO with no new owner.
- An install/setup failure can leave native state installed or uncertain. Once
  native installation is attempted, unexpected failure quarantines that binding
  for the rest of the boot. No automatic disable, rollback, retry or rearm is
  promised. Report FAULTED; a watchdog may already be running despite the error.
- On success state is ACTIVE only after accepted native setup and a successful
  target running-status observation. Feed validates the issued token and active
  state, then calls the selected native feed operation. Invalid/zero token is
  INVALID_ARGUMENT. Known native loss/error yields IO/FAULTED. No fabricated
  hardware acknowledgement: native OK is the driver's accepted feed operation.
- EXPIRED is observable only in the Host simulation; subsequent feed returns
  TIMEOUT, and restart remains invalid. On real hardware expiry can reset the
  processor before any status call; the API cannot promise an EXPIRED snapshot.
- No stop, destroy, release, runtime reconfiguration, multi-channel timeout,
  callback, public interrupt handler or ownership recovery operation. Loss of the
  application supervisor does not cancel the hardware watchdog. Tokens do not
  survive reboot and must not be persisted/replayed across boots.

The configured hardware timer is independent of normal task scheduling. The
application must arrange timely, progress-conditioned feeds with sufficient
margin for native calls, locks, logging and other work. No automatic feed on
query/error, no worker and no extension of freshness to obtain a passing test.

## Reset causes and reset request

Cause query reads currently latched native reasons and supported mask without
clearing them or creating a journal. Get/clear belong to the same composition
owner. Data can accumulate until explicitly cleared and is not necessarily only
the last boot; persistence across reset/power cycles is native-specific. Do not
invent POR or SOFTWARE when the provider cannot report it. Unsupported query is
NOT_SUPPORTED, known device unready NOT_READY and observed read failure IO;
failed query preserves the caller's report.

Clear is explicit and destructive. It clears the native reported-cause latch
and verifies reported flags are zero before returning OK; native clear/read
failure or remaining flags yields IO. This verifies reported flags only, not
undocumented hardware latches, backup storage or durable log deletion. Consumers
capture/log the returned cause before choosing to clear. No startup auto-clear.

`system_reset` is a synchronous reset request: successful target execution never
returns; error paths return NOT_SUPPORTED/NOT_READY/IO as applicable. It never
returns OK as a pretend acknowledgement. Linux returns NOT_SUPPORTED and never
reboots the machine. Native Zephyr uses `sys_reboot`; the Cortex-M path calls
NVIC_SystemReset and ignores warm/cold distinctions. Promise neither power-cycle
nor a particular preserved RAM/peripheral state. It does not flush Logging,
persist Configuration, make busy QSPI safe or join threads. The application must
prepare its safe state; hardware reset/testing always needs separate approval.

## Pinned native findings and qualified composition

Inspected native revisions: Zephyr `684c9e8f32e4373a21098559f748f06915f950c9`,
NXP HAL module `2c2f28ac333e995d7279777409817c4b4f92c1ec`. The Zephyr
`modules/hal_nxp/mcux/mcux-sdk-ng/drivers/drivers.cmake` selects `driver.wdog01`
for `CONFIG_WDT_MCUX_IMX_WDOG`.

- Zephyr 4.4.0 `wdt_mcux_imx_wdog.c` maps `nxp,imx-wdog` to the SDK **wdog01** HAL,
  not the unrelated generic `drivers/wdog` implementation. `WDOG_Deinit` documents
  WDE as write-once and may do nothing while Zephyr disable still returns 0.
- Zephyr encodes timeout as `((ms*2)/1000)-1`, accepts no positive minimum window,
  and stores the value before a `>128` check. The advertised 0.5..128 s comment does
  not describe that actual acceptance boundary. The proposed conservative subset
 500..64000 ms in 500 ms steps avoids silent rounding/overflow/unsupported values.
  No upstream patch or full-range production claim is proposed.
- `hwinfo_mcux_src.c` maps RT1060 LOCKUP_SYSRESETREQ to both CPU_LOCKUP and
  SOFTWARE; supported masks have feature-conditioned definitions. Preserve the
  returned combinations and OTHER rather than claiming one exact initiating cause.
- Initial target: `mimxrt1060_evk@B//qspi`, explicit `crux-watchdog0` alias to enabled
  wdog0/WDOG1 at 0x400b8000, supported SRC, native HWINFO/REBOOT/WATCHDOG facilities.
  Reject missing/wrong resources and conflicting M3 watchdog owner, WDT Shell or
  Task-WDT hardware fallback. Unregistered direct native clients violate ownership.
- Start uses minimum 0, requested nominal maximum, RESET_SOC, no callback; retain
  the reviewed pause-when-debug-halted option and no sleep-pause promise. Debug,
  low-power modes, oscillator/tick behaviour and exact reset consequences remain
  target-specific/physically unverified. No power-management policy added.
- Linux is an explicitly emulated development provider, using monotonic time and
  lazy deterministic expiry observation on get/feed; it never resets the host and
  does not claim an autonomous hardware guard. Reset-cause get/clear are normally
  NOT_SUPPORTED. Private test controls inject time/faults, outside public headers.
- native_sim uses the real Zephyr kernel/device watchdog dispatch with a controlled
  fixture device and reset/hwinfo hooks where no real peripheral is available.
  Never call an actual machine reset. Nonreturn reset mapping can be checked in a
  disposable test thread with a correctly nonreturning native hook; do not wrap a
  noreturn function with an ordinary returning fake (undefined compiler behaviour).

## Design completeness check

A fresh independent embedded-platform builder read this proposal and relevant
constraints and returned **nothing important left** before planning. This checks
design completeness only: it is not implementation verification or human approval.
The architecture, API/clear semantics, timeout subset and emulated Host semantics
remain proposals for the human review below.

## Concurrency, context and errors

Normal task context after startup only; detectable ISR/pre-kernel calls return
NOT_SUPPORTED. Status/get/clear/start/feed are privately serialized with native
mutexes, not a spin loop or a generic public lock API. A reset request releases
component metadata before entering a nonreturn native reset. State transitions
and tokens must remain coherent under overlap. No whole-call latency/fairness or
IRQ-off/WCET claim. Caller-owned output storage must not race other writers.

Native errors remain private. Retain supported-vs-unready-vs-fault distinctions.
No native access after a failed validation; no successful publication of partial
reports; no silent adoption or cleanup of uncertain irreversible hardware state.

## Concise TDD and delivery outline (after contract approval)

1. Host RED/GREEN: unavailable services, status validity, invalid inputs/token,
   start/feed/nominal timeout, single ownership, monotonic deterministic expiry,
   wrong-state/second activation, partial install/setup and feed failure/quarantine,
   cause query/explicit clear/verification failure through controlled native ports.
   Keep the simulation free of a background worker and actual system operations.
2. Focused Zephyr tests: native device mappings and passed timeout/flags/channel,
   unavailable/prearmed/failing native states, cause-bit combinations/clear error,
   ISR rejection and correctly nonreturning reset forwarding. Do not duplicate the
   entire Host table or pretend controlled driver mapping is physical expiry.
3. Small Controller example reuses existing sample health policy and public APIs;
   independently authored C17/C++17 runtime consumer, native-free usage/source
   negative, selected revision-B cross-build/config/resource observation. Normal
   target bootstrap never automatically arms or requests reset.
4. Affected sanitizers/format/analysis/dependency checks during work. One complete
   final Host/Zephyr PR/Push CI set on stable HEAD, independent Reviewer/Challenger,
   actual failures and limits retained. Reuse #61 baseline, no unrelated replay.
   Add only needed result/config/log artefacts; leave the separate existing CI
   bulk-artifact/duplicate-trigger optimisation outside this PR.
5. Draft PR references #30 without closing it. Include criterion coverage, source/
   provider/config identity, software results, static budgets and minimal separately
   gated physical proposal: identified fresh boot/cause, explicit clear, healthy
   progress-conditioned feed, one approved withheld-feed reset, fresh cause and
   unarmed post-reset state. No new physical action, merge or Issue closure.

Earlier process-exit delays remain unresolved; passing tests do not establish a
cause or fix. This is Pilot software qualification, not production safety,
watchdog endurance, persistent reset provenance or worst-case scheduling proof.

## Human approval requested

Approve or revise the six operations, one boot-local watchdog feed token and no
stop/reconfigure, fixed service-availability status rather than application-health
policy, explicit nondestructive cause query plus verified clear, synchronous
nonreturning target reset, conservative nominal timeout subset and the concise
TDD allocation above. The selected one-shot direction alone does not approve this
full contract. Implementation starts only after approval of this document.
