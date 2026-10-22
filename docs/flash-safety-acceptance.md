# Software Flash safety acceptance and handoff

Scope: Issue #22 software investigation and verification only. Baseline is merged
PR #35, `32405ad79f08492f2bbd11d98ccf0d70af5c338e`; its reviewed head was
`e194cbf`, with original target/implementation evidence from `10b32a7`.
No driver, partition, backend, production safety policy or hardware operation is
adopted by this assessment. Issue closure remains a human decision after review.

## Acceptance assessment

| AC | Software acceptance evidence and outcome | Downstream gate |
| --- | --- | --- |
| 1. Failure/hazard dispositions | Satisfied: accepted [assessment](storage-configuration-assessment.md), [PoC](flash-safety-poc.md) and [phase matrix](flash-xip-evidence.md) connect discarded errors, nested waits, startup and XIP dependencies to experimental responses or explicit blockers. | Maintained native remediation and final failure/recovery policy require approval. |
| 2. Truthful errors and bounded strategy | Satisfied for evaluated experiment: 68 NOR combinations and three actual lower-wait scenarios; selected errors stop subsequent commands, uncertain XIP failures reach terminal before ordinary cleanup/return. Fresh focused harness passed. Count budgets are not elapsed-time bounds; terminal is not recovery. | FIFO/command-done execution coverage, qualified time source, part timing, recovery and final driver qualification. |
| 3. Placement and unresolved closure | Satisfied as an evidence inventory with recorded blockers: phase matrix distinguishes inherited ELF, PoC ELF and source inference, including startup, veneers/data, exceptions/IRQs and DMA. Selected PoC ELF positive/negative checks passed again. Complete closure is explicitly unknown. | Complete final-composition runtime/startup/data/exception/bus-master closure before physical writes. |
| 4. Candidate bounds and negative controls | Satisfied for proposed detection: 21 tests exercise ELF physical load bounds (including RAM VMAs), BIN/span/boot-data/header limits, owner/boot protection, alignment and overlapping hypothetical requests. Actual retained PoC ELF/BIN accepted. This is a test oracle, not installed linker/programmer/driver enforcement. | Adopted layout, oversized production-link rejection, production request fencing and programmer erase-preservation verification. |
| 5. Source/build/regression provenance | Satisfied: pinned source identities and historical cross-build remain identified separately; current Host build/CTest 42/42, build-tool tests 176/176, focused PoC harness and three retained-ELF tests passed. Native PoC code/configuration unchanged; no fresh Zephyr cross-build claimed. | Rebuild and qualify any subsequently adopted native implementation and storage composition. |
| 6. Handoff decisions/preconditions | Satisfied: the assessment and PoC specify board/part/data identity, backup, exclusive ownership, exact sector/operation/abort/recovery limits and separate preflight/write authority. | Approve and execute those procedures separately; no physical result is claimed here. |

These verdicts concern software evidence sufficiency, including explicit unresolved
blockers, rather than proof of usable writable XIP storage. Recommend Issue #22
closure only after independent review accepts this assessment. M4 and dependent
storage integration remain gated even if #22 is later closed.

## Candidate checks and reproduction

`tests/native/flash_safety/image_bounds.py` is a read-only, stdlib test helper.
It rejects nonempty file-backed PT_LOAD physical ranges outside candidate firmware,
including RAM-VMA sections with Flash load addresses; zero-file-size BSS contributes
no programmed bytes. It checks selected pinned FCB/IVT/boot-data fields and complete
BIN span. It does not compare ELF/BIN contents or validate every ROM field; retain
existing image/content checks for any actual download procedure.

The unchanged proposal is firmware `[0x60000000,0x60620000)`, LittleFS
`[0x60620000,0x607f0000)`, NVS `[0x607f0000,0x60800000)`. Request tuples use
absolute CPU addresses and exclusive owner regions. Overlap checks concern a
hypothetical disjoint allocation/request batch, not a legal sequential erase/write
transaction. No requests are dispatched. Erase alignment is 4 KiB; native write
granularity is one byte, not a guarantee that NOR bits can be overwritten.

