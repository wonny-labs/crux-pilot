# Stage 2 recommendation for #141

Date: 1 November 2026. Recommendations only. Nothing here adopts a bootloader, a Flash layout, a manifest change or an execution model. Row identifiers refer to [stage1-pinned-source-findings.md](stage1-pinned-source-findings.md) (A, B, L) and [stage2-matrix.md](stage2-matrix.md) (S2, V, R). Evidence labels follow [plan](../../plans/pre-m8-investigation-plan.md) section 5.2. No claim is verified by build or on hardware.

Notes added when the record was assembled: Stage 3 has since resolved the items marked "Stage 3 (pending)" as far as documents allow (see [stage3-summary.md](stage3-summary.md)). The owner approved A1 on 1 November 2026 with both trial builds, rank 1 and rank 2. That approval answers decision 2 in section 6. It does not adopt any recommendation here.

## 1. Owner constraint (expected result of Stage 2)

No stock MCUboot mode at the pin avoids NOR writes during XIP while still providing test, confirm and recovery.

| Path | NOR writes that remain | Rows |
| --- | --- | --- |
| Swap modes | App writes image, pending and confirm during XIP. BL rewrites both slots during its own XIP, twice if it reverts. | S2-2 to S2-6 |
| Overwrite-only | App writes image and pending during XIP. BL erases and copies the primary during XIP. No revert. | S2-8, S2-13 |
| Direct-XIP with revert | App writes image, pending and confirm during XIP. BL writes `copy_done` and erases unconfirmed images during XIP. | B28, B63, S2-14 |
| RAM load with revert | App writes image, pending and confirm, but executes from SDRAM. BL writes `copy_done` and erases unconfirmed images during XIP. | B61, B63, B65 |
| Plain direct-XIP or plain RAM load | No test, confirm or revert. BL writes only on failed validation. | B24, B60, B65 |
| microSD read by the BL | Can remove NOR from the update path, but needs bootloader customisation. | B74 to B77 |

This is recorded as a constraint for the owner: either accept bounded NOR writes by a stock bootloader, with the #22 closure work that implies (R2), or accept bootloader customisation in a later, separately approved scope. Stage 2 does not propose a custom design.

## 2. Candidate modes for the Stage 5 trial build

### 2.1 Ranked candidates

| Rank | Mode | Why | Main costs and risks | Rows |
| --- | --- | --- | --- | --- |
| 1 | RAM load with revert | Only stock mode where the App does not execute from NOR while it writes NOR. Provides test, confirm and revert. Nominal boot of a confirmed image reads only. Allows MCUboot image encryption. Close analogue of SDRAM execution loaded from block storage. | BL still writes `copy_done` per new image and erases a whole slot on revert, during its own XIP. Needs the share-data backend, a retained memory region and a `zephyr,bootloader-info` chosen node that the board lacks. BL RAM and image load address collide in SDRAM by default. No BL older-image rejection. Boot copy time not measured. | B58, B61, B63, B64, B65, B56, B60 |
| 2 | Swap using offset | Sysbuild default, so the smallest configuration change. Works with the existing partitions (no scratch). Test, confirm, revert and resume after an interrupted swap. Optional BL downgrade prevention. Keeps the current XIP execution model, so it is the comparison build for rank 1. | Heaviest BL NOR write set (two full-slot rewrites per update, one more per revert). App writes NOR during XIP, exactly the open #22 case. Not compatible with address-bound encryption. | A4, B11, B12, B18, S2-1 to S2-7, R2 |
| 3 | Direct-XIP with revert | Smallest BL write set among XIP modes with revert. The vendor SDK uses direct-XIP for this board (with a vendor flash-remap extension that stock MCUboot does not use). | Two builds per release, one per slot. No BL downgrade prevention. MCUboot image encryption not allowed. App writes NOR during XIP. | B14, B27, B28, B29, V1 |

The plan allows a second optional build. Building rank 1 and rank 2 gives one RAM-load and one XIP data point for size, configuration and ELF placement.

### 2.2 Rejected modes

| Mode | Reason | Rows |
| --- | --- | --- |
| Swap using move | Same behaviour as swap using offset with more erase cycles and one extra primary sector. No advantage on uniform 4 KiB sectors. | S2-9, S2-13 |
| Swap using scratch | Needs a devicetree scratch partition. Its stated benefit is heterogeneous sector layouts, which this NOR does not have. Adds scratch wear. | B5, B10, S2-13 |
| Overwrite-only | No revert. The old image is destroyed when the copy starts. Its strengths (BL erases older images, basis of the vendor BEE path) do not outweigh the loss of recovery. Keep it as a reference for the encryption question only. | S2-8, S2-13, B22, V2 |
| Plain direct-XIP | No test or confirm. A valid but faulty image keeps booting. | B24, B65 |
| Plain RAM load | Same gap as plain direct-XIP. It is the fallback if the share-data backend cannot be configured, and then needs a separate recovery argument. | B60 |
| Single slot, single app RAM load, firmware updater | No second application image for in-field fallback. | A3, B78 |

## 3. Recommended staging medium

