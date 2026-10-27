# Crux Time API contract

Time provides typed clocks, authority state and Gregorian UTC conversion. The
[accepted redesign](superpowers/specs/2026-10-25-time-redesign.md) replaces the
original Pilot's whole-second RTC-backed get/set contract. The initial plan and
[baseline](baseline.md) retain historical evidence; those physical results do
not qualify this redesigned API or its native system realtime implementation.

## Common status ownership

`<crux/status.h>` belongs to independent `crux::base`. Time uses its existing
status codes, including INVALID_ARGUMENT, OUT_OF_RANGE, NOT_READY, NO_DATA, IO,
NOT_SUPPORTED and INVALID_STATE. No duplicate Time status enum is introduced.

## Public Time API

The C17 API supports C++17 consumers through `extern "C"`. Link `crux::time`;
do not include private implementation directories or name backend targets.

| Header | Contract |
| --- | --- |
| `<crux/time_types.h>` | Distinct duration, monotonic timestamp and realtime value types; checked arithmetic |
| `<crux/time.h>` | Monotonic observation/deadlines, realtime init/observation and consistent snapshot |
| `<crux/time_authority.h>` | Explicit realtime set, adopt, synchronization and invalidation |
| `<crux/time_calendar.h>` | Pure Gregorian UTC/realtime conversion, including nanoseconds |

`crux_duration_t` is signed nanoseconds. `crux_monotonic_time_t` is nonnegative
provider/session-local nanoseconds, not an epoch or interchangeable clock domain.
`crux_realtime_t` contains signed POSIX epoch `seconds` and normalized uint32
`nanoseconds` in 0..999999999. Representation does not promise resolution or
accuracy. No leap seconds, implicit normalization, saturation or FOREVER encoding.
Checked failures preserve outputs; constructors and arithmetic retain the typed
foundation's overflow and signed rounding rules.

Realtime/calendar support `[2000-01-01T00:00:00, 2100-01-01T00:00:00)` UTC.
The seven-field `crux_datetime_t` adds nanoseconds to year/month/day/hour/minute/
second. Both conversion directions preserve fractions. Validate NULL, field
ranges and actual month length before the year range; malformed values are
INVALID_ARGUMENT and valid dates outside 2000..2099 are OUT_OF_RANGE. Conversion
is pure and reentrant with distinct output storage, without init/native locks.

The superseded `crux_time_instant_t`, `crux_time_get()` and `crux_time_set()`
are removed. Migrate to typed realtime and explicitly supply authority source.
No compatibility wrapper silently assigns authority or persists RTC storage.

## Typed monotonic observation and deadlines

