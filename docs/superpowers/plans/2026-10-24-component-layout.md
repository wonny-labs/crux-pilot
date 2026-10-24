# Component Layout Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans with independent Reviewer and Challenger checks for each PR. Steps use checkbox syntax for tracking.

**Goal:** Adopt component-owned OS ports without changing public APIs or behaviour.

**Architecture:** Retain public headers, portable sources, native build configuration and explicit composition. Time's OS-independent controlled store becomes a memory provider; genuine native implementations move into `ports/<os>/`. Tests remain under top-level `tests/`.

**Tech Stack:** C17, C++17 consumers, CMake, Linux Clang 18, Zephyr/west/Twister.

**Spec:** User-approved Goal 01 — Crux Component Layout Refactoring (2026-10-24). The Goal explicitly authorises autonomous planning, implementation, regression tests, independent reviews and Draft PR creation, superseding routine plan approval. No feature development, public API/ABI changes, Execution split, physical operations, merges or direct main pushes.

## Global Constraints

- Preserve public interfaces and supported consumer-facing CMake targets.
- Keep native dependencies private and explicitly select implementations.
- Keep OS/board configuration in its authoritative build system.
- Preserve historical evidence, `tests/target/`, top-level Zephyr integration and sample ownership.
- Do not introduce a universal HAL/OSAL, generic backend framework, or unnecessary abstraction layers.
- Do not split the existing Execution implementation.

## Review Focus

- Private native headers must stay forbidden through moved `ports/<os>/` paths, including Linux.
- Zephyr composition must select RTC, never the memory provider or test instrumentation.
- Independent C/C++ consumers must retain headers, linkage and no native usage leakage.
- Twister discovery, controller overlay references and CI upload paths must follow moved tests.
- Historical qualification paths remain historical; current maintained instructions use current paths.

## Assessment and baseline

Baseline: current `origin/main` `ea11e47d2b8dda9cfc07240723ab5d9d4da20a13`, including PRs #45/#46. Primary checkout is clean on unrelated documentation branch and is left untouched. No open PRs at assessment. Host CI succeeded; latest Zephyr CI was still running. Current contracts, CONTRIBUTING, AGENTS, scripts, CI and current architecture design were read. The optional `01-component-layout-refactoring-v1.md` was not found in supplied project files or current planning repository.

Time's controlled store includes only ISO C and the private portable contract; it belongs in `src/time_port_memory.c`. Zephyr RTC remains native. Existing host CMake explicitly selects the controlled store for functional validation; no runtime registry or implicit fallback is needed. Keep a compatibility alias for the old internal host target where practical. Retain instrumented variants and private controls in tests.

## Tasks

### Task 1: Establish regression baseline

- [ ] Run Linux Debug and ASan/UBSan via `scripts/check-host.sh`, Python `unittest discover -s tests/build -v`, formatting, dependency checks and configured analysis on unmodified baseline.
- [ ] Run applicable Time Twister/native_sim and EVKB cross-build baseline; record exact commands and results outside historical evidence.

### Task 2: Time reference PR

Files: move `components/time/zephyr/` to `components/time/ports/zephyr/`; move `host/time_port_host.c` to `src/time_port_memory.c`; flatten `tests/time/{src,private}`; consolidate `tests/zephyr_time` into `tests/time/zephyr` and flatten its single source.

Interfaces: public C declarations and `crux::time` unchanged. Host composition selects `crux_time_memory`; Zephyr selects its RTC target. Test controls remain private, compiled only with `CRUX_TIME_TESTING`.

- [ ] Extend lexical and CMake boundary negative cases for `ports/linux`, `ports/zephyr`, and memory source coverage; observe relevant new failures before updating validation logic.
- [ ] Perform moves and update CMake, analysis/script paths, current contract/test documentation, controller overlay and CI artifact paths. Preserve test semantics and historical documents.
- [ ] Re-run applicable baseline checks, independent consumers, Time Twister, native and EVKB sample builds. Confirm public headers and provider source contents unchanged except required internal naming.
- [ ] Inspect full diff, obtain independent Reviewer and Challenger PASS, fix material findings and repeat affected review. Commit and create Draft PR with actual evidence.

### Task 3: Remaining applicable ports PR

Start from the same main baseline after Time structure validation. Keep this independent of the Time PR, sharing no required branch changes.

- [ ] Classify remaining providers by actual dependencies; identify exact moves and retained component/test structures. No native Configuration implementation should be invented, nor socket wrapper code added.
- [ ] Move genuine native Netif, Sockets, File, Queue, Monotonic and applicable Execution files to component-local OS ports. Retain existing target names and explicit composition. Do not split Execution.
- [ ] Update all affected maintained CMake, scripts, boundary rules, negative tests and current documentation. Avoid unrelated test restructuring.
- [ ] Run Debug/Sanitizer, component/integration and independent consumer suites, formatting/analysis/dependency negatives, affected Zephyr Twister/native_sim and firmware cross-builds.
- [ ] Independent Reviewer and Challenger inspect source and evidence; resolve material findings and re-review. Create coherent Draft PR(s) from main.

### Task 4: Handover

- [ ] Record tested source revisions, commands, counts, skips/failures, CI status and review verdicts in PR/evidence report.
- [ ] Keep branches recoverable and PRs unmerged. Record thread/sync split as a separately gated follow-up, with production qualification unchanged.

## Plan self-review

The staged approach uses Time to validate the pattern before other moves. One broad PR would hinder review; dependent stacked PRs would impede parallel human review. Independent Time and remaining-port branches are preferred, with overlapping boundary protections reconciled explicitly if needed. No new capability or generic provider mechanism is justified. Mechanical moves use regression-first validation; new boundary detection uses failing negative cases first.
