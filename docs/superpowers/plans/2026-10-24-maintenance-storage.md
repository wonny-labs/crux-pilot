# Maintenance storage software implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development or executing-plans task by task, with TDD and independent review.

**Goal:** Implement the approved limited M4 maintenance storage candidate without production adoption or physical operations.
**Architecture:** Native composition permits persistence only before controller/watchdog start. Retain XIP with one minimal RAM-contained operation design; use full-RAM only if evidence shows less complexity. Four independently owned regions, explicit provisioning, fail-closed uncertainty, normal tested GC.
**Tech Stack:** C17, Python unittest, pinned Zephyr/NXP source copies, native_sim, ARM ELF.
**Spec:** User conditional approval in this chat, 24 October 2026; supersedes mandatory GPT2/MPU, GC pre-prediction and permanent-platform-limit wording in the earlier local proposal.

## Global constraints
- Software-only candidate: no production patch/layout adoption, upstream edits, hardware execution/program/reset/write/erase, PR merge, Issue closure or M4 completion.
- Maintenance-only is a limited M4 qualification profile, not Crux architecture; M7 live controller/network/storage workload remains unqualified.
- GPT2 and MPU are not mandatory: first establish actual early reset/watchdog state and clock/ownership feasibility. Unknown safety means refuse physical write.
- Existing portable contracts unchanged. Composition gates backend access; RAM-only validated Configuration set/get may remain available after maintenance, persistence load/save returns NOT_READY. All File operations that touch provider/media return NOT_READY; no live handles cross the transition.
- No autoformat or unqualified destructive recovery. Explicit provisioning only; normal representative LittleFS/NVS GC and remount tested using actual engines.
- Firmware [0,0x620000), LittleFS [0x620000,0x7ef000), scratch [0x7ef000,0x7f0000), NVS [0x7f0000,0x800000). Candidate only, no production devicetree changes.
- Two controller parameters and four scalar saves are trace data, not a capacity guarantee. Keep existing scan cost evidence.

## Review focus
- Native calls bypassing composition gate or leaking file handles past transition.
- Early clock/LUT/probe changes before RAM copy/vector/stack readiness.
- Initial inherited watchdog state changed before evidence capture, or debugger state mistaken for cold boot.
- Scratch overlap, owner crossing and image/programmer erase boundaries.
- Readiness gate blocking healthy internal mount/GC writes versus admitting uncertain storage.

### Task 1: Separate scratch ownership in experimental configuration
- [x] Add failing boundary tests: File end 0x7ef000, scratch end 0x7f0000; both crossing directions denied; owner-specific File requests cannot address scratch. Test candidate devicetree geometry.
- [x] Implement minimal owner fence and sample-only overlay, update candidate guards to four extents without altering production partitions.
- [x] Run existing request/image/linker and new tests, retain RED/GREEN evidence. Independent task review.

### Task 2: Maintenance composition and real-engine lifecycle
- [x] Test NOT_READY on persistent calls after one-way maintenance exit, RAM Configuration get/set still works, open File handles prevent exit.
- [x] Implement private candidate composition, not public contract change. Explicit provisioning permission separate from normal operation.
- [x] Verify two-key four-save healthy normal remount; representative actual NVS/LittleFS GC paths, reuse existing engine tests. No blanket recovery/whole-media claim.
- [x] Run applicable native/host tests and independent task review.

### Task 3: One target execution design and software closure
- [ ] Inspect exact reset/SystemInit/copy/clock/device-init ordering and hazardous transitive dependencies before implementation.
- [ ] Implement only justified isolated candidate early-entry/exception/stack path; TDD selected refusal/terminal cases. If an unresolved architectural prerequisite prevents safe implementation, record the exact blocker rather than invent hardware facts.
- [ ] Cross-build exact candidate; inspect final ELF, vectors, data/load/stack/callees and evidence of early guard ordering. Compare full-RAM only on demonstrated island complexity.
- [ ] Record remaining physical reset/watchdog/NOR/timer/master evidence, provisioning/recovery procedure and NOT READY verdict.

### Task 4: Integration and handoff
- [ ] Reconcile latest main UART-only delta without altering accepted dependent PRs. Run affected checks and obtain independent Reviewer and Challenger assessments.
- [ ] Focused Draft PR with approval conditions, exact tested source, manual gate commands, unresolved evidence and physical blockers. No merge or closure.

## Checkpoint outcome

Tasks 1 and 2 are complete for this software candidate. Task 3 has the reviewed pre-C snapshot and conditional read-only RAM context; actual mutating Flash paths, deadlines and operational target binding remain unfinished software. The target always refuses native Flash operations. Reviewer and whole-change Challenger PASS apply only to that declared slice. A focused stacked Draft PR is the current handoff; it does not finish the M4 Goal or approve physical activity. Latest-main integration has two additive component-selection conflicts, retained as an eventual integration prerequisite.
