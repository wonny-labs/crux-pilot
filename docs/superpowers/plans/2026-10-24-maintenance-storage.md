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


### Task 5: Approved 100 MHz FCB and consumed read-confirm preflight

User approved the100MHz software candidate in this chat after Reviewer/Challenger escalation. This does not adopt a production boot profile, select GPT2/another deadline source, operate hardware, merge or close anything.

**Files:** stage the pinned EVKB-B boot-FCB through `tests/native/flash_target_candidate/prepare.py` or a small sibling; modify only experimental sample CMake/private profile code. Add focused tests under `tests/native/rom_storage_candidate/`. Preserve accepted base PoC and pinned upstream unchanged.

- [x] First failing tests: stage FCB copy at100MHz and retain all other fields/LUT bytes; reject wrong source/unsafe publication. Compile-time witness serial_clk_freq offset and enum, exact finalELF/BIN field check.
- [x] Consume a small private pure-C profile predicate from actual sample preflight (not an unused abstraction). Read-confirm raw controller geometry/read-LUT/selected command slots and derived clock under declared104MHz runtime ceiling. Actual register-derived frequency is not a physical frequency measurement. Unknown/mismatching/disabled/unsafe state refuses. Do not trust the FCB constant as actual register state.
- [x] Reuse FCB READ0/status1/WREN3/sector5/page9; **no commands or LUT/clock/mode writes in this task**. Identity9Fh, actualWIP and timer remain pendingTask6. Do not seed a JEDEC slot without verified ROM slot/ownership reasoning.
- [x] Use existing pinned NXP facilities for read-only clock derivation if possible, outside the RAM busy window; no Zephyr kernel/timer abstraction or GPT2/DWT adoption. Validate precise API/divider semantics before use. Restrict source/config as currentcandidate does.
- [x] Test wrong selector/geometry/LUT/clock/disabled inputs and positive declared profile; then implement minimal code. Crossbuild exactEVKBBcandidate and inspectFCB100, copiedsource identity andpreflight placement/order. Existing targetinit/media calls remain refused at this checkpoint; no claim of mutating-path completion.
- [x] Scoped independent review before nexttask. Retain RED/GREEN, artifact/input hashes and exact reproducible manualgate. No unrelated whole-suite rebuild.

### Task 6: Real command path inside selected RAM context (after Task5)

Continue the approved100MHz design through native helper/sequence adaptation, software MEMC bookkeeping without set_device_config/reclock, sector-onlyerase, explicit identity/status read-confirm, typed one-context command execution, success-ready buffer refresh versus RAM uncertainty disposition and actual lower error/deadline test paths. Reuse existing helpers/SDK rather than create a general Flash driver. GPT2 and any production elapsed-time source remain unselected; target must refuse commands when required time/identity/safety qualification is absent. Do not relabel source-only/controlled-time tests as target qualification. This is still unfinished, not satisfied by read-only preflight.

### Task 6 bounded outcome

The private selected native command/helper slice is implemented and independently reviewed at `7ef53d9`: controlled effects, exact image and 46 focused checks pass. Target submission and ordinary native init/media operations remain refused. This completes that bounded software experiment, not full Task 3 execution safety or target File/Configuration binding. A real elapsed-time source and lower-wait design, identity/protection/slot policy, runtime verification of added relocation regions and hardware prerequisites remain unresolved.

Disposable integration of the PR #62 checkpoint with accepted main `bf048c1` preserved the two additive module selections and passed 66 designated source checks, 115 Host tests and nine native maintenance cases, with independent review. It did not merge parent PRs or test this later command image as an integrated operational target. Preserve that distinction before main acceptance.

### Task 7: Verify actual relocated command regions before entry

The existing approved RAM safety design requires code/data verification. Add consumed comparisons using actual relocation symbols before context entry: immutable text/rodata each entry; initialized and mutable data only at a justified lifecycle phase, without overwriting live state. Reject malformed extents and mismatches. TDD actual consumed verification plus linked-region/order evidence. Preserve target refusal, 100 MHz FCB and all unselected timer/identity gates. No new loader, cryptographic framework or hardware operations. Independent review of the delta and final image is required.

### Tasks 8–9: Approved GPT2 and temporary identity-slot software candidates

User explicitly approved both independently reviewed next-safety proposals in this chat. This selects software experiments only: target admission remains refused; production adoption, hardware/register operations, merges and closure remain unapproved.

Task 8 implements an isolated GPT2 oscillator-backed candidate using native facilities, explicit source/free-running/prescaler/progress/wrap/expiry semantics and selected lower-wait hooks. Do not trust nominal DTS25MHz or adopt fitted-part deadlines from incomplete sources. The real target admission/elapsed qualification leaf remains fail-closed. TDD actual candidate/helper effects, affected native regressions and new ARM graph/data audit; scoped independent review.

Task 9 follows Task8 review: prove full inherited LUT/port/AHB-read-write/IP/exclusive use before selecting an experimental9F slot; save/verify/restore slot and applicable lockstate only with knownNORready/controlleridle. Uncertainty staysRAM; BootFCBunchanged. TDD actualnativehelper/stagedSDK effects, failure/restore checks and finalARMproof; independentReviewer/Challenger. No production layout/backend/public API. Final focused Draftupdate after both tasks, reusing prior evidence and running affected gates only.

### Task 10: Approved isolated RT1060 File/Configuration software integration

User approved the independently reviewed target-integration-decision.md in this chat. This is sample-only software connection, not actual target command admission, production provider/patch/layout adoption or hardware permission.

**Files:** existing components/file/ports/zephyr/{CMakeLists.txt,file_native.c}; components/configuration/ports/zephyr/configuration_settings.c; sample-owned flash_target_candidate composition/config/adapter and focused tests; reuse tests/native/maintenance_storage and controlled Settings/NVS pipeline. Preserve public contracts and existing general Kconfig defaults.

- [ ] TDD restrictive selection/ABI: ordinary builds remain simulator-only; exact experimental EVKB-B sample may connect existing providers. Unsupported board/profile refuses. Keep native_sim width/non-PIE flags off ARM, preserve public native-free C17/C++17 consumer targets.
- [ ] TDD truthful command read-result delivery and bounded native Flash read/page-write/4KiB erase adapter with region/owner/error checks. Reuse staged native helpers and island dispatcher, not a general driver or new NOR emulator. Partial failures must not claim complete success or rollback. Actual crux_rom_command_submit and elapsed qualification remain constant refusal.
- [ ] Defer backend/native Flash initialization until private maintenance ownership and command prerequisites; refuse device/media access before readiness. Account for Settings/NVS/LittleFS mount-time mutation and explicit provisioning. Reuse controlled pinned corrections/readiness validation, no upstream edits. No autoformat/unknown recovery.
- [ ] Compose sample-private fixed File + Configuration trial with existing APIs and one-way maintenance exit, consuming close/fence semantics, two keys/four scalar saves, representative file sync/close/read and remount evidence. Positive behavior uses existing real engine simulators/controlled leaves. Main must not admit target commands or execute a writable trial under this approval.
- [ ] Run affected native/provider/maintenance tests, unsupported-profile checks, ARM build + independent C/C++ ABI/boundary checks. Inspect all newly reachable RAM functions/data/waits and retain exact artifact/input hashes plus reproducible commands. Reuse unchanged previous evidence and do not repeat broad unrelated suites.
- [ ] Self-review and commit focused implementation; independent task spec+quality review and final Challenger, address material findings, then focused stacked Draft PR with exact software limitations and remaining physical/admission gates. No merge/closure.
