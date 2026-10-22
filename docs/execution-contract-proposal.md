# Portable execution and synchronisation proposal

**Status: contract approved and implemented; qualification results and limits are
recorded in [execution validation](execution-validation.md).**
Scope: [Issue #25](https://github.com/wonny-labs/crux-pilot/issues/25).
Implementation baseline: `94b11e43fcc745bf15abddcf48f43ab04384b29a`.
This proposal preserves the existing Time API and historical M2/M3 evidence.

## Adopted decision

A small capability-owned C17 execution API, implemented directly by native
Zephyr and private Host pthread facilities. Its scope is joinable threads,
nonrecursive mutexes, bounded counting semaphores, and poll/finite monotonic waits.
Use bounded provider-owned resource pools and checked opaque value handles.
Do not adopt a general OSAL, public provider vtable, allocator, scheduler framework,
or runtime-specific API.

The user approved this recommendation as recorded below. The consumer requirements
below are representative qualification scenarios selected for this issue, not
claims about an existing proprietary runtime or all future products.

## Basis and continuity

The [M2 investigation](execution-profile.md), its source probes in
`tests/execution`, and [physical evidence](m2-physical-verification.md) established
a composition-local native Zephyr profile. The portable controller core did not
need execution APIs, so M2 deliberately introduced none. That rationale remains
valid for the existing controller. Issue #25 adds a different qualification need:
firmware and common runtime-porting source must manage execution through a portable
boundary. A passing M2 probe is neither approval nor conformance for this new API.

The current architecture permits either a deliberately qualified portable standard
subset or bounded capability contracts. Native code belongs in controlled providers;
firmware and common runtime-porting code may not call Zephyr/NXP APIs directly.
The planning documents retain working-design status; this task explicitly applies
those dependency boundaries, without adopting their speculative physical layout.
Authoritative inputs were read from `wonny-labs/crux-platform`: Architecture Design
blob `b1e20234f2ef3d43689d33bdea6008f1935a39c2`, Architecture Concept blob
`5464a8b8b10e594d123e6cbab7712e820a026fe7`, Development Plan blob
`5e2a4f4e783b7ec6d914132dd328d4bc49570845`. Those documents are not modified here.

## Representative needs and exclusions

| Consumer | Qualification scenario | Needed semantics |
| --- | --- | --- |
| Firmware fixture | A finite worker updates mutex-protected state and signals progress to its owner; the owner handles empty/full/timeout cases, joins and repeats | Explicit execution ownership, memory visibility, bounded signals, reusable resources, finite waits |
| Independent synthetic runtime-porting fixture | Separately written runtime-like task/session adapter creates workers with translated stack/priority requests, serialises shared state, waits for completion, handles resource exhaustion and tears down a quiescent session | Portable entry/argument mapping, create/join, explicit failure translation, cleanup and reuse without private source |
| Existing controller | Preserve phase advancement, skip-overrun policy and Time serialisation; document the later migration seam | No new public clock, sleep or scheduler-policy interface in #25 |

The synthetic runtime is original validation code. It proves usability of a bounded
porting seam, not compatibility with CODESYS or another production runtime.
Its author should receive the approved contract and scenario, not provider source.
Its supported build must use public targets and its own configuration.

Public monotonic clocks/sleep, timers, queues, events and periodic-release mechanisms
belong to #26. Diagnostics, devices, networking and wholesale M3 migration are outside
this change. Providers may use private monotonic clocks to implement waits.

## Alternatives

| Approach | Benefits | Costs and limits | Disposition |
| --- | --- | --- | --- |
| Qualified POSIX subset | Familiar API, standard types, direct runtime reuse, less Crux code for already equivalent operations | Pin-specific stack ownership/recycling; direct mutex/semaphore deadlines use realtime; bounded join requires extensions; semaphore-full behavior differs; qualifiers and consumer-side predicate bookkeeping remain | Credible if consumers accept those restrictions; not selected for the proposed finite monotonic wait contract |
| Small Crux-owned capability | One explicit functional contract, native Zephyr static resources, consistent errors and finite relative waits; private Host implementation may use POSIX | Crux owns wrappers, resource lifecycle, tests and documentation; no general runtime compatibility or scheduler equivalence | Recommended for these representative lifecycle and wait needs |
| Composition-lifetime resources only | Simplest storage ownership; prebound worker slots can start/join successive sessions and synchronisation objects can be reused once quiescent | Fixed composition supplies resource identities/cardinality; runtime adapter maps requests to prebound objects rather than obtaining/releasing resources through public create/destroy | Prefer when fixed resource mapping is sufficient; remains a viable smaller alternative |

The need for finite monotonic waits and consumer-controlled resource reservation
and reclamation is the key choice. Neither is an established requirement of the old
controller. Sessions alone do not require pools. The recommendation assumes the
runtime-shaped fixture should validate bounded create/destroy, rather than only
map runtime requests to permanently bound resources. If that assumption adds no
useful adoption evidence, prefer the simpler composition-lifetime option. If review
accepts weaker wait semantics, reassess the qualified POSIX option as well.

## POSIX assessment against the pinned implementation

Pin: Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0).

