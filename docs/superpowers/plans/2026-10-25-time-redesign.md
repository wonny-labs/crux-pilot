# Typed Time Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task with TDD and independent final review for each Draft PR increment.

**Goal:** Complete typed Time, realtime authority/RTC separation, bounded Delay,
and migration while retaining supported behaviour and component ownership.

**Architecture:** Time provides clocks, values and authority; Thread owns waits;
Delay owns busy wait. Native implementation selection stays in component build
composition. Private native locks protect realtime and metadata together.

**Tech Stack:** C17/C++17, CMake, Linux CLOCK_* and pthreads, pinned Zephyr/SDK.

**Spec:** [Accepted Time redesign](../specs/2026-10-25-time-redesign.md).

## Global constraints

- Preserve PR #75 types, output preservation and checked arithmetic.
- Realtime/calendar support [2000-01-01, 2100-01-01), normalized nanoseconds.
- Monotonic needs neither RTC nor realtime init; no Time→Thread/Sync dependency.
- Preserve Queue/Sync/join millisecond timeouts; no general provider framework.
- No actual CLOCK_REALTIME mutation, physical flashing or hardware qualification.
- Small coherent Draft PRs, independent source/evidence review, no merging.

## Review focus

1. Fractional clock observations must not create early positive deadlines.
2. Near INT64_MAX conversions must reject overflow without changing outputs.
3. Snapshot timestamp and source must describe one Crux write transaction.
4. Optional RTC failure must not invalidate a successful system realtime write.
5. Minimal external selections must have explicit related dependencies without
   leaking native compile requirements into portable source.

### Task 1: Typed monotonic and Thread sleep

**Files:** Time `time.h`, new `src/time_clock.c`, `time_clock_port.h`, Linux/Zephyr
clock ports; Thread `thread.h`, new `thread_sleep.c`/private wait declarations,
existing native ports; component/root/Zephyr build definitions; focused Time,
Thread and independent consumer tests.

**Interfaces:** Consume PR #75 values/arithmetic. Produce
`crux_time_get_monotonic(crux_monotonic_time_t *)`,
`crux_time_make_deadline_after(crux_duration_t, crux_monotonic_time_t *)`,
`crux_thread_sleep_for(crux_duration_t)` and
`crux_thread_sleep_until(crux_monotonic_time_t)`, all returning `crux_status_t`.
Private clock reads provide observation or conservative quantisation upper bound;
private Thread wait accepts absolute nanoseconds and may wake early.

- [ ] Add focused common/native tests: NULL/negative/context/error preservation,
  zero/no-padding, positive-padding, 128-Hz fractions, overflow, repeated early
  wake, native EINTR, and deadlines beyond INT32_MAX milliseconds.
- [ ] Run the focused build and record missing-function RED evidence.
- [ ] Implement common validation and checked Linux/Zephyr clock conversions;
  Thread predicate loop keeps one original absolute deadline.
- [ ] Reconcile explicit Thread→Time build dependency, RTC-independent Kconfig,
  supported external selections and source policy; retain old consumers until
  their migration increment, without adding compatibility wrappers.
- [ ] Run focused tests, Host suite, independent consumers, relevant Zephyr
  native_sim/EVKB checks; review complete diff, commit, publish Draft and obtain
  independent review before taking this increment as complete.

### Task 2: Realtime authority and calendar

**Files:** Time public headers, common state/calendar, memory and native lock
ports, Time tests and sample/controller time consumers.
**Interfaces:** Task 1 clocks plus spec's source/snapshot types and authority
get/set/adopt/notify/invalidate functions; calendar consumes `crux_realtime_t`.

- [ ] Add failing lifecycle/transition/error tests and concurrent snapshot/write
  tests, including invalid snapshot zero fields and retained source on failures.
- [ ] Add failing fractional calendar/range/invalid-field round-trip tests.
- [ ] Implement state and timestamp under one private lock, local output commit,
  all argument precedence and native read/write failure rules from the spec.
- [ ] Migrate current realtime callers/CLI (fraction zero) and tests; run Host
  Debug/Offline/Sanitizer and source/evidence review, then publish a small Draft.

### Task 3: Native realtime and optional RTC

**Files:** Linux realtime port, Zephyr realtime/RTC ports and native adapter
header; component CMake/Kconfig; native sample compositions and provider tests.
**Interfaces:** Task 2 port read/write/init/lock and explicit RTC load/store
results; no new public RTC framework or implicit RTC write in authority.

- [ ] Add failing wrapped Linux/Zephyr native clock and fake RTC tests: system
  set succeeds even when a later explicit RTC store fails; no RTC required.
- [ ] Select memory or Linux realtime explicitly on Host; use Zephyr system
  realtime and retain the existing RTC mapping as optional source/storage.
- [ ] Verify supported external consumers, native_sim and representative EVKB
  builds; review selection and native boundary behaviour, publish Draft.

### Task 4: Bounded busy wait

**Files:** new Delay component/public `delay.h`/private port; Linux/Zephyr ports;
root/module build options, common/native tests and provider-limit documentation.
**Interfaces:** spec's max-duration query and busy-wait signatures.

- [ ] Add failing zero/negative/limit/failure-preservation tests and Zephyr
  counter-frequency/unit rounding cases, including ISR and unsupported timers.
- [ ] Inspect pinned native implementations, derive a documented safe limit,
  implement checked preflight and CPU wait without allocation or scheduler sleep.
- [ ] Run focused Host/native_sim tests and EVKB compilation; independently
  review actual native limits and evidence, publish Draft.

### Task 5: Complete migration and remove superseded APIs

**Files:** remaining consumers/tests/samples, old Monotonic component and build
targets, CMake boundary/policy inventories, active contracts/build documentation,
CI integration tests and workflow selection.
**Interfaces:** all new contracts from Tasks 1–4; no old Time/Monotonic public API.

- [ ] Migrate each caller by semantic units, preserving original deadline,
  ownership/error paths and test coverage. Add consumer tests for C17/C++17
  headers, wrong-type rejection and explicit minimal capability composition.
- [ ] Remove superseded APIs/targets/implementation after callers migrate;
  retain historical evidence without presenting it as new qualification.
- [ ] Run complete applicable Host/Zephyr workflows and independent consumers
  against final integrated source, including Debug/Offline/Sanitizer,
  native_sim and EVKB builds. Capture exact revisions and actual limitations.
- [ ] Obtain independent Reviewer/Challenger PASS on final source/evidence;
  audit every goal/spec requirement and leave all PRs Draft for human approval.
