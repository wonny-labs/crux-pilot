# Thread and Sync contract

**Status: implemented contract.** This is the current consumer contract for the
independent Thread and Sync components. It consolidates the adopted behavioral
rules from PR #36 and the ownership split merged in PR #52; it introduces no API
or behavior change. Public declarations remain authoritative for their exact C
types and signatures: [thread.h](../components/thread/include/crux/thread.h) and
[sync.h](../components/sync/include/crux/sync.h).

Thread provides bounded joinable workers. Sync provides nonrecursive mutexes and
bounded counting semaphores, including use from native OS-created threads. The
contract covers functional lifecycle, ownership, visibility and relative waits;
it does not promise production runtime compatibility or realtime scheduling.
For application/composition responsibilities, see the [architecture](architecture.md).

## Ownership, resources and errors

- Public ownership: `components/thread/include/crux/thread.h` and
  `components/sync/include/crux/sync.h` provide C/C++ compatible C17 declarations
  without native headers/types. Consumers link `crux::thread` and/or `crux::sync`.
  Final composition selects each native implementation independently. Sync uses
  native OS identity and does not depend on Crux Thread. Each component owns its
  resource metadata, lock and deadline helpers; neither accesses the other's state.
  Providers own native objects, stacks, configuration and private headers.
- Handles are distinct portable value types per resource kind, backed by a slot
  and generation. Zero is invalid. Validate null output pointers and invalid/stale
  handles; generation wrap must retire a slot rather than make an old handle valid.
  No global service locator or consumer-accessible native object lookup is added.
- Pools are bounded at build time. Default resource profile: four thread
  slots, eight mutexes, eight semaphores, 4096 usable stack bytes per Zephyr worker.
  These are defaults, not a platform-wide product resource budget. Creation failure
  leaves the output invalid and rolls back reservations. No public allocator.
- Operations return `crux_status_t`. Relevant values include
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

The declaration shape below is implemented in the component-owned public headers.
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

## Threads, stack and priority

- Operations: `crux_thread_create`, `crux_thread_join`. Entry is
  `void (*)(void *)`; normal return completes work. Argument storage and all reachable
  resources remain valid until successful join. Entry can run before create returns.
- Create accepts a minimum usable stack request and one of three priority classes:
  low, normal, high. Zero stack request selects provider default. Zephyr rejects a
  request larger than its configured per-slot capacity; it never silently shrinks.
  Requests below that capacity use the whole static slot. Host rounds up to at
  least its documented native minimum/default; actual allocations are recorded.
- Zephyr maps high/normal/low to preemptive priorities 4/5/6 in the current profile;
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

## Synchronisation

- Mutex operations: `crux_mutex_create`, `crux_mutex_lock`,
  `crux_mutex_unlock`, `crux_mutex_destroy`. Nonrecursive, owner-only unlock.
  Self-lock/wrong-owner unlock are invalid-state errors; destroy of a held or
  referenced mutex returns busy. No robust owner-death recovery or fairness promise.
- Zephyr uses native priority inheritance with its nested-lock limitations; Host
  validates functional exclusion and ownership. Portable priority inheritance or
  bounded inversion is not guaranteed. Nested-lock scheduling qualification is out
  of scope; consumers must define a consistent lock order.
- Semaphore operations: `crux_semaphore_create(initial, maximum)`,
  `crux_semaphore_take`, `crux_semaphore_give`, `crux_semaphore_destroy`.
  Maximum is 1..`INT32_MAX`; initial must be <= maximum. Give at maximum returns
  busy without changing count. This preserves tokens and detects an unexpected full
  channel; intentional coalescing remains consumer policy. No reset/drain API.
- Semaphores carry no ownership and provide release/acquire visibility on successful
  give/take. Successful mutex unlock/lock likewise publishes protected writes.
  Waiter ordering is unspecified. Session teardown joins workers, unlocks mutexes
  and stops users before destroying synchronisation objects.

## Wait and context semantics

- Wait arguments are unsigned relative milliseconds: zero polls, 1..`INT32_MAX`
  are finite; larger values return out-of-range. No forever sentinel in this profile.
  This makes every supported acquisition/join wait explicitly bounded in time intent.
- Relative waits use monotonic elapsed time, independent of RTC/UTC. Convert upward
  to native resolution, check arithmetic overflow and retry interruptions/spurious
  wakeups against one original deadline. No early expiry caused by truncation.
- A zero-time poll never waits for provider metadata or native object locks. It returns
  busy if either lock cannot be acquired immediately, even when the resource might be
  available; no handle lifetime or token state changes in that case. Otherwise an
  unavailable resource returns busy. Once admitted, polls complete their native try
  and private bookkeeping before releasing provider metadata, so they never block on
  a second metadata acquisition after changing resource state. A finite wait uses one
  original monotonic deadline for metadata admission and the native resource wait.
  Availability racing expiry may produce success or timeout, without duplicated/lost
  ownership or tokens. Error/output rules must be uniform across providers.
- The timeout bounds metadata admission and the requested resource wait. Scheduler
  delays and the private bookkeeping needed to finish an admitted operation may delay
  function return; no maximum observed latency or realtime guarantee follows from this
  API.
- Thread context only, including give and poll. ISR, signal-handler and native
  callback contexts that forbid blocking are unsupported. Zephyr rejects ISR entry
  before calling blocking native primitives. Host asynchronous-signal use is a
  caller precondition, not a claim of detectable signal context.

