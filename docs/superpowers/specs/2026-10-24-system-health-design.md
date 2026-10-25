# Portable System Health and Reset — proposal and TDD outline

Status: **approved for implementation** by the user on 24 October 2026 at
proposal commit `72e5295`. Approval includes the revised six-function contract
and TDD outline. Hardware operation, merge and Issue closure remain excluded.

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

## Approved public surface

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

Reset-cause flag names and values are component-owned, not native constants:

```c
enum {
    CRUX_RESET_CAUSE_POR         = 1u << 0,
    CRUX_RESET_CAUSE_PIN         = 1u << 1,
    CRUX_RESET_CAUSE_SOFTWARE    = 1u << 2,
    CRUX_RESET_CAUSE_WATCHDOG    = 1u << 3,
    CRUX_RESET_CAUSE_DEBUG       = 1u << 4,
    CRUX_RESET_CAUSE_CPU_LOCKUP  = 1u << 5,
    CRUX_RESET_CAUSE_HARDWARE    = 1u << 6,
    CRUX_RESET_CAUSE_SECURITY    = 1u << 7,
    CRUX_RESET_CAUSE_USER        = 1u << 8,
    CRUX_RESET_CAUSE_TEMPERATURE = 1u << 9,
    CRUX_RESET_CAUSE_OTHER       = 1u << 10
};
```

A provider need not support every flag; `supported` reports its current normalized
native supported-mask. OTHER means one or more native reasons outside the ten
named categories, including supported native categories not exposed individually.
Native constants/register values never escape. Multiple bits may coexist; zero
means no currently reported flag, not proof that no reset happened. Flags are
neither an ordered history nor a disambiguated initiating-event diagnosis.

## Status and readiness

`status_get` reports this component's fixed services: watchdog lifecycle/provider
availability, reset-request availability and reset-cause query availability. It
is not application readiness, fault assessment, recovery eligibility or a vote
on whether all required work is healthy. It does not inspect Time/network/Serial/
Logging progress or replace the existing controller health/readiness helpers.

Return semantics are distinct from the contents of the report:

- Function OK means a complete report was produced. An unavailable/failing service
  is represented in its own field; it does not turn that field into OK. Null
  output is INVALID_ARGUMENT, unsupported context NOT_SUPPORTED and an inability
  to produce a coherent report IO. On a non-OK function return, output is unchanged.
- The report serializes component metadata and individual provider probes; it is
  not an atomic measurement of every peripheral at one instant. Later calls may
  observe different availability. No query starts/feeds a watchdog, clears causes,
  requests reset or authorizes cause-clear. Query may latch an observed watchdog
  fault or advance the Host simulation's observed expiry state.

| Field | Exact meaning |
| --- | --- |
| `watchdog_status` | OK for an available IDLE/ACTIVE service; NOT_SUPPORTED for absent service, NOT_READY for a known unready device, IO with FAULTED, TIMEOUT with Host EXPIRED |
| `watchdog_state` | Crux lifecycle/observed state, not proof that the application is making useful progress or a whole-system safety verdict |
| `watchdog_emulated` | Selected watchdog provider is a development/test simulation; not an autonomous hardware protection guarantee |
| `watchdog_timeout_ms` | Requested nominal timeout of the last successfully started watchdog; zero before success. A retained value in FAULTED does not prove current hardware timing/state |
| `reset_status` | OK if the selected reset-request mechanism is available; NOT_SUPPORTED if absent (including normal Linux Host). It does not promise successful completion or a reset duration |
| `reset_cause_status` | Result of supported-mask/read probing, without clearing: OK on available, valid observation; NOT_SUPPORTED when unimplemented, NOT_READY if known unready, IO on provider/validation failure. It does not establish that a diagnostic consumer received the initial cause |

`controller_health_observe`, `controller_health_joint_observe` and
`controller_readiness` remain sample-owned. The application decides which
services/progress are required, when to report READY/DEGRADED, whether to arm/feed
or withhold feed, and what recovery action is justified. There is no platform
READY/HEALTHY setter, monitoring registry, automatic recovery or policy daemon.

## Watchdog lifecycle and ownership

One composition binds one exclusive watchdog. Only a successful `start` issues a
boot-local opaque token. Copying that value delegates feed access; it does not
create another watchdog, stop it, extend its lifetime or establish a security
boundary. The application owns one supervision policy and controls token sharing.
No dependency on Crux Thread and no fragile native thread-ID lifetime binding.

