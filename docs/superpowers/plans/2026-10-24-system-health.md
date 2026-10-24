# Portable System Health and Reset Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline. Separately authored public-only consumer and final independent Reviewer/Challenger are required; no implementer-per-task delegation. Steps use checkbox (`- [x]`).

**Goal:** Deliver the approved six-operation System capability, software evidence and Draft PR for #30.
**Architecture:** Common boot-local lifecycle/observation state with private Linux monotonic watchdog emulation and Zephyr watchdog/hwinfo/reboot mappings. No automatic supervision, feeder, global readiness policy, extra worker or public registry.
**Tech Stack:** C17/C++17, CMake/CTest, private pthread/Zephyr mutex, pinned Zephyr4.4.0/SDK1.0.1, existing native_sim/ztest/build-boundary tools.
**Spec:** [Approved contract](../specs/2026-10-24-system-health-design.md), human approved at proposal72e5295 on24October2026. Approval explicitly authorizes implementation/TDD; no additional operational-plan approval gate.

## Global Constraints

- Public `<crux/system.h>` / `crux::system`, Base only. Exactly system_status_get, reset_cause_get, reset_cause_clear, system_reset, watchdog_start, watchdog_feed with approved signatures/types/flags.
- One watchdog/boot, opaque zero-invalid token, no stop/destroy/rearm/reconfigure; copied token delegates feed but is not security or native-resource arbitration.
- Explicit public successful cause query permits one clear attempt. Status probes/failed reads never grant it. Clear consumes permission even on failure, verifies reported flags zero, never runs at startup.
- Function OK for status means valid report, not service/application health. Service errors remain explicit; failed report/query leaves output unchanged. Never fabricate missing cause/POR or mask inconsistent native causes.
- Target reset does not return once dispatched; nonreturn can also mean native failure/hang. Public function never returns OK. Normal Host reset/cause operations NOT_SUPPORTED with no machine/process reset or synthetic epoch.
- Initial RT1060 profile only: positive500..64000ms, exact500ms steps, initial2000ms. Host emulation mirrors it for conformance; no universal range/physical timing guarantee.
- Private task-context serialization, no spin loop or public OSAL. No automatic hardware feed/clear/reset. Detectable ISR/pre-kernel NOT_SUPPORTED; no whole-call bound.
- Reject known M3 native owner even with its commands off, WDT/HWINFO Shell, Task-WDT fallback, missing/wrong watchdog binding and non-B target profiles. Observe unowned prearmed hardware before installing.
- Preserve existing capability contracts and M3 images. No hardware, merge or #30 closure. Keep old process-exit delays unresolved.
- Affected checks during development; full existing Host/Zephyr PR/Push CI once source is stable. No unrelated baseline replay: mainbf048c1 tree equals qualifiedPR61bf6706f.

## Review Focus

- Status OK with an unavailable service must not become a healthy/ready verdict (Task1 + independent consumer).
- An unready start may retry; partial native install/setup or unexpected prearming must quarantine without token/rearm (Task1/3).
- Status probing/failed cause reads must not allow premature clear; failed clear needs another explicit get (Task1/3).
- Host expiry near feed must not refresh an already expired simulation; no reset/persistence side effects (Task2).
- Native reset tests must use a truly nonreturning hook; native M3 clear/feed must not bypass exclusivity (Task3).

## Task 1: public declarations and shared lifecycle

**Files:** create components/system/include/crux/system.h, src/system.c, src/system_port.h; tests/system/common.c and initial CMakeLists.txt.
**Produces:** exact approved six APIs. Private task-only lock/context; watchdog profile validation, probe(running/expired), emulation identity, start/feed; reset availability/request; normalized cause read/native clear. All private declarations remain ISO C/Base.

- [x] Write controlled-port tests first: report/function error distinction and unchanged outputs; zero/null/invalid token; start2000/feed; unready then valid start; second start; prearmed/partial install/setup/probe/feed quarantine; explicit query/clear permission, consumed failure, remaining/inconsistent flags; unsupported context/reset.
- [x] Compile missing public header/functions RED and retain actual output. Implement public/private declarations and common policy using native mutex ports, no dynamic allocation or worker.
- [x] Run strict C17 focused common cases GREEN; retain exact commands, compiler/environment and intentional test-double limitations. Commit.