**Variant (c): no microSD staging. The image goes into the inactive NOR slot.** For a network-delivered image, prefer receiving the whole image into an SDRAM buffer, checking size, signature and version, then writing NOR in one bounded phase with network activity quiesced.

| Variant | Recommendation | Reason |
| --- | --- | --- |
| (c) NOR slot, optionally via SDRAM buffer | Recommended for the trial and as the M8 reference candidate | Stock. No bootloader customisation. SDRAM capacity looks sufficient (32 MiB, images under 3 MiB). Separates network DMA from NOR program and erase. |
| (a) App copies from microSD into the NOR slot | Optional later variant | Gives resumable download and a persistent receive buffer. Removes no NOR write. Needs an owned staging path that #147 does not define, and inherits the open uSDHC, removal and pin-ownership items. |
| (b) BL reads from microSD | Not recommended within #141. Record as the only route that removes NOR from the update path | Needs bootloader customisation (flash-area backend or hooks, SD host driver, possibly a filesystem). Conflicts with #147 mount ownership and the FAT32 card model. |

Rows: B55, B74 to B79, R4, R5, R6, matrix sections 3, 6 and 8.

## 4. Recommended reference execution model for M8

**Recommendation: evaluate RAM load into SDRAM as the M8 reference candidate. Keep the standalone QSPI XIP image as the reference until M7 target validation is closed.** Confirm the RAM-load choice only after the Stage 5 build and Stage 6 analysis show no blocker. If they do, fall back to XIP with swap using offset.

Reasons:

1. It is the only stock path where App-side update writes do not happen during App XIP (matrix 4.1).
2. It is a close analogue of the SDRAM execution path kept open in [plan](../../plans/pre-m8-investigation-plan.md) section 1.4.
3. It keeps MCUboot image encryption available, which direct-XIP does not (B29, B58).

Conditions before adoption (owner decision A3):

| Condition | Rows |
| --- | --- |
| BL RAM separated from the image load window by configuration | B53 to B56 |
| Share-data backend and retained memory configured for the confirm path | B64 |
| App ELF shows no NOR-resident code, data, vectors or literals | B68, R2 |
| BL size fits the `mcuboot` partition or a revised layout | A9, Stage 5 |
| BL FlexSPI write path reviewed against the #22 matrix | R2, Stage 6 |

Impact on M6 and M7 evidence:

| Effect | Detail |
| --- | --- |
| Image identity | Any bootloader composition changes the App link address (slot or SDRAM), removes the App boot header and DCD, and changes the ELF and BIN hashes. Evidence tied to the standalone image hash does not transfer automatically. Rows A7, B68, R3. |
| Programming and debug | Scripts and procedures that program a standalone image at `0x60000000` would change. Two images (BL and signed App) must be programmed and identified. |
| Peripheral behaviour | Driver behaviour should not change, but this is an inference. A defined smoke subset would need re-running on the new composition. **Not verified.** |
| Timing-dependent evidence | Execution from SDRAM instead of cached QSPI and ITCM can change timing. Any timing or busy-wait evidence would need re-measurement. Which records depend on timing is **Not verified**. |
| SDRAM dependence | Already present (current RAM is SDRAM, R4). RAM load makes code depend on it too. |

Timing proposal: change the reference composition at the start of M8, after M7 target validation, so that no milestone's evidence is invalidated mid-way. This is a recommendation for Stage 6 to confirm.

## 5. What stays unknown

| Unknown | Resolved by |
| --- | --- |
| MCUboot size, RAM use and effective write alignment per candidate mode | Dev Container build (Stage 5, needs A1) |
| Whether MCUboot ships an RT1060 board overlay that changes `zephyr,sram` (B8) | Dev Container checkout and build (A1) |
| RAM-load App placement: nothing used at runtime in NOR | Dev Container build and ELF analysis |
| Whether the configuration-only fix for the SDRAM address conflict builds | Dev Container build |
| Share-data backend configuration and retained memory region | Dev Container build, then hardware |
| Which FlexSPI driver path the BL links, and its #22 closure | Dev Container build and Stage 6 ELF analysis. Closure is not expected within #141. |
| Boot ROM runs the BL's DCD. SDRAM state across the BL-to-App jump. RAM load copy time. Any revert, recovery or interrupted-update behaviour. uSDHC behaviour | Hardware, outside #141 scope |
| BEE key source and fuse needs, BEE region use with a BL. HAB open configuration. Boot ROM boot from microSD | Stage 3 (pending) |
| App-side signature and version check before NOR write | Needs an application crypto provider (#142, unscheduled) |

## 6. Owner decisions needed

1. Accept the constraint in section 1, or open a separate scope for bootloader customisation (variant b).
2. Approve the ranked candidates for the Stage 5 trial build (A1), including whether the optional second build (swap using offset) is wanted.
3. Decide whether RAM load into SDRAM should be pursued as the M8 reference execution model, and when the reference composition may change relative to M7.
4. Decide whether an App-side version policy (no older image, compared on major, minor and revision) is required, since the RAM-load BL provides none.
5. Before any network-facing update interface (M9): require authentication and authorisation of update requests, and an App-side check before any NOR write.
6. Whether the microSD may later hold a staging path (variant a), which would change the #147 card layout.
