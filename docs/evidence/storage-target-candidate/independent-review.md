# Independent target candidate review

Scope: 24 October 2026 human-authorised software proposal, based on accepted
PR #56 `b31f4f0`. No production patch/layout adoption or hardware qualification.

## Reviewer — PASS after REVISE

Independently inspected the actual narrowed Settings patches, fixtures, source
manifests, configs and reports; native dispatch/image/linker fences; startup and
runtime transforms; exact final ELF/BIN and phase assessment. Two material Flash
findings required revision: output publication could follow links, and outer
probe tests missed inner IS25/mode-enable errors. Fresh-inode atomic publication
and actual-helper error checks plus RED/GREEN regressions resolved both. Final
source and evidence re-review returned PASS. Settings fixture hashes matched
current inputs; retained actual startup and provider runs each showed8/8, Host
42/42 each. Reviewer independently hashed final ELF/BIN matching the proposal.

## Challenger — PASS

Independently challenged actual sources and claimed outcomes, checked all14 Flash
owned-input hashes, retained Flash14/14 and actual Settings engine/provider8/8
logs. No remaining material software-proposal finding. No broad workload replay.
Confirmed that exception/indirect/startup/stack/cache, master/watchdog, timing and
uncertain recovery obligations remain open. Mount writes/erase before count
validation, partial-read CRC limits and provisioning are explicitly disclosed.

Both verdicts are limited to evidence/proposal quality. They do not qualify
physical safety or authorize adopting/booting/programming the candidate, merging,
closing Issues or completing M4. The concrete physical decision is NOT READY.

Separate PR #51 test-plan acceptance/integration review also returned PASS; see
[that record](plan-integration.md). The full existing ACs remain unchanged.
