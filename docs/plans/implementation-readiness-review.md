# Crux Implementation Readiness Review

| Item | Assessment |
| --- | --- |
| Date | 16 October 2026 |
| Reviewed plan | Crux Initial Implementation Plan v0.4 |
| Verdict | Ready for bounded software implementation; conditional for target completion |
| Review method | Comparison with current personal architecture documents and selected official Zephyr source; consistency and execution-path review |
| Validation limit | No environment was installed, code compiled or hardware operated during this planning review |

## 1. Can Codex implement from this package?

Yes, once explicitly instructed to implement in an authorised working directory. The plan is specific enough to start repository bootstrap, independent host builds, the C17 Time component, example implementation and host verification without inventing a larger platform framework. The later implementation decision uses a C17 command handler; host GoogleTests consume the public C API from C++ and link the C17 implementation in the test composition.

It is not sufficient to promise unattended end-to-end completion. Exact target identification, host tools, board connectivity, the contract-review gate and physical-execution authorisation remain prerequisites at their stated milestones. These are deliberately local gates rather than reasons to stop all independent work.

The original planning review produced planning files only. A later local setup created `/Users/jiwonkim/Projects/crux-workspace/crux-pilot`, initialised Git and copied the planning package. This v0.4 update changes planning documents; it does not establish implementation/test completion or authorise physical execution.

## 2. Architectural information added

The earlier plan's principal weakness was leaving enough architectural discretion that different agents could implement materially different platforms while claiming the same milestones. Version 0.3 narrows that discretion:

| Earlier ambiguity | Resolution in v0.3 |
| --- | --- |
| What is Core versus application versus native integration? | Common C17 Time semantics/conversion and commands, thin host/shell bridges and native backends have separate responsibilities. |
| How are backends connected? | One private link-selected Time port with initialise/read/write operations. No registry, weak-symbol fallback or public backend framework. |
| Who owns readiness and validity? | Common implementation owns state; the port implements native access. Error precedence, retry, repeated init and failed writes are specified. |
| Does C++ support mean a wrapper library? | No. C++ consumers call the C17 API directly; no speculative wrapper layer. |
| Can common CLI code include private conversion helpers? | No. Bounded public calendar conversion is Time-owned; parsing/presentation is sample-owned. |
| Is the host merely a unit-test build? | No. A minimal stdin/stdout reference composition exercises the same portable command handler with a clearly identified controlled clock. |
| Does the module entry point conflict with host CMake? | Host root and `zephyr/` module CMake have separate entry points; module discovery must be demonstrated. |
| Can ordinary Zephyr RTC shell commands prove Crux? | No. Crux commands must go through Crux contracts; direct driver commands are not acceptance evidence. |
| What constitutes an identifiable test run? | Expected build marker, source/configuration identity, firmware checksum and raw logs are associated. |
| Can container build output simply be flashed with macOS west? | No. A host-native flash procedure is required; container CMake paths and runner metadata are not assumed portable. |

These additions are sufficient for the first slice. Detailed CLI/SFTP service architecture, a general HAL, a service lifecycle framework and a future Linux distribution design are not needed now.

### Version 0.4 result-ownership update

Use the small common `crux_status_t` as the default operation result for Time and future services whose semantics fit it. The minimal base header/INTERFACE target is independent of Time, native dependencies and service runtime. Module-owned states, data and recovery-relevant results remain local to the module; common results are not a mandate to flatten every service API.

The earlier Time-specific error type was unnecessary because its initial failures fit shared categories. Time API no-data is now `CRUX_ERR_NO_DATA`; the sample still reports `ERR NOT_SET` through an explicit presentation mapping. M2-01/M2-02 and new M2-09 verify ownership/header boundaries and this distinction; M3-05 covers native error translation. Stable existing AC identifiers are preserved. No error registry, global last-error store, generic C++ result framework or future service implementation is introduced.

## 3. Evidence that changes implementation assumptions

The official v4.4.0 RTC sample writes a fixed date during startup. Copying its startup behaviour would violate Crux's requirement not to establish an arbitrary default time. Adapt device acquisition and API use, not that policy.

The same sample's test metadata does not list RT1060 EVK. Existing SNVS/RTC support is a useful starting point, but the sample is not evidence that this exact board/revision has passed. Codex must establish the board-specific configuration and retain its own results.

The selected generic RTC Counter implementation maintains calendar offset/validity state. Counter progression and calendar restoration across reset are distinct. The prototype's session-validity restriction avoids an unsupported persistence claim; it must not become a permanent platform policy without product requirements and native evidence.

