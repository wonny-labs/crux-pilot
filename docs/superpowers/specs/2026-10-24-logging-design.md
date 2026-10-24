# Portable Logging — approved contract and TDD outline

Status: **approved for implementation** by the user on 24 October 2026, at
proposal commit `ed6f3bd`. Approval includes the completed-text contract and
TDD outline; hardware, merge and issue closure remain excluded.

## Intent, baseline and scope

Issue #29 needs one small, independently consumable diagnostic capability for a
reference controller and a separate runtime-shaped consumer. Follow the merged
M5 Pilot plan, current native-free boundary and component ownership rules.

Starting `main`: `6a5814501197b995b96e2e2661ad3730270247b9`, the squash merge
of PR #58. Serial's approved public contract and static resource checks are part
of this baseline; Issue #27 remains open. Zephyr is pinned at
`684c9e8f32e4373a21098559f748f06915f950c9` (4.4.0).

Keep existing M3 compositions and their evidence intact. Existing native output
uses Zephyr deferred logging, UART output, a 2048-byte native logger stack and
nonblocking drop-new buffering. The integrated M3 profile reserves 16 KiB; its
older diagnostic fixture uses 4 KiB. Neither establishes this new capability's
physical qualification or production sizing.

Do not add a second logging framework, Crux queue/worker, backend registry,
instances, dynamic backend switching, file/network persistence, panic/fatal
logging, hexdump, public flush/drain, per-module filter registry or timestamps.
The native logging worker and buffer remain owned by Zephyr. No dependency on
`crux::serial`, `crux::thread`, `crux::sync` or `crux::queue` is needed.

## Alternatives and selected direction

1. Expose Zephyr `LOG_*` plus a Host compatibility header: reuses its features,
   but puts native macros/configuration in portable application sources and fails
   the required independently consumable native-free contract. Rejected.
2. Small Crux completed-text interface with native providers: common validation
   and level filtering, native Host output and actual Zephyr logging backend.
   Narrow format/lifetime semantics and no second formatting engine. **Selected
   and approved by the user.**
3. Crux printf-style submission: convenient, but introduces format/variadic and
   truncation/error portability into this first capability. Deferred; applications
   can use suitable ISO C formatting and check their own formatting results.

## Proposed public surface

Owned by `components/logging/include/crux/logging.h`, C17 with C++17 C linkage;
public target `crux::logging`, requiring only `crux::base`.

```c
typedef enum {
    CRUX_LOG_OFF = 0,
    CRUX_LOG_ERROR = 1,
    CRUX_LOG_WARN = 2,
    CRUX_LOG_INFO = 3,
    CRUX_LOG_DEBUG = 4
} crux_log_level_t;

crux_status_t crux_log_set_level(crux_log_level_t maximum_level);
crux_status_t crux_log_write(crux_log_level_t level,
                            const char *tag, const char *text);
```

Only these two functions are proposed. No public native types, configuration
objects, handle creation/destruction, getter, delivery receipt or loss-counter
API. Use component-owned public constants for the initial 24-byte tag and
256-byte text limits, excluding the terminating NUL.

### Severity, filtering and lifecycle

Default threshold INFO. Emit ERROR/WARN/INFO at that threshold; DEBUG is filtered.
The threshold permits non-OFF record levels numerically <= its value; OFF
suppresses all Crux records. OFF is valid for the setter and invalid as a record
severity. Invalid enum values return INVALID_ARGUMENT. Invalid setters preserve
the previous threshold.

The threshold is process/boot-local and thread-safe. Each submission samples it
once after validation; a concurrent change may affect either side of an
overlapping call. Already submitted native records are not cancelled or
retroactively filtered. It controls Crux submissions, not other Zephyr modules
or redirected printk. No per-tag filtering or persistent settings.

No public init/shutdown call: final native composition owns static startup and
backend readiness. Calls are supported only in normal task context after runtime
startup. ISR/pre-kernel contexts return NOT_SUPPORTED; no crash/panic/interrupt
logging guarantee. There is no consumer-controlled native backend activation.

### Text, formatting and ownership