- `pthread_create`/`pthread_join` support normal-return lifecycle, but standard join
  has no timeout. This pin offers `pthread_tryjoin_np`/`pthread_timedjoin_np`; those
  are extensions, and timed join converts a realtime deadline at entry.
- `pthread_condattr_setclock(CLOCK_MONOTONIC)` is supported; condition variables
  provide a genuine portable monotonic predicate-wait route. A semaphore or worker
  completion predicate can be built with a mutex and condition variable, with
  spurious-wakeup loops. A completion predicate alone does not prove that the worker
  has fully exited or make a subsequent standard join bounded.
- Direct `pthread_mutex_timedlock` and `sem_timedwait` use realtime conversion,
  truncating sub-millisecond time and clamping long durations. The POSIX.1-2024
  clock-selection interfaces do not establish support in this older pinned source:
  `pthread_mutex_clocklock` and `sem_clockwait` are absent here.
- `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)` returns `ENOTSUP` in this
  pin. Native inheritance underneath is not support for that POSIX attribute.
- Attributes can report successful initialisation after stack allocation failed;
  later stack validation reports `EINVAL`. Caller-supplied attributes carry stack
  ownership that must outlive their worker. Do not destroy them immediately after
  create merely because that is safe on the Host implementation.
- `sem_post` calls saturating `k_sem_give` and returns success at maximum count.
  Linux reports overflow. Neither direct POSIX semaphores nor existing M2 whole-image
  differences establish the proposed bounded-token semantics or wrapper overhead.

Source references: `lib/posix/options/{pthread,mutex,semaphore,cond,clock_selection,
timespec_to_timeout}.c`, `kernel/{mutex,sem,thread}.c`. The M2 footprint comparison
uses unequal test content, pool capacity and heap provisioning; it cannot quantify
the incremental cost of the proposed API.