- Inputs must be validated before native effects. Null output is INVALID_ARGUMENT;
  zero timeout is OUT_OF_RANGE (it never means disabled or immediate expiry).
  Positive timeout bounds/resolution are provider-profile requirements, not
  universal platform limits. The initial RT1060 provider profile accepts nominal
  500..64000 ms in exact 500 ms steps: outside range is OUT_OF_RANGE; unsupported
  resolution is NOT_SUPPORTED. Linux's initial emulated profile mirrors this for
  conformance. Other/future providers need their own documented profile; portable
  callers cannot assume that every provider accepts this range or step. No silent
  rounding/overflow or invented expiry/WCET guarantee. The initial scenario uses
  2000 ms; this is not product-derived production timing/sizing.
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

## Reset causes: flags, initial observation and clear ownership

Cause query reads currently latched native reasons and the supported mask;
normalizes them to the Crux flags listed above; and returns both only after all
required reads/validation succeed. It never clears hardware. Supported means
what this provider can report, not a promise that one flag uniquely identifies
an initiating event. Known reasons preserve their distinct bits; unrepresented
native reasons map to OTHER rather than disappearing. Do not intersect causes
with the supported mask to silently discard evidence. If a native read reports cause bits outside its own advertised native supported
mask, return IO without publishing the report; do not erase that inconsistency by
masking or broadening `supported`. Unknown but natively advertised reasons may
still normalize to OTHER. A failed supported-mask query also returns its failure,
not a fabricated healthy/confirmed-missing value.

Multiple flags may accumulate across resets. Zero after a successful read means
no currently reported flags, not no prior reset. Persistence/clearability across
reset or power loss is native-specific. RT1060's combined SOFTWARE/CPU_LOCKUP
flags remain combined; the API does not disambiguate them or infer SOFTWARE/POR
when a provider cannot report it. No persistent boot journal or component-owned
copy of initial boot evidence is introduced.

| Outcome | Query/clear behaviour |
| --- | --- |
| Native mechanism unimplemented | NOT_SUPPORTED; no invented cause or successful no-op clear |
| Known device unready | NOT_READY |
| Failed native read/clear, invalid native report, failed verification or flags remaining after clear | IO |
| Failed query | Caller output unchanged; never opens clear permission |
| Clear before a successful explicit public query (provided service) | INVALID_STATE; no native clear call |
| Successful query with zero flags | OK with `causes=0`; caller may retain that observation |

**Initial boot diagnostic owner:** final application composition must designate
one boot/reset-diagnostic owner. In the new reference scenario it is the bootstrap/
supervisor, which calls `reset_cause_get`, retains its returned report, and hands
copies to the intended diagnostic/runtime consumers before it elects to clear.
Required recipients and retention/delivery policy belong to that application.
Logging OK alone is not a delivery acknowledgement and cannot justify discarding
the only copy. Startup, status probing, watchdog start/feed and other components
must not auto-clear. Other consumers may make nondestructive reads; only the
composition's designated diagnostic owner may request clear. This is a declared
ownership contract, not an authentication system or a consumer registry.

A small internal ordering guard prevents accidental clear-before-observation:
only a successful explicit `reset_cause_get` allows a subsequent clear attempt.
`status_get` probes, failed reads and native probes do not count as consumption.
An admitted clear attempt consumes this permission even if the native operation
fails; another explicit successful query is needed before another attempt. The
application owner must still ensure that the intended consumers actually received
or retained their copy; the component cannot infer that from another thread's
read or from a Logging submission. No new public API/token is added for this guard.

Clear is explicit and destructive. It calls the native clear and verifies the
currently reported flags are zero before OK. It does not erase caller-owned
copies, verify undocumented latches or guarantee persistent-history deletion.
On partial/failed clear, the owner retains the previously captured diagnostic
report and may query remaining flags; there is no automatic clear retry. Calls
are serialized, but unregistered direct native readers/clearers violate this
composition contract and cannot be made safe by a Crux watchdog token.

## Reset request: return and execution semantics

The public function returns `crux_status_t` because rejection/error paths return;
it must not be declared unconditionally noreturn. **It never returns CRUX_OK.**

