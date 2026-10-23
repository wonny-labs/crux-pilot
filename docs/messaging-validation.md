# Issue #26 software qualification

Status: initial 8ef39d8 qualification is retained below; the timeout correction
and its superseding results are recorded at the end. Current final-HEAD hosted
CI is recorded in the Draft PR. Approved baseline
main 225cd21b5fff37c31c5dff52570e9f26179682a1.
The contract is docs/messaging-contract.md. No hardware operation is authorised
or performed. No PR merge or issue closure is authorised by this record.

## Acceptance traceability

| Issue #26 criterion | Implementation | Verification/evidence |
| --- | --- | --- |
| Monotonic vs UTC, units/range/ownership/timeouts | monotonic public/common/provider code; queue public/common code | Monotonic.Common/NativeBoundaries/RealHost; Queue.Basic/limits/Faults; tests/monotonic/zephyr |
| Host event/queue/time success, timeout, full, cooperative cancellation | existing Execution semaphore plus queue and monotonic | Queue.Concurrency/mpmc; independently authored tests/messaging_consumer runtime/controller, repeated sessions, empty/full/continuous-traffic stop |
| Zephyr wake/order/concurrency and private native types | queue private condvar provider, monotonic native tick provider | tests/queue/zephyr, tests/monotonic/zephyr and independent native consumer; 7 Queue + 3 Monotonic + 1 consumer cases |
| Native-free reference scenario and resource limits | samples/messaging/controller.c, guarded C17/C++17 runtime consumer | scripts/test-messaging.sh; matched-content scripts/measure-messaging.sh; EVKB cross-builds successful; static accounting below |
| Explicit unsupported features | approved default contract | No event-bit/callback timer/atomic abort/priority/zero-copy/ISR/forever API; no hard scheduling/fairness or physical timing qualification |

## Reproducible commands

Use the documented Linux container and external west workspace. Run sequential
native validation to limit shared-emulator contention. No command below programs
or accesses a board.

- `scripts/check-host.sh`
- `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh`
- `scripts/test-messaging.sh --host-only` and `scripts/test-messaging.sh`
- `scripts/measure-messaging.sh`
- `scripts/test-portable-execution.sh`, `scripts/test-networking.sh`
- `scripts/check-dependencies.sh`, `scripts/check-format.sh`, `scripts/analyze-host.sh`
- `python3 -m unittest discover -s tests/build -v`

Initial 8ef39d8 source identity is retained in `evidence/m5-messaging/source-sha256.txt`;
all 64 implementation/test/integration inputs were independently hash-checked.
Logs were captured before the source snapshot was committed. Local results are
not final-HEAD CI; the Draft PR records hosted results separately.

## Implementation choices and limitations

The queue uses a fixed ring and one provider metadata mutex protecting all four
slots, with per-slot native condition variables. Operation admission increments a
reference before releasing the mutex to wait; destroy refuses active operations.
No metadata mutex/condition variable storage is reclaimed while admissions may
reference it. Poll tries metadata once and completes copy/bookkeeping before
unlock. Native waits retain the original deadline. This small common predicate
implementation avoids direct-handoff provider differences and duplicate lifecycle
logic, but unrelated queues contend on the same mutex. It promises functional
semantics, not fair scheduling or bounded wall-clock return latency.

Public facades hide provider-specific types and native headers. New Host providers
are selected in executable composition. Integration exposed existing Execution
usage-graph leakage: the empty Threads imported target propagated on supported
Ubuntu/glibc, and Zephyr redundantly exposed its already-registered provider. The
correction retains actual threading link requirements where needed and retains
independent Zephyr provider registration; public headers/semantics are unchanged.
The new independently guarded combined consumer is a regression gate for these
seams; source/build checkers are representative enforcement, not a full C parser.

## Observed failures and corrections

- Queue missing implementation baseline failed create assertion; Host FIFO/copy
  fixture passed after implementation. Monotonic has separate common, Host-native
  and Zephyr RED/GREEN logs retained under build/monotonic-agent.
- Combined Zephyr consumer initially failed link because new Queue metadata mutex
  shared Execution's `metadata` symbol. New Queue symbol was namespaced; the same
  consumer then linked. Runtime verification is separately required.
- First full sanitizer run: ControllerHealth.RequiresCompletedWorkAndExpiresOnActualCompletionTime
  printed all assertions passing but process-exit timed out at ten seconds. Raw
  LastTest log retained; five unchanged targeted reruns passed. No root cause or
  causal connection to new services has been established. Do not describe it as
  resolved. An unchanged standard full-suite rerun subsequently passed 86/86; this does not establish that the intermittent exit issue is resolved.

## Physical qualification

Static ELF/map sizes, configured worker stacks and native_sim behaviour are not
physical stack high-water, resource margin, scheduling latency or oscillator/
power-state guarantees. Physical EVKB work remains a later separately authorised
activity; Issue #26 must remain open until its acceptance is actually met.

