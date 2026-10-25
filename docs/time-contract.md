# Crux Time API contract

This document records the M2 contract implemented by the host reference composition. It follows
Sections 4–5 of the [initial implementation plan](plans/initial-implementation-plan.md). This is a
bounded Time API contract, not a general clock framework.

## Common status ownership

`<crux/status.h>` is owned by the independent `crux::base` CMake INTERFACE target. It has no Time,
Zephyr, operating-system, allocation or service-runtime dependency. The shared status values are:

| Status | Value | Meaning |
| --- | ---: | --- |
| `CRUX_OK` | 0 | Operation succeeded |
| `CRUX_ERR_INVALID_ARGUMENT` | -1 | Invalid pointer, malformed calendar or invalid field combination |
| `CRUX_ERR_OUT_OF_RANGE` | -2 | Public input or observed time outside the supported interval, or a provider-reported range limit |
| `CRUX_ERR_NOT_READY` | -3 | Time backend has not been successfully initialised |
| `CRUX_ERR_NO_DATA` | -4 | No valid time has been established for this session |
| `CRUX_ERR_IO` | -5 | Backend operation failed |

Time does not define a duplicate Time-specific status enum. Modules retain ownership of their
private state, data and recovery-relevant results; common codes are used only when their meaning
fits. No other service is introduced to demonstrate this rule.

## Public Time API

`<crux/time.h>` defines `crux_time_instant_t` as signed 64-bit UTC Unix seconds and declares
explicit initialise, get and set operations. `<crux/time_calendar.h>` defines a six-field UTC
calendar and conversion functions. These headers document the range, state, output, and call-context
contract at the declarations. The public API is C17 and its function declarations support C++ consumers;
C++ callers include these headers and link to the C17 implementation through the exported
`crux::time` usage target in both Host and Zephyr builds. Application consumers do not link
internal common/backend targets or add Time public include paths themselves. The Host entry point
selects the controlled host backend; the Zephyr module selects its RTC backend when
`CONFIG_CRUX_TIME=y`. This is a CMake usage target, not a runtime dispatch layer or an installed
package/ABI stability promise. Host GoogleTests exercise C++ calls against the C17 test composition;
they do not independently verify a C++ consumer link to the production usage target.

The supported interval is `[2000-01-01T00:00:00 UTC, 2100-01-01T00:00:00 UTC)`. It uses UTC, second
precision, Gregorian calendar rules and no leap seconds. It does not promise binary ABI stability
across compilers, concurrent access, ISR use, persistence across reset or a progressing host clock.

Calendar validation checks null pointers, field ranges and actual month length before checking the
supported year interval. Malformed combinations return `CRUX_ERR_INVALID_ARGUMENT`; valid dates
outside the interval return `CRUX_ERR_OUT_OF_RANGE`. Instant conversion similarly rejects values
outside the interval. Output arguments are assigned only on success.

## Header and build ownership

