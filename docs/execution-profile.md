# Reference-controller execution investigation

Status: investigation in progress for #6. No profile is selected by this draft.
Physical execution and timing/resource qualification are **Pending**: this task forbids
hardware operations. Selection for provisional software implementation requires an
independent reviewer PASS under the user-authorised #6/#7 delegation.

## Consumer and comparison conditions

The proposed consumer periodically samples the existing Time capability and reports a
cycle number, Time status and UTC value only when valid. Portable scenario logic needs
no thread handle, OS queue or scheduler API: environment composition invokes each cycle
and owns Time serialisation. No product control, network, storage or watchdog is needed.

Compare composition-local native Zephyr services and the relevant POSIX subset on the
pinned Zephyr v4.4.0 (`684c9e8f32e4373a21098559f748f06915f950c9`) and Linux functional
host. Retain C17, current public targets and the Time contract. A shared Crux execution
API has no demonstrated consumer here and is not proposed. This investigation does not
adopt a platform-wide standard or a new timing guarantee.

Before the first probe run, the validation objectives are:

- Six releases on a 10 ms monotonic phase grid, with no release
  earlier than its deadline. This is a short functional probe, not a 10 ms real-time
  guarantee; completion time and jitter are measurements. A two-second native thread join
  bounds native liveness; POSIX joins rely on the 10-second host / 20-second Twister
  outer harness bounds, not scheduling qualification.
- Native thread create/join and static-stack reuse; POSIX create/join with result
  delivery and attribute validation. Report configured stack, policy and priority.
- Mutex exclusion under contention, bounded lock expiry, subsequent lock success;
  native priority inheritance is checked with a priority-7 owner and priority-5 waiter.
  POSIX protocol-attribute support is assessed separately, including a negative result.
- Semaphore empty/nonblocking failure, finite wait expiry, successful wake-up and native
  bounded-count saturation; one-slot message delivery, full/empty failure and recovery.
- Native timeouts must not expire before the requested 10 ms. POSIX finite-wait checks
  allow 1 ms for the pinned millisecond conversion and observation quantisation. No
  upper latency limit is asserted by a host or simulated test.
- Deliberate 25 ms work after the second release crosses at least two deadlines. Skip
  elapsed phase points and resume at the next future deadline, without catch-up bursts;
  record per-release lateness and skipped points. A delayed semaphore producer waits
  20 ms before signalling; the initially empty wait must complete after that delay.
- Observe POSIX worker stack headroom using native test instrumentation. Exhaust its
  bounded stack pool via attributes, record the pinned failure outcome, release held slots and initialise
  an attribute again; this is allocation/recovery evidence, not long-term leak testing.

`tests/execution` separates native and POSIX probes. The same POSIX functional cases run
on Linux and Zephyr, using environment-specific test assertions only. Both Zephyr
candidate configurations enable stack accounting. Native worker stack is 4096 bytes;
POSIX explicitly uses eight 4096-byte dynamic-stack pool slots and a 32768-byte heap for
this investigation, not an adopted memory budget. Image map/configuration and measured
stack observations must accompany cost comparisons; different test content and pool
capacity prevent attribution of the entire image delta to API overhead.

## Source-backed semantic findings

Paths below are relative to the pinned Zephyr tree, not the moving latest branch.

| Concern | Native Zephyr | Selected POSIX comparison |
| --- | --- | --- |
| Lifecycle/ownership | `k_thread_create` uses caller-owned thread/stack objects; join before reuse. Static composition need not allocate a heap. | `lib/posix/options/pthread.c` maintains a bounded thread pool. Attribute initialisation obtains a stack when dynamic threads are enabled. Join/recycling and attribute ownership matter; `pthread_create` can return `EAGAIN`, attribute stack allocation can fail. In this pin `pthread_attr_init` still returns success after a failed stack allocation; subsequent runnable-attribute validation returns `EINVAL`, so do not promise an `ENOMEM` result. |
| Priorities | Lower numeric priority is more urgent; cooperative priorities are negative, pre-emptive non-negative. Composition chooses supported priorities explicitly. | `pthread.c` maps Zephyr negative priorities to `SCHED_FIFO`, non-negative to `SCHED_RR`, with reversed priority numbering. This mapping is not a claim of Linux-equivalent scheduling. |
| Mutex | `kernel/mutex.c` implements ownership, recursive locking and priority inheritance, with nested-lock limitations documented in its source. | `options/mutex.c` wraps kernel mutexes but `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)` returns `ENOTSUP` in this pin. Native underlying inheritance does not establish the requested POSIX attribute contract. |
| Timed waits | Native kernel relative/absolute uptime deadlines are distinct from RTC UTC. | `options/mutex.c`, `semaphore.c` and `mqueue.c` convert `CLOCK_REALTIME` absolute deadlines to millisecond kernel timeouts at entry using `timespec_to_timeout.c`; sub-millisecond time is truncated and long waits clamp to `UINT32_MAX` ms. Wall-clock adjustments before entry can affect those waits; an already converted wait is not a continuously tracked realtime deadline. |
| Periodic clock | `k_uptime_get` plus absolute kernel timeout avoids scheduling on Crux UTC. Tick resolution/configuration remains relevant. | `clock_selection.c` provides monotonic `clock_nanosleep`; its failure convention in this pin is `-1` plus `errno`, so do not assume every Linux/POSIX error convention matches. |
| Messaging/resources | `k_msgq` can use a statically bounded buffer; full/empty and expiry are observable. | `mqueue.c` allocates descriptors, queue objects, names and buffers from the heap; timed operations use realtime conversion. This consumer does not yet require a queue service. |

