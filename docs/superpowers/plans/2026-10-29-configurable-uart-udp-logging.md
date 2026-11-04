# Configurable UART and UDP Syslog Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let unchanged portable Crux Logging consumers select Zephyr UART, UDP Syslog, or both while preserving the public API and current UART-only behavior.

**Architecture:** Reuse Zephyr 4.4.0 deferred Logging and native UART/NET backends. Select backends through project Kconfig; apply UART devicetree ownership checks only when UART is selected. Use Zephyr's native connection-manager integration to gate NET backend activation on L4 readiness, subject to runtime recovery tests. Keep Host provider and public consumer surface unchanged.

**Tech Stack:** C17, CMake, Zephyr 4.4.0, Kconfig/devicetree, Twister `native_sim`, EVKB Rev B cross-build, existing Crux logging composition tests.

**Spec:** Approved Issue #103 outcome and this implementation's acceptance criteria in [Issue #110](https://github.com/wonny-labs/crux-pilot/issues/110); public contract in `docs/logging-contract.md`; prior Logging design in `docs/superpowers/specs/2026-10-24-logging-design.md`.

## Global Constraints

- Keep `components/logging/include/crux/logging.h`, `crux_log_write()`, and `crux_log_set_level()` unchanged.
- Preserve the common Crux severity filter, task-context restriction, bounded completed-text contract, best-effort `CRUX_OK`, and deferred drop-new behavior.
- Do not add a Crux queue, worker, dispatcher, backend registry, public backend selector, delivery receipt, flush API, or dynamic reconfiguration.
- Support only UART, UDP Syslog, and their concurrent configuration in this issue. Reject TCP Syslog; do not enable RTT or File Logging.
- Preserve UART resource ownership, Console exceptions, EVKB Rev B constraints, and existing Issue #29 physical acceptance boundaries.
- Do not incorporate PR #109's SPIKE-only workflow or SEGGER dependency changes. Integrate tests into existing logging suites/scripts.
- Do not connect to, reset, program, or erase physical hardware. EVKB checks are cross-builds only.
- Do not merge a PR or close Issue #110. Prepare a focused Draft PR only after Gate 2 approval.

## Review Focus

- **UDP server unavailable or unresponsive:** UDP cannot report application-level collector absence; tests and docs must distinguish local send attempt from peer receipt and must exercise route/interface failure.
- **Repeated native NET initialization/send failure:** Ensure the shared logger still processes UART records, characterize worker delay, loss, and retry behavior, and avoid interpreting socket-send success as delivery.
- **L4 disconnect/reconnect:** native_sim loopback does not provide the physical connection manager's route lifecycle; inject Zephyr management events only to exercise backend activation/deactivation, label this as event-handler simulation, and leave real manager/route recovery for target validation.
- **UDP-only selection:** Prove UART devicetree selection and ownership are not required when UART is disabled, while at least one supported backend and valid NET configuration remain mandatory.
- **UART-only compatibility and routing conflicts:** Keep the current UART-only tests and every negative UART owner/Console/board case passing; concurrent mode must apply the same guards.

---

### Task 1: Add failing backend-selection composition tests

**Files:**
- Modify: `tests/build/test_logging_composition.py`
- Test through: `scripts/test-logging.sh` and its existing `CRUX_LOGGING_COMPOSITION_CASE` selector

**Interfaces:** Keep the current composition-test harness and diagnostic matching; add profile cases rather than a new test framework.

- [ ] **Step 1: Add selection cases before changing provider code.** Cover the existing UART-only positive case; UDP-only with IPv4/UDP/sockets and a nonempty numeric Syslog address; concurrent UART+UDP; no backend selected; UDP selected without its network/socket capability; missing or malformed Syslog address; TCP URI; and UART-required binding errors when UART is selected.
- [ ] **Step 2: Run the new cases against the baseline.** Run `CRUX_LOGGING_COMPOSITION_TESTS=1 CRUX_LOGGING_COMPOSITION_CASE=<case> python3 tests/build/test_logging_composition.py` for each new positive/negative case. Confirm currently unsupported profiles fail and record the current UART-only controls that still pass.
- [ ] **Step 3: Keep expected failures specific.** Require rejection diagnostics for unsupported profiles and successful CMake configure/build for all three supported profiles. Do not weaken existing diagnostics to make a case pass.

### Task 2: Implement conditional backend configuration and readiness

**Files:**
- Modify: `components/logging/ports/zephyr/CMakeLists.txt`
- Modify: `components/logging/ports/zephyr/logging_binding.c`
- Modify: `components/logging/ports/zephyr/logging_binding.h`
- Modify: `components/logging/ports/zephyr/logging_zephyr.c`
- Test: Task 1 composition cases