Standard references: [POSIX join](https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/functions/pthread_join.html)
and [POSIX clock-selection rationale](https://pubs.opengroup.org/onlinepubs/9799919799/xrat/V4_xsh_chap01.html).
These inform the comparison; they are not compliance obligations for Crux.

## Approved contract

### Ownership, resources and errors

- Public ownership: `components/execution/include/crux/execution.h`, with C/C++
  compatible C17 declarations and no native headers/types. Consumers link
  `crux::execution`; final composition selects exactly one Host or Zephyr provider.
  Providers own their native objects, stacks, build configuration and private headers.
- Handles are distinct portable value types per resource kind, backed by a slot
  and generation. Zero is invalid. Validate null output pointers and invalid/stale
  handles; generation wrap must retire a slot rather than make an old handle valid.
  No global service locator or consumer-accessible native object lookup is added.
- Pools are bounded at build time. Initial qualification configuration: four thread
  slots, eight mutexes, eight semaphores, 4096 usable stack bytes per Zephyr worker.
  These are test inputs, not a platform-wide resource budget. Creation failure
  leaves the output invalid and rolls back reservations. No public allocator.
- Operations return `crux_status_t`. Retain existing values and append
  `CRUX_ERR_BUSY=-6`, `CRUX_ERR_TIMEOUT=-7`, `CRUX_ERR_NO_RESOURCES=-8`,
  `CRUX_ERR_NOT_SUPPORTED=-9`, `CRUX_ERR_INVALID_STATE=-10`.
  Null outputs, invalid priority values and invalid/stale handles return invalid
  argument; excessive stack/wait/count requests return out-of-range; native/pool
  creation failure due to capacity returns no-resources. An operation on a valid
  object in an inappropriate lifecycle/ownership state returns invalid-state.
  Unsupported execution context returns not-supported where detectable. Unexpected
  native failure returns the existing I/O error. Native errno stays private.
- Callers must stop publishing handles before destroy; no new operation may begin
  concurrently with destruction. Providers atomically validate generation and admit
  each operation under private metadata protection, retaining an operation reference
  until native access is complete. Destroy returns busy for existing admitted users,
  waiters or held mutexes. It cannot recycle a slot between validation and access.
  Concurrent join admission returns busy. This is not automatic cancellation or
  permission for callers to race fresh admissions against destruction.

The declaration shape below is implemented in the capability-owned public header.
The handle types are distinct portable structs containing reserved 32-bit slot and
generation fields; consumers treat them as opaque values. The thread-options type
contains `size_t stack_size_bytes` and the priority-class enum. Both are mandatory
inputs; a null options pointer is invalid. Priority enum values are low=0, normal=1,
high=2. All create operations take the output last, set it invalid on failure and
require that it does not currently hold a live resource. Join/destroy zero the
supplied handle on success; copied aliases become stale and are rejected.

```c
typedef void (*crux_thread_entry_t)(void *argument);

crux_status_t crux_thread_create(const crux_thread_options_t *options,
                                 crux_thread_entry_t entry, void *argument,
                                 crux_thread_t *out);
crux_status_t crux_thread_join(crux_thread_t *thread, uint32_t timeout_ms);

crux_status_t crux_mutex_create(crux_mutex_t *out);
crux_status_t crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
crux_status_t crux_mutex_unlock(crux_mutex_t mutex);
crux_status_t crux_mutex_destroy(crux_mutex_t *mutex);

crux_status_t crux_semaphore_create(uint32_t initial, uint32_t maximum,
                                    crux_semaphore_t *out);
crux_status_t crux_semaphore_take(crux_semaphore_t semaphore, uint32_t timeout_ms);
crux_status_t crux_semaphore_give(crux_semaphore_t semaphore);
crux_status_t crux_semaphore_destroy(crux_semaphore_t *semaphore);
```

Callers serialise writes to their handle variables. Successful join/destroy retires
the provider lifetime even if the caller kept another copy; copying a handle does
not transfer join ownership or extend resource lifetime.

### Threads, stack and priority

- Proposed operations: `crux_thread_create`, `crux_thread_join`. Entry is
  `void (*)(void *)`; normal return completes work. Argument storage and all reachable
  resources remain valid until successful join. Entry can run before create returns.
- Create accepts a minimum usable stack request and one of three priority classes:
  low, normal, high. Zero stack request selects provider default. Zephyr rejects a
  request larger than its configured per-slot capacity; it never silently shrinks.
  Requests below that capacity use the whole static slot. Host rounds up to at
  least its documented native minimum/default; actual allocations are recorded.
- Zephyr maps high/normal/low to preemptive priorities 4/5/6 in the initial profile;
  configuration must reject an unsupported mapping. Host uses unprivileged normal
  scheduling for all classes. Classes express Zephyr ordering and Host functional
  acceptance, not matching Host priorities, preemption or realtime behavior. No
  cooperative/user-mode threads or runtime priority changes are promised.
- Exactly one join owner. Poll/timeout preserves the handle and all owned resources.
  Successful join publishes worker writes, invalidates the handle and reclaims its
  slot/native thread resources for reuse. Create on exhausted pools fails visibly.
- Self-join and concurrent join are rejected. Worker return must release all mutexes.
  No cancellation, abort, detach, restart-in-place, return-value transport, automatic
  deadlock detection, or forced reclamation. A new create after join is a new lifetime.
- A join timeout is a failed wait, not a stopped worker. The application chooses
  retry or its own cooperative stop protocol; this API cannot reclaim a stuck task.

### Synchronisation

- Proposed mutex operations: `crux_mutex_create`, `crux_mutex_lock`,
  `crux_mutex_unlock`, `crux_mutex_destroy`. Nonrecursive, owner-only unlock.
  Self-lock/wrong-owner unlock are invalid-state errors; destroy of a held or
  referenced mutex returns busy. No robust owner-death recovery or fairness promise.
- Zephyr uses native priority inheritance with its nested-lock limitations; Host
  validates functional exclusion and ownership. Portable priority inheritance or
  bounded inversion is not guaranteed. Nested-lock scheduling qualification is out
  of scope; consumers must define a consistent lock order.
- Proposed semaphore operations: `crux_semaphore_create(initial, maximum)`,
  `crux_semaphore_take`, `crux_semaphore_give`, `crux_semaphore_destroy`.
  Maximum is 1..`INT32_MAX`; initial must be <= maximum. Give at maximum returns
  busy without changing count. This preserves tokens and detects an unexpected full
  channel; intentional coalescing remains consumer policy. No reset/drain API.
- Semaphores carry no ownership and provide release/acquire visibility on successful
  give/take. Successful mutex unlock/lock likewise publishes protected writes.
  Waiter ordering is unspecified. Session teardown joins workers, unlocks mutexes
  and stops users before destroying synchronisation objects.

### Wait and context semantics

- Wait arguments are unsigned relative milliseconds: zero polls, 1..`INT32_MAX`
  are finite; larger values return out-of-range. No forever sentinel in this profile.
  This makes every supported acquisition/join wait explicitly bounded in time intent.
- Relative waits use monotonic elapsed time, independent of RTC/UTC. Convert upward
  to native resolution, check arithmetic overflow and retry interruptions/spurious
  wakeups against one original deadline. No early expiry caused by truncation.
- Unavailable poll returns busy; unavailable finite wait returns timeout. Availability
  racing expiry may produce success or timeout, without duplicated/lost ownership or
  tokens. Error/output rules must be uniform across providers.
- Timeouts limit the requested blocking interval; scheduler/preemption can delay
  return. No maximum observed latency or realtime guarantee follows from this API.
- Thread context only, including give and poll. ISR, signal-handler and native
  callback contexts that forbid blocking are unsupported. Zephyr rejects ISR entry
  before calling blocking native primitives. Host asynchronous-signal use is a
  caller precondition, not a claim of detectable signal context.

## Test-first implementation and independent adoption

The approved implementation plan requires failing contract cases before provider implementation. Cover
create/join and visibility, finite expiry, poll, retained resources after timeout,
post-join reuse, pool exhaustion/rollback, stale handles, stack/priority rejection,
mutex contention/ownership/busy cleanup, semaphore full/empty/timeout/wakeup, invalid
arguments and quiescent destruction. Avoid using a completion notification as proof
of full worker termination. Use handshake-based concurrency; outer harness deadlines
are liveness guards, not the contract's timeout evidence.

Run the common behavioral cases on Linux Host and Zephyr `native_sim`. Test Host
sanitizers and selected static analysis; inspect actual command lines and public
target transitivity. Build the firmware fixture separately through supported targets;
have an independent author produce the runtime-shaped consumer against the approved
public contract, without provider source. Validate C17 and C++17 inclusion/use.

Cross-build the same conformance/consumer inputs for EVKB B/QSPI, retaining generated
configuration, maps, object-stack allocation, Flash/RAM, compiler and input identities.
Physical execution remains pending: no board, probe, UART, reset or programming
operation is authorised. Historical M2 stack/timing measurements do not qualify this
new provider. The PR must state this unresolved Issue #25 target-evidence dependency.

Issue #24 remains a shared integration dependency. Add bounded M5 leakage fixtures
where necessary, but do not claim that the current Time/controller grep checks
already enforce this new boundary or implement an unrelated enforcement framework.
Review the actual #24 state before final integration and retain missing gate coverage
as an explicit dependency. No edits to the Flash Safety branch or private planning repo.

## Review and approval record

Independent Challenger `/root/execution_challenger` returned **REVISE** on the initial
direction: monotonic POSIX condition variables must be considered, finite join must
not imply task shutdown, and new consumers/resource lifecycle must justify an API.
Subsequent review requested a precise static-composition alternative and mandatory
operation admission/reference protection for checked reusable handles. Both corrections
are incorporated. The Challenger then returned **PASS for presenting this document
as a human approval proposal only**. The recommendation remains conditional: pools
qualify consumer-controlled reservation/reclamation; composition-owned resources
remain smaller if that objective adds insufficient value. This PASS grants no
contract, implementation, integration or hardware approval.
The user explicitly approved this proposed Crux contract in this chat on 22 October
2026, selecting “Approve the proposed Crux contract (recommended)”. That approves
implementation of the contract above, not hardware operations, integration, Issue
closure or untested guarantees. Subsequent implementation evidence belongs in the
contract and validation record; this proposal retains the decision rationale.