## Evidence and decision

Results, actual inputs, configuration identities, remaining physical cases and independent
review outcome will be recorded after execution. Existing recorded Time success/unset/error,
UART/RTC and reset evidence remains applicable to unchanged Time production source; it
cannot supply controller scheduling/resource evidence.

## Proposed bounded software profile for independent review

The candidate recommendation is native Zephyr execution in the reference composition,
with Linux-specific monotonic sleep in its host composition. The portable consumer has
no common OS-service dependency, so it does not need a POSIX dependency or Crux wrapper.
The POSIX comparison remains evidence, not a platform-wide rejection of POSIX.

| Area | Proposed reference composition support |
| --- | --- |
| Thread lifecycle | One caller-owned Zephyr thread/stack, created once for a finite scenario, normal return then `k_thread_join` before reuse. No cancellation, abort, detached lifecycle or restart contract. Linux may invoke the same scenario from its main thread. |
| Scheduling | Zephyr pre-emptive priority 5, statically configured 4096-byte worker stack, no runtime policy changes. Physical stack sufficiency and scheduling performance remain Pending. |
| Periodic release | A demonstrator period is a validation input, not a platform guarantee. `k_uptime_get`/absolute uptime sleep on Zephyr, `CLOCK_MONOTONIC` absolute sleep on Linux. After work crosses a deadline, skip elapsed phase points and resume at the first future point without a catch-up burst. |
| Time ownership | One scenario execution context owns all Time init/get/set calls; no concurrent Shell/command writer is included. Explicit scenario set operations are controlled inputs, not an implicit default clock. UTC changes never select the next release. |
| Synchronisation | Native `k_mutex` ownership/priority inheritance and `k_sem` bounded signalling/timed waits are tested services available to this composition. The single-owner scenario need not acquire an otherwise unnecessary mutex. No ISR use or nested-mutex guarantee is proposed. |
| Messaging | Native one-slot `k_msgq` comparison demonstrates bounded delivery/full/empty/expiry. A controller messaging service is not required for this increment. |
| Memory/failure | Static thread and message storage; no controller heap allocation. Probe queues/finite waits expose capacity/expiry results. A failed Time init is observable and prevents normal scenario execution; unset/read errors never fabricate UTC. |
| Evidence limit | Linux verifies functional behaviour, native_sim verifies Zephyr integration, EVKB cross-build proves compilation/footprint only. Physical execution, actual stack use and latency/jitter qualification remain Pending. No real-time, ISR or production support guarantee is added. |

This remains a proposal until the recorded independent evidence review returns PASS.
A materially different outcome, public interface or guarantee must pass its applicable
human gate rather than be inferred from this table.

The initial Zephyr POSIX build exposed a `<mqueue.h>`/signal-type configuration dependency: this pin requires `CONFIG_POSIX_REALTIME_SIGNALS=y` for the referenced `sigval`/`sigevent` declarations. The comparison configuration records that extra dependency. A probe variable also collided with the native `signal()` declaration and was renamed. These build failures are retained, not reported as passed tests.

## Recorded intermediate evidence

