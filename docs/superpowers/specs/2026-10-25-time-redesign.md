# Typed Time redesign

This records the detailed Time proposal accepted for implementation under the
current user-authorised Time redesign goal. PR #75 supplies the typed value
foundation; its starting HEAD is `42b1ea0` on main `314e1ab`.
The current goal supersedes earlier work-item restrictions that kept the old
RTC Time API unchanged. It does not authorise merging or hardware operations.

## Ownership and scope

- Time owns `time_types.h`, clock observation/deadlines in `time.h`, authority
  operations in `time_authority.h`, and calendar conversion in `time_calendar.h`.
- Thread owns `crux_thread_sleep_for(crux_duration_t)` and
  `crux_thread_sleep_until(crux_monotonic_time_t)`, using Time and its private
  native wait port. Time must not depend on Thread or public Sync.
- Delay owns `crux_delay_get_max_duration(crux_duration_t *)` and
  `crux_delay_busy_wait(crux_duration_t)` in `delay.h`, with a private native port.
- RTC remains an optional source/storage adapter. System realtime writes and
  RTC writes have separate results. No general RTC framework is introduced.
- Preserve C17/C++17, native-free public headers, explicit build composition,
  existing Queue/Sync/Thread join timeout contracts, and historical evidence.

## Values, clocks and waits

Preserve PR #75's signed nanosecond duration, nonnegative signed-64-bit
monotonic timestamp, and POSIX realtime `{seconds, nanoseconds}` representation.
Nanoseconds describe representation rather than resolution or accuracy.
Monotonic comparisons/differences require the same provider and execution
session. Checked failures preserve outputs; no overflow, saturation or implicit
FOREVER conversion is permitted.

`crux_time_get_monotonic(crux_monotonic_time_t *)` needs neither Time init nor
RTC. Thread context is supported; ISR returns NOT_SUPPORTED.
`crux_time_make_deadline_after(crux_duration_t, crux_monotonic_time_t *)`
rejects negative duration, returns the sampled observation unchanged for zero,
and compensates positive durations for clock quantisation from that sample.
Zephyr uses the upper bound of the next tick boundary from one tick sample.
Pure `crux_monotonic_add_duration()` never adds clock padding.

Sleep rejects negative input and unsupported context. Zero relative sleep is
immediate without yielding. Past/current absolute deadlines are immediate.
Early wake or retryable interrupt rechecks the original deadline; success
requires a final observation at or after it. Remove the old INT32_MAX-ms limit;
reject native absolute timeout overflow instead. No cancellation or maximum
scheduling latency is promised.
Time deadlines also serve comparison/polling consumers. Thread checks native
absolute-wait encoding limits separately; they do not restrict Time-only values.

## Realtime and authority

`crux_time_init()` prepares the selected realtime provider, initially unset;
successful repeated init preserves state and failed init may be retried.
`crux_time_get_realtime(crux_realtime_t *)` and
`crux_time_get_realtime_snapshot(crux_realtime_snapshot_t *)` use one private
native lock with all authority operations. No re-entry while holding it.
This consistency covers Crux writes, not external native clock setters.

Sources are NONE=0, UNKNOWN=1, RTC=2, MANUAL=3 and EXTERNAL=4. A snapshot has
`timestamp`, `last_source`, `is_time_valid` and `is_actively_synchronized`.
Synchronized implies valid and EXTERNAL; valid implies source != NONE.
`crux_time_adopt_realtime()` validates native time and adopts it as UNKNOWN,
valid, unsynchronized, without writing it. `crux_time_set_realtime(const
crux_realtime_t *, crux_time_source_t, bool)` accepts RTC/MANUAL/EXTERNAL;
only EXTERNAL may be actively synchronized. Invalid arguments preserve state.
An actual failed native write invalidates time/sync while retaining last source.
`crux_time_notify_sync_state(bool)` never writes the clock: invalid time gives
NO_DATA; synchronizing non-EXTERNAL valid time gives INVALID_STATE.
`crux_time_invalidate_realtime()` clears validity/sync and retains last source.

Before init, getters preserve outputs and return NOT_READY. Invalid time gives
NO_DATA from the timestamp getter, but an OK invalid snapshot with zero timestamp
without reading native time. Native NO_DATA invalidates and follows these same
results. Native range failure invalidates and returns OUT_OF_RANGE preserving
output. Other read errors preserve state/output. Validate pointers/values before
state/native access. Source metadata expresses adoption/synchronization state,
not a quantitative accuracy guarantee.

## Providers, calendar and delay

Linux uses CLOCK_MONOTONIC/CLOCK_REALTIME, absolute monotonic sleep and private
native mutexes. Test realtime writes with wrapped calls, never the system clock.
Retain controlled non-progressing memory realtime for development/tests.
Zephyr uses uptime, SYS_CLOCK_REALTIME and private native mutexes; Time must
configure without RTC. Preserve the RTC adapter and explicit load/store tests.

Calendar retains Gregorian UTC, seconds 0..59, no leap seconds and supported
interval [2000-01-01, 2100-01-01). Add normalized `nanoseconds` to the existing
six calendar fields; convert to/from `crux_realtime_t` without fractional loss.
Existing second-only command syntax supplies zero fractional seconds.

Busy wait uses CPU, not scheduler sleep. Zero succeeds, negative is invalid,
and provider-limit excess is rejected before waiting. Limit-query failure
preserves output. Zephyr rounds up native units and checks the actual native
counter/conversion limit, not merely UINT32_MAX microseconds. It supports thread
and ISR without Crux-added locks/allocation/scheduler blocking. The user explicitly
approved the pinned Zephyr native counter driver's short IRQ critical section on
the verified single-core EVKB; this is not a general lock-free/SMP guarantee.
Unsupported timer configurations return
NOT_SUPPORTED. Early boot and stopped timer/power states are not qualified.

## Migration and evidence

Migrate consumers, examples, tests, CMake/Kconfig, policy inventories and active
documentation together. Remove `crux_time_instant_t`, old get/set signatures,
`monotonic.h`, `crux::monotonic`, its native composition target and superseded
implementation after consumers move. No permanent compatibility layer.
Preserve historical files with historical API references clearly identified.
Use meaningful RED→GREEN tests, Host Debug/Offline/Sanitizer, applicable independent
consumers, native_sim and EVKB cross-builds, final CI and independent reviews.
Keep increments in Draft PRs and report any failures, skips and evidence limits.