## Task 2: Linux emulated provider

**Files:** components/system/CMakeLists.txt, ports/linux/system_host.c; tests/system/host.c/CMakeLists.txt; root CMake; focused analysis wiring.
**Consumes:** private functions from Task1; **produces:** real pthread serialization, checked monotonic lazy expiry, explicitly emulated identity, unsupported reset/cause services.

- [x] First actual provider tests: IDLE/start/feed at1999ms then expiry at2000ms since last feed, no revival after expiry; no false status OK on clock failure/backwards time; two simultaneous starters yield one owner; normal reset/cause unsupported without process effects. Wrap only the private clock dependency in tests, with no public test API.
- [x] Retain missing-provider RED; implement host provider and explicit host selection/usage target. Run focused Host and ASan/UBSan, format/analysis/dependency checks. Commit.

## Task 3: Zephyr mapping and ownership

**Files:** components/system/ports/zephyr/{CMakeLists.txt,system_zephyr.c,system_binding.c,system_zephyr.h}; zephyr/{CMakeLists.txt,Kconfig}; tests/system/zephyr/; tests/build/test_system_composition.py; scripts/test-system.sh.
**Consumes:** same private Task1 functions; **produces:** native mutex/device wdt calls, supported/cause normalization, explicit native clear, SYS_REBOOT_COLD forwarding and qualified binding.

- [x] First failing native tests for device readiness, prearmed/start/feed/partial failure; captured min0/max2000/no-callback/RESET_SOC/debug-pause/channel; real native_sim HWINFO POR/read/clear plus controlled mixed/error flags; ISR rejection and reset forwarding via correctly noreturn disposable-thread hook.
- [x] Retain undefined capability/missing provider RED, implement native mapping and private running-status observation. On RT1060 use native HAL WDOG running flag plus Zephyr wdt API; no direct reimplementation of watchdog control.
- [x] Actual missing/wrong resource, unsupported provider/revision and known owner negatives with intended diagnostics. M3 negative uses its real existing composition/Kconfig, not a fake symbol; retain a supported positive. Keep case set representative, no exhaustive device/mode grid.
- [x] Run focused native_sim and applicable dependency/format checks, preserve failures and commit. Controlled device mapping is not physical expiry/reset evidence.

## Task 4: portable consumers and selected target

**Files:** samples/controller/system/controller_system.{c,h}; tests/system_consumer/{CMakeLists.txt,src/,host/,zephyr/}; portable-source/consumer guard inventory and regression; scripts/CI wiring.
**Consumes:** public six functions and existing sample controller_health policy only. Independent author owns tests/system_consumer/src; native bootstrap stays separate.

- [x] Separately author C17/C++17 runtime-shaped helpers using public interfaces, reporting optional/unavailable services honestly and retaining initial cause before any acknowledgement. Never reset/arm automatically at boot.
- [x] Controller example reuses existing freshness/readiness policy for explicit start/feed decisions; initial diagnostic owner retains a copy and distributes it before an explicit clear. Add focused policy and actual Host consumer checks; native tests exercise representative portable calls.
- [x] Run native-free source/usage negative and actual C17/C++17 builds. Cross-build mimxrt1060_evk@B//qspi with wdog0 alias, HWINFO/REBOOT, inspect actual config/provider/stack/memory, retain source identity. No hardware operation. Commit.

## Task 5: independent review and Draft PR

**Files:** approved design (canonical contract), docs/system-validation.md, docs/evidence/m5-system/, tests guide; final PR description.

- [ ] Reviewer and Challenger inspect actual code/contracts/tests/evidence; resolve material findings under affected RED/GREEN and independent re-review.
- [ ] Map every #30 acceptance criterion to actual software proof or explicit physical limitation. Include ownership/feed scheduling/reset consequences, static resources, unsupported modes, prior failures and a separately gated minimal physical scenario.
- [ ] Run all applicable Host/Zephyr hosted CI against final stable HEAD. Inspect exact head/run results and actual relevant logs. Do not create repeated CI cycles solely to record outcomes; final PR/ledger records those results.
- [ ] Create/attach Draft PR referencing #30 without closing keywords; report head/results/reviews/remaining physical gates. No merge/Issue closure.
