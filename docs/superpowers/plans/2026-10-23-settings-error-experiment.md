# Settings error-propagation experiment plan

> **For agentic workers:** Execute inline with systematic debugging and TDD; independent Reviewer and Challenger assess the final experiment.

**Goal:** Reproduce and propose minimal corrections for the approved Settings/NVS failure paths using controlled pinned-source copies.
**Architecture:** Compile exact pinned store/NVS backend sources against explicit Host dependency shims. Apply reviewable patches only to temporary copies; compare unchanged and candidate behavior with deterministic inputs. No native adapter or production backend is installed.
**Tech stack:** C17, Python unittest, ASan/UBSan, existing Linux tooling.
**Spec:** User's M4 Final Software Acceptance and Settings Investigation Decision (23 October 2026), plus the accepted Configuration contract/PR43 handoff.

## Constraints and decisions

- Baseline main225cd21; pinned Zephyr684c9e8f32e4373a21098559f748f06915f950c9.
- Preserve PR42/43 and their acceptance. Merge order42 then43 is recorded; neither merge nor Issue closure is authorised.
- Preserve positive direct-callback termination, advertised full value size on intentional buffer truncation, and zero/no-change NVS-write success.
- No new public API, pinned checkout changes, production patch adoption, Flash/layout changes or hardware.
- Crux Zephyr Native Integration owns investigation and any subsequently approved downstream patch maintenance. Prefer existing upstream fixes and small upstream contributions.

## Task 1: Store error and read-count semantics

Files: tests/native/settings_error/{support.py,store.c,test_experiment.py,settings_store.patch}.

- [x] Retain/replay the approved legacy probe as baseline; verify pin and unchanged input hashes.
- [x] Add literal expected outcomes for lost traversal, first-source error, exact/short/failed reads, intentional small/zero buffers, positive callback stop and first length-query error. Run against unchanged copies and retain RED.
- [x] Make the smallest copied-source correction; require GREEN with valid/missing and multi-source success controls.

## Task 2: NVS scanning, repair and stored-name bounds

Files: tests/native/settings_error/{nvs.c,settings_nvs.patch}; shared private support/runner.

- [x] Inject NVS API errors underneath actual settings_nvs.c, cache off/on. Model actual NVS read advertised-length and write-zero semantics.
- [x] Verify no destructive repair after hard read errors; surface repair delete/count-write failures. Validate init count length/range and cached/uncached save errors.
- [x] Test stored-name boundary, embedded NUL/optional terminal NUL and count metadata. Capture original failure safely in isolated bounded processes before candidate changes.
- [x] Preserve valid, confirmed missing/deleted repair and no-change successes. Record partial-effect/recovery/cache limits rather than promise rollback.

## Review focus

Ordinary h_set error suppression in settings.c is outside the two-file experiment. Return-code changes can alter existing best-effort and multiple-source behavior. Fake NVS operations are not the actual NVS engine, Flash or physical recovery. Existing upstream name-bound fix must be credited; fail-closed malformed-record policy remains proposed. No experiment success implies adopted native safety.

## Task 3: Verification and adoption handoff

- [x] Run Python suite with explicit process timeouts, strict C warnings and sanitizers; unchanged-source negative controls and candidate controls.
- [x] Run applicable repository software regressions and boundary/format checks; keep tests requiring pinned sources as a documented focused gate, without a new CI job.
- [x] Compare current official upstream and contribution requirements. Record proposed changes, API compatibility, upstream strategy, maintenance cost and adoption prerequisites.
- [x] Obtain fresh independent Reviewer/Challenger results; resolve material findings and prepare the focused Draft PR contents. Publication status is recorded in the task/PR; keep16/M4 incomplete and all merge/native/hardware gates intact.