## Build selection and provider limits

| Integration | Selection | Resource and native limits |
| --- | --- | --- |
| Linux Host | `CRUX_BUILD_THREAD` and `CRUX_BUILD_SYNC`, independently selectable; top-level defaults ON, dependency defaults OFF | Default 4 workers, 8 mutexes and 8 semaphores; current thread provider requests 65536 stack bytes for every worker and rejects requests above 65536 |
| Zephyr Thread | `CONFIG_CRUX_THREAD` | `CONFIG_CRUX_THREAD_COUNT` (default 4, range 1..64), `CONFIG_CRUX_THREAD_STACK_BYTES` (default 4096, range 1024..65536 usable bytes) |
| Zephyr Sync | `CONFIG_CRUX_SYNC` | `CONFIG_CRUX_SYNC_MUTEX_COUNT` and `CONFIG_CRUX_SYNC_SEMAPHORE_COUNT` (each default 8, range 1..64) |

Zephyr Thread and Sync each require multithreading and select 64-bit timeouts.
Thread requires more than six preemptive priority levels for its 4/5/6 mapping.
The native Thread port owns static worker stacks; the Sync port owns native
identity and synchronization. Native include paths and private implementation
configuration are not consumer interfaces. Use the [component build definitions](../components/thread/CMakeLists.txt),
[Sync build definition](../components/sync/CMakeLists.txt), [Zephyr Kconfig](../zephyr/Kconfig)
and [module integration](../zephyr/CMakeLists.txt) for the current composition.
The [source-tree integration contract](build.md#external-linux-application-using-crux-source)
covers selected Linux Host consumers, not an installable SDK or arbitrary toolchains.

The timeout contract above applies to `join`, `mutex_lock` and `semaphore_take`.
Create, unlock, give and destroy take no timeout; they can acquire internal
metadata locks and do not carry the finite-wait or nonblocking-poll guarantee.
The Linux Host port is development/test support, not a qualified production Linux
variant. Zephyr cross-builds do not establish physical board behavior.

## Design decisions and alternatives

- **Small capability contract rather than universal OSAL.** Representative firmware
  and runtime-shaped consumers needed portable worker ownership, bounded tokens,
  resource reclamation and finite monotonic waits. The existing controller's
  composition-local native lifecycle remains valid where those needs do not apply.
- **Bounded pools and checked values.** Consumer-controlled create/join/destroy and
  repeated session cleanup justify bounded reservation and reclamation. Generation
  checks, admission protection and quiescent destruction prevent stale aliases from
  reaching recycled objects. Fixed composition-lifetime resources remain a smaller
  alternative when consumer-controlled reservation is unnecessary.
- **Qualified POSIX remains a valid alternative for different requirements.** The
  original pinned assessment considered monotonic condition variables, while a
  completion predicate alone did not establish worker termination or bounded join.
  Direct timed waits, join extensions, stack lifetime and semaphore-full behavior
  required pin-specific qualification. The retained probe also found a condition
  timeout ownership failure on its Zephyr revision. Those observations are about
  that recorded pin, not a claim about all POSIX or current upstream Zephyr.
- **Separate component ownership.** Thread and Sync have independent native state,
  metadata protection and deadline helpers. Small private duplication avoids a
  shared provider dependency; Sync relies on native identity, not a Crux worker.

The [archived decision and approval record](history/execution-contract-proposal.md)
retains the detailed alternatives, original pin/source references, planning-input
identities and human approval, including the PR #36 poll-contention clarification.
The [archived validation](history/execution-validation.md) and its linked raw probes
retain the evidence for the POSIX assessment. These are historical rationale and
evidence, not additional current usage contracts.

## Validation and traceability

Current test entry points and supported configurations are in the
[test guide](../tests/README.md#portable-execution-capability). Tests retain some
Execution names for continuity; that does not reintroduce an Execution component.
Conformance covers finite/poll waits, retained resources after timeout, join
visibility, pool exhaustion/rollback, stale handles, ownership, busy destruction,
and contention/admission cases. Independent Thread-only/Sync-only consumers check
C/C++ public usage, exclusion of the unselected component and native-thread Sync
ownership. Tests and successful cross-builds do not establish physical scheduling,
worst-case stack headroom, fairness, bounded inversion or stuck-worker recovery.

| Record | Meaning |
| --- | --- |
| [PR #36](https://github.com/wonny-labs/crux-pilot/pull/36), [archived validation](history/execution-validation.md) and [retained evidence](evidence/m5-execution/README.md) | Original combined Execution implementation, qualification revisions, failures, follow-up corrections and measurements; results apply only to those inputs |
| [PR #52](https://github.com/wonny-labs/crux-pilot/pull/52), reviewed HEAD `210a7cdd`, merged as `2b33fbcd50e3fc3ff77b955791e36aaa32b5f554` | Thread/Sync separation, current ownership and independent consumer evidence |
| [Pre-cleanup snapshot](https://github.com/wonny-labs/crux-pilot/tree/ab3a7f545d62b619a09801fccde8d6d22792c396/docs) | Original document paths/content before this consolidation; archived files link to their exact original versions |

Historical Issue #24/#25 dependency statements and earlier test totals are retained
with their original revisions, not asserted as current issue status. This document
adds no new runtime test result or physical qualification. Historical M2 measurements
and simulated/cross-build evidence do not satisfy a later physical acceptance gate.