```sh
python3 -m unittest discover -s tests/build -p test_flash_bounds.py -v
python3 tests/native/flash_safety/image_bounds.py --elf "$POC_ELF" --bin "$POC_BIN"
# Supported Linux environment; existing Host CI discovers the new tests:
python3 -m unittest discover -s tests/build -v
ZEPHYR_BASE="$PINNED_ZEPHYR" python3 tests/native/flash_safety/test_driver.py
python3 tests/native/flash_safety/test_elf.py "$POC_ELF"
```

Use the existing [PoC build instructions](flash-safety-poc.md#verification) to
produce the identified artifacts if absent. The checker does not execute a
programming tool. Deliberately oversized fixture inputs demonstrate rejection
by this candidate checker; the current production linker still permits all 8 MiB.
Programmer erase granularity, implicit recovery and preservation of data are
unverified. Neither a small valid BIN nor a partition label proves protection.

The [verification record](evidence/flash-acceptance/verification.json) identifies
checker/test hashes and counts. Committed logs normalise the RED workspace path
and trim Host-output trailing whitespace; originals remain local.

TDD [RED](evidence/flash-acceptance/bounds-red.txt) records the permissive initial
stub failing negative assertions; [GREEN](evidence/flash-acceptance/bounds-green.txt)
records all 21 methods passing. The [actual-image result](evidence/flash-acceptance/actual-image.txt)
uses the retained ELF SHA-256
`eb313d4c8df82e9c45b32be4bfadb02f16e4437742928f612621e2aec575fc0c`.
Existing [fault](evidence/flash-acceptance/poc-faults.txt) and
[ELF](evidence/flash-acceptance/poc-elf.txt) tests were rerun; source/config/target
behavior are unchanged. The [inherited pointer record](evidence/flash-acceptance/inherited-pointer-chain.json)
preserves the concrete Flash configuration dependency used by the phase matrix.

Linux tests ran in the existing amd64 development image with network disabled and
no device/socket/home mount. Host CMake used cached GoogleTest, then built and ran
CTest. Build-tool tests used a disposable writable source snapshot with a local Git
repository because the dependency tests intentionally create and remove probe files.
An earlier read-only mount failed those probes; an initial snapshot command also
excluded `tests/build` accidentally and failed discovery. Both were corrected without
changing tests. A macOS baseline run had two provenance errors from `/var` versus
`/private/var` resolution and two Linux-guard assertion failures. Those unsupported
environment results are preserved locally, not relabelled as passing. Linux results
are recorded separately. No unrelated portability repair was attempted.

## Decisions and ownership after handoff

- #14 retains medium/filesystem/backend/layout adoption: QSPI + LittleFS and
  separate NVS remain recommendations, with existing contents and exact fitted
  part unknown. This work does not repeat that selection.
- Approve native remediation ownership/upstream strategy and a final timeout,
  uncertain-state and recovery policy before production adoption. Retain or retire
  the opt-in PoC as appropriate; do not grow it into a second production stack.
- #15 owns file persistence integration/qualification; #16 owns Configuration and
  Settings behavior, capacity and recovery. Their affected approval gates remain.
- Physical preflight needs its own authority. Sacrificial-sector writes additionally
  require approved ownership, image/erase fencing, full closure, part timing,
  backup/sentinels, watchdog/bus-master policy and reviewed abort/recovery limits.

## Independent review

Separate-context Reviewer `/root/acceptance_independent` returned **PASS for the
software-only acceptance scope**, with no material revisions. The Reviewer read
the actual checker, tests, matrix, retained evidence and Issue #22 requirements;
independently reran the 21 bounds methods, actual retained ELF/BIN checker, three
ELF placement tests and four native fault methods. Additional independent
mutations of the actual RAM-VMA load's physical address, boot length and oversized
BIN were rejected by the intended guards. Full Host/build-tool results were
reviewed as retained logs, not independently re-executed by that Reviewer.

This author-maintained review summary is not a GitHub Approve event. Its PASS
concerns investigation/test evidence sufficiency with explicit downstream blockers,
not production safety or authority to merge, close the Issue or use hardware.