This component follows the [repository header/dependency rules](../CONTRIBUTING.md#public-headers-and-module-dependencies).

| Files / target | Ownership and visibility |
| --- | --- |
| `components/base/include/crux/status.h` | Independent platform public header, provided by `crux::base` |
| `components/time/include/crux/time.h`, `time_calendar.h` | Time public API, provided to applications through `crux::time` |
| `components/time/src/time_port.h` | Private link-selected backend contract; no application include requirement |
| `components/time/ports/zephyr/time_port_internal.h` | Private native adapter helper; available explicitly to relevant integration tests |
| `tests/time/*.h` | Test controls, excluded from public include roots and normal consumer requirements |
| `samples/time/include/crux_time_command.h` | Sample-owned helper contract, not an exported Crux platform API |

Host and Zephyr applications link `crux::time`; the base dependency follows the public Time
headers. Internal host/Zephyr build definitions assemble the common implementation and selected
backend. Their private port paths do not propagate to applications.

The Host `crux_time_commands` helper currently receives Time's public header path privately and
links `crux::base` for its exposed status type. It intentionally does not bind the production Time
backend: the host sample supplies `crux::time`, whereas host tests supply the instrumented Time
test composition. This is internal sample composition wiring, not a recipe for ordinary consumers.
Zephyr integration tests explicitly access private headers to exercise adapter mapping and test
controls; production applications do not acquire those paths.

## State and error precedence

The common implementation owns three session states:

1. **Not ready:** no successful backend initialisation has completed.
2. **Ready but unset:** the backend is ready, but this session has not established a time.
3. **Ready and valid:** a set operation succeeded and the backend has a session-valid value.

The first successful `crux_time_init()` checks the selected backend and enters ready-but-unset. It
never writes a default time. A failed initialisation leaves the API not ready and can be retried.
Repeated initialisation after success is a no-op that preserves validity.

Pointer and value validation precede readiness checks and backend calls. A valid get/set before
successful init returns `CRUX_ERR_NOT_READY`; an out-of-range set returns
`CRUX_ERR_OUT_OF_RANGE` even before init. A ready but unset get returns `CRUX_ERR_NO_DATA` without
reading the backend. A backend `CRUX_ERR_NO_DATA` clears common validity; other read failures leave
the caller output unchanged and do not fabricate a value. An out-of-range value read from the
backend returns `CRUX_ERR_OUT_OF_RANGE` and leaves output unchanged. The selected provider may also
return this status for a native range limitation before supplying a time, or after receiving an
in-range set request. A rejected input never calls the backend and preserves an existing valid
value. A failed backend write, including a provider-reported range error, invalidates session
validity because native storage may have changed despite the reported failure.

Applications serialise calls. No mutex or re-entrancy framework is provided.

## Private port and link selection

The private Time port provides only `init`, `read` and `write`, using Crux-owned statuses and
instant types. Common code contains no host or Zephyr headers. Each build composition selects one
backend. Host CMake marks the host provider with a compatible-interface property; the Zephyr module
marks its RTC provider the same way. CMake rejects a consumer that combines targets with conflicting
provider identities. The Zephyr module does not include the host backend, and the host root does not
load Zephyr. There is no runtime backend registry, weak-symbol discovery or public function table.

The host composition explicitly selects the separate `crux_time_memory` target from
`components/time/src/time_port_memory.c`. It is OS independent and is excluded from
the common source inventory. The private `crux_time_host` name remains an alias.
The Zephyr module delegates Time build ownership to `components/time/ports/zephyr/`
and registers only the RTC implementation; it has no memory fallback.

The memory backend is a controlled deterministic store: it starts with no time, never reads host wall
time and advances only through private test-only controls. Those controls live outside the exported
headers and are compiled into the test backend only. The host sample uses the non-instrumented memory
backend and exposes no advance/failure controls.

## Host command composition

The portable C17 command handler consumes `time set YYYY-MM-DDTHH:MM:SS`, `time get` and `help`.
It belongs to the sample layer, not the Zephyr module. The host process and Zephyr application each
compile the same command source; the Zephyr Shell adapter translates parsed arguments back to the
portable line grammar. It deliberately lets Shell pass argument counts through so malformed input
within the `time` group receives the Crux `ERR INVALID_ARGUMENT` response. Both compositions
initialize Time explicitly and do not set an implicit date. The host sample prints a
`CRUX_TIME_READY` marker and identifies itself as a controlled simulation; it does not follow wall
time. API no-data remains `CRUX_ERR_NO_DATA`; the command presentation maps it to `ERR NOT_SET`.
The readiness marker says `composition=host-simulation` or `composition=zephyr`; it is not a build
identity. A source/configuration ID and checksum manifest remain required for physical artefact
acceptance. Physical pytest associates readiness with the manifest-identified ELF through the
explicit download and section-comparison log before accepting UART cases.

The Zephyr composition selects the standard `rtc` devicetree alias and calls `rtc_get_time()` and
`rtc_set_time()` through a private backend. It converts `struct rtc_time` at that boundary and does
not expose Zephyr types in the public API or portable command handler. `crux_time_init()` checks
device readiness, and the application prints readiness only after initialization succeeds. The
upstream RTC Shell is disabled so it cannot bypass the shared command path. Native simulation
verifies API mapping and command behavior. EVKB download/debugging and retained physical M4 checks passed: UART pytest 5/5 and target
Ztest 2/2 cover progression, bounded set/read, representative upper-range/rejection cases,
repeated init and reset-session behaviour. Actual measurements, firmware identity and raw logs
are recorded in the baseline. These checks do not establish clock drift or persistent validity.