**Interfaces:** Consume Zephyr's `CONFIG_LOG_BACKEND_UART` and `CONFIG_LOG_BACKEND_NET`; keep `crux_log_zephyr_device()` private and available only for UART-selected compositions. No exported Crux interface changes.

- [ ] **Step 1: Implement backend-selection checks.** Retain deferred processing, source-level DEBUG ceiling, severity display, line-boundary, drop-new, and no-capacity-wait requirements. Permit only UART and/or NET; reject zero supported backends, unavailable NET prerequisites, empty/malformed server configuration, and `tcp://` Syslog selection. Keep UART-specific backend settings conditional on UART selection.
- [ ] **Step 2: Make binding checks conditional.** Compile and require the current single enabled chosen UART and its ownership checks only if the UART backend is enabled. Ensure UDP-only composition has no Crux Logging UART dependency.
- [ ] **Step 3: Preserve provider status behavior.** Check UART readiness and return `CRUX_ERR_NOT_READY` when UART is selected and known unavailable. When UART is not selected, do not query a UART device. NET remains native best-effort logging; do not claim network readiness or peer receipt from `CRUX_OK`.
- [ ] **Step 4: Run Task 1 cases.** Confirm all supported compositions configure/build and every invalid profile emits its intended diagnostic; rerun legacy UART negative cases.

### Task 3: Add failing runtime coverage for UART, UDP, and fan-out

**Files:**
- Modify: `tests/logging/zephyr/src/main.c`
- Modify: `tests/logging/zephyr/prj.conf`
- Modify: `tests/logging/zephyr/testcase.yaml`
- Modify: `tests/logging/zephyr/app.overlay` only if a dedicated loopback fixture is needed
- Modify: `scripts/test-logging.sh` only to run the new profile variants through its existing Twister flow

**Interfaces:** Reuse the same actual C17/C++17 portable consumer targets, Crux API, UART emulator capture, and Twister application. Add test variants/config fragments, not a second logging test application or framework.

- [ ] **Step 1: Add runtime profile variants.** Run one source-identical consumer suite with UART-only, UDP-only, and UART+UDP Kconfig selections. UDP variants configure a Zephyr loopback receiver and native connection-manager readiness events; use an explicit test-only Syslog endpoint/port.
- [ ] **Step 2: Add assertions before implementation changes.** For every selected backend, assert complete delimited records and correct ERROR/WARN/INFO/DEBUG behavior after the common filter. Assert the same portable consumer source works in all profiles. Assert the non-selected UART or NET output is absent. Include a maximum-length Crux text record and caller-buffer mutation before deferred processing.
- [ ] **Step 3: Run the runtime variants and record RED evidence.** Confirm UDP profiles fail against the UART-only baseline and retain the pre-existing UART-only result.
- [ ] **Step 4: Implement only fixture/configuration needs revealed by RED.** Use Zephyr-native NET backend and socket receiver; do not use PR #109's SPIKE runner or add product test hooks to public headers.
- [ ] **Step 5: Run Twister on `native_sim/native/64`.** Confirm each profile executes (not merely builds) and captures the expected records on the selected transport(s).

### Task 4: Prove startup, repeated failure, network loss, and recovery

**Files:**
- Modify: `tests/logging/zephyr/src/main.c`
- Modify: `tests/logging/zephyr/testcase.yaml` and Kconfig profile fragments as needed
- Modify: `components/logging/ports/zephyr/CMakeLists.txt` or Zephyr project configuration only if evidence requires a policy correction

**Interfaces:** Prefer `CONFIG_LOG_BACKEND_NET_USE_CONNECTION_MANAGER` with `CONFIG_LOG_BACKEND_NET_AUTOSTART` and native L4 connected/disconnected events. Do not create a Crux-specific worker or runtime backend API.

- [ ] **Step 1: Add lifecycle tests.** Confirm no NET output before a test-injected L4-ready event; NET output begins after that event; interface-down plus injected disconnect does not prevent later UART records; interface-up plus injected reconnect resumes UDP. Call this event-handler simulation, not proof that the connection manager emits events for real route changes.
- [ ] **Step 2: Exercise unavailable collector and repeated failure accurately.** With UDP, a missing listener may still accept datagrams locally, so do not assert a sender-side error or delivery failure in that case. Separately force unavailable route/interface conditions and submit repeated records while observing UART completion, surviving-record order, call duration, worker delay, and aggregate dropped records.
- [ ] **Step 3: Run tests before lifecycle implementation changes.** Confirm the baseline/new profile fails the relevant lifecycle and recovery assertions, or preserve concrete evidence showing which behavior is already native and passing.
- [ ] **Step 4: Implement the smallest evidence-backed policy.** First use native connection-manager start/stop behavior. If recovery reuses a stale socket or otherwise fails, investigate a narrow native-event lifecycle correction; stop and return for a design decision before adding a new worker/dispatcher.
- [ ] **Step 5: Rerun lifecycle tests and record bounds.** Document that UDP provides no peer acknowledgement, the no-listener test does not exercise sender failure, and caller submission timing does not measure the deferred worker. State measured simulator caller timing as an observation rather than a target WCET guarantee.

