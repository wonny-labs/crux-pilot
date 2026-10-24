# Serial software qualification — Issue #27

Status: software implementation and independent source reviews PASS. Hosted CI
for implementation revision `ae10819f6f48893ca7e4fe7b98b2d66258875653` completed
with all four runs successful:
[Host PR](https://github.com/wonny-labs/crux-pilot/actions/runs/40107827224),
[Host Push](https://github.com/wonny-labs/crux-pilot/actions/runs/40107783812),
[Zephyr PR](https://github.com/wonny-labs/crux-pilot/actions/runs/40107827245), and
[Zephyr Push](https://github.com/wonny-labs/crux-pilot/actions/runs/40107783798).
Subsequent revision status is tracked by the checks on [PR #58](https://github.com/wonny-labs/crux-pilot/pull/58).
No physical UART test has been performed; Issue #27 remains open for its remaining
acceptance conditions.

## Acceptance map

| Requirement | Implementation / evidence |
| --- | --- |
| Public lifecycle/configuration/ownership/timeout/partial/error contract | Four functions in `crux/serial.h`; common lifecycle/configuration; Base-only public usage |
| Reference application uses native-free contract | Public-only `samples/serial/controller.c`; finite binary transaction, short I/O loops and caller nonce |
| Separate native-free runtime consumer | Separately authored C17 runtime and C++17 wrapper; caller nonce, one application deadline, cleanup status preserved |
| Logical Zephyr binding and RT1060 UART candidate path | Nonblocking raw Linux TTY; bounded Zephyr UART IRQ rings and permanent gated callback context |
| Host invalid/short/timeout/unready/recovery; resource conflicts and excluded modes | Host PTYs + focused common tests; UART emulator + native mapping, error/overflow/backpressure, stale callback and logging-owner negative |

The runtime and controller require a matching response before reporting success;
queued TX alone does not qualify. Consumer policy doubles are distinct from real
provider integration evidence. C17/C++17 portable objects receive only public
usage; native bootstrap and peer fixtures are separate. Source-policy regression
cases reject injected Zephyr headers in the new designated portable roots.

## Reproduction

Use the documented Linux Dev Container and pinned Zephyr workspace, without a
board/probe. `scripts/test-serial.sh --host-only` builds/runs C17, C++17, controller,
stale-response rejection and transaction-policy cases. `scripts/test-serial.sh`
also runs native_sim suites and the EVKB cross-build. Existing Host/sanitizer,
build safeguards, dependency and format gates remain enabled; hosted CI runs all
applicable integration checks on the final revision.

Development used read-only sources and temporary container output directories.
Affected evidence and independent review results are recorded under
`docs/evidence/m5-serial/`. A cross-build is compilation/resource evidence only,
not physical functionality, scheduling or electrical qualification.

## Known unresolved observations

An intermediate full Host run timed out at process exit in Queue.mpmc and
Client.FailedClockCannotLookLikeSuccessEvenWithoutErrno after their success
output. A single bounded affected/failed-case recheck passed. Root cause remains
unresolved: neither later passes nor Serial changes establish a fix or causal
relationship. Keep the original failure evidence for recurrence investigation.

During development overlapping invocations of the same temporary Zephyr build
were stopped; that output directory is excluded from passing evidence. A fresh
single build directory was used. An initial build-safeguard invocation outside
Linux was inappropriate for Linux-only gates; supported-container results are
reported separately.

## Minimum physical proposal — separate approval required

1. Identify the actual EVKB, probe, power and connector/electrical levels; verify
   AD_B1_06 TX / AD_B1_07 RX candidate mapping against board documentation and the
   peer, with CSI disabled and LPUART1 diagnostics retained. Do not infer physical
   connector availability solely from DTS pinmux.
2. Review a task-specific safe Flash/program/reset procedure before any access.
   Use a bounded external peer at 115200 8N1, no flow control, fresh transaction
   identity per exchange; capture binary request/ack and partial transfer handling.
3. Check silent-peer timeout, stop/join/close resource cleanup, reconnect using a
   fresh identity and rejection of a delayed previous response. Record native
   error-detection limits instead of asserting disconnect detection.
4. Measure actual RAM/stack and transaction timing under the reference workload;
   preserve UART diagnostic coexistence. Cross-build static counts are not board
   resource/timing measurements or production capacity guarantees.

No physical steps above are authorised by this software implementation task.
Issue #27 stays open until its remaining acceptance conditions are explicitly
resolved; no automatic closing keyword is used for the Draft PR.

## Recorded local results

| Check | Observed result |
| --- | --- |
| Common + actual Linux PTY Serial | 13/13 PASS (also ASan/UBSan) |
| Independent Host C17/C++17/controller + prior-response/policy | 5/5 PASS (also ASan/UBSan) |
| Actual Zephyr UART emulator | 4/4 PASS |
| Native_sim public consumer/controller | 2/2 PASS |
| Separate chosen logging UART owner | 1/1 PASS after reproduced RED |
| EVKB CSI conflict | Expected compile rejection at Serial binding guard |
| EVKB cross-build | PASS; full fixture Flash 42,964B, RAM 8,064B confirmed after final quality-only helper adjustment |
| Source dependency boundary | 58 portable files PASS |
| Build safeguard development run | 302 methods attempted; one method's four subcases blocked by read-only source mount; no other error |
| Writable-copy affected boundary recheck | Dependency test 1/1 and native-source policy 16/16 PASS |

The supported build-safeguard run could not write its deliberate native-leak
probe into the read-only checkout. A disposable writable source copy reran that
method and the affected native-source policy suite successfully. This is not a
claim that the initial full run passed. The hosted build-safeguard gate subsequently
passed all 303 tests in the implementation revision recorded above.

The measured Serial-owned EVKB static objects total 632B: 512B payload rings
within 572B permanent native context, 8B common slots, 20B metadata mutex, and
32B native semaphore objects. This excludes UART/kernel infrastructure and stack
usage; no Serial worker stack is created. Firmware totals include the controller,
consumer, UART, kernel and diagnostics and are not an incremental Serial cost.
