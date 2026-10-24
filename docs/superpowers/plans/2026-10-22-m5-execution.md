# Portable Execution Implementation Plan

> Execute inline with Superpowers executing-plans and test-driven-development.
> Use independent consumer authorship and independent final review.

**Goal:** Implement Issue #25's approved bounded execution capability and prepare a review-ready PR.

**Architecture:** One C17 public capability, bounded checked resource handles, direct
native Zephyr and private pthread Host providers. Keep provider implementation and
native build requirements private. No public generic provider abstraction or OSAL.

**Tech Stack:** Existing C17/C++17, CMake, GoogleTest/plain C conformance, Zephyr ztest,
Linux amd64 container, pinned Zephyr/SDK; no upgrades.

**Spec:** [Historical approved contract and decision](../../history/execution-contract-proposal.md).

## Global constraints

- Preserve Time and historical M2/M3 source/evidence.
- Only this worktree/branch; no hardware, merge, Issue closure or Flash Safety edits.
- Four thread slots, eight mutex slots, eight semaphore slots; 4096 usable Zephyr stack
  bytes; priority high/normal/low maps to 4/5/6; Host priorities remain normal.
- Poll=0, finite 1..INT32_MAX relative monotonic ms, no forever sentinel.
- Append statuses -6 busy, -7 timeout, -8 no resources, -9 unsupported, -10 state.
- #24 enforcement remains an explicit integration dependency; #26 owns public timing/queues/events.

## Review focus

- Resource admissions and stale copied handles must not race reuse.
- Timed-out join retains a live worker; notification does not prove native termination.
- Concurrent semaphore give must detect full capacity atomically.
- Native recursive mutex behavior must not leak into nonrecursive contract.
- Provider housekeeping must not restart finite wait budgets or hold metadata locks while blocking.

## Task 1: Public boundary and thread lifecycle

Files: `components/execution/include/crux/execution.h`, `components/execution/src/`,
`components/execution/host/`, `components/execution/CMakeLists.txt`, root CMake,
`components/base/include/crux/status.h`, `tests/portable_execution/`.

- [x] Write conformance tests for create/normal return/join visibility, invalid input,
  poll/expiry retaining resources, successful reuse, stale handles, exhaustion,
  stack/priority errors, self/concurrent join.
- [x] Observe missing API/build failure, then minimal stub assertion failure; retain RED logs.
- [x] Add declarations and bounded provider lifecycle with checked generations.
- [x] Pass Host thread cases before adding synchronisation implementation.

## Task 2: Mutex/semaphore ownership and waits

Files: same capability, common conformance and Host tests.

- [x] Add failing tests for exclusion, wrong owner/self-lock, busy destruction,
  active waiters, monotonic expiry, wakeup, empty/full semaphores, simultaneous
  givers, pool exhaustion, generation aliases, repeated quiescent cleanup.
- [x] Implement the minimum operations; validate/admit under metadata protection,
  never across blocking native waits. Preserve one original monotonic deadline.
- [x] Pass conformance, full Host regression and ASan/UBSan.

## Task 3: Native Zephyr provider and independent consumers

Files: `components/execution/zephyr/`, `zephyr/CMakeLists.txt`, `zephyr/Kconfig`,
`tests/portable_execution/{CMakeLists.txt,prj.conf,testcase.yaml}`, independent
`tests/execution_runtime/`, firmware consumer/build fixture, `scripts/test-portable-execution.sh`.

- [x] Run existing conformance against unimplemented Zephyr provider to establish RED.
- [x] Implement native static-stack/thread, mutex and semaphore behavior against
  the same cases; include ISR rejection and actual stack/priority observations.
- [x] Have a separate author write the runtime-shaped fixture using the approved
  public contract only. Build firmware/C++ consumers as separate projects without
  provider-private source or native headers in their portable source.
- [x] Pass native_sim and cross-build EVKB; retain maps/configuration/resource hashes.
  No physical execution claim.

## Task 4: Boundaries, evidence and independent review

Files: existing quality scripts/workflows as needed; execution contract/validation
docs, dependency fixtures and durable evidence under `docs/evidence/`.

- [x] Add negative native-header/type/link and private-include leakage cases scoped
  to this capability; preserve legitimate native providers. State #24 limits.
- [x] Run formatting, selected static analysis, dependencies, relevant existing
  Host/Zephyr checks and source/input identity capture.
- [x] Independent reviewer inspects full diff, source, reports and unresolved
  target/#24 dependencies; resolve REVISE findings with affected re-tests.
- [x] Commit/push dedicated branch, open and attach a PR with actual results,
  limits and unresolved physical evidence; do not merge or close Issue #25.

Publication: [PR #36](https://github.com/wonny-labs/crux-pilot/pull/36) is open for human review.
Physical target acceptance and Issue #24 remain unresolved integration gates.