Source links and exact reference tags are listed in the plan. Current architecture inputs reviewed were Concept v0.17 and Design v0.10 from the connected repository. Both remain working architecture documents. This review does not approve the company delivery programme or transfer company assets.

## 4. Remaining inputs and when they matter

| Input | Consequence if missing | Can useful work continue? |
| --- | --- | --- |
| Authorised local checkout/output location | Cannot safely decide where implementation changes belong | Planning and read-only discovery only |
| GitHub owner and intended access | Cannot publish or create the remote correctly | Local implementation can continue when authorised |
| Disk capacity and Docker availability | Local container setup cannot finish | Source/contract work can proceed; an approved alternative machine can be proposed |
| Exact EVK model/revision and flash variant | Exact target configuration cannot be accepted | Host and suitable native_sim work continue |
| Probe, UART and reset method | Cannot complete flash/UART validation | Software builds and tests continue |
| Physical execution approval | Cannot operate the board | All independent software work continues |
| Contract-review response at M2 | Cannot accept the native implementation against the new contract | Upstream readiness and independent checks continue |

Codex should discover these inputs where possible, then ask a focused question only for the missing item that affects its next dependent action. It must not repeatedly ask about routine implementation details covered by the plan.

## 5. Residual architecture risks

### Scope of the Time contract

UTC Unix seconds, years 2000–2099, second precision, single instance and serialised calls are bounded prototype choices. They do not settle future monotonic time, concurrency, clock quality, synchronisation or persistent validity requirements. Record these limits rather than generalising the first API to all clock use.

### Reuse without unnecessary abstraction

The private port is justified by host/native substitution. An additional HAL, C++ object hierarchy or generic service manager is not yet justified. A failed native readiness probe should produce a focused investigation rather than speculative architecture.

### Native implementation dependence

The selected Zephyr RTC/Counter/libc configuration must support the required date interval and C17 runtime policy. The production sample and Zephyr integration tests use C17; host GoogleTests exercise public C API calls from C++ against the C17 test composition, and the separate M1 C++ Hello World is a toolchain smoke check. The production `crux::time` target is not independently linked from C++ after the dedicated smoke test was removed. Source inspection is insufficient for target guarantees. The plan makes failures visible before they become hidden contract changes.

### Test independence

Agents can write matching defects into conversion code and expected results. Independent known timestamps, invalid-date anchors and cross-environment checks are required in addition to round trips. Review the oracle, not merely the test count.

### Proportional engineering

GoogleTest, Ztest and pytest introduce maintenance, but each has a distinct role. Do not create a fourth runner or duplicate every test in every framework. Formatting and focused analysis are initial controls, not claims of full MISRA compliance.

## 6. Is more architecture required before starting?

No additional platform-wide architecture document is necessary to start M0–M2. The updated plan supplies the missing composition, private boundary, state, error, CLI and build contracts.

A short implementation-level contract review is still warranted before native capability implementation. Confirm that the selected Time semantics are useful, the port remains private and the sample tests both portability and native integration. This is a review of a concrete bounded design, not a request to design the entire platform first.

After the slice, update architecture documents only when evidence changes a governing boundary or justified design decision. Naming and folder changes alone do not require rewriting the architecture set. The pilot's simplified `components/` and `samples/` layout preserves the current design's module ownership and composition rationale.

## 7. Current execution decision

This section records the original M0–M1 handoff decision from 16 October 2026. It is historical: the user later authorized M2, M3 and software-only M5. Current AC status is maintained in `docs/implementation-status.md`; the active plan is v0.5. Remote publication and physical operation were not authorized at that historical handoff. Later authorizations and execution results are recorded in `AGENTS.md` and the current status table.

Do not authorise a new AI Harness as a prerequisite. Thin standard commands, repository guidance, ordinary build/test tools and evidence are sufficient for this slice. Add automation later only for a repeated, observed need.

## 8. Implementation architecture follow-up (17 October 2026)

Review of the implemented repository found that the root host build coupled library consumers to
GoogleTest/C++, the Zephyr module compiled sample commands, backend duplication was not rejected by
the build graph, and the registered Shell command could reject excess arguments before the shared
handler. The v0.5 plan records the bounded corrections: an offline C-only library path, independent
base and sample ownership, CMake backend identity checks, public API contracts at declarations, and
Ztest through the actual Shell adapter. This does not authorize a generic HAL, service framework,
driver catalogue or hardware operation.
