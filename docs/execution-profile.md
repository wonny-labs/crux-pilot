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
  bounded stack pool via attributes, require ENOMEM, release held slots and initialise
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
| Lifecycle/ownership | `k_thread_create` uses caller-owned thread/stack objects; join before reuse. Static composition need not allocate a heap. | `lib/posix/options/pthread.c` maintains a bounded thread pool. Attribute initialisation obtains a stack when dynamic threads are enabled. Join/recycling and attribute ownership matter; `pthread_create` can return `EAGAIN`, attribute allocation can fail. |
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
