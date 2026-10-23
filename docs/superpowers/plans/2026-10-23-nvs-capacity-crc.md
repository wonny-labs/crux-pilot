# Candidate NVS capacity, CRC and remount verification plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan inline. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Measure the approved candidate 64 KiB BOOL/U32 workload through actual Settings/NVS and verify CRC/full-read and fresh-backend remount behavior.

**Architecture:** An isolated native_sim application uses unmodified pinned Zephyr Settings/NVS and simulated Flash. A test-only chosen partition at simulated offset 0x100000 has 16 × 4096-byte sectors. No PR #44/#45 candidate patch or production composition is selected.

**Tech Stack:** Zephyr 4.4.0 `684c9e8f32e4373a21098559f748f06915f950c9`, native_sim host toolchain, ztest/Twister, C17 golden-vector test.

**Spec:** User approval of candidate 64 KiB BOOL/U32 capacity/CRC/remount software verification; `docs/configuration-contract.md` canonical encoding; the capacity/CRC/restart row of the existing actual-engine plan (local handoff).

## Global constraints

- Baseline main `ca7655b9ebce6debe444456eb0c4197a0f76f838`; preserve pinned sources.
- Software simulator only: no hardware, patch/backend/layout adoption, merge or issue closure.
- CRC off/on are independent freshly initialized simulator profiles, not migration approval.
- NVS lookup cache 256 entries; Settings name cache off; bad-region reinitialization off.
- Reuse existing tooling and owned read-only-mounted container. Final independent Reviewer/Challenger required.
- Inline authoring is the existing execution method; no per-task implementer agents.

## Review focus

- Raw 65536 bytes versus acknowledged payload and Settings name/count overhead.
- Capacity failure may have partial effects; verify only acknowledged records and record failures honestly.
- Remount rebuilds volatile backend state with retained simulator bytes; not physical reboot/power-loss evidence.
- Partial reads skip NVS data CRC; observe full native and Settings callback results separately from outer wrapper status.
- Names/values/CRC, bounds and cache settings are workload-specific; no universal capacity or timing promise.

### Task 1: Reproducible native workload

**Files:** `tests/native/nvs_capacity/{CMakeLists.txt,prj.conf,app.overlay,testcase.yaml,main.c,records.c,records.h,test_records.c}`.
**Interfaces:** Test-only `size_t probe_record(unsigned index, unsigned revision, uint8_t wire[8])` emits version1/schema1 BOOL (5 bytes, even indices) or U32 (8 bytes, odd indices); no public API.

- [x] Write golden-vector assertions before implementing the generator; observe missing implementation RED, then implement and run strict C17 GREEN.
- [x] Add native assertions: actual configured 16-sector area; mixed 64-key write/update/remount; bounded saturation up to 4096 keys, acknowledged-value verification; deletion/reclamation/remount; CRC-corrupted payload full versus partial reads before/after remount, including actual Settings callback result.
- [x] Use real Settings names `cfg/p%04u`, values from the test generator and exact known expected bytes; no NVS API doubles.
- [x] Clear only owned simulated region between tests. Rebuild backend NVS/count state without erasing its media for remount; preserve registered store addresses. Reject unexpected records/lengths and report callback errors independently of wrapper status.
- [x] Run both CRC configurations with Twister. Expected: executed cases pass, capacity observed before the finite bound, acknowledged records survive. Any genuine native failure is evidence to report, not permission to patch native code.
- [x] Run format/dependency guards and commit the focused fixture after verification.

### Task 2: Evidence, review and handoff

**Files:** `docs/nvs-capacity-investigation.md`, `docs/evidence/nvs-capacity/`.
**Interfaces:** Consume Task1 actual Twister reports, simulator geometry/config, source/build hashes and handler logs.

- [ ] Retain measured capacity, failed operation/result, payload bytes, reclaim/remount counts, CRC full/partial/callback results and exact commands/configuration. Do not aggregate profiles into universal capacity.
- [ ] Identify limitations and #14/#15/#16 production/physical gates; a passing investigation does not qualify native integration.
- [ ] Obtain fresh independent Reviewer and Challenger; resolve material findings and re-review under repository requirements.
- [ ] Prepare focused Draft PR if the repository changes are retained; no merge/closure. Standard CI remains distinct from this manual simulator gate.