Both strings are nonnull NUL-terminated readable strings, valid and unchanged
until return. Tags contain 1..24 ASCII letters/digits, underscore, hyphen or dot.
Text contains 0..256 bytes and no CR/LF; it is submitted literally, including `%`.
No encoding conversion or UTF-8 validity guarantee is made. Callers omit the
newline; the selected provider renders one record boundary. Other control-byte
interpretation is caller policy, not a terminal-sanitisation feature.

Null pointers, malformed tags, CR/LF or invalid severity return INVALID_ARGUMENT;
overlong tag/text returns OUT_OF_RANGE. Validate these even when filtered, with
bounded string scans. C cannot validate arbitrary pointers: callers must provide
readable objects through their terminator or through the checked size limit.
No oversized record is silently truncated or submitted as a valid prefix.

After return the caller can modify/free both strings, for every result. Host
uses them only during the call. Zephyr uses fixed native format strings and
copies writable string data into the native deferred record before returning;
the selected native packaging configuration must preserve that behaviour, proven
by delayed processing with mutated stack buffers. A filtered/dropped call retains
no caller storage.

Metadata includes the actual severity and tag plus text. Host and Zephyr may
render different native prefixes/line endings; exact byte-identical rendering,
a portable timestamp and wire protocol are not part of this contract. Tests must
check content, level and record boundaries on each selected provider.

### Return values, overflow and delivery

**CRUX_OK means a valid call was handled: filtered, or forwarded as a native
logging attempt. It does not confirm queue acceptance, output completion or
peer receipt.** Zephyr's standard `LOG_*` macros return no per-record admission
result; native capacity loss may therefore follow a CRUX_OK submission. This
weaker diagnostic success meaning is intentional and was approved by the user.

Selected Zephyr policy: deferred native logging, no capacity wait, drop the new
record when full (`LOG_BLOCK_IN_THREAD=n`, `LOG_MODE_OVERFLOW=n`). The native UART
backend's aggregate dropped-message report is retained and verified. It is a
native system-wide loss report, not an exact per-Crux-source/per-call counter.
Do not steal/reset native global drop counters or introduce a new observer backend
in production just to manufacture per-record acknowledgement.

Report known pre-submission backend unready state as NOT_READY and known Host
output failure as IO. Native asynchronous UART/backend failures may not be
reported to the submitter. The existing UART backend can report bytes consumed
without an error result; do not promote that into a delivery guarantee. Native
startup failures remain platform-initialisation failures, not promised recoverable
Logging lifecycle errors. No automatic retries: an IO result may follow a partial
Host output, so retry can duplicate a prefix.

### Concurrency and blocking

Normal tasks may submit concurrently using separate caller buffers. One task's
sequential nonoverlapping submissions preserve submission order among surviving
records. There is no total ordering promise for overlapping calls or for other
native output sources. Selected providers serialize complete Crux records so
concurrent Crux calls do not interleave record bodies. Other raw/unregistered
writers to the same resource violate the declared composition boundary.

Host output is synchronous and may block on native stream serialization/I/O.
Zephyr submission does not wait for native buffer capacity or UART transmission;
formatting/copying, short native critical sections and scheduling still take time.
Neither provider offers a whole-call/WCET bound. No new timeout parameter, Crux
worker or reliability protocol is introduced.

## Providers and static composition

Common `components/logging/src/` owns bounded validation and atomic threshold.
Private `ports/linux/` renders to stderr using suitable native stream facilities;
private `ports/zephyr/` maps severities to the standard Zephyr `LOG_*` API and its
existing UART backend. No `z_log_*` private entry point or maintained Zephyr patch.
Native linkage/headers stay private; native-free consumer guards remain enabled.

Provider selection follows the current component pattern: Host/Zephyr are the
only respective supported selections; empty/unknown/multiple selections fail
configuration. Native backend selection uses existing Zephyr configuration, not a
new registry or runtime API. Initial Zephyr profile:

- LOG enabled, deferred mode, native process thread and UART text backend;
  autostart, polling UART output, no frontend, immediate/minimal/async mode.
- Native maximum level DEBUG and an explicit DEBUG-capable Crux source; native
  runtime filtering disabled so it cannot silently override the Crux threshold.
- All other pinned native logging backends, including native_posix/RTT/file/network,
  disabled. Reject additional/unsupported selections at build/configuration time.