At `4d002b37`, hosted Linux run [39074635017](https://github.com/wonny-labs/crux-pilot/actions/runs/39074635017)
passed 28/28 CTest cases and the existing sample/quality/provenance checks. The POSIX
periodic probe recorded six releases, 80 ms aggregate and two skipped phase points.
These millisecond observations are not jitter bounds.

Zephyr run [39074635116](https://github.com/wonny-labs/crux-pilot/actions/runs/39074635116)
passed the existing Time path and native execution 4/4, but failed to compile the POSIX
probe for the configuration/name defects recorded above. Native periodic output was
100 ms aggregate, four skipped phase points and zero recorded millisecond lateness;
its stack accounting reported 4056 bytes unused out of 4096. Native simulation does
not model the physical Cortex-M stack use or establish a 40-byte target stack need.
The POSIX candidate and EVKB footprint comparison remain unverified at this checkpoint.
Downloaded raw logs/JUnit and artefacts are retained under ignored `build/evidence/m2`.

During local preparation, the first direct static-analysis run found a reserved feature-test macro definition and a swappable helper-parameter diagnostic. The macro was moved to the host target's compile definitions and the helper narrowed to realtime deadlines, without suppressions. Probe analysis is now included in the maintained host analysis path. The concurrent local Twister preparation attempt was stopped and retained as invalidated because source inputs changed; only a subsequent frozen-input run can be accepted.

## Independent decision and completed investigation

Independent reviewer `/root/m2_reviewer` returned **PASS for provisional composition-local
native Zephyr selection and #7 software start only**, after reconstructing requirements
and inspecting source, actual reports and downloaded maps/configurations. The recorded
[decision gate](https://github.com/wonny-labs/crux-pilot/issues/6#issuecomment-6242385007)
retains the hardware/merge prohibitions and physical Pending status. Initial REVISE
findings concerning per-release/overrun observations, stack recovery, wake-up and join
bounds were corrected and independently re-reviewed. This is not platform-wide adoption.

Clean local `d1e40d26` subsequently passed both EVKB candidate cross-builds as well as
Host Debug/sanitizer 28/28, native_sim 9/9 and expanded analysis/format/dependencies.
Before/after input captures match exactly, including four clean pinned dependencies.
Native FLASH/RAM: 52,328/17,412 bytes; POSIX:73,928/82,516 bytes. The earlier hosted
`a95c489` POSIX image was 16 Flash bytes smaller; the helper/macro/analysis changes and
actual source identities are kept separate. These configurations include different test
content and POSIX stack-pool/heap provisioning; the whole delta is not API overhead.

POSIX native_sim recorded six releases over 160 ms, ten skipped phase points and 10 ms
lateness per release, compared with native 100 ms/four skipped/0 ms recorded lateness.
Both observed 4056 unused simulated-stack bytes. POSIX exhausted eight 4096-byte stack
slots; attribute initialisation still succeeded, subsequent stack query returned EINVAL,
and a usable 4096-byte stack was verified after release. These results constrain the
comparison, not physical scheduling/stack guarantees. Static pool ownership and native
uptime fit the demonstrated consumer without a common OS dependency; this is the basis
for the scoped native choice. POSIX remains an option for another evidenced consumer.

The [early controller](../samples/controller/README.md) implements this bounded choice.
Physical target execution/timing/stack/resource validation remains **Pending** and cannot
be replaced by the successful software checks or used to close #7/M2.


## M2 coverage and outstanding acceptance

| M2 requirement | Evidence / remaining acceptance |
| --- | --- |
| Review base/Time boundaries before reorganisation | Existing module/header review and current source inspection retained; no component reorganisation or public contract change. |
| Time contract, host/Zephyr success, unset, error and reset; EVKB RTC/UART | Existing Time contract, host/Zephyr suites and recorded physical UART5/5/target2/2 reused; source remains unchanged. New controller integration tests assess its own handling. |
| Portable modules and prohibited-coupling detection | Host builds the same controller core; bounded checks cover core/headers, with deliberate Zephyr/board/vendor/header and environment-macro injection tests. New checks require actual passing results. |
| Native/POSIX comparison, execution profile and API limitations | Investigation results and independent scoped PASS above; target timing/resource execution remains Pending. |
| Early reference controller using Time and selected services; basic periodic scenario | Implemented software scenarios and unit cases await final verification/review; physical EVKB run remains Pending. |
| Relevant synchronisation/timing cases and no universal OSAL | Native/POSIX probes9/9 and explicit overrun/ownership/timeout limits; native composition uses existing APIs, with no new shared execution API. |
| Repeatable quality/CI and evidence | Existing paths extended for new sources and scenarios; final source checks and hosted results must be recorded. |

No row establishes M2 completion while the physical controller/timing/resource cases remain
Pending. M3 devices/network/watchdog, M4 persistence and M5 concurrent-load/full independent
consumer qualification remain outside this increment.

The first controller CTest run at `4a2feaa` built the test binary but failed its registration (`crux_controller_tests_NOT_BUILT`). Direct execution confirmed the five controller cases passed, but this was not counted as a successful CTest run. Including GoogleTest in the owning test directory fixes the directory-scoped discovery helper; final hosted verification must include the corrected registration. This CMake-only fix does not change the controller/Zephyr source inputs.
