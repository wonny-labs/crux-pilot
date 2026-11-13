# Thread and Sync contract

**Freeze notice (13 November 2026).** Thread and Sync are frozen for new product code: no new features and no real-time extension. New product code uses Zephyr directly for threads, locks, queues and UARTs; see [Choosing an API](choosing-an-api.md). This adds no promise and removes none. The contract below and its tests and evidence stay as they are, and existing users keep working. Whether the interface is deprecated waits for a later decision under P9 of [ADR-0002](decisions/0002-platform-direction-zephyr-execution-crux-services.md). The product template's [domain include check](../templates/product/README.md#zones-and-the-domain-include-check) rejects the header in a product's domain.

**Status: implemented contract.** This is the current consumer contract for the
independent Thread and Sync components. It consolidates the adopted behavioral
rules from PR #36 and the ownership split merged in PR #52, with typed monotonic
sleep owned by Thread. Public declarations remain authoritative for their exact C
types and signatures: [thread.h](../components/thread/include/crux/thread.h) and
[sync.h](../components/sync/include/crux/sync.h).

Thread provides bounded joinable workers and current-thread sleep. Sync provides nonrecursive mutexes and
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
  resource metadata, its protection (a lock in Thread, atomics in Sync) and deadline
  helpers; neither accesses the other's state.
  Providers own native objects, stacks, configuration and private headers.
  Thread additionally depends on Time for typed monotonic observation/deadlines;
  Time does not depend on Thread or Sync, and monotonic use requires no RTC/init.
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
  each operation, retaining an operation reference until native access is complete:
  Thread under private metadata protection, Sync with one atomic update of the slot
  ([Sync slot metadata](#sync-slot-metadata)). Destroy returns busy for existing
  admitted users, waiters or held mutexes. It cannot recycle a slot between validation and access.
  Concurrent join admission returns busy. This is not automatic cancellation or
  permission for callers to race fresh admissions against destruction.

The declaration shape below is implemented in the component-owned public headers.
The handle types are distinct portable structs containing reserved 32-bit slot and
generation fields; consumers treat them as opaque values. The thread-options type
contains `size_t stack_size_bytes` and the priority-class enum. Both are mandatory
inputs; a null options pointer is invalid. Priority enum values are low=0, normal=1,
high=2. All create operations take the output last, write it only on success and
leave it unchanged on failure. This replaces the earlier invalid-on-failure contract;
check the result before using new output storage. Do not overwrite an existing live
resource on successful creation. Join/destroy zero the
supplied handle on success; copied aliases become stale and are rejected.

A zero-initialised `crux_thread_options_t` selects LOW priority, not NORMAL.
For normal priority with the provider-default stack size, specify both fields:

```c
crux_thread_options_t options = {
    .stack_size_bytes = 0,
    .priority = CRUX_THREAD_PRIORITY_NORMAL,
};
```

```c
typedef void (*crux_thread_entry_t)(void *argument);

crux_status_t crux_thread_create(const crux_thread_options_t *options,
                                 crux_thread_entry_t entry, void *argument,
                                 crux_thread_t *out);
crux_status_t crux_thread_join(crux_thread_t *thread, uint32_t timeout_ms);
crux_status_t crux_thread_sleep_for(crux_duration_t duration);
crux_status_t crux_thread_sleep_until(crux_monotonic_time_t deadline);

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
  configuration must reject an unsupported mapping. Crux's own service workers can be
  moved away from their class's number by Kconfig: see
  [Priority map of Crux threads](#priority-map-of-crux-threads). Host uses unprivileged normal
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

## Priority map of Crux threads

Decision P3 of [ADR-0002](decisions/0002-platform-direction-zephyr-execution-crux-services.md) gives every thread that Crux itself creates on Zephyr its own priority setting, so that a product can place Crux's threads in its one priority map ([coexistence rule 1](choosing-an-api.md#coexistence-in-one-image)). Each default is the priority that the thread has today, so an image that sets none of them is unchanged. Thread's API, its three classes and their 4/5/6 mapping do not change: Thread is frozen.

**Threads that Crux creates on Zephyr:**

| Thread | Started by | Today's priority | Setting | Default |
| --- | --- | --- | --- | --- |
| Security event drain | `crux_security_event_start()` | 6, a `LOW` Crux Thread worker | `CONFIG_CRUX_SECURITY_EVENT_DRAIN_PRIORITY` | 6 |
| Reboot service worker | `crux_reboot_start_worker()` | 6, `LOW` | `CONFIG_CRUX_REBOOT_WORKER_PRIORITY` | 6 |
| Network settings service worker | `crux_network_settings_start_worker()` | 6, `LOW` | `CONFIG_CRUX_NETWORK_SETTINGS_WORKER_PRIORITY` | 6 |
| Web session login worker | `crux_web_session_start_worker()` | 6, `LOW` | `CONFIG_CRUX_WEB_SESSION_WORKER_PRIORITY` | 6 |
| Web management save worker | `crux_web_management_start_worker()` | 6, `LOW` | `CONFIG_CRUX_WEB_MANAGEMENT_WORKER_PRIORITY` | 6 |
| SSH listener | the SSH provider's start, a native `k_thread` | `K_LOWEST_APPLICATION_THREAD_PRIO`, which is `CONFIG_NUM_PREEMPT_PRIORITIES - 1` (14 at Zephyr's default of 15) | `CONFIG_CRUX_SSH_LISTENER_PRIORITY` | -1, the same |
| SSH connection workers, up to `CONFIG_CRUX_SSH_MAX_CONNECTIONS` | the listener, one per connection, native `k_thread`s | The same | `CONFIG_CRUX_SSH_WORKER_PRIORITY` | -1, the same |
| A product's own Crux Thread workers (existing users) | `crux_thread_create()` | 4, 5 or 6 by class | None: the class decides | — |

The command line's serial console runs on the thread that the composition calls `crux_cli_console_run()` from, and SSH commands run on their connection's worker. A status read runs on its caller's thread. None of them creates a thread. The web adapter's HTTP server thread is Zephyr's: it runs at the lowest preemptive priority, or cooperatively with `CONFIG_NET_TC_THREAD_COOPERATIVE` (see [Web sessions](web-session.md)), and Crux has no setting for it. Zephyr's network, logging and work queue threads are set by Zephyr's own Kconfig. The product's map lists them too.

**Values.** Each setting is a Zephyr preemptive priority, from 0 to `CONFIG_NUM_PREEMPT_PRIORITIES - 1`, where a lower number runs first. The two SSH settings also take -1, their default, which keeps `K_LOWEST_APPLICATION_THREAD_PRIO` and so follows `CONFIG_NUM_PREEMPT_PRIORITIES`. Kconfig cannot subtract, so each range ends at `CONFIG_NUM_PREEMPT_PRIORITIES`, the idle thread's priority. A value above the range or below it fails Kconfig, and the idle priority itself fails a `BUILD_ASSERT` in `components/service_priority` or in the SSH provider. Cooperative (negative) priorities are refused, as Crux's workers have always been preemptible and Thread promises no cooperative threads. Thread itself still requires more than six preemptive levels for its 4/5/6 mapping, whatever these settings are.

**Why numbers, not classes.** A setting names a Zephyr priority, not one of Thread's three classes. A class would tie the setting to the shim and to 4, 5 and 6, which is the per-class alternative that P3 did not choose. A number places a worker anywhere in the map, for example below the product's real-time threads, and it keeps its meaning if the workers later become native threads under P9.

**How a setting is applied.** Each service worker is still created by `crux_thread_create()` at the `LOW` class. On Zephyr its entry first calls a private placement, `crux_service_priority_place()` in `components/service_priority`, which sets the calling thread's priority to the setting. At the default the priority is already 6 and nothing changes. The Crux module builds the placement with Crux Thread, and defines a private interface target, `crux_service_priority_placement`, with no `crux::` alias. It carries the private include directory and `CRUX_SERVICE_PRIORITY_PLACEMENT`, and nothing else gets them. A composition or test that compiles one of the five worker sources links that target to the target that compiles them, as the compositions under `tests/native` and `tests/zephyr` do. `tests/service_priority/check_placement.py` fails a Zephyr CMake file that compiles a worker source without the link, so a worker cannot be left at 6 unnoticed. The Linux Host calls nothing, as Host Thread has no priorities. Two limits follow from the frozen Thread API:

- Until a worker first runs, it is at 6. A worker set below 6 can therefore run its first instructions, the placement call, at 6 before it drops to its setting. A worker set above 6 starts when 6 is next scheduled. Each start does this once. The compositions start each worker once and do not join it. Only the security event drain can be stopped, joined and started again, through `crux_security_event_test_stop_drain()`, which exists in test builds only.
- The workers take slots from `CONFIG_CRUX_THREAD_COUNT` and stacks of `CONFIG_CRUX_THREAD_STACK_BYTES`, as before. The product sets those budgets ([coexistence rule 2](choosing-an-api.md#coexistence-in-one-image)).

**Declaring the map.** A product writes its map once, with these settings in the image's `prj.conf` next to its own threads' priorities. For example, with real-time threads at 2 to 5:

```
CONFIG_NUM_PREEMPT_PRIORITIES=15
CONFIG_CRUX_SECURITY_EVENT_DRAIN_PRIORITY=10
CONFIG_CRUX_REBOOT_WORKER_PRIORITY=10
CONFIG_CRUX_NETWORK_SETTINGS_WORKER_PRIORITY=10
CONFIG_CRUX_WEB_SESSION_WORKER_PRIORITY=11
CONFIG_CRUX_WEB_MANAGEMENT_WORKER_PRIORITY=11
CONFIG_CRUX_SSH_LISTENER_PRIORITY=12
CONFIG_CRUX_SSH_WORKER_PRIORITY=13
```

The numbers are an illustration, not a measured recommendation. The web session worker at 11 still preempts the HTTP server thread at 14, as it does at its default.

**Stability.** These are Kconfig symbols, which are interface under rule 5 of [ADR-0001 Decision 4](decisions/0001-application-boundary-and-api-stability.md#42-change-rules). No Zephyr image outside `tests/` starts the services they place: the security event drain, the network settings worker and the two web workers are started only by images under `tests/native` and `tests/zephyr`, and no Zephyr image starts the reboot worker at all. By [Decision 4.1](decisions/0001-application-boundary-and-api-stability.md#41-tiers) they therefore start as Experimental. `components/service_priority` is private.

**Evidence.** `tests/service_priority/zephyr` reads the priorities back with `k_thread_priority_get()` on `native_sim`, from a `LOW` Crux Thread worker that places itself as a service worker does, at the defaults and with every worker moved (0 to 14). It also checks that the class mapping stays 4/5/6. The services and the SSH provider are not started there, as they need their whole composition and SSH needs wolfSSH, so `tests/service_priority/check_placement.py` checks statically which entry calls which identifier, the defaults, the SSH provider's two priorities and that no service creates a thread outside this table. Nothing has been measured on a target.

## Current-thread sleep

`sleep_for()` uses a Time deadline with positive-duration clock quantisation
compensation; zero succeeds immediately without yielding. Negative duration or
deadline is invalid-argument. `sleep_until()` returns immediately for an observed
past/current deadline, and rechecks the same original deadline after early wake
or retryable interrupt. Success means a final monotonic observation reached the
deadline. Sleep supports the typed timestamp/native absolute timeout range,
without the old Monotonic INT32_MAX-millisecond limit. Overflow is rejected rather
than clamped or converted to FOREVER. ISR is unsupported; no cancellation or
maximum scheduling latency is promised. Join's existing millisecond timeout and
lifetime rules remain separate.

## Synchronisation

- Mutex operations: `crux_mutex_create`, `crux_mutex_lock`,
  `crux_mutex_unlock`, `crux_mutex_destroy`. Nonrecursive, owner-only unlock.
  Self-lock/wrong-owner unlock are invalid-state errors; destroy of a held or
  referenced mutex returns busy. No robust owner-death recovery or fairness promise.
- Zephyr uses native priority inheritance with its nested-lock limitations; Sync holds
  no kernel mutex of its own around the native calls, so a holder's priority follows
  the native mutex alone. Host validates functional exclusion and ownership.
  Portable priority inheritance or
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
  Sync has no metadata lock: its admission never waits, and only the Host semaphore's
  internal lock can make a Sync poll return busy while a token is available.
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

## Sync slot metadata

Sync keeps its slot metadata with atomics and has no metadata lock. It used to take one
private kernel mutex in every call, and `crux_mutex_unlock` released the native mutex
inside it. On Zephyr, releasing that metadata mutex restored the priority saved when it
was taken, so a holder kept an inherited priority after unlock.
[Draft PR #174](https://github.com/wonny-labs/crux-pilot/pull/174) locked the scheduler
instead; the owner chose on 13 November 2026 to remove the lock (decision P6
of the platform direction review), with the pools unchanged.

- Each slot has one 32-bit atomic state word: the number of admitted operations
  (14 bits), a mutex's held flag, a live flag and the low 16 bits of the generation.
  The full generation is a separate atomic, written only while the slot is claimed.
- Admission is one compare-and-swap that requires the live flag and the handle's
  generation bits and adds a reference; the full generation is then compared under that
  reference. Create claims a free slot with one compare-and-swap. Destroy succeeds only
  by exchanging "live, its own reference only, not held" for claimed, so it still
  returns busy for admitted users, waiters and held mutexes. A slot whose generation
  reaches `UINT32_MAX` retires after that lifetime.
- A mutex owner stores its native identity after the native lock and clears the held
  flag before the native unlock. Once the native unlock hands the mutex over, the old
  owner writes nothing in the slot except its reference release. Self-lock and
  wrong-owner checks compare the caller with the stored identity.
- No Sync lock is held while a native object is locked, unlocked, given or taken, so
  Sync adds nothing to native priority inheritance. On Zephyr, `k_sem_give` at the limit
  drops a token silently; a semaphore therefore keeps an atomic token count that a give
  reserves before `k_sem_give` and a take releases after `k_sem_take`. A give can report
  full while a take that has consumed a token has not yet returned.

Limits:

- Lock-free 32-bit and identity-sized atomics are required (a pointer on Zephyr,
  `unsigned long` for glibc `pthread_t`); the build fails without them, for example
  on ARMv6-M. There is no CPU-count, meta-IRQ or scheduler assumption and no scheduler
  or interrupt lock. A compare-and-swap loop runs again only after another thread
  changed the same word or, on load-linked/store-conditional processors, after a
  spurious failure; no Sync call waits for another.
- Destroy no longer waits for an operation in progress. It returns busy while another
  thread's call on the same object has not returned, including a give or unlock that
  has already woken the destroying thread: on Zephyr a higher-priority waiter runs
  before the signaller's call returns. Join the signalling thread first, or retry on
  busy. With the old metadata mutex, "signal, then the woken waiter destroys" succeeded.
- At most 16383 operations can be admitted on one object at a time; another returns
  no-resources, and destroy returns busy.
- Create checks the slots one at a time. While other threads create and destroy
  objects of the same kind, it can return no-resources although some slot was free at
  a moment during the call.
- A stale handle can briefly hold a reference to a later lifetime of its slot only if
  that slot is recreated a multiple of 65536 times during the call; a destroy of that
  lifetime at that moment returns busy.
- Thread context only, as before; ISR rules are unchanged.
- Out of scope: Zephyr's nested-lock limitation, and other components whose own kernel
  mutex is taken while the caller holds a Crux mutex.

## Build selection and provider limits

| Integration | Selection | Resource and native limits |
| --- | --- | --- |
| Linux Host | `CRUX_BUILD_THREAD` and `CRUX_BUILD_SYNC`, independently selectable; Thread also requires `CRUX_BUILD_TIME=ON` and an explicit Time provider (`memory` for controlled Host realtime); top-level defaults ON, dependency defaults OFF | Default 4 workers, 8 mutexes and 8 semaphores; current thread provider requests 65536 stack bytes for every worker, raised to the libc's `PTHREAD_STACK_MIN` where that is larger (131072 on aarch64 glibc, 16384 on x86-64 glibc, so 65536 there), and rejects requests above 65536 |
| Zephyr Thread | `CONFIG_CRUX_THREAD` | `CONFIG_CRUX_THREAD_COUNT` (default 4, range 1..64), `CONFIG_CRUX_THREAD_STACK_BYTES` (default 4096, range 1024..65536 usable bytes) |
| Zephyr Sync | `CONFIG_CRUX_SYNC` | `CONFIG_CRUX_SYNC_MUTEX_COUNT` and `CONFIG_CRUX_SYNC_SEMAPHORE_COUNT` (each default 8, range 1..64) |

Zephyr Thread and Sync each require multithreading and select 64-bit timeouts.
Thread selects Time for monotonic sleep without requiring RTC.
Thread requires more than six preemptive priority levels for its 4/5/6 mapping.
The per-service priority settings of Crux's own workers and SSH threads are in
[Priority map of Crux threads](#priority-map-of-crux-threads).
The native Thread port owns static worker stacks; the Sync port owns native
identity and synchronization. Native include paths and private implementation
configuration are not consumer interfaces. Use the [component build definitions](../components/thread/CMakeLists.txt),
[Sync build definition](../components/sync/CMakeLists.txt), [Zephyr Kconfig](../zephyr/Kconfig)
and [module integration](../zephyr/CMakeLists.txt) for the current composition.
The [source-tree integration contract](build.md#external-linux-application-using-crux-source)
covers selected Linux Host consumers, not an installable SDK or arbitrary toolchains.

The timeout contract above applies to `join`, `mutex_lock` and `semaphore_take`.
Create, unlock, give and destroy take no timeout and do not carry the finite-wait or
nonblocking-poll guarantee. Thread create can wait for Thread metadata. Sync calls take
no Sync lock; the Host semaphore's give waits for that semaphore's internal lock.
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
  metadata (Thread under a lock, Sync with atomics) and deadline helpers. Small private duplication avoids a
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