### Task 5: Verify EVKB compositions and resource impact

**Files:**
- Modify: `scripts/test-logging.sh`
- Modify: `tests/logging_consumer/zephyr/prj.conf` or add narrow Zephyr config fragments consumed by the existing build command
- Test: `tests/logging_consumer/zephyr` and actual `tests/logging/zephyr` profiles

**Interfaces:** Use the existing pinned west workspace, Zephyr SDK, board target `mimxrt1060_evk@B//qspi`, and public C17/C++17 consumers. Cross-build only.

- [ ] **Step 1: Add reproducible EVKB UART-only, UDP-only, and UART+UDP cross-build invocations.** Preserve the current default UART-only EVKB profile and console/Serial ownership constraints. Supply a test-only numeric Syslog endpoint for UDP profiles.
- [ ] **Step 2: Run all three cross-builds.** Retain each resolved `.config`, linker map, board/revision, Zephyr commit, SDK/toolchain identity, and build command. Confirm TCP, RTT, and File backends are not selected.
- [ ] **Step 3: Compare resource outputs.** Record whole-image Flash/RAM and compare against UART-only while identifying network stack/buffer and backend buffer configuration. Do not claim these as runtime peaks or physical Ethernet qualification.

### Task 6: Documentation, full checks, and independent review

**Files:**
- Create: `docs/logging-udp-validation.md`
- Modify: `docs/logging-contract.md` only if the supported provider behavior or configuration contract needs a durable clarification; public declarations remain unchanged
- Test: Host, Zephyr, format, analysis, dependency, and CI workflows

**Interfaces:** Keep evidence separate from Issue #29 physical acceptance; link Issue #110 and exact implementation revision/configuration identities.

- [ ] **Step 1: Document supported profiles and failure policy.** Include Kconfig ownership/server-address policy, startup/recovery behavior, validated severity/delimiter/maximum-record behavior, socket and worker limitations, best-effort loss semantics, memory observations, and remaining physical validation. Explicitly exclude TCP, RTT, File, delivery guarantees, and hardware results.
- [ ] **Step 2: Run focused verification.** Run `scripts/test-logging.sh`, `scripts/check-format.sh`, `scripts/analyze-host.sh`, and `scripts/check-dependencies.sh` in the documented Linux Dev Container; run `scripts/check-host.sh` and applicable logging consumer/boundary checks. Confirm all runtime Twister tests actually execute and every negative configuration case rejects as intended.
- [ ] **Step 3: Run final Zephyr checks and hosted CI.** Run the issue-applicable Zephyr checks and all PR workflows on the final candidate commit. Keep cross-build evidence distinct from physical operation.
- [ ] **Step 4: Dispatch separate Reviewer and Challenger reviews.** Reviewer checks Issue #110 acceptance, actual source/config/tests/evidence, and compatibility. Challenger pressure-tests failure/recovery, UDP semantics, resource claims, and architectural simplicity. Require explicit PASS/REVISE/ESCALATE verdicts with evidence.
- [ ] **Step 5: Address findings and revalidate.** Fix material findings and rerun affected tests; obtain fresh independent review for any material revision.
- [ ] **Step 6: Present Gate 2 results for human approval.** Do not submit the final Draft PR until the user approves the implementation results; do not merge the PR or close Issue #110.

## Baseline evidence

- Approved architecture: Issue #103 closed with UART+UDP as the first implementation candidate, conditional RTT, deferred File, and TCP excluded.
- Current Crux provider baseline: UART readiness is unconditional; `logging_binding.c` requires `zephyr,log-uart`; Zephyr CMake rejects `CONFIG_LOG_BACKEND_NET`.
- Existing test base: `tests/logging/zephyr` verifies the native UART emulator and source-identical portable consumers; `tests/build/test_logging_composition.py` verifies real native build/config rejection; `scripts/test-logging.sh` runs Host, composition, native_sim and EVKB checks.
- Starting revision: `9c3ab6433f77d98e117d7b470ccba2ce9282c265` on `feat/issue-110-configurable-uart-udp`.
