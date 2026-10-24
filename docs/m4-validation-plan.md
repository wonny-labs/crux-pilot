# M4 pilot validation plan

Status: proposed for review. This plan defines the minimum evidence for the existing M4 acceptance criteria. It does not change those criteria, select a production storage layout, or authorise hardware operations.

## Purpose and scope

Qualify a usable File and Configuration path on RT1060-EVKB without turning Crux Pilot into product-level storage qualification. Use the smallest representative tests that establish the declared behaviour and preserve mandatory Flash/XIP safety gates. Issues [#14](https://github.com/wonny-labs/crux-pilot/issues/14), [#15](https://github.com/wonny-labs/crux-pilot/issues/15), and [#16](https://github.com/wonny-labs/crux-pilot/issues/16) retain ownership of their outcomes and acceptance criteria.

## Allocate evidence by what it can prove

| Evidence | Pilot validation | Limitation |
| --- | --- | --- |
| Host tests and independent C17/C++17 consumers | File/Configuration API contract, argument, ownership, invalid data and deterministic failure paths | No native persistence, device timing or power-loss claim |
| Zephyr software tests with actual VFS/LittleFS and Settings/NVS over simulated Flash | Selected mount/read/write/sync, capacity-full and reclamation, error propagation, CRC, and one bounded interrupted-operation/reinitialisation scenario for each affected service | Simulation is not a physical reboot or a guarantee against arbitrary power loss |
| RT1060-EVKB after separate approval | Protected data-region operation, a representative File and BOOL/U32 Configuration save/load, one declared normal reboot, and basic controller execution impact | No exhaustive load, endurance, fault-injection or field-reliability qualification |

Reuse accepted Host tests, File and Configuration contract evidence, and the bounded Settings/NVS investigations in PRs #44–#46. The existing NVS capacity/remount tests do **not** test File/LittleFS or interrupted-write recovery. Add only the missing scenarios relevant to the selected provider. Rerun affected checks when code, configuration or the pinned upstream baseline changes.

## Mandatory gates before any physical QSPI program or erase

These are safety prerequisites, **not optional product-level tests**.

- Record the #14 human decisions on storage medium, File and Settings backends, layout, capacity headroom, native remediation ownership, and uncertain-state/recovery policy. Candidate offsets are not adopted partitions.
- Verify the actual board and fitted Flash, existing contents, ownership, backup and a recoverable firmware image. Approve the exact sacrificial data area and its erase/program limits. Never infer that an unused label or address range is safe to erase.
- Enforce final-image ROM/header, ELF load-address, BIN, linker, Flash-request and programmer erase bounds so firmware and boot data cannot be overwritten.
- Resolve truthful native driver errors and bounded/terminal failure handling. Demonstrate the final composition's XIP call/data/startup/interrupt/exception/DMA/bus-master safety while Flash is unavailable. A timed-out but still-busy Flash is not safe for ordinary XIP return.
- Review watchdog, interrupt masking and controller execution effects. Use a separately approved bounded first-write procedure with abort/recovery conditions. No automatic format, whole-device erase or unapproved physical reset.

Software-only implementation, simulated fault injection and cross-build work may proceed before these physical gates, within their existing approvals.

## Minimum software verification still required

- **File (#15):** Exercise the actual selected Zephyr VFS/LittleFS software path, including mount, create/read/write/sync/remove, full/reclaim, and a representative failure with observable result. Run one bounded interrupted-write/reinitialisation case with simulated nonvolatile bytes. Keep failed-close/truncate lifecycle and storage errors observable. The existing Host File tests alone are insufficient.
- **Configuration (#16):** Exercise the actual Settings/NVS provider path and approved Configuration contract with BOOL/U32, defaults, validation, save/load, corrupted and short records, and provider errors. A CRC or callback read error must not be reported as success or confirmed absence, and unvalidated bytes must never become active. Demonstrate one bounded interrupted-save/reinitialisation outcome without claiming multi-key atomicity. PR #46's remount/CRC evidence is reusable but does not close these gaps.
- Keep native-free boundary, Host regression, sanitizer and relevant Zephyr checks. Do not replay broad test matrices merely because they exist. A known failure in the pinned backend remains a blocker for adopting the affected provider.

## Minimum approved physical session

Only after all relevant safety decisions, software prerequisites and **explicit board-operation approval**:

1. Verify the approved image, programmer settings and exact protected/owned Flash range. Perform a strictly bounded first erase/write/readback only in the authorised data area, and confirm the board remains bootable.
2. Exercise one representative File scenario through the public API. Create, write, sync, close, reopen and verify content, then verify the planned delete path. Save representative valid BOOL and U32 parameters through the Configuration API.
3. Perform a declared **normal reboot** and verify the expected file and saved parameters are restored. Record the exact reboot type and configuration. A single combined session may cover both capabilities while retaining separate results.
4. Record representative storage-call duration, errors, basic controller timing and watchdog/health observations under stated conditions. Do not claim unrestricted concurrent or real-time operation from this sample.

Do not deliberately interrupt power, reset the CPU while Flash is busy, corrupt physical storage or force hardware faults as part of this minimum session. Such trials require their own safety justification and approval.

## Acceptance mapping and reporting

| Issue | Software evidence | Physical evidence |
| --- | --- | --- |
| #14 — Storage decision and safety | Approved design, final image and native-driver/XIP/bounds checks, safe procedure | Actual part/data identity and authorised first-write preflight; safety requirements remain mandatory |
| #15 — File Persistence | Provider behaviour, storage-full/reclamation, fault and bounded interruption/reinitialisation evidence | Safe File operations and retained content after the declared normal reboot |
| #16 — Configuration | Portable behaviour plus actual Settings/NVS error handling, corruption, bounded interruption/reinitialisation and recovery | Representative BOOL/U32 saved and restored after the declared normal reboot |

Record commit/firmware identity, selected backend and region, test conditions, actual results, limitations and which AC each result supports. **Only mark an AC complete when its stated outcome has evidence.** Do not present simulator recovery as physical interrupted-write or power-loss qualification.

## Outside the pilot's default qualification scope

Exhaustive power-cut/recovery combinations, long-term wear/endurance, large fault-injection matrices on the board, worst-case concurrent workload coverage, certification and production lifetime guarantees are deferred. They must not be claimed as validated. If a required M4 acceptance outcome cannot be met with this bounded plan, raise a specific scope/AC decision instead of silently dropping it.

The [storage assessment](storage-configuration-assessment.md), [Flash safety acceptance](flash-safety-acceptance.md), [Settings investigation](settings-error-investigation.md), and [NVS capacity investigation](nvs-capacity-investigation.md) remain historical evidence. Do not rewrite their findings to match this plan. The cross-repository [Crux Development Plan](https://github.com/wonny-labs/crux-platform/blob/main/docs/platform-development-plan.md) remains unchanged because M4 delivery and exit outcomes are not being reduced.
