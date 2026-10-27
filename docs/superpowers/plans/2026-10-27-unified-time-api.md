# Unified Time / Busy-wait implementation plan

Goal: deliver Issue87 on baseline8d5dd0a, preserve timing algorithms while replacing explicitly superseded API/build boundaries. Specification: [adopted Issue #87](https://github.com/wonny-labs/crux-pilot/issues/87), with its retrieved text retained in local build/evidence/time-redesign/issue87/issue-spec.md (no comments at baseline). User explicitly authorizes inline execution without routine approval; independent Reviewer and Challenger only are delegated at final gate.

Architecture: components/time owns all time values/clocks/calendar and optional busy wait. A private interrupt implementation has no public include/usage target. Keep base header-only and Thread -> Time; never reverse. Use CRUX_TIME_BUSY_WAIT / CONFIG_CRUX_TIME_BUSY_WAIT with a common always-linkable crux_busy_wait entry and explicitly selected enabled provider/disabled implementation. Top-level Host defaults ON, external OFF; Zephyr default OFF. Reject obsolete flags even OFF and unsupported enabled profiles. No ABI wrappers, maximum query, arbitrary cap or new provider framework.

Migration matrix:
- crux_time_get_monotonic -> crux_monotonic_now; crux_time_make_deadline_after -> crux_monotonic_deadline_after
- crux_monotonic_add_duration -> crux_monotonic_add (base/delta/out)
- crux_time_init/get_realtime/get_realtime_snapshot -> crux_realtime_init/now/get_snapshot
- crux_time_adopt_realtime/set_realtime/notify_sync_state/invalidate_realtime -> crux_realtime_adopt/set/set_synchronized/invalidate
- crux_time_source_t/CRUX_TIME_SOURCE_* -> crux_realtime_source_t/CRUX_REALTIME_SOURCE_*
- snapshot is_time_valid/is_actively_synchronized -> is_valid/is_synchronized
- crux_time_utc_t/from_utc/to_utc -> crux_datetime_t/crux_realtime_from_utc/crux_realtime_to_utc
- crux_delay_busy_wait -> crux_busy_wait, declared only in time.h; remove get_max interfaces
- time_clock_port.h -> monotonic_port.h, time_port.h -> realtime_port.h; touched provider symbols use prescribed _impl_ names
- CRUX_BUILD_DELAY / CONFIG_CRUX_DELAY -> CRUX_TIME_BUSY_WAIT / CONFIG_CRUX_TIME_BUSY_WAIT; component delay removed, validation moved under tests/time/busy_wait

Tasks / completion checks:
1. Establish clean baseline and pinned environment. Read contracts and current consumers; save spec/refs/risks. Baseline Host full suite passes before changes.
2. RED: new C17/C++17 external Time consumers exercise target signatures and disabled negative/zero/positive link/runtime without provider. Check memory/Linux and enabled configurations plus obsolete flags. Minimal public/API migration and Time ownership; preserve all existing relevant behavior tests.
3. RED: shared private ISR query / realtime native lock tests reject ISR before lock; preserve argument precedence and Thread zero/past deadline rejection. Introduce private Linux/Zephyr target and process-local/thread-local fake. Migrate Time/Thread and direct Sync/Queue/System native predicates; preserve System pre-kernel guard. Add real upper-sample non-integer tick-rate regression.
4. RED: enabled Busy-wait preflight/ceil/range/forbidden operations/profile/IRQ behavior. Move provider implementations, retain internal supported range checks without get_max queries, update unit/integration/native configurations including disabled case. Reject unsupported explicit enabled compositions.
5. Update build boundaries, scripts/CI/examples/maintained docs and all affected consumers. Retain historical evidence unchanged. Keep complete coverage and artifact capture; no physical operations or Host clock writes.
6. Run Host Debug/offline/sanitizer, public and independent consumers, full Python/boundary checks, format/analysis/dependencies, docs; Zephyr RTC on/off, sleep/Busy-wait/native/EVKB. Independent whole-source Reviewer/Challenger; resolve material findings; commit/push feature branch and Draft PR. Final exact-source CI and acceptance audit. No merge/issue closure.

Risk register / verification:
- Header renames can break bespoke fake-provider targets: inspect every CMake/source-policy/private-header reference and run full Host/Python/native suites.
- Static private dependencies must not leak interrupt src or native include roots: independent C/C++ boundary tests, Time-only/Sync-only and no public interrupt target.
- Disabled feature must truly avoid optional provider/clock reads: link/execute Time-only C/C++ with native clock wrappers that fail and inspect linked artifacts.
- Removing max queries must not weaken preflight or safe ceil/range. Preserve explicit Linux resolution/headroom and Zephyr half-counter limits; test boundaries and unsupported profiles directly.
- Shared ISR policy is not universal eligibility: native lock ISR guard first; arguments still first; System pre-kernel remains; no signal-safety or physical ISR/timing certification.
- Non-integer tick upper observation must sample once and use ticks+1 before rounding; never replace by rounded sample+rounded quantum.
- Preserve realtime metadata/locking/state transitions and output-on-error tests. Real clock setters remain wrapped or memory-controlled.
- Issues25/26 remain consumer/dependency context, not permission to close their hardware milestones; Issue87 supersedes stale RTC wording in26 only within this migration.

Record exact RED/GREEN commands/revisions and failures. Reuse existing isolated worktree and stopped Docker container after confirmed terminal state; preserve all previous branches/evidence. No new architectural approval checkpoint because exact adopted target and autonomous execution are explicitly approved.