`crux_monotonic_now()` and `crux_monotonic_deadline_after()` need neither
realtime init nor RTC. Linux uses CLOCK_MONOTONIC; Zephyr uses uptime ticks.
Concurrent thread callers use distinct output storage; ISR is NOT_SUPPORTED.
Negative duration is INVALID_ARGUMENT. Zero returns the unadjusted observation.
Positive duration compensates clock quantisation from one internal sample, not
from function return. Zephyr uses the upper bound of the next tick boundary.
Overflow/failure preserves output. Pure `crux_monotonic_add()` adds no
padding. [Thread sleep](thread-sync.md#current-thread-sleep) owns native wait
encoding limits and rechecks one original absolute deadline after early wakes.
A valid Time comparison deadline can exceed Thread's native finite timeout range.

## Header and build ownership

Follow the [repository dependency rules](../CONTRIBUTING.md#public-headers-and-module-dependencies).

| Implementation | Selection and visibility |
| --- | --- |
| `components/time/src/` | Common state, types/calendar/clock logic and separately selected OS-independent memory store |
| `components/time/src/realtime_port.h` | Private realtime init/read/write/lock contract; native lock rejects ISR |
| `components/time/src/monotonic_port.h` | Private raw-nanosecond observation and single-sample conservative upper bound |
| `components/time/src/busy_wait_port.h` | Private optional native wait or explicitly disabled implementation |
| `components/interrupt/src/interrupt.h` | Shared private ISR mechanism, no public usage target or native header leakage |
| `components/time/ports/linux/` | Native monotonic clock, pthread lock and optional CLOCK_REALTIME provider |
| `components/time/ports/zephyr/` | Native uptime/system realtime/lock and optional RTC adapter |
| `tests/time/*.h` | Private test controls, excluded from public usage roots |
| `samples/time/include/` | Sample-owned command interface |

External Linux applications select `CRUX_BUILD_TIME=ON` and explicitly choose
`CRUX_TIME_PROVIDER=memory` or `linux`. Top-level development builds default to
memory. Exactly one realtime provider is composed; Linux cannot silently fall
back to memory. Native lock/clock requirements stay private, and the established
consumer boundary checker remains enforced. Current supported glibc resolves
pthread operations in libc, as for Thread/Sync; other toolchains retain their
actual link requirements. This is not an installed package/ABI promise or
production Linux qualification.

Memory realtime is a controlled, non-progressing development/test store. It never
reads/writes the OS wall clock. Only private test controls advance it or inject
failures; these require serialized test use. Controlled Host shell/controller
samples require memory, so choosing the Linux system provider cannot silently
turn their demonstration writes into operating-system clock changes.

Zephyr `CONFIG_CRUX_TIME=y` provides native system realtime (`SYS_CLOCK_REALTIME`)
and monotonic uptime without RTC. Thread selects Time for sleep. Time does not
depend on Thread or public Sync. Optional `CONFIG_CRUX_TIME_RTC=y` additionally
requires RTC and compiles a trusted native adapter for the `rtc` devicetree alias;
it is not selected automatically. There is no runtime provider registry.

## State and error precedence

The first successful `crux_realtime_init()` enters ready-but-unset. It never writes a
default, adopts native time or loads RTC. Failed init is retryable; repeated
successful init preserves authority state. Public realtime operations require
thread context and share a private native mutex across clock access and metadata.
They support concurrent callers with distinct outputs. ISR is NOT_SUPPORTED;
validated argument errors precede context/readiness/native access. Do not re-enter
Time from provider operations while the lock is held.

`crux_realtime_now()` returns NO_DATA without a native read when invalid.
`crux_realtime_get_snapshot()` instead returns OK with a zero timestamp,
retained `last_source`, validity false and synchronization false. Before init,
both return NOT_READY and preserve output.

| Native observation | Result and state |
| --- | --- |
| OK, normalized and in range | Commit timestamp and metadata from one locked transaction |
| NO_DATA | Invalidate time/sync; getter returns NO_DATA, snapshot returns OK/invalid/zero |
| OUT_OF_RANGE or observed seconds outside supported range | Invalidate time/sync, retain source, return OUT_OF_RANGE and preserve output |
| Other read error, including malformed native nanoseconds mapped to IO | Preserve output and authority state |

The lock guarantees consistency among Crux operations. It cannot serialize native
clock setters outside Crux or guarantee snapshot consistency for those changes.
The designated product Time Authority must coordinate native clock changes with
any external NTP/PTP time manager and owns source selection and stale
synchronization callbacks. Separating the authority header is not a runtime
permission or authentication mechanism.

On Linux, setting CLOCK_REALTIME requires appropriate system permissions,
normally [CAP_SYS_TIME](https://man7.org/linux/man-pages/man7/capabilities.7.html).
Selecting the Linux provider or calling the Time Authority API does not grant
those permissions. Automated tests must not modify the real Host system clock.
Linux realtime-write tests use the controlled Memory provider or wrapped native
setters.

## Time Authority

`last_source` is NONE (0), UNKNOWN (1), RTC (2), MANUAL (3) or EXTERNAL (4).
It records the last successful establishment; it is not an accuracy estimate.
Valid time has a non-NONE source. Active synchronization implies valid time and
EXTERNAL source.

| Operation | Effect |
| --- | --- |
| `crux_realtime_adopt()` | Read/validate native clock without writing; success sets UNKNOWN, valid, unsynchronized |
| `crux_realtime_set(&value, source, sync)` | Set system clock and metadata as one transaction; accepts RTC/MANUAL/EXTERNAL and sync=true only with EXTERNAL |
| `crux_realtime_set_synchronized(sync)` | Metadata only, no clock write; invalid time gives NO_DATA; true with non-EXTERNAL valid source gives INVALID_STATE |
| `crux_realtime_invalidate()` | Clear validity/sync, retain previous source, no native read/write |

Reject malformed values/sources before native access and preserve state. An
actual native write failure invalidates time/sync because the clock might have
changed, while retaining the **previous** source. Lock/context/readiness failures
do not attempt a native write. Set success says nothing about RTC persistence.

## Private port and link selection

Common realtime state and selected native clock access share a provider-owned
lock initialized independently of Time readiness. Linux uses a statically
initialized pthread mutex. Zephyr uses a static kernel mutex; with multithreading
disabled the sole non-ISR context cannot interleave. No allocation or new public
synchronization service is required. Monotonic reads remain independent of this
realtime lock and initialization.

The optional native-only `time_rtc_zephyr.h` adapter offers explicit RTC read/store.
It reuses existing error mapping and calendar conversion, preserves read outputs
on failure and never accesses common authority state. Application composition
chooses when to read, establish system time with source RTC, or separately store
it. A failed store does not invalidate a successful system realtime update.
Current RTC storage accepts whole seconds only; normalized nonzero fractions
return NOT_SUPPORTED before any write rather than being silently truncated.
Missing/unready RTC returns NOT_READY. No generic public RTC framework is added.

## Host command composition

The portable sample still accepts `time set YYYY-MM-DDTHH:MM:SS`, `time get` and
`help`. Set supplies zero fractional seconds and MANUAL/unsynchronized authority.
Get renders whole seconds; the underlying typed API/calendar retain fractions.
No-data remains NO_DATA in the API and `ERR NOT_SET` in the command presentation.
Host and Zephyr Shell adapters share this parser and initialize Time explicitly.
The upstream RTC Shell stays disabled. Zephyr commands now use native system
realtime and do not implicitly persist RTC. Native simulation, wrapped Linux
writes and software cross-builds provide software evidence only; historical
physical RTC measurements are not evidence for the new implementation.

## Unified Time migration (Issue #87)

Time also owns [optional CPU Busy-wait](time-busy-wait.md). `crux_busy_wait()` is
always declared in `<crux/time.h>` when Time is available, regardless of native
Busy-wait selection. There is no separate Delay component/header/usage target or
maximum query. Thread retains sleep; Time does not depend on Thread or Sync.

| Previous API | Current API |
| --- | --- |
| `crux_time_get_monotonic`, `crux_time_make_deadline_after` | `crux_monotonic_now`, `crux_monotonic_deadline_after` |
| `crux_monotonic_add_duration` | `crux_monotonic_add(base, delta, out)` |
| `crux_time_init`, `crux_time_get_realtime`, `crux_time_get_realtime_snapshot` | `crux_realtime_init`, `crux_realtime_now`, `crux_realtime_get_snapshot` |
| `crux_time_adopt_realtime`, `crux_time_set_realtime` | `crux_realtime_adopt`, `crux_realtime_set` |
| `crux_time_notify_sync_state`, `crux_time_invalidate_realtime` | `crux_realtime_set_synchronized`, `crux_realtime_invalidate` |
| `crux_time_source_t`, `CRUX_TIME_SOURCE_*` | `crux_realtime_source_t`, `CRUX_REALTIME_SOURCE_*` (numeric values retained) |
| snapshot `is_time_valid`, `is_actively_synchronized` | `is_valid`, `is_synchronized` |
| `crux_time_utc_t`, `crux_time_from_utc`, `crux_time_to_utc` | `crux_datetime_t`, `crux_realtime_from_utc`, `crux_realtime_to_utc` |

`crux_datetime_t` contains calendar fields without timezone data. The UTC
conversions interpret those fields as UTC; no timezone or leap-second support is
added. This is an intentional source migration without permanent wrappers or an
installed ABI guarantee. Existing signed arithmetic, quantization compensation,
realtime metadata/error transitions and output-preservation rules are retained.
Touched private OS boundaries use `_impl_` names with `monotonic_port.h`,
`realtime_port.h`, `busy_wait_port.h` and `thread_sleep_port.h`. The common private
ISR query is shared without exposing its header to public consumers.
