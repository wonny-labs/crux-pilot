# Time CPU Busy-wait

`<crux/time.h>` provides exactly one CPU Busy-wait operation,
`crux_busy_wait(crux_duration_t)` with the [typed Time values](time-contract.md).
Busy-wait consumes CPU while scheduled. Use [Thread sleep](thread-sync.md#current-thread-sleep)
when the caller should release the CPU to the scheduler.

The API adds no Crux-owned locks, dynamic memory allocation or scheduler-based
blocking. Brief native Zephyr interrupt-masked clock-read sections are permitted;
interrupts are not masked around the requested delay. Caller interrupt state is
preserved. IRQ-delivery validation assumes ordinary thread entry with interrupts
enabled. ISR support depends on the selected provider and target configuration.
No general lock-free guarantee or maximum execution latency is claimed.

## Inputs and results

- Zero succeeds immediately without native access or yielding, even when no
  positive delay is supported. Negative durations are INVALID_ARGUMENT.
- Every enabled wait checks its provider-local safe input bound and native
  representation before waiting. No public or private Port maximum query exists.
- Exceeding a limit is OUT_OF_RANGE before wait. Native units round upward;
  conversion never wraps, saturates or changes into an infinite wait.
- With Busy-wait disabled, the symbol remains declared and linkable: negative
  returns INVALID_ARGUMENT, zero OK, positive NOT_SUPPORTED. This path reads no
  clock and links no optional Busy-wait native provider. No realtime init or RTC
  is required. Enabled native read failures are IO or the documented native error.
- Preemption/interrupts can extend return time. Early boot, stopped clocks,
  unvalidated power states and zero-latency/NMI interrupt contexts are outside
  the verified scope. The API does not provide cancellation.

## Native limits and context

Linux uses CLOCK_MONOTONIC polling. The supported profile is a signed 64-bit
nanosecond counter. Enabling this provider with another `time_t` representation
fails the build explicitly, rather than providing an always-unsupported native
implementation. A positive interval includes the reported clock resolution
from its start sample. Its maximum is the remaining INT64_MAX counter headroom
after that resolution adjustment. Positive preflight re-observes the bound
before starting the polling interval.
Counter conversion, padding and addition are checked before polling. The Linux
implementation calls no scheduler sleep/yield, allocator or mutex.

The provider's representational limit is not a recommended busy-wait duration
for a product. Long CPU busy-waits can reduce responsiveness even when the input
is supported. Product requirements should govern where busy-wait is appropriate
and set practical usage limits. The API adds no arbitrary global maximum.

Zephyr uses native `k_busy_wait()` with a conservative finite half-counter
profile. For fixed frequency `h` in Hz:

```text
maximum_us = min(UINT32_MAX, floor(INT32_MAX * 1000000 / h))
maximum_duration_ns = maximum_us * 1000
```

This bounds the native ceiling conversion into 32-bit cycles and leaves a wide
counter observation window before wrap. It is the provider's supported input
limit, not every target's theoretical maximum or an execution-latency bound.
For the selected EVKB 600 MHz configuration, the input limit is 3,579,139 us.
Passing 7,158,279 us directly to the pinned native conversion would wrap to
104 cycles; the Crux preflight rejects it.

Stable single-core generic counters and the known `native_sim` custom model are
supported by this profile. No system timer, zero frequency, runtime frequency
selection/update, SMP and unknown custom busy-wait implementations reject an
explicitly enabled build configuration until validated. Defensive direct-provider
calls in unsupported profiles and a zero runtime frequency return NOT_SUPPORTED. The native simulator models CPU waiting using a
simulated timer and CPU halt, allowing interrupts; it does not measure physical
CPU consumption. Its ordinary ISR and IRQ-delivery tests validate that model.
EVKB cross-compilation does not qualify physical timing, IRQ delivery or ISR use.

## Build ownership and verification

External Linux applications select `CRUX_TIME_BUSY_WAIT=ON`, `CRUX_BUILD_TIME=ON`
and an explicit Time provider (`memory` for controlled development or `linux`).
Link `crux::time`; it supplies typed declarations, checked duration constructors
and the always-linkable Busy-wait entry point. Unused realtime implementation code is excluded
from the tested Linux consumer ELF. No new types/provider framework is introduced.
The Host development top-level build defaults Busy-wait ON; external applications
and Zephyr default OFF. Zephyr explicitly selects `CONFIG_CRUX_TIME=y` and
`CONFIG_CRUX_TIME_BUSY_WAIT=y`; RTC and Thread/Sync are not required.

| Obsolete selection | Current selection |
| --- | --- |
| `CRUX_BUILD_DELAY=ON` | `CRUX_BUILD_TIME=ON`, explicit Time provider, `CRUX_TIME_BUSY_WAIT=ON` |
| `CONFIG_CRUX_DELAY=y` | `CONFIG_CRUX_TIME=y`, `CONFIG_CRUX_TIME_BUSY_WAIT=y` |
| `crux::delay`, `<crux/delay.h>` | `crux::time`, `<crux/time.h>` |
| `crux_delay_busy_wait(duration)` | `crux_busy_wait(duration)` |
| `crux_delay_get_max_duration()` | Removed; provider validates each positive request internally |

Old Host flags fail even when OFF; old Zephyr symbols fail Kconfig validation.
For an existing Host cache, intentionally remove the obsolete cache entry with
`cmake --preset host-debug -UCRUX_BUILD_DELAY` and select the new option. There is
no silent compatibility alias or enabled-to-disabled fallback.

Host CTest includes controlled common/native failures, kernel-profile conversion
shims and actual Linux monotonic integration. Shims validate conversions and
configuration rejection; they do not qualify a native ISR. The native Ztest
fixture verifies upward microsecond rounding, normal ISR entry and a timer IRQ
executing before the thread wait deadline. C17/C++17 external consumers check the
public build and type boundaries. Run `scripts/test-time-busy-wait.sh` in the documented
Linux environment for native_sim and an EVKB B/QSPI cross-build. It performs no
physical programming, reset or target execution.