- Explicit `zephyr,log-uart` chosen list containing exactly one enabled UART;
  no implicit Console fallback in the initial Crux profile. Reject missing,
  disabled or unsupported bindings.
- Native_sim uses a dedicated UART emulator; EVKB candidate uses LPUART1 at
  115200 8N1, with Serial remaining on its existing LPUART3 binding if enabled.
- Reject an enabled Serial/Console/Shell/management/Bluetooth/other known child
  owner claiming that UART. Reject unsupported electrical modes. No Serial/
  Logging sharing, no new project-wide resource model; arbitrary direct native
  users remain subject to the declared composition rule and review.

On 24 October 2026 the user approved a narrow EVKB composition amendment:
the pinned native UART backend requires `UART_CONSOLE=y`, so Console support is
compiled as its prerequisite. On shared LPUART1, direct Console stdout/input,
Console subsystem helpers, early Console and Shell are disabled; printk is
routed through native logging (`LOG_PRINTK=y`). Build checks enforce this
exception only for the qualified revision-B EVKB LPUART1 profile. Native_sim keeps separate
UARTs and does not gain a shared Console exception. No upstream patch or extra
UART owner is introduced.
This does not rewrite or requalify existing M3 Console/Shell images. Native_sim
keeps its test harness Console on a different UART with `LOG_PRINTK=n`, so native
logging capture cannot swallow Twister/ztest control output. The actual native configuration must be inspected,
not merely the requested flags. Missing dependencies must fail the enabled
capability's configuration, not quietly disable the component with a warning.

Initial qualification configuration: native log buffer 4096 bytes, native worker
stack 2048 bytes and priority 14, based on existing diagnostic patterns. These
are initial test settings, not permanent platform/production capacity guarantees.
Measure actual linked static resource use; physical stack/timing remains separate.

## Concise proposed TDD implementation plan

After approval, use writing-plans for the executable file/commit checklist,
then implement inline with one separately authored portable consumer. This
outline is part of the present human review; no product code exists yet.

1. Focused Host RED: severity/default/change/OFF filtering, bounded literal text
   including `%`, invalid inputs and exact limits, output representation, mutable
   string lifetime, concurrent record integrity, controlled drop and IO failure.
   Implement common policy and private stderr provider; run affected sanitizer,
   format/static analysis and native-free checks. No Crux queue simulation stack.
2. Zephyr RED using actual UART-emulator capture through the native UART logging
   backend: severity/filter output, delayed writable-string mutation, bounded burst
   with actual native drop reporting, and representative concurrent records.
   Implement only the native port and its private composition.
3. Actual negative configuration/build cases for missing/disabled binding,
   unsupported provider/backend/resource/mode, Serial collision and other enabled
   native owner collisions; retain one successful dedicated-resource control.
   Reuse the existing Python/CMake/Zephyr runner style and intended-diagnostic checks.
4. Small public-only controller example and independently authored C17/C++17
   runtime-shaped consumer. Application source stays unchanged when native
   composition selects providers. Add actual consumer, source/usage boundary
   checks, native_sim integration and one supported EVKB cross-build.
5. Independently review actual implementation/evidence with Reviewer and
   Challenger; correct material findings and rerun affected cases only. Then
   full applicable Host Debug/ASan-UBSan/Zephyr CI on the final PR HEAD.
6. Draft PR referencing #29, adopted contract, criterion map, actual results,
   native selection/resource observations, unsupported features, reviews and a
   minimum separately approved physical scenario. Keep #29 open and do not merge.

Reuse the merged main/PR #58 software baseline rather than replaying unrelated
full suites during design. Preserve real failures and unresolved process-exit
observations. Cross-build is not physical UART or stack/timing acceptance.

## Design check

A fresh independent builder checked the written proposal and constraints and
returned **nothing important left** to resolve before
planning. This checks design completeness, not implementation correctness or
human API approval. The author additionally made the explicit native_sim/EVKB
printk routing distinction above to preserve test-harness observability.

## Human review and remaining boundaries

The user approved the two-function completed-text contract, global filter,
best-effort success/loss semantics, task-only/context/blocking limits and the
initial dedicated native-UART profile plus TDD outline. Implementation and software
qualification may proceed within that scope. Hardware operation, PR merge and Issue #29 closure remain unauthorised.
