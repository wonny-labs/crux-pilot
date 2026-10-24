# Portable Logging validation

This records software evidence for Issue #29. The final revision and hosted Host
and Zephyr check results are recorded on the Draft PR. Physical acceptance remains
separate; no programming, reset, UART access or hardware measurement was performed.

## Software coverage

The baseline is merged `main` `6a58145` (Serial PR #58); the approved contract was
recorded at `ed6f3bd`. See the [contract](logging-contract.md),
[design](superpowers/specs/2026-10-24-logging-design.md),
[reproduction/evidence](evidence/m5-logging/README.md) and
[independent reviews](evidence/m5-logging/reviews.md).

| Issue #29 criterion | Executed software evidence and limits |
| --- | --- |
| Independently consumable contract | Two public C17 functions, Base-only usage target, actual C17/C++17 consumers; no native declarations in exported headers |
| Severity/filter/format/lifetime/drop/errors | Focused Host 9/9; ASan/UBSan 9/9; actual native_sim 5/5, including mutable caller strings, maximum 256-byte body, all native severity labels, drop-new/aggregate notice and failed Host output |
| Observable controller/backend records | Actual Host capture plus real native UART emulator observes the portable Controller and independent runtime records; EVKB image cross-built, physical output unverified |
| Composition without firmware-source changes | Private Linux/Zephyr providers selected by CMake/Kconfig; explicit single chosen log UART; native TEXT backend/worker reused |
| Reject missing/unsupported/conflicting selections | Actual resolved-configuration/build cases cover bindings, UART modes, exclusive owners, provider selection, hidden backends/filtering, missing worker, suppressed severity/boundaries and board revision; intended diagnostics required |
| Serial coordination | Same-UART Serial/Logging is rejected; EVKB cross-build selects Logging LPUART1 and independently enabled Serial LPUART3. No public Serial dependency or sharing profile added |
| Resource/concurrency/context limits | Native 3×3 complete record count and per-producer order, ISR rejection, Host serialized records and IO cases; documented blocking/best-effort limits and static observations below |

Independent public consumers passed actual Host capture and policy checks 2/2.
The native-interface regression suite passed 17/17 and public usage-boundary suite
23/23 in the supported Linux environment. Dependency inspection covers all 66
current designated portable sources. Owned-code formatting and focused Clang 18
analysis passed. Full applicable Host/sanitizer/Zephyr gates run in hosted CI;
consult the exact final HEAD outcomes on the PR rather than extrapolating local
focused counts to the full repository.

## Configuration and corrections

Pinned Zephyr 4.4.0 requires UART_CONSOLE for its native UART backend. The user
approved a narrow revision-B EVKB exception: compile that prerequisite, disable
direct Console stdout/input/subsystem helpers, early Console and Shell, and route
printk through native Logging. Guards reject other shared Console configurations.
Native_sim keeps its harness Console on a separate UART with LOG_PRINTK=n.

The supported profile requires native severity labels and record delimiters and
rejects custom LOG macro overrides. An independent review found that the earlier
guard admitted suppressed labels/boundaries and board revisions outside B. The
three unsupported configurations were shown to build in RED; corrected guards
reject them with intended diagnostics. The existing finite concurrency probe now
counts all nine completed records and checks each producer's sequence. It does
not impose a total order across concurrent tasks.

Other retained RED/GREEN evidence includes missing public/native capability,
parity and automatic Shell logging-backend activation. ARM GCC 14 rejected a
redundant enum lower-bound comparison under Werror; unsigned range comparison
preserves invalid-input rejection and passed affected Host/native/target builds.
The initial Host compiler rejected deprecated ATOMIC_VAR_INIT; standard C17
static scalar atomic initialization is used. A harness rewind was replaced with
checked fseek after analysis. No broad warning-policy relaxation was introduced.

## Static resource observations

The combined Logging/Serial `mimxrt1060_evk@B//qspi` image cross-built using the
pinned Zephyr SDK 1.0.1/ARM GCC 14.3.0. The linker reported Flash 44,148 B and RAM 13,952 B.
These are whole-image totals, not Logging-only deltas or runtime measurements.
The selected native log buffer is 4096 B (`buf32`); the logging stack is configured
2048 B and its target allocation is 2112 B including target guard/alignment. The
Crux threshold occupies 4 B. Native worker priority 14 and 115200 8N1 UART settings
are initial fixture choices, not production capacity or timing guarantees.
There is no additional Crux queue, worker or dynamic backend registry.

## Limits and remaining acceptance

OK acknowledges valid handling/native attempt, never queue admission, wire output
or delivery. Native losses may follow OK and only aggregate reporting is available.
Host synchronous locking/I/O can block; Zephyr does not wait for native capacity,
but copying/critical sections/scheduling still take time. Neither provider has a
whole-call worst-case latency guarantee. Task context only; no public panic,
flush/drain, timestamps, per-tag registry, File/Network logging or persistence.

Earlier local process-exit delays remain unresolved. Subsequent successful tests
neither establish their cause nor prove them fixed. Issue #29 remains open;
software review/CI does not authorize physical acceptance or merge.

## Minimal physical proposal (not executed)

A separate approval should select the exact revision-B board, probe, safe image
procedure and LPUART1 capture connection. Retain the qualified Console routing and
separate Serial LPUART3. Confirm healthy/error/sequence records physically, check
surviving-record integrity under a small finite burst and observe any aggregate
loss notice. Record the image revision, UART format, native stack/RAM observations
and representative timing without claiming a whole-call latency bound. Backend,
board revision, ownership or sizing changes need affected validation before use.