Physical measurement plan, requiring later explicit hardware approval: retain the
consumer's configured 4096-byte worker stacks with INIT_STACKS/THREAD_STACK_INFO;
exercise representative and worst-case full/empty/continuous-traffic stop and
repeated sessions on EVKB. Before worker teardown, collect each thread's unused
stack/high-water, report configured stack, observed peak and remaining margin,
board/toolchain/configuration and traffic/load. Measure timing separately under
recorded load. No stack margin or timing claim is made before those measurements.

## Implementation-review corrections

Independent Reviewer found that pinned Zephyr condition-variable timeout returns
without reacquiring its mutex. The Queue port now reacquires the persistent
metadata mutex before any shared state bookkeeping. The admitted reference remains
active during reacquisition; the documented final-bookkeeping return delay applies.
A second finding rejected an absolute tick deadline equal to INT64_MAX rather
than letting K_TIMEOUT_ABS_TICKS clamp it. Separate deterministic native-contract
model tests failed on both pre-fix behaviours and passed after correction. Actual
native timeout-ownership, admitted-cleanup interleaving and 2P/2C tests supplement
the model and passed all seven actual native Queue cases. Native wrapper setup itself initially
failed because PRIVATE link options on the static app did not reach the final
link; it now uses the upstream PUBLIC target-link-options pattern. Neither a
model test nor a successful build alone is treated as actual kernel proof.

## Focused implementation review

Reviewer initially returned REVISE for timeout mutex ownership and absolute tick
clamping. Both were corrected with separate failing/passing native-contract
regressions. Actual pinned-kernel native_sim now passes seven tests, including
mutex ownership after timeout, admitted-cleanup versus destroy/transfer and
2P/2C copied-record integrity. Reviewer independently reran both private native
model modes and returned PASS for source corrections and focused regressions.

Challenger independently inspected native semantics and the fixes, accepted the
common-ring/native-synchronisation design and documented global-lock contention,
then returned PASS for corrected source/architecture. Both assessments reserved whole qualification until final canonical checks,
sanitizer disposition, resource accounting and hosted CI. Local gates are now
complete as recorded below; hosted CI remains a separate exact-HEAD gate. Initial Queue RED evidence establishes the
missing implementation fixture, not requirement-by-requirement TDD; richer
concurrency/fault tests were added as regression coverage. Monotonic and the two
review corrections retain separate behavioural RED/GREEN evidence.

A second sanitizer run failed Monotonic.RealHost on process exit after printing
the completed 20 ms wait result. Five unchanged targeted repeats passed. A
separate diagnostic ran a minimal sanitized C++ program ten times and the real
Monotonic binary ten times without reproducing the hang. These observations do
not establish a root cause or resolution; preserve both full failing LastTest
logs alongside subsequent complete runs. No timeout or sanitizer policy was
relaxed to hide either observation.

## EVKB standard-library sysroot integration

The first guarded EVKB consumer configure rejected the SDK standard-library
`--sysroot` compile option solely because its path contained `zephyr`. An attempt
to remove a compiler-default duplicate was abandoned: the SDK compiler reports
no default sysroot, so that inference was not justified. No sysroot option is
removed by the final change. Reviewer and Challenger instead agreed a narrowly
classified standard toolchain option: one complete compile-option token matching
the selected absolute, normalised `SYSROOT_DIR`. Positive and negative regression
tests cover exact/normalised matches, different/child/missing/relative roots,
compound options and include/link property misuse. The original native-header
and arbitrary native dependency rules remain in force. This is a correction
within the existing CPU/ABI/sysroot contract, not new native API permission.

## Initial 8ef39d8 local verification and environment

Linux x86_64 Dev Container executed under Rosetta on an Apple host; Zephyr
684c9e8f32e4373a21098559f748f06915f950c9 (4.4.0), SDK 1.0.1, ARM GCC 14.3.0,
CMake 3.28.4 and west 1.4.0. EVKB target: `mimxrt1060_evk@B//qspi`.

- Host Debug: 86/86 tests plus sample stdin/stdout scenario passed.
- Standard Host ASan/UBSan command: 86/86 plus sample passed on an unchanged
  unassisted rerun (`sanitize-unassisted.txt`). Existing timeouts and sanitizer
  settings were preserved.
- A separate diagnostic CTest run added a 20-second default only for previously
  unbounded tests, without changing explicit timeouts: 86/86 passed. This is
  supplementary evidence, not a replacement for the standard command above.
- Another full-suite invocation stopped progressing on an existing Networking
  test. `/proc` showed its child as zombie with exit status 0 while CTest waited
  in `do_epoll_wait`. The CTest parent was manually terminated after recording
  this observation. This invocation is excluded from passing evidence. This
  observation narrows that occurrence to process collection; it does not prove
  the cause of earlier timeouts. Preserve the failures and diagnostic record.
