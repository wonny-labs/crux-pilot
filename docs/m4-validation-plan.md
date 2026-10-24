# M4 pilot validation plan

Status: review proposal (revision 2, 24 October 2026). This plan refines **test methods**, not the existing M4 outcomes or acceptance criteria. Git history tracks revisions at this stable path. It does not authorise production Flash changes or physical operations.

## Objective and evidence standard

Demonstrate that the approved portable File and Configuration contracts work with their selected Zephyr providers and preserve representative data on RT1060-EVKB, without undertaking product-level storage qualification. Prefer existing evidence and focused regression tests over broad new test matrices.

Issues [#14](https://github.com/wonny-labs/crux-pilot/issues/14), [#15](https://github.com/wonny-labs/crux-pilot/issues/15) and [#16](https://github.com/wonny-labs/crux-pilot/issues/16) remain the authoritative requirements. Portable File contract Issue [#23](https://github.com/wonny-labs/crux-pilot/issues/23) requires its own evidence-based closeout. Keep accepted M3 results and independent M5 work intact.

## Smallest useful validation scope

| Level | Required pilot evidence | Not claimed |
| --- | --- | --- |
| Existing Host contracts | Reuse File/Configuration TDD, invalid/error/ownership tests, independent C17/C++17 consumers and source/build boundary checks | Native storage or physical durability |
| Actual Zephyr services with simulated Flash | Test each selected native File and Configuration provider, not merely a mock of the portable port. Cover representative success, one selected failure, space-full/reclaim where applicable and one bounded interrupted-operation/reinitialisation outcome per service. Reuse PRs #44–#46 for NVS fault and capacity evidence | Physical reboot, arbitrary power loss or production capacity |
| Approved RT1060 session | Confirm protected data-only Flash operation; run one File and representative BOOL/U32 save/readback; perform one declared **normal reboot** and verify both persisted outcomes; observe basic controller timing/health effects | Endurance, exhaustive hardware faults, arbitrary concurrent loads or power-loss safety |

Do not repeat large capacity sweeps on hardware. The PR #46 64 KiB NVS figures were workload-specific; they do not set a universal production parameter limit. Reuse existing tests when the selected source/configuration matches, and rerun affected evidence whenever it changes.

## Mandatory safety gate before physical QSPI write/erase

These conditions cannot be bypassed to accelerate a Pilot:

- **Decision and ownership:** Human approval of #14's medium, backend, partition ownership, representative capacity/headroom, native-remediation responsibility and uncertain-operation/recovery policy. The existing suggested addresses remain candidates until separately adopted.
- **Board and data preflight:** Identify the actual board, fitted Flash and existing data, backed-up recovery image, exclusive board owner and exact sacrificial Flash area. Never infer that an apparently unused partition is safe to erase.
- **Effective protection:** Verify the final image's ROM/header, ELF load ranges (including RAM load images), BIN, linker and programmer erase boundaries, as well as runtime Flash-request fencing. A test-only checker without effective enforcement is insufficient.
- **Actual write path:** Resolve discarded native write/erase errors and unsafe waits. Verify the **reachable first-write composition**, including code/data/veneer/startup and relevant interrupt, exception and DMA/bus-master activity, cannot fetch unavailable XIP Flash. A timeout while Flash remains busy must not return to ordinary XIP execution.
- **Controlled operating conditions:** Review interrupt-masked time, watchdog/health implications, provider synchronization and recovery behaviour. In particular, the current Host File spinlock around blocking I/O is **not** accepted as a Zephyr-target synchronization design. Establish a bounded first-write/abort/recovery procedure and obtain separate explicit hardware authority.

Scope the safety argument to the controlled Pilot composition and operation, not every conceivable future configuration. If a condition cannot be established, do not perform the physical write. Software-only implementation, inspection and simulator tests may continue independently within their approved limits.

## Focused software integration

**File (#15).** Implement the selected Zephyr VFS/LittleFS mapping behind `crux::file` while keeping mount/media/format decisions private to board/project composition. Verify a representative create/write/sync/read/remove/reopen sequence, a bounded full/reclamation case, an observable failure, and one simulated interrupted-write/reinitialisation outcome. Preserve tested failure handling for truncate, close and short I/O. Do not add a new VFS framework.

**Configuration (#16).** Implement the approved BOOL/U32 Configuration-to-Settings/NVS path. Verify defaults, validated update, save/load, one malformed/CRC or short-read case, an observable provider failure and one simulated interrupted-save/reinitialisation outcome. A hidden Settings callback error must **never** become public success or confirmed absence; do not publish unvalidated bytes. Use PRs #44–#46 and the pinned upstream source rather than repeating their investigation. Patch adoption remains a separate human decision.

Use existing Host/debug/sanitizer, native-free interface and targeted Zephyr tests. Only add cases for **demonstrated contract or integration gaps**. Software simulation of interrupted writes may satisfy the corresponding representative recovery evidence when the tested guarantees and limitations are explicit; it is not a physical power-cut test.

## One shared-board session, when authorised

M4 and M5 Codex work continues in parallel, but the RT1060 board has **one exclusive owner at a time**. M4 must schedule its approved session with the M5 owner and agree the image, preserved Flash areas, programmer behaviour and hand-back state. No concurrent programming, flashing, reset or cable operations.

1. Confirm approved image, safety evidence, backup, exact permitted erase/program region and stop/recovery rules. Execute the bounded first data-only write/readback after explicit approval, checking bootability.
2. Exercise a small File save/sync/readback and Configuration BOOL/U32 save through their **public APIs**.
3. Perform the separately approved, declared **normal reboot** and verify both File and parameter restoration. Record the reboot type, firmware and tested storage configuration.
4. Collect one representative storage-call duration and controller timing/health observation. Report what was observed, without claiming worst-case timing or unrestricted real-time concurrency.

Combine File and Configuration in the same session, with distinguishable results. Deliberate physical power interruption while Flash is busy, destructive fault injection, repeated power-cut matrices and long-term wear/endurance are **outside this Pilot's default scope** and require their own explicit approvals.

## Acceptance and closeout

Maintain a short matrix mapping each remaining #14/#15/#16 acceptance criterion to existing or new evidence, outcome, configuration and known limitation. Mark an AC complete **only if the stated outcome has been demonstrated**. If a simulator test cannot satisfy a particular criterion, raise a focused scope decision rather than silently reinterpreting it.

Before closing M4, confirm approved/reviewed design and enforced first-write protection (#14), File native integration with representative persistence and recovery (#15), and Configuration provider with valid error observability and representative persistence and recovery (#16). Merge and Issue closure remain separate human actions.

The [storage assessment](storage-configuration-assessment.md), [Flash safety handoff](flash-safety-acceptance.md), [Settings error investigation](settings-error-investigation.md) and [NVS capacity investigation](nvs-capacity-investigation.md) remain historical evidence. Do not rewrite their findings. The [Crux Development Plan](https://github.com/wonny-labs/crux-platform/blob/main/docs/platform-development-plan.md) and M4 milestone scope remain unchanged because this plan does not reduce M4 delivery outcomes.
