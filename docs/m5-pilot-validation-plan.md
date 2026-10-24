# M5 Pilot validation and completion plan

**Status:** Proposed for human review. This plan does not approve a public API, hardware operation, PR merge or issue closure. Prepared against `main` `2b33fbc` after the Thread/Sync structural refactor. The current GitHub Issues #25–#30 and their existing acceptance criteria remain authoritative.

## Objective and completion boundary

Qualify the *selected* M5 portable contracts and representative RT1060-EVKB behaviour sufficiently to evaluate the Crux architecture. Demonstrate observable success, important failure semantics, resource ownership and the native-free consumer boundary. Do not claim production timing, sustained-load reliability, all error combinations or generic third-party runtime compatibility. A cross-build and `native_sim` pass are not physical evidence.

The obsolete `crux::execution` target and `<crux/execution.h>` have been replaced by independent `crux::thread` and `crux::sync`. All new consumer/build examples must use the active targets and headers. Preserve existing Queue, Monotonic, Time, Networking and native-free contracts. Do not use unrelated M4 writable storage work as a prerequisite for M5.

## Delivery sequence

1. **Issue #27 — Serial:** Review the concrete controller and independent-consumer needs and compare an appropriate standard interface with a minimal Crux-owned contract. Obtain the required human API/design decision before adoption. Implement the selected UART byte-stream behaviour, private Host/Zephyr mappings, logical-port binding and test consumers.
2. **Issue #29 — Logging:** Prefer one existing supported Zephyr logging backend; implement only the portable consumer boundary justified by the reference use case. Reuse existing console/diagnostic infrastructure where valid. Do not make Logging depend on the new Serial component unless a proven requirement demands it.
3. **Issue #30 — System Health and Reset:** Reuse qualified Zephyr reset/watchdog mechanisms. Scope the public contract to required readiness, reset cause and watchdog lifecycle/ownership. Keep application fault policy outside the platform.
4. **Physical qualification in parallel where approved:** Validate already implemented #25 Thread/Sync, #26 Queue/Monotonic and #28 Networking without waiting for all three new interfaces. Validate #27, #29 and #30 only once the corresponding software composition and physical procedure are approved.
5. **M5 closeout:** Integrate the final reference consumer, check native-free C17/C++17 usage, run final required CI and map actual evidence to each Issue's acceptance criteria. Leave any Issue open where an essential physical condition remains unverified.

Use one capability-sized implementation PR per issue where practical. Keep planning/design gates separate from adopted implementation. Independent Reviewer and Challenger review actual changes and evidence; their PASS does not replace human API, hardware, merge or closure approval.

## Test allocation — keep the fast, valuable checks

| Layer | Required for Pilot | Defer unless an actual failure or consumer requirement justifies it |
| --- | --- | --- |
| Host | TDD-based success, representative invalid/error/timeout, ownership/lifetime and boundary tests; applicable ASan/UBSan | Recreating broad native stacks or exhaustive combinations |
| Zephyr native_sim | Real native mapping, important timeout/concurrency/error semantics and a representative application path | Repeating all portable Host cases and artificial long-duration stress |
| Independent consumer | C17/C++17 public-only build/use and negative native-dependency guard | Complete third-party runtime port or ABI-parity claim |
| EVKB cross-build | Selected final composition, exact build/configuration identity and static resource budget | Multiple redundant binaries solely for evidence volume |
| Physical EVKB | Essential functionality, one representative failure/recovery where applicable, observed resource/timing limits | Sustained load, exhaustive fault injection, jitter/WCET/endurance certification |

During development, run the affected tests and relevant dependency/format checks. Run the full existing Host/Zephyr CI on the final PR HEAD and after material integration changes. Do not delete useful existing automated tests. Avoid new frameworks, repeated provenance machinery or elaborate manual orchestration when an existing runner provides trustworthy evidence.

## Minimum representative physical observations

| Issue | Pilot evidence on the identified EVKB |
| --- | --- |
| #25 Thread / Sync | One worker lifecycle and join, one mutex/semaphore handoff, finite timeout, and observed worker stack/resource use |
| #26 Queue / Monotonic | Copied FIFO send/receive, full/empty timeout, absolute monotonic deadline and cooperative stop/join. Record basic stack/timing observations |
| #28 Networking | Actual portable-profile TCP and UDP success, one silent-peer timeout, one early-close/error case, one controlled link-down/up with successful fresh-socket recovery, and start/end resource status |
| #27 Serial | Actual project-bound UART configuration, bounded send/receive, a representative timeout/error, and evidence of unambiguous port ownership |
| #29 Logging | One approved backend emits records, level/filter behaviour is observable, and output/drop policy and resource limits are recorded |
| #30 Health / Reset | Watchdog ownership/feed and reset-reason behaviour with one controlled, explicitly approved reset/watchdog scenario where supported |

These are **minimum scenario types**, not mandated repetitions or a promise that one single image can exercise them all. Preserve relevant actual failures and record limits. Capture a measured time and available stack/memory when useful, not unsupported real-time guarantees. If a scenario cannot be performed safely, retain an explicit blocker rather than silently treating software evidence as physical PASS.

## Networking reduction and existing evidence

`docs/networking-physical-preparation.md` proposes three boots and 48 TCP/UDP pairs (96 calls), detailed per-operation resource settling and numerous timing/transport checks. That plan and its software runner/evidence remain valuable **extended qualification material**, but its full repetition count should not be required for this Pilot. Before using the EVKB, derive and independently review one bounded pilot session covering the #28 table above, reusing the existing image identity, routing/lease, raw-result and recovery safeguards rather than bypassing them. A single accepted qualification boot is the initial target; add another only for a specific reset/verification need.

The M3 native-controller physical results cannot substitute for an M5 portable-consumer test. Do not infer successful link-loss recovery from a disconnected NIC without target/peer evidence. Do not require packet-pool snapshots after every ordinary success if start/end counts and explicit error cleanup establish the Pilot claim.

## Physical safety and evidence gate

Before **any** board programming, probe operation, reset, UART/network ownership change or cable manipulation, obtain explicit human authorisation for the exact operation, source and ELF/BIN/configuration/tool identities, board/probe identity, current firmware/data preservation, restore image and applicable recovery/abort procedure. No Goal, issue, review PASS or this proposal grants that authority. Do not perform M4 writable QSPI, partition/format, erase or fault-injection work through M5 tests.

For each approved run retain the tested source/image identity, configuration and topology, command/fixture version, observed PASS/FAIL/INCONCLUSIVE, relevant raw evidence and representative resource/timing figures. Preserve failures. Keep physical approval evidence separate from runtime results.

## M5 exit and deferred qualification

M5 can close only when Issues #25–#30 have demonstrably satisfied their existing acceptance criteria with an approved representative scope, or a human has explicitly revised those criteria. A passing simulated suite, a merged feature PR or an unexecuted plan alone is insufficient.

Defer product-grade stress, exhaustive link/timeout matrices, repetitive boots without diagnostic value, worst-case latency and watchdog guarantees, long-term reliability, storage endurance and broad third-party compatibility. Record these limitations clearly instead of representing the Pilot as production-ready.