- Execution regressions: both independent Host consumers, 15/15 native cases
  and both EVKB cross-builds passed. Networking independent/native Picolibc
  scenarios and EVKB cross-build passed. Relevant logs are retained separately.
- Canonical `scripts/test-messaging.sh`: Host independent consumers 2/2,
  native_sim 3/3 configurations and 11/11 cases, then guarded combined EVKB
  cross-build passed (command exit0).
- `scripts/measure-messaging.sh`: both matched profiles, six-symbol retention
  checks and complete resource evidence generation passed (exit0).
- Formatting, dependency checks (33 portable sources), configured static
  analysis and all 295 build safeguard tests passed. Static-analysis output
  retains upstream/non-user warning counts; no new owned-code suppression.
- Source hashes and normalized raw logs are retained under
  `evidence/m5-messaging/`; independent Reviewer/Challenger final source and
  evidence reviews PASS. Final-HEAD hosted CI and its run links are maintained
  in the Draft PR; this local document does not claim an unobserved CI pass.

## Initial 8ef39d8 static resource comparison

`scripts/measure-messaging.sh` builds the same no-op main with Queue/Monotonic
both disabled or enabled. Both profiles retain timeout64, 128 Hz ticks and a
4096-byte main stack; only the two capability configuration toggles differ.
The enabled composition deliberately retains all six public APIs, verified from
ELF symbols. Both `main` instruction sequences remain identical.

| Allocated linker region | Baseline | Enabled | Delta |
| --- | ---: | ---: | ---: |
| Flash bytes | 32,088 | 34,984 | +2,896 |
| RAM bytes | 7,360 | 11,648 | +4,288 |

Queue symbols account for 4,264 bytes: 4,096 payload, 112 slot metadata,
32 bytes for four condition variables, 4 bytes for preparation flags and
20 bytes for the native mutex. The
remaining 24 allocated RAM bytes are placement/alignment changes. Monotonic
adds no static storage. GNU section/symbol totals and linker region allocation
are distinct measures; do not equate their absolute totals.

This is retained callable-capability overhead in this composition, not measured
application overhead or on-board resource usage. The combined runtime EVKB
consumer cross-build occupies 39,496 Flash and 29,696 RAM bytes.
Generated configs, commands, toolchain/modules, input and ELF SHA256, size/nm/
objdump and ROM/RAM reports are reproducible under
`build/messaging-resource-evidence/` and uploaded by Zephyr CI. Selected text
artifacts are retained in `docs/evidence/m5-messaging/` with machine roots replaced.

## Human review of design conditions

The approved proposal already defines atomic lifecycle admission, BUSY destroy
with live calls, preserved failed handles/resources, retired exhausted generations
and discard of copied queued records at quiescent destruction. It requires exact
record size and buffer validity until return, while explicitly stating C cannot
validate an allocation's real extent. Monotonic defines int64 milliseconds,
past deadlines, overflow/errors, no early successful wait and unbounded scheduling
delay. Cooperative stop is checked between every operation even under continuous
traffic; a stop/admission race may still allow an operation to commit. Initial
4/16/64 limits remain qualification configuration, not permanent platform maxima.
No new contract or feature was required by the independent design feedback.

## Independent-review timeout correction (supersedes initial behaviour)

The external REVISE identified that initial `8ef39d8` accepted late availability
when native wait had already returned TIMEOUT. The old native cleanup test also
expected that incorrect success. This was broader than the expiry-race contract.

The common commit now requires CRUX_OK. A native TIMEOUT remains terminal while
the admitted reference is retained through locked cleanup. No provider/lifecycle
or public API redesign. Successful native wake/expiry races retain their existing
allowance. The contract now explicitly distinguishes these cases.

Both receive and send have separate deterministic Host and actual Zephyr RED
failures before the correction, then GREEN after it. Tests delay metadata
reacquisition, preserve BUSY destroy/handles, verify unchanged receive output,
no ghost send or late consumption, FIFO continuity and safe reuse. Host's
fixture-only post-wait unlock/relock is distinguished from Zephyr's actual
pre-provider-reacquisition pause in the evidence README.

Final local checks: Host Debug88/88 plus sample; standard ASan/UBSan88/88 plus
sample; messaging independent Host2/2; native3configurations12/12;
combined EVKB cross-build; format/dependencies and affected source/test analysis
all passed. Static remeasurement gives Flash+2,888 bytes (enabled34,976),
allocated RAM+4,288 (enabled11,648). Combined consumer Flash39,488/RAM29,696.
These values supersede the earlier source snapshot's Flash figures.

Logs, commands, toolchain/config/source identities and RED/GREEN outcomes are in
`evidence/m5-messaging/timeout-correction/`. Initial wrapper identifier analysis
findings were corrected without suppressions and full Host suites rerun.
Independent Reviewer/Challenger assessments and final-head hosted workflow links
are recorded in the PR. Earlier intermittent local process-exit delays remain
unresolved; successful later runs do not infer root cause or resolution.