| Provider/path | Execution and observable return |
| --- | --- |
| Zephyr rejection before dispatch | Detectable ISR/pre-kernel use returns NOT_SUPPORTED; absent mechanism NOT_SUPPORTED; known unready/pre-dispatch failure returns NOT_READY/IO where observable. No reset has been dispatched |
| Qualified Zephyr target dispatch | Release component metadata and enter native `sys_reboot(SYS_REBOOT_COLD)`. Normal success resets the processor and never returns to the caller; other tasks and in-flight operations may be terminated |
| Native target fails to reset after dispatch | Zephyr's noreturn implementation can idle forever after a failed architecture reset. The API cannot turn this into a returned IO or a bounded completion. Nonreturn alone is not evidence that a reset actually occurred |
| Normal Linux Host provider | Return NOT_SUPPORTED without rebooting the machine, exiting/restarting the application, inventing a boot epoch or changing reset-cause evidence. Host emulates watchdog state/expiry, not successful machine reset |
| Controlled software test hook | A correctly nonreturning hook may record dispatch and terminate only a disposable test thread/process. This proves mapping/nonreturn forwarding, not a Host/board reset or a new production simulation success contract |

The Cortex-M native path calls NVIC_SystemReset and ignores warm/cold distinctions;
SYS_REBOOT_COLD is the private initial mapping, not a public power-cycle guarantee.
Promise neither preserved RAM/peripheral state nor persistent reset provenance.
The request does not join threads, drain Logging, save Configuration, make busy
QSPI safe or implement recovery policy. The application prepares its safe state.
A real reset and its fresh-boot evidence need a separately approved physical
scenario; software dispatch/absence of return cannot substitute for that evidence.

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
  Reject missing/wrong resources. Exclusive ownership must be enforced before
  any token is issued: `CRUX_SYSTEM` together with `CONFIG_CRUX_CONTROLLER_M3`
  (including its integrated variant) fails configuration, even if M3 test commands
  are off. That old main owns native arm/feed and unconditionally reads/clears
  reset cause at boot; the token cannot arbitrate or protect against those calls.
  Also reject WDT Shell, HWINFO Shell clear access and Task-WDT hardware fallback
  in this initial composition. Existing M3 images remain unchanged and separate.
  A HAL pre-armed observation is an extra runtime check, not a substitute for
  rejecting two statically selected owners. Unknown direct native clients remain
  outside this exclusive ownership contract; no generic reservation system added.
- Start uses minimum 0, requested nominal maximum, RESET_SOC, no callback; retain
  the reviewed pause-when-debug-halted option and no sleep-pause promise. Debug,
  low-power modes, oscillator/tick behaviour and exact reset consequences remain
  target-specific/physically unverified. No power-management policy added.
- Linux is an explicitly emulated development provider, using monotonic time and
  lazy deterministic expiry observation on get/feed; it never resets the host and
  does not claim an autonomous hardware guard. Reset request and reset-cause
  get/clear are NOT_SUPPORTED in its normal profile. Private test controls inject
  time/faults outside public headers, without pretending a machine reset occurred.
- native_sim uses the real Zephyr kernel/device watchdog dispatch with a controlled
  fixture device and controlled reset/fault hooks where needed. Reuse the pinned
  native_sim HWINFO get/clear implementation for its positive synthetic POR/
  SOFTWARE path; those flags are simulator evidence, not physical reset evidence.
  Never call an actual machine reset. Nonreturn reset mapping can be checked in a
  disposable test thread with a correctly nonreturning native hook; do not wrap a
  noreturn function with an ordinary returning fake (undefined compiler behaviour).

## Design completeness check

A fresh independent embedded-platform builder read this proposal and relevant
constraints and returned **nothing important left** before planning. This checks
design completeness only: it is not implementation verification or human approval.
The original completeness check predates the human request to clarify status,
reset execution, initial cause ownership and provider-specific timeouts. Those
clarifications are now part of this revised proposal. A fresh follow-up builder
check of these exact requested clarifications again returned **nothing important
left**. This remains a completeness check, not technical implementation PASS or
human approval. The user subsequently approved the revised contract at `72e5295`.

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
   cause query/explicit clear/verification failure through controlled native ports
   (including clear-before-observation refusal in those existing lifecycle cases).
   Keep the simulation free of a background worker and actual system operations.
2. Focused Zephyr tests: native device mappings and passed timeout/flags/channel,
   unavailable/prearmed/failing native states, cause-bit combinations/clear error,
   M3 owner-conflict configuration rejection, ISR rejection and correctly
   nonreturning reset forwarding. Do not duplicate the
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

## Human approval and remaining boundaries

The user approved this revised six-operation contract and TDD outline at
`72e5295`, including service-report boundaries, explicit initial cause ownership,
read/clear ordering, native reset semantics, single watchdog owner and the initial
RT1060-specific nominal profile. Implementation and software qualification may
proceed. No hardware operation, PR merge or Issue #30 closure is authorized.
