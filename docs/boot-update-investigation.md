# Boot and update investigation for RT1060-EVKB

| Field | Value |
| --- | --- |
| Issue | [#141](https://github.com/wonny-labs/crux-pilot/issues/141) "Investigate verified boot and firmware update feasibility for RT1060-EVKB and RT1170-EVK" |
| Status | **Draft. Desk results and build-only trial results.** Stages 0 to 4 and 7 are desk results. Stages 5, 6 and 8 record the A1 trial builds of 2 November 2026 for RT1060-EVKB (cross-compilation only, results archive SHA-256 `8e2e9839eae863b79a42a7a294a97e39f093a83c3f48d0598f9fb582f01cd3ed`). Stage 7 (RT1170-EVK Rev A1) is a desk study only. No RT1170 build was run. No hardware or fuse operation was performed. Nothing in this record is adopted except the owner decisions listed under [Approvals](#approvals) and [Owner decisions needed](#owner-decisions-needed), which include the acceptance of 7 November 2026 for rows 1, 3, 4, 5, 6, 8, 9, 10 and 11. Revised after independent review 1 of PR #151 (verdict REVISE, findings F1 to F15) and independent review 2 (verdict PASS, optional findings N1 to N5). Stages 5, 6 and 8 were added after review 2 and have not had an independent review. |
| Target | Primary target and the only board for physical validation: RT1060-EVKB Rev B, `mimxrt1060_evk@B/mimxrt1062/qspi`. Secondary, desk and build-only: RT1170-EVK Rev A1, `mimxrt1170_evk@A/mimxrt1176/cm7` (owner decision of 2 November 2026). RT1170 hardware verification is deferred and tracked in [#155](https://github.com/wonny-labs/crux-pilot/issues/155). |
| Pinned upstream | Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0). MCUboot `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2`. `hal_nxp` `2c2f28ac333e995d7279777409817c4b4f92c1ec`. |
| crux-pilot read | `f67e22a` (Stages 0, 3 and 4) and `f57ff22` (Stage 2, after #147 merged). The trial builds of Stages 5 and 8 used `1d6ac4f`, an older ancestor of `f57ff22` (see [Stage 5](evidence/boot-update/stage5-trial-builds.md#1-inputs-and-provenance)). |
| Plan | [Pre-M8 investigation plan](plans/pre-m8-investigation-plan.md) |
| Evidence | [docs/evidence/boot-update/](evidence/boot-update/README.md) |

## Purpose and scope

#141 asks for the technical basis for verified boot and firmware update on
RT1060-EVKB Rev B before any implementation is planned. Its results inform the public
milestones M8 "Boot platform foundation and verified boot" and M9 "Secure
firmware update and recovery". It does not meet any M8 or M9 criterion.
RT1170-EVK Rev A1 is a secondary, desk and build-only item, and its hardware
verification is deferred to [#155](https://github.com/wonny-labs/crux-pilot/issues/155).

| #141 scope item | Covered by | State in this draft |
| --- | --- | --- |
| 1. MCUboot against the adoption criteria | Stages 1, 2, 3 and 5 | Desk results recorded. Size and RAM cost Verified by build for both trial modes (Stage 5). |
| 2. Update mode and staging medium | Stage 2 | Recommendation recorded |
| 3. Flash writes during XIP for the candidate mode | Stages 2 and 6 | Write inventory by actor and phase recorded for both trial modes (Stage 6). Bootloader write-path closure is not achieved: a #22 data-closure failure was found in the bootloader write path. Full closure is Not verified. |
| 4. Current placement and a fenced layout | Stages 4 and 6 | Placement re-verified from committed records and one baseline rebuild. Fenced layout proposal recorded (Stage 6). Not adopted. |
| 5. Record the RT1170-EVK Rev A1 layout and boot differences (desk and build-only) | Stage 7 | Desk study recorded. RT1170 build-only items (sysbuild configure, bootloader cross-build) not done. Hardware verification deferred to #155. |
| 6. HAB open configuration without fuse writes | Stage 3 | Documentation result recorded. RT1060 behaviour not verified. |
| 7. Reproducible bootloader and signed image cross-build | Stages 5 and 8 | Done as build-only evidence for RT1060: unsigned binaries reproduce byte for byte, all signed images verify against the documented public key (Stage 8). |

Out of scope: any hardware programming, reset, erase, debugger download or fuse
operation, production keys, implementation of update, image-state or recovery
features, bootloader customisation, upstream source changes, and adoption of a
manifest change, Flash layout or bootloader.

Many controller designs execute the application from external SDRAM loaded
from block storage and boot from eMMC. The pilot does not validate these paths.
The investigation keeps them open by avoiding choices that would rule them out,
and evaluates a close analogue on the evaluation kit where one is cheap: MCUboot
RAM load for SDRAM execution, and microSD on uSDHC for eMMC.

## Approvals

| ID | Approval | State |
| --- | --- | --- |
| A0 | Start the #141 work package: desk work, decision record and Draft PR | Given by the owner |
| A1 | Fetch MCUboot at the pin outside the repository, install imgtool dependencies in a separate virtual environment, generate development keys outside the repository | Given on 1 November 2026, for two RT1060 trial builds: rank 1 (RAM load with revert) and rank 2 (swap using offset). Used on 2 November 2026 for both builds (Stages 5, 6 and 8). Extended by the owner on 2 November 2026 to an RT1170 sysbuild configure and bootloader cross-build (owner decision 14). The RT1170 extension has not been used yet. |
| A2 | Licence review before any NXP signing tool is downloaded (CST, SPSDK, SEC) | Given by the owner for the SPSDK download (recorded on 2 November 2026). Not used yet: no SPSDK result is in this record. Stage 3 stayed documentation-only. CST and SEC are not covered. |
| A3 | Gate 1: accept or reject the recommendations in this record | Accepted in part by the owner on 7 November 2026, after independent review: for rows 1, 3, 4, 5, 6, 8, 9, 10 and 11 of the owner decisions table. Rows 12 and 15 are not decided. Recommendations that are not decision rows, such as the update mode ranking, the staging medium and the signing tools, are not covered by this acceptance. |
| A4 | Gate 2: merge the PR | Pending. It follows the update of this record with the A3 acceptance and a narrow independent review of that update. The owner is told before the merge. |
| A5 | Any manifest, module or Dev Container change | Not part of this investigation |

## Evidence labels

Labels are used exactly as each stage recorded them. They are never upgraded
in this summary.

| Label | Meaning |
| --- | --- |
| Verified on pinned source | Checked at a cited file and line of a pinned revision. Shows what the code allows, not what a board does. |
| Verified by public vendor document | Stage 3 only. A public vendor page or repository says so. It is the vendor's statement, not a Crux result. Where a document was read only through a summarising fetch, the label says "summarising fetch, not a full read". |
| Verified by artefact | Stage 4 only. Read from a committed build output, record or map. |
| Verified in repository | Stage 7 only. Read from a committed crux-pilot document. |
| Inference | Reasoned from verified lines. |
| Not verified | Not checked, not accessible or inferred. The reason is given in the evidence file. |
| Verified by build | Stages 5, 6 and 8. Read directly from the A1 trial outputs (`.config`, devicetree, linker output, map, ELF, signed image, `imgtool` output), with the results archive SHA-256 `8e2e9839eae863b79a42a7a294a97e39f093a83c3f48d0598f9fb582f01cd3ed` as provenance. Shows that an image configures and links, not that it boots. Stage 6 also uses a small static disassembly of the archived ELF files, made outside the Dev Container and labelled as such. |

The [evidence index](evidence/boot-update/README.md#evidence-labels) lists all
labels, including the Stage 4 label "Verified on source". The plan defines only
three labels and classes vendor documentation as Not verified. The extension is
recorded in [Corrections to the plan](#corrections-to-the-plan).

## Method

| Item | How it was done |
| --- | --- |
| Pins | Zephyr and `hal_nxp` from crux-pilot `west.yml` and the pinned Zephyr `west.yml`. MCUboot from the pinned Zephyr `west.yml` lines 337 to 341. |
| Upstream source access | The GitHub contents API was refused for the upstream repositories. Files were read as raw files at the pinned commits. Stage 3 also used read-only blobless Git fetches of the pinned commits, made outside the repository, for tree listings. Local copies are not committed. |
| Limits of absence claims | Stage 1 had no directory listing or code search. A claim that something is absent is limited to the files named in the row. Stage 3 added file-name listings, not content searches. |
| NXP documents | Documents on `www.nxp.com/docs/` could not be read from the environment. The reference manual, security reference manual and application notes are cited by title only and are Not verified. The EVKB Board User Manual Rev. 0 (Tables 9 and 10 only) was later read by a parallel desk study through a summarising fetch, not as a full read. Public NXP repositories (SPSDK, MCUXpresso SDK examples) and public web pages were read on 1 November 2026. |
| crux-pilot records | Read with `git show` at `origin/main`. The latest `main` CI artefact (run 41351833959) was listed but its download was refused, so it was not read. |
| Stage 1 second check | The plan asks for a second agent to re-read each cited Stage 1 line. No such separate re-check is recorded. Stage 1 itself re-checked the plan Appendix A rows. Independent review 1 of PR #151 spot-checked 47 rows against the pins: 45 matched as worded, and 2 (B63, B68) had correct lines applied to the wrong mode. Both are now corrected. This is partial cover, not the planned full re-check. |
| Builds, tools, hardware | Stages 0 to 4 and 7: no build was run. Stages 5 and 8: two sysbuild trial builds, their rebuilds and a baseline rebuild, run on 2 November 2026 in a container created from the repository Dev Container image, with MCUboot and `imgtool` 2.4.0 from a pinned clone outside the repository (A1). No NXP tool was downloaded. No licence was accepted. No hardware was used. |

| Stage | Evidence |
| --- | --- |
| 0 Baseline | [stage0.md](evidence/boot-update/stage0.md) |
| 1 MCUboot capability on pinned source | [stage1-pinned-source-findings.md](evidence/boot-update/stage1-pinned-source-findings.md), [stage1-answers.md](evidence/boot-update/stage1-answers.md) |
| 2 Update mode and staging | [stage2-matrix.md](evidence/boot-update/stage2-matrix.md), [stage2-recommendation.md](evidence/boot-update/stage2-recommendation.md) |
| 3 HAB, BEE, boot media, signing tools | [stage3-findings.md](evidence/boot-update/stage3-findings.md), [stage3-summary.md](evidence/boot-update/stage3-summary.md) |
| 4 Placement | [stage4.md](evidence/boot-update/stage4.md), [placement-rt1060.json](evidence/boot-update/placement-rt1060.json) |
| 5 Trial builds | [stage5-trial-builds.md](evidence/boot-update/stage5-trial-builds.md), inputs and text outputs in [trial-inputs/](evidence/boot-update/trial-inputs/) |
| 6 Layout and write analysis | [stage6-layout-and-write-analysis.md](evidence/boot-update/stage6-layout-and-write-analysis.md) |
| 8 Reproducibility and signing | [stage8-reproducibility-and-signing.md](evidence/boot-update/stage8-reproducibility-and-signing.md) |
| 7 RT1170-EVK Rev A1 differences | [stage7-rt1170-delta.md](evidence/boot-update/stage7-rt1170-delta.md) (desk only, read at crux-pilot `main` `47da9e5`) |

Row identifiers in brackets below (for example B61 or S2-4) point to the
evidence files. A, B and L rows are in the Stage 1 findings. S2, V and R rows
are in the Stage 2 matrix. Stage 3 claims are written as "Stage 3 A13" and so
on.

## MCUboot against the adoption criteria

| Criterion | Result | Label |
| --- | --- | --- |
| Sysbuild integration | Sysbuild offers MCUboot for every board and builds it as a separate image from MCUboot `boot/zephyr/` (B1, B2). The board QSPI devicetree has `mcuboot`, `image-0` and `image-1` partitions and no scratch partition (A9, B5). MCUboot lists `mimxrt1060_evk/mimxrt1062/qspi` as a sample target, which means the default revision C (A2). MCUboot has an i.MX RT FlexSPI branch (B6). MCUboot has no RT1060 board file at the pin (Stage 3 D11, confirmed by the trial). Both trial builds for `mimxrt1060_evk@B//qspi` configure and link through the external module path, without a manifest change or source patch (Stage 5). | Source rows Verified on pinned source. Trial builds Verified by build. Boot Not verified. |
| Modes on XIP NOR | Sysbuild lists eleven modes, not nine as the plan said (A3). The default is swap using offset (A4, B11). Swap using scratch needs a devicetree change. | Verified on pinned source |
| SoC XIP encryption (BEE) | No BEE handling in the MCUboot and Zephyr files read (A8, B72, B73). MCUboot image encryption is a different feature: it protects a staged image at rest (B70). See [BEE](#bee-xip-encryption). | Verified for the listed files. Whole-tree absence Not verified. |
| Validation on every boot | On by default in swap modes (`BOOT_VALIDATE_SLOT0`). Unconditional in direct-XIP and RAM load. RAM load validates the copy in RAM (A5, B57). | Verified on pinned source |
| Crypto backend without Zephyr crypto modules | ECDSA P-256 with tinycrypt from MCUboot's own `ext/` directory needs no Zephyr crypto module. The sysbuild default is RSA, which needs Zephyr mbedTLS, so ECDSA P-256 must be set explicitly (A4, A6, B31, B32, B35). | Verified on pinned source |
| RAM and Flash cost | MCUboot Flash: 51,760 B (RAM load with revert) and 53,940 B (swap using offset) of the 128 KiB `mcuboot` partition, headroom 79,312 B and 77,132 B. MCUboot RAM: 14,656 B in OCRAM2 and 27,904 B in SDRAM. ITCM 8,544 B in both. Trailer estimates from MCUboot CMake: 198 B image footer and 48 B upgrade footer for RAM load, 4,246 B and 4,096 B for swap using offset. Write alignment 1. Boot time and run-time stack are not measured. | Verified by build on the trial tree `1d6ac4f`, not `f57ff22` (Stage 5 sections 1, 4 and 9) |
| Test, confirm, recovery and older-image rejection | Per mode, see the next two sections. | See below |
| Licences | MCUboot Apache-2.0 (L1). `ext/tinycrypt` BSD-3-Clause and BSD-2-Clause by licence text (L2). `ext/mbedtls-asn1` files carry Apache-2.0 tags, but no licence file was found for the directory (L3). | Text and tags Verified on pinned source. Identifiers for tinycrypt are assigned from the text. |

### Older-image rejection and its comparison basis

The comparison is major, then minor, then revision. The build number counts
only with `BOOT_VERSION_CMP_USE_BUILD_NUMBER`. Equal versions compare as equal
(B15, B16). All rows are Verified on pinned source.

| Mode | Rejection by the bootloader | Equal values | Rows |
| --- | --- | --- | --- |
| Swap (offset, move, scratch) | Optional, off by default. A strictly older secondary image is erased before a test or permanent swap. Not applied to a revert. | Accepted | B13, B18, B19 |
| Overwrite-only | Optional. A strictly older secondary image is erased. | Accepted | B22, B23 |
| Direct-XIP, with or without revert | Not available. The highest valid version boots. | Primary slot stays the candidate | B14, B24 |
| RAM load, with or without revert | The option can be set but has no effect, because its code is compiled only for swap modes. The highest valid version boots. | Primary slot stays the candidate | B14, B21, B60 |
| Any mode with hardware rollback protection | A lower security counter than the stored counter is rejected. The Zephyr port has no non-volatile counter backend at the pin. A trusted counter on RT1060 would need fuses. | Accepted | B25, B26 |

Two documentation mismatches were found. The Kconfig help names only major and
minor, but the code also compares revision (B20). A code comment says `<=`, but
the code rejects only a strictly older image (B18).

Consequence: in every stock mode an older **signed** image can boot under some
condition. The application must apply its own version policy before it writes
an image to Flash (matrix section 2.4).

## Update mode and staging medium

### Constraint for the owner

No stock MCUboot mode at the pin avoids NOR writes during XIP while it still
provides test, confirm and recovery. This was the expected result. It is
recorded as a constraint, not as a reason to design a custom bootloader.

| Path | NOR writes that remain |
| --- | --- |
| Swap modes | The application writes the image, the pending request and the confirm while it executes from NOR. The bootloader rewrites both slots up to the larger image size while it executes from NOR, and again on a revert. |
| Overwrite-only | The application writes the image and the pending request. The bootloader erases and copies the primary slot up to the image size. No revert. |
| Direct-XIP with revert | The application writes the image, the pending request and the confirm during XIP. The bootloader writes `copy_done` and erases unconfirmed images. |
| RAM load with revert | The application writes the image, the pending request and the confirm, but it executes from SDRAM. The bootloader writes `copy_done` and erases the whole slot of an unconfirmed image while it executes from NOR. |
| Plain direct-XIP or plain RAM load | No test, confirm or revert. The bootloader writes only when validation fails. |
| microSD read by the bootloader | Can remove NOR from the update path, but needs bootloader customisation. |

Source: [stage2-recommendation.md](evidence/boot-update/stage2-recommendation.md)
section 1, built from S2-2 to S2-8, B28, B61, B63 and B65. Write extents corrected
after independent review 1: swap and overwrite stop at the image size
(MCUboot `loader.c`:1141-1197, 1227 and 977-991), while the RAM load and
direct-XIP revert paths erase the whole slot (`loader.c`:2209).

### Ranked candidate modes

These are recommendations only.

| Rank | Mode | Why | Main costs and risks |
| --- | --- | --- | --- |
| 1 | RAM load with revert | The only stock mode in which the application does not execute from NOR while it writes NOR. Provides test, confirm and revert. A nominal boot of a confirmed image only reads Flash. Allows MCUboot image encryption. Close analogue of SDRAM execution loaded from block storage. | The bootloader still writes `copy_done` and erases a whole slot on revert, during its own XIP. The confirm path needs MCUboot's share-data backend, a retained memory region and a `zephyr,bootloader-info` chosen node, which the board does not have (B64). Bootloader RAM and the image load address both default to the start of SDRAM (B56, Inference). No older-image rejection by the bootloader. Boot copy time not measured. The stock upgrade request always marks slot 1. When the application runs from slot 1, the request writes nothing and the bootloader then erases the new image in slot 0 (B63a). Rank 1 therefore needs application code that marks the inactive slot, or images signed with trailer magic (imgtool `--pad`). A revert erases the whole 3 MiB slot. |
| 2 | Swap using offset | Sysbuild default, so the smallest configuration change. Works with the existing partitions. Provides test, confirm, revert and resume after an interrupted swap. Optional downgrade prevention. Keeps the current XIP model, so it is the comparison build for rank 1. | The largest bootloader write set per update: both slots rewritten up to the larger image size, and again per revert. The application writes NOR during XIP, which is the open #22 case. Not compatible with address-bound encryption. |
| 3 | Direct-XIP with revert | The smallest bootloader write set among XIP modes with revert. The vendor SDK uses direct-XIP for this board, with a vendor flash-remap extension that stock MCUboot does not use (V1). | Two builds per release, one per slot. No downgrade prevention. MCUboot image encryption not allowed (B29). The application writes NOR during XIP. |

The upgrade request path of rank 1 was found by independent review 1 and
checked against the pins:

| Point | Result | Label |
| --- | --- | --- |
| Confirm | An application built for RAM load with revert compiles the swap variant of `boot_set_next`. It writes `image_ok` on the active slot, which is enough because the bootloader already wrote `copy_done`. The direct-XIP variant is compiled only for direct-XIP with revert (B63 as corrected). | Verified on pinned source |
| Test request | Zephyr `boot_request_upgrade()` always opens slot 1. If slot 1 already has GOOD magic and is not active, nothing is written (B63a). | Verified on pinned source |
| Consequence | When the application runs from slot 1 and writes a new image to slot 0, the request has no effect. The bootloader selects the higher-version image in slot 0, finds no GOOD magic and erases the whole slot. The update is lost without an error. | Inference |
| Remedies | Application code that opens the inactive slot (from `BLINFO_RUNNING_SLOT`) and calls `boot_set_next(fa, false, permanent)`, or images signed with trailer magic (imgtool `--pad`). With a padded image, writing the image is itself the test request. A padded binary is the full slot size (imgtool `image.py`:453-454). | Code paths Verified on pinned source. Effect of padding is an Inference. |
| mcumgr image management | Whether it avoids the problem was not checked. | Not verified |
| Default signed image of the trial builds | Sysbuild signed both applications with `--align 1 --slot-size 3145728 --header-size 0x400`, without `--pad` and without `--confirm`. `MCUBOOT_GENERATE_CONFIRMED_IMAGE` is not set. The signed application is 33,032 B, so a padded image would be the full 3 MiB slot. Applied to the remedies above, the default image carries no trailer magic, so under rank 1 it would be erased on first selection unless the application writes the magic (Inference). Owner decision 13 stays open, now with this build evidence. | Arguments and sizes Verified by build (Stage 5 section 9). Consequence Inference. |

### Trial build results for rank 1 and rank 2

Both ranked trial modes were built on 2 November 2026 (A1, build-only) on the
trial tree `1d6ac4f`, which is an older ancestor of the PR base `f57ff22`.
Details are in [Stage 5](evidence/boot-update/stage5-trial-builds.md). All rows are
Verified by build unless stated.

| Point | Rank 1, RAM load with revert (Build A) | Rank 2, swap using offset (Build B) |
| --- | --- | --- |
| Configuration needed beyond the sysbuild options | Two Kconfig fragments and two devicetree overlays (in [trial-inputs/](evidence/boot-update/trial-inputs/)). No source patch. | None |
| SDRAM clash (B53 to B56) | Solved by configuration: MCUboot `zephyr,sram = &ocram2` (OCRAM2 trimmed to `0x20200000` size `0x7ff00`) and the executable RAM window set explicitly to SDRAM `0x80000000`, 32 MiB. MCUboot RAM ends at `0x20203940`. | Not applicable. MCUboot RAM stays in SDRAM by default (27,904 B from `0x80000000`), which confirms the default that B56 inferred. |
| Confirm path (B64) | Configured and linked: retained memory node at `0x2027ff00`, 256 B, `zephyr,bootloader-info` in both images, MCUboot share-data with the retention backend. **Not exercised**: the controller sample has no update or confirm code and no Flash driver. Survival of the shared data across the jump needs hardware. | Not applicable |
| Application link | Linked and loaded at `0x80000000` (vectors `0x80000400`). No section, LOAD segment or symbol other than an absolute Kconfig constant in `0x60000000` to `0x6FFFFFFF`. Indirect references were not checked. | Executes in place from `0x60020000` (vectors `0x60020400`) |
| MCUboot Flash driver | FlexSPI NOR driver with `XIP_MEM="ITCM"`. Driver code in ITCM, configuration object in NOR. See [Flash writes during XIP](#flash-writes-during-xip). | Same |

Rejected modes:

| Mode | Reason |
| --- | --- |
| Swap using move | Same behaviour as swap using offset, with more erase cycles and one extra primary sector. No advantage on uniform 4 KiB sectors. |
| Swap using scratch | Needs a devicetree scratch partition. Its benefit is for mixed sector sizes, which this NOR does not have. |
| Overwrite-only | No revert. The old image is destroyed when the copy starts. Kept only as the reference for the vendor BEE path. |
| Plain direct-XIP, plain RAM load | No test or confirm. A valid but faulty image keeps booting. Plain RAM load is the fallback if the share-data backend cannot be configured. |
| Single slot, single application RAM load, firmware updater | No second application image for fallback in the field. |

### Staging medium

**Recommendation: variant (c), no microSD staging.** The image goes into the
inactive NOR slot. For a network-delivered image, receive the whole image into
an SDRAM buffer, check size, signature and version, then write NOR in one
bounded phase while network activity is stopped.

| Variant | Recommendation | Reason |
| --- | --- | --- |
| (c) NOR slot, optionally through an SDRAM buffer | Recommended for the trial and as the M8 reference candidate | Stock MCUboot. No customisation. SDRAM capacity looks sufficient (32 MiB, images under the 3 MiB slot). Separates network DMA from NOR program and erase. |
| (a) Application copies from microSD into the NOR slot | Optional later variant | Gives a persistent receive buffer and resumable download. Removes no NOR write. Needs an owned staging path that #147 does not define. |
| (b) Bootloader reads from microSD | Not recommended within #141 | The only route that removes NOR from the update path. Needs a flash-area backend or hooks, an SD host driver and possibly a filesystem in the bootloader. Conflicts with the #147 mount ownership and FAT32 card model. |

#141 scope item 2 asks whether microSD staging avoids application writes to
XIP NOR. On pinned source it does not, in any stock mode with test and confirm.
The pending and confirm writes go to slot trailers in NOR (S2-2, S2-3, B63).

## Flash writes during XIP

Stage 6 adds the write inventory by actor and phase for both trial modes:
[stage6-layout-and-write-analysis.md](evidence/boot-update/stage6-layout-and-write-analysis.md)
section 2. The main results:

| Point | Result | Label |
| --- | --- | --- |
| Where the bootloader executes | MCUboot keeps the NXP boot header and DCD (FCB at offset 0, IVT `0x60001000`, boot data `0x60001020`, DCD `0x60001030`) and executes in place from QSPI NOR, code partition `boot_partition`, in both trial builds. Every bootloader write happens while it executes from the same NOR. The desk statement (A7, A9, MCUboot `boot/zephyr/app.overlay`:1-5) is now confirmed by build. | Verified by build (Stage 5 section 3) |
| FlexSPI driver path linked by MCUboot | `flash_mcux_flexspi_nor.c` with `CONFIG_FLASH_MCUX_FLEXSPI_XIP=y` and `XIP_MEM="ITCM"`. The NOR, MEMC and HAL transfer functions run from ITCM (`.itcm_text_reloc`, `0x2160` B). The vector table, fault and NMI handlers and the MCUboot write logic stay in NOR. | Verified by build (Stage 5 section 8) |
| Bootloader data closure | **The #22 data-closure failure is present in the MCUboot write path of both builds.** The busy-wait function calls `memc_flexspi_transfer`, which reads the FlexSPI register base through the device's config pointer, and that pointer is `memc_flexspi_config_0` in NOR. The instruction addresses match those #22 recorded for the inherited image I. | Instructions and pointer value: Verified by build (author disassembly of the archived ELF outside the Dev Container, Stage 6 section 3). Run-time use of the shallow device copy: Inference from #22. |
| Other #22 phases for the bootloader | No watchdog is configured in MCUboot (`CONFIG_WATCHDOG` and `BOOT_WATCHDOG_FEED` not set). No DMA or network driver is enabled in MCUboot. Startup, entry, completion, exception and bus-master closure were not analysed. | Configuration Verified by build. Closure **Not verified**. |
| Bootloader writes, RAM load with revert | `copy_done` on first selection, whole-slot erase (3 MiB) of an unconfirmed or unmarked image, erase of an invalid image. The write functions are linked. | Behaviour Verified on pinned source (B61, B65). Linked functions Verified by build. |
| Bootloader writes, swap using offset | Swap of both slots up to the larger image size, status, magic, `copy_done`, and the same again with `image_ok` on revert. The write functions are linked. | Behaviour Verified on pinned source (S2-4 to S2-7). Linked functions Verified by build. |
| Application writes | Image, test request and confirm. In RAM load modes the application executes from SDRAM during these writes. In swap modes it executes from the NOR it writes, which is the case #22 found blocked. A network download adds Ethernet DMA, which #22 lists as an open bus-master item. In RAM load modes, bus masters that reference NOR, cache coherence and FlexSPI reconfiguration still need review. The trial application links no Flash driver (`CONFIG_FLASH` not set), so no application write path was built. | Paths Verified on pinned source. Absence in the trial Verified by build. Bus-master and cache items Not verified. |
| Write extent | Swap and overwrite write per update only up to the image size. The RAM load and direct-XIP revert paths erase the whole 3 MiB slot under bootloader XIP. Erase duration and watchdog behaviour on the board are Not verified. | Verified on pinned source. Duration Not verified. |

Closure of the bootloader write path against the #22 matrix is **not
achieved**. One data-closure failure is shown (Stage 6 section 3). Full
closure is **Not verified** in every phase. The data-closure failure above applies to both
modes. Swap using offset writes much more under bootloader XIP per update, so
the mode ranking is unchanged (Inference).

## Current placement

Stage 4 re-verified the current placement from committed records only. The
latest `main` CI artefact could not be downloaded, and nothing was rebuilt in
Stage 4. The Stage 5 trial later rebuilt the controller sample without MCUboot
as a baseline (last row).

| Composition | Source | Boot header addresses | Highest Flash load end | Label |
| --- | --- | --- | --- | --- |
| `reference-normal` (historical baseline `fd9e341`) | `docs/evidence/storage-configuration/verification.json` | FCB `0x60000000`, IVT `0x60001000`, boot data `0x60001020`, DCD `0x60001030` | `0x6001a560` (107,872 bytes) | Verified by artefact. `CONFIG_FLASH_LOAD_OFFSET=0`, `CONFIG_BOOTLOADER_MCUBOOT=n`, `CONFIG_NXP_IMXRT_BOOT_HEADER=y`. |
| `execution_runtime` (EVKB map) | `docs/evidence/m5-execution/` | Same addresses | `0x6000d148` (53,576 bytes) | Verified by artefact for addresses. Configuration values Not verified. |
| `portable_execution` (EVKB map) | `docs/evidence/m5-execution/` | Same addresses | `0x60010348` (66,376 bytes) | Verified by artefact for addresses. Configuration values Not verified. |
| Latest `main` CI EVKB builds (`f67e22a`) | CI artefact | Not read | Not read | Not verified |
| Controller sample, EVKB (trial tree `1d6ac4f`, Stage 5 baseline rebuild) | Results `baseline/` | FCB `0x60000000`, IVT `0x60001000`, boot data `0x60001020`, DCD `0x60001030`, vectors `0x60002000` | `0x600092a0` (37,536 bytes) | Verified by build. `CONFIG_FLASH_LOAD_OFFSET=0`, `# CONFIG_USE_DT_CODE_PARTITION is not set`, `# CONFIG_BOOTLOADER_MCUBOOT is not set`, `CONFIG_NXP_IMXRT_BOOT_HEADER=y`, `CONFIG_FLASH_LOAD_SIZE=0`. Older than `f57ff22`. |

The upstream partitions are `mcuboot` `0x000000` to `0x020000`, `image-0`
`0x020000` to `0x320000`, `image-1` `0x320000` to `0x620000` and `storage`
`0x620000` to `0x800000`, with 4 KiB erase blocks (A9). None of the three
images exceeds 128 KiB.

Open item, kept by Stage 6: which erase size the update path may rely on. The devicetree declares 4 KiB, and
the [storage assessment](storage-configuration-assessment.md) records 4 KiB
sectors and 64 KiB erase blocks for this NOR. The LinkServer Flash algorithm
used on this board detected the 8 MiB device as 128 sectors of 64 KiB and wrote
in 64 KiB sectors
([#14 comment of 30 October 2026](https://github.com/wonny-labs/crux-pilot/issues/14#issuecomment-6530776373)).
This does not show that 4 KiB erase is unavailable. Recommendation, supported
by Stage 6: keep partition boundaries 64 KiB aligned until the erase size the
update path relies on is measured on the board. The upstream partition
boundaries are already 64 KiB aligned.

The 65,964-byte image size in the
[M1 readiness record](platform-m1-readiness.md) matches no retained map or
record and is unreconciled. The 37,536-byte baseline above does not match it
either.

### Fenced layout proposal

**PROPOSAL, not adopted.** Source:
[Stage 6](evidence/boot-update/stage6-layout-and-write-analysis.md) section 1.
The proposal keeps the upstream partitions for both modes. Offsets were checked
against the devicetree of the trial builds and the Stage 4 record.

| Region | Range (relative, end exclusive) | Size | Use under RAM load with revert | Use under swap using offset |
| --- | --- | --- | --- | --- |
| `mcuboot` | `0x000000` to `0x020000` | 128 KiB | MCUboot with boot header, 51,760 B used | MCUboot with boot header, 53,940 B used |
| `image-0` | `0x020000` to `0x320000` | 3 MiB | Slot 0, copied to SDRAM `0x80000000` | Primary, executes in place at `0x60020000` |
| `image-1` | `0x320000` to `0x620000` | 3 MiB | Slot 1, equivalent to slot 0 | Secondary, update from `0x321000` |
| `storage` | `0x620000` to `0x800000` | 1,920 KiB | Kept, no owner and no writer in M8 (recommended option) | Same |

All boundaries are 64 KiB multiples. The bootloader link is fenced to the
128 KiB partition, and the swap application link to the slot less the trailer
estimate (Verified by build). Rejection of an oversized image was not tested.
The storage region options (kept, removed, moved to microSD) and a smaller slot
size are owner decision 17 below.

## HAB in open configuration

| Question | Result | Label |
| --- | --- | --- |
| Does a Zephyr-built image have anything for HAB to authenticate? | No. The Zephyr IVT sets `csf` to `CSF_ADDRESS`, which is 0 (Stage 3 A1, A2). Only the bootloader image keeps an IVT, so only the bootloader can be a HAB target (A6). | Verified on pinned source |
| What does open configuration do with blank SRK hash fuses on RT1060? | Expected: HAB can show that a CSF was found and processed, and which checks failed. It cannot show a passing authentication. This is an expectation from generic HABv4 documents (A10), not an RT1060 result. | Not verified (security reference manual and AN12079 not readable, A13) |
| Can events be read without fuse writes? | Only on a board: software calling the HAB `report_event` and `report_status` API, or possibly the serial downloader `error-status` command. The pinned RT1062 ROM API driver has no HAB entry, so Zephyr would need new code (A8, A11, A15). Event readback is out of scope for #141. | Mixed. See [stage3-findings.md](evidence/boot-update/stage3-findings.md) section A. |
| What can be checked without SRK fuse writes? | Offline image structure (IVT, CSF, SRK table) and offline signature checks, for example with SPSDK. Not ROM acceptance, closed-configuration rejection or boot behaviour. | Stage 3 summary section 1. No tool was run. RT1060 behaviour Not verified. |
| Closed configuration (later work only) | Needs a dedicated board that may be locked, SRK hash fuse programming, a signed bootloader that boots without HAB events, then the closed-configuration fuse (E1). MIMXRT1060-EVKB is listed as "No Longer Manufactured" (E2). | Requirements only. RT1060 procedure Not verified. |

### Trust level without fuse programming

Achievable: MCUboot verifies the application image with a development key on
every boot. Not achieved and to be declared: the boot ROM does not authenticate
the bootloader, so there is no hardware root of trust. Anyone with Flash or
debug access can replace the bootloader and its embedded public key. The trial
builds confirm that MCUboot embeds the development public key and that it
matches the key hash in each signed image (Stage 8, Verified by build). There is
no hardware anti-rollback counter. HAB enforcement and BEE are not active.
Source: [stage3-summary.md](evidence/boot-update/stage3-summary.md) section 2.

## BEE (XIP encryption)

| Point | Result | Label |
| --- | --- | --- |
| Driver | `hal_nxp` has a BEE driver and an RT1060 BEE header (C1). Zephyr does not wire it (C3). | Verified on pinned source |
| Address binding | BEE uses AES-CTR with the address supplied by hardware, so ciphertext only decrypts at the address it was encrypted for (C5). | Verified by public vendor document |
| Effect on swap modes | Swap and move change addresses, so images would need re-encryption per slot. | Inference |
| Vendor MCUboot support | NXP's own MCUboot extension supports BEE only with a modified overwrite-only mode without revert, using customised code and hooks (C7, C8). Upstream Zephyr MCUboot has no BEE support in the files read. | Verified by public vendor document. Upstream whole-tree absence Not verified. |
| Key source and fuses | NXP SDK documentation says a software key is the default and allows evaluation without fusing (C4). The `hal_nxp` driver comment says a fuse must be set for a register-loaded key (C2). These conflict. Whether the ROM applies BEE to the bootloader region without a fuse is unknown (C9). | Not verified |
| RAM load | The application executes from SDRAM, so BEE does not apply to it. Confidentiality at rest would come from MCUboot image encryption, which sysbuild allows with RAM load (C10, B58). Protection of the decryption key is open. | Kconfig Verified on pinned source. Not verified by build. |

## Signing toolchain

| Role | Tool | Finding |
| --- | --- | --- |
| Application images | MCUboot `imgtool`, pinned in-tree version 2.4.0 | Apache-2.0. Already used by `west sign` and sysbuild (Stage 3 A9). Used by sysbuild in the A1 trial builds, run from the pinned MCUboot clone (Stage 5). |
| Scripted HAB signing of the bootloader | SPSDK `nxpimage hab`, 3.12.0 | BSD-3-Clause. Scripted YAML workflow without a GUI (B6). Python 3.10 or later and 34 required dependency entries. Licences of binary dependencies not checked. Deferred until A2. |
| Optional manual cross-check | MCUXpresso Secure Provisioning Tool (SEC) 26.09 | GUI front-end over SPSDK. Download needs an NXP account. Licence name not shown. Not a reproducibility path (B10 to B12). |
| Legacy HAB signer | NXP CST | Click-through licence LA_OPT36. Superseded by SPSDK (B6, B13). Not recommended. |

Stage 1 desk findings for the signed build (B37 to B47):

- Sysbuild's default signing key is MCUboot's public debug key, and the build
  warns about it. The trial must set its own development key.
- The public key is embedded through `imgtool.py getpub` into a generated
  source file in the build directory. The Zephyr port holds exactly one key.
- imgtool writes no timestamp. The unsigned payload and hash are therefore
  reproducible on source. Corrected by Stage 8: ECDSA P-256 signatures from
  `imgtool` 2.4.0 with `cryptography` 50.0.2 are randomised. Two signatures of
  the same payload differ (Verified by build). The plan's criterion does not
  need signature determinism: unsigned images must reproduce byte for byte and
  signed images must verify against the documented public key.
- Encrypted images are never byte-reproducible because the AES key is random.

Whether SPSDK accepts a Zephyr image that already has an FCB and IVT is Not
verified (Stage 3 A14).

### Signed build results (Stage 8)

| Point | Result | Label |
| --- | --- | --- |
| Reproducibility | Unsigned `zephyr.bin` of MCUboot and the application identical between two clean builds of the same commit, in both modes. ELF files differ only in `.debug_info`, `.debug_line` and `.debug_str`, which contain the build-directory path. That the path is the only cause was not confirmed by a rebuild in the same path. The plan stop condition is not triggered. | Verified by build. Cause of the ELF difference: consistent with the path, not confirmed. |
| Signed images | Header and payload identical. `SHA256` and `KEYHASH` TLVs identical. Only the ECDSA signature TLV differs. | Verified by build |
| Verification | `imgtool verify` with the development public key: exit 0 for all four signed images (two modes, original and rebuild). A different throwaway key: exit 1. | Verified by build |
| Public key | ECDSA P-256, DER SubjectPublicKeyInfo SHA-256 `0aafb408cfe1de0f6cdb2e1f7f357a6bb171b6e53ea5b856321c2a8dcb7ea368`. The key embedded in MCUboot (`autogen-pubkey.c`, also found in `zephyr.bin`) equals it. The public key file is not committed. | Verified by build |
| Private key | Kept outside the repository. No PEM private-key header and no raw private scalar in the container checkout, the build directories or the results. | Verified by build (search output) |
| Key kinds | Fixed development key outside the repository for later board tests. Temporary CI key with the bootloader rebuilt in the same run. The trial key sits on a shared volume and should be moved or deleted. | Recommendation |

## Boot media, SDRAM and DCD

| Point | Result | Label |
| --- | --- | --- |
| SDRAM initialisation | The board enables DCD and SDRAM by default. The DCD in the boot header programs the SEMC (B80 to B82). Zephyr's SoC code does not initialise SEMC (B84). | Verified on pinned source |
| Application built for MCUboot | Has no boot header and no DCD. It relies on the bootloader's DCD having run (A7, B86). | Build logic Verified on pinned source. That the boot ROM runs the bootloader's DCD is Not verified. A build cannot show it. Stage 3 D10 and the Stage 3 summary have been corrected to this label. |
| RAM load address conflict | The board `zephyr,sram` is 32 MiB SDRAM at `0x80000000`. Bootloader RAM and the RAM load address both default to it (B53 to B56). MCUboot has no RT1060 overlay to change it. Its RT1050 RAM load overlay places retained boot information in SDRAM at `0x80007f00` (Stage 3 D11). On this board that would overlap an image loaded at `0x80000000`. The trial solved the conflict by configuration only: MCUboot RAM in OCRAM2, retained region at the top of OCRAM2, executable window in SDRAM (Stage 5 section 5). | Lines Verified on pinned source. Default MCUboot RAM in SDRAM and the configuration-only fix: Verified by build. That the fix works on the board: Not verified. |
| Boot from microSD as an eMMC analogue | Pinned Zephyr cannot build an SD or eMMC boot image (D1). SPSDK lists SD, MMC and eMMC boot layouts for this family (D4). The EVKB Board User Manual gives SW4 `ON-OFF-ON-OFF` for SD card boot (Table 10, D5 as corrected). | Switch setting: Verified by public vendor document (summarising fetch, not a full read). SD boot on EVKB without fuse settings: Not verified (D5 partly answered). |
| Boot switch names | The Zephyr board document uses EVK (Rev A) names: SW7 for the boot switch and SW9 for reset (D3). On the EVKB the boot switch is SW4. QSPI boot is `OFF-OFF-ON-OFF`, recorded for the physical board in [baseline](baseline.md). Serial Downloader is SW4[3:4] = `01` (manual Table 9). With SW4[1:2] OFF this is `OFF-OFF-OFF-ON`. SW7 is a reset button on the EVKB. | QSPI setting: repository record. Serial Downloader two-bit value and SD setting: Verified by public vendor document (summarising fetch, not a full read). Four-position Serial Downloader string: Inference. SW7 role: same, summary only. Rev A naming: Inference. |
| Recovery of a corrupted bootloader | The bootloader has one copy. Recovery depends on boot ROM paths, for example the serial downloader. Stage 3 did not answer this, although Stage 2 marked it pending Stage 3. | Not verified |

## Execution model

**Recommendation: evaluate RAM load into SDRAM as the M8 reference candidate.
Keep the standalone QSPI XIP image as the reference until M7 target validation
is closed.** The desk record made the RAM load choice depend on Stages 5 and 6
showing no blocker. **They show no build blocker.** The remaining conditions
below are owner decisions or need hardware. If one of them fails, fall back to
XIP with swap using offset.

Reasons: it is the only stock path where the application's update writes do
not happen during its own XIP. It is a close analogue of SDRAM execution
loaded from block storage, which the platform keeps open. It keeps MCUboot image encryption available, which direct-XIP
does not.

Conditions before adoption (A3), with the trial results:

| Condition | Rows | State after the trial build |
| --- | --- | --- |
| Bootloader RAM separated from the image load window by configuration | B53 to B56 | Met at build level (Stage 5 section 5). Not run. |
| Share-data backend and retained memory configured for the confirm path. The retention region must lie outside the image load window and the application's linked RAM. | B64, Stage 3 D11 | Configured and linked, region at `0x2027ff00` outside both (Stage 5 section 5). Not exercised by the sample. Survival across the jump needs hardware. |
| Upgrade request reaches the inactive slot: application code that marks it, or images signed with trailer magic (imgtool `--pad`) | B63a | Open. The trial default image has no trailer magic and the sample has no update code (Stage 5 section 9). Owner decision 13. |
| Application ELF shows no code, data, vectors or constants in NOR. The RAM link comes from sysbuild `CONFIG_XIP=n` and `CONFIG_FLASH_SIZE=0`. `USE_DT_CODE_PARTITION=y` is expected in the application `.config` and does not mean a Flash link. | B68a, R2 | Met for sections, LOAD segments and symbols, and the three Kconfig values are as B68a predicted (Stage 5 sections 6 and 7). Indirect references were not checked. |
| Bootloader size fits the `mcuboot` partition or a revised layout | A9, Stage 5 | Met: 51,760 B of 131,072 B |
| Bootloader FlexSPI write path reviewed against the #22 matrix | R2, Stage 6 | Reviewed at placement level and for the busy-wait path. The #22 data-closure failure is present in both modes, so closure is not achieved. Full closure Not verified. Owner decision 16. |

Effect on M6 "Device interfaces and platform configuration" and M7 "Controller
platform integration and validation" evidence (detail in
[Stage 6](evidence/boot-update/stage6-layout-and-write-analysis.md) section 4):

| Effect | Detail |
| --- | --- |
| Image identity | Any bootloader composition changes the application link address, removes the application boot header and DCD, and changes ELF and binary hashes (Verified by build). Evidence tied to the standalone image hash does not transfer automatically. Signed files change on every signing run, while the unsigned binary and the image digest do not (Stage 8). Evidence should therefore cite the unsigned binary hash or the image digest, the bootloader hash and the public key hash (recommendation). |
| Link address | Standalone `0x60000000`. Swap using offset `0x60020000`. RAM load `0x80000000` (Verified by build). |
| Programming and debug | Procedures that program a standalone image at `0x60000000` would change. Two images, the bootloader at offset `0x0` and the signed application at `0x20000`, must be programmed and identified. Under rank 1, a programmed application image must carry trailer magic and be confirmed (for example Zephyr's `*.signed.confirmed.hex`, Zephyr `cmake/mcuboot.cmake`:246-255), or the bootloader erases the slot when it selects it (MCUboot `loader.c`:2199-2211). The trial default produces neither (Stage 5 section 9). The RAM load application ELF has its LOAD segments in SDRAM, so a debugger download of that ELF would not program the slot (Inference). |
| Peripheral behaviour | Driver behaviour should not change, but this is an inference. A defined smoke subset would need re-running. Not verified. |
| Timing | Execution from SDRAM instead of cached QSPI and ITCM can change timing. Timing evidence would need re-measurement. Copy time, boot time and which records depend on timing are Not verified. |
| SDRAM dependence | Current RAM is already SDRAM. RAM load makes code depend on it too. In the trial, the RAM load bootloader itself uses on-chip OCRAM2, while the swap bootloader uses SDRAM by default (Verified by build). |

Timing recommendation: change the reference composition at the start of M8,
after M7 target validation, so that no milestone's evidence becomes invalid
part-way through. **The Stage 6 results support this recommendation**
(Inference, because a build cannot confirm a timing decision). They refine it: make
the switch as one step (bootloader, layout, signed-image options and
programming scripts together), after owner decision 13, and rebuild at the
revision that starts M8, because the trial used the older tree `1d6ac4f`.

## Network-delivered updates

The bootloader verifies the signature at boot, so unsigned code cannot run. An
unauthenticated update request could still cause NOR writes and erases, mark an
image pending or permanent, or install an older signed image in modes without
rejection. Before any network-facing update interface (M9), update requests
must be authenticated and authorised, and the application should check
signature, version and size before it writes NOR. An application-side
signature check needs an application crypto provider (#142, unscheduled).
Source: [stage2-matrix.md](evidence/boot-update/stage2-matrix.md) section 6.

## RT1170-EVK Rev A1

On 2 November 2026 the owner returned RT1170-EVK Rev A1 to #141 as scope item
5, desk and build-only. RT1060-EVKB Rev B remains the primary target and the
only board for physical validation. RT1170 hardware verification is deferred
and tracked in [#155](https://github.com/wonny-labs/crux-pilot/issues/155).
Evidence: [stage7-rt1170-delta.md](evidence/boot-update/stage7-rt1170-delta.md).
Stage 7 is a first-draft desk study. Its key layout, boot header, sample target
and OTFAD claims were re-read at the pins when this record was assembled and
matched (its "Assembly check" section). It has not had an independent review.

**Build-only items are not done.** No RT1170 sysbuild configure and no RT1170
bootloader cross-build was run. Both are covered by the A1 extension of
2 November 2026 (owner decision 14) and need the documented Dev Container.
Neither has been run.

Summary: on pinned source the RT1170-EVK Rev A has a 16 MiB QSPI NOR at
`0x30000000` with 7 MiB slots, 64 MiB SDRAM started by XMCD, the FCB at
`0x400`, and OTFAD in place of BEE. The mode ranking, version policy, staging
variant (c) and the no-CSF finding carry over. RAM load needs an RT1170
overlay. The XIP write evidence, HAB behaviour and OTFAD key and fuse needs are
unknown. The upstream MCUboot sample target resolves to Rev B, not Rev A. No
RT1170 build or hardware result exists.

### Differences from RT1060

| Topic | RT1060-EVKB Rev B | RT1170-EVK Rev A | Label | Rows |
| --- | --- | --- | --- | --- |
| External NOR | 8 MiB at `0x60000000` | IS25WP128, 16 MiB at `0x30000000` | Verified on pinned source | S7-L3, S7-L4 |
| Upstream partitions | `mcuboot` 128 KiB, two 3 MiB slots, `storage` 1,920 KiB | `mcuboot` 128 KiB, two 7 MiB slots, `storage` 1,920 KiB. No scratch partition. | Verified on pinned source | S7-L6, S7-L8 |
| Erase and write block in the devicetree | 4 KiB, 1 byte | 4 KiB, 1 byte. All boundaries are 64 KiB multiples. | Verified on pinned source. 64 KiB check is arithmetic. | S7-L6, S7-L7 |
| SDRAM and its start-up | 32 MiB, DCD in the boot header | 64 MiB, XMCD in the boot header, DCD not built | Verified on pinned source | S7-L10, S7-B8 to S7-B10 |
| Boot header offsets | FCB `0x0`, IVT `0x1000`, boot data `0x1020`, DCD `0x1030` | FCB `0x400`, IVT `0x1000`, boot data `0x1020`, XMCD `0x1040` | Offsets in Kconfig: Verified on pinned source. RT1170 addresses: Inference (no RT1170 map read). | S7-B3, S7-B6, S7-B9, S7-B12 |
| Boot configuration file | One FCB file for revisions B and C | One FCB file per revision (Rev A and Rev B differ) | Verified on pinned source | S7-B2, S7-B4, S7-B5 |
| XIP encryption | BEE | OTFAD (SPSDK also lists IEE). No BEE in the files read. | Verified by public vendor document. No BEE: Inference. | S7-S6, S7-S7 |
| Upstream MCUboot sample target | Resolves to revision C | Resolves to revision B, not Rev A | Verified on pinned source | S7-A1 to S7-A3 |
| MCUboot board files at the pin | None | None for the EVK. Only a `vmu_rt1170` configuration file. | Verified on pinned source (file-name listing) | S7-A5 |
| Sectors per slot at 4 KiB | 768 | 1,792. Bootloader RAM effect not measured. | Inference | S7-A6 |
| Boot switch | SW4 on the EVKB | SW1 patterns in the upstream board document, revision not named. Rev A1 settings Not verified. | Document text Verified on pinned source | S7-S15 |

### Applicability of the RT1060 findings

| Finding | RT1170-EVK Rev A | Reason |
| --- | --- | --- |
| Update mode ranking | Carries over | Mode logic is board-independent MCUboot code. A revert erases a 7 MiB slot instead of 3 MiB (Inference). |
| RAM load with revert as rank 1 | Changes | Needs an RT1170 overlay for bootloader RAM, load window and retention region. The slot 1 request problem (B63a) and the RAM link (B68a) carry over. The SoC clock code warns about the SEMC clock only "If DCD is used", and this board uses XMCD. The SDRAM clock effect of the application's PLL set-up is Not verified. |
| Staging variant (c) | Carries over | Stock MCUboot. 64 MiB SDRAM against 7 MiB slots. |
| 64 KiB alignment recommendation | Carries over | Upstream boundaries conform. The erase size the update path may rely on is unknown for Rev A. |
| XIP write hazards (#22) | Mechanism likely carries over. RT1060 evidence does not. | Same mechanism and driver family (Inference). The #22 evidence comes from RT1060 images only. RT1170 closure is Not verified. |
| Older-image rejection and version policy | Carries over | MCUboot comparison logic without board input. |
| Signing tools | `imgtool` carries over. SPSDK unknown. | Whether SPSDK accepts a Zephyr image with FCB, IVT and XMCD is Not verified. |
| No CSF in a Zephyr image | Carries over | Same IVT structure with `csf` 0. |
| HAB open configuration | Unknown | RT1170 security reference manual not readable. |
| BEE out of the candidate path | Does not apply | OTFAD replaces BEE. Its key source and fuse needs are Not verified. Address binding gives the same swap constraint (Inference). |
| SDRAM readiness for an MCUboot application | Carries over, with XMCD in place of DCD | That the ROM runs the bootloader's XMCD is Not verified. |
| Execution model and M6/M7 evidence | Not assessed | RT1170 is not part of the M6 and M7 scope. |

### Later work for #155 (hardware)

Stage 7 section 6 lists 13 items. They include confirming that the Rev A1 PCB
is Zephyr revision `A` with the devicetree's flash and SDRAM, Rev A1 boot and
serial downloader switch settings, ROM processing of the bootloader XMCD, the
erase size, silicon revision, RAM load copy, watchdog, cache and clock
behaviour, revert and recovery, #22-style closure, HAB event readback, OTFAD
key behaviour and SD boot. This record proposes no hardware operation. Each
item needs its own approval.

## Recommendations and decisions

Nothing in this record is adopted except the owner decisions below. The owner decisions for #141 are the
RT1170-EVK scope decisions of 1 and 2 November 2026, A0, A1 with its RT1170
extension of 2 November 2026, and A2 for the SPSDK download. On 7 November 2026 the owner also accepted the
recommendations for rows 1, 3, 4, 5, 6, 8, 9, 10 and 11 of the owner decisions
table (A3 in part). All other rows are recommendations for A3.

| Item | Content | Status |
| --- | --- | --- |
| Bootloader | MCUboot at the Zephyr pin, ECDSA P-256 with tinycrypt | Recommendation |
| Update mode | Rank 1 RAM load with revert, rank 2 swap using offset, rank 3 direct-XIP with revert | Recommendation |
| Rank 1 test request | Application code that marks the inactive slot, or images signed with trailer magic | Accepted by the owner on 2 November 2026 (row 13) |
| Trial builds | Rank 1 and rank 2 | Approved (A1, 1 November 2026). Done as build-only evidence on 2 November 2026 (Stages 5 and 8). |
| Staging medium | Variant (c), NOR slot, optionally through an SDRAM buffer | Recommendation |
| Execution model | RAM load into SDRAM as M8 reference execution model, after M7 target validation. No build blocker found. Timing supported by Stage 6 (Inference). | Accepted by the owner on 7 November 2026 (row 3) |
| Application version policy | Reject older images in the application before any NOR write | Accepted by the owner on 7 November 2026 (row 4) |
| Signing tools | `imgtool` for application images (used in the trial). SPSDK under A2, not yet run. SEC optional. No CST. | Recommendation |
| BEE | Not part of the candidate path | Accepted by the owner on 7 November 2026 (row 10) |
| HAB | Documentation result only. Event readback and closed configuration are later, separately approved work. | Recommendation |
| Fenced layout | Upstream partitions kept for both modes, 64 KiB aligned boundaries, `storage` kept with no owner and no writer in M8 (Stage 6) | Recommendation (PROPOSAL, not adopted) |
| Bootloader data-closure failure | The #22 data-closure failure is present in the MCUboot write path of both trial modes (Stage 6 section 3) | Accepted by the owner on 2 November 2026 (row 16) |
| Development keys | Fixed development key outside the repository for board tests, temporary CI key with the bootloader rebuilt per run. Move or delete the trial key. | Accepted by the owner on 2 November 2026 (row 18) |
| RT1170-EVK Rev A1 | Desk differences recorded. Hardware deferred to #155. A1 extended to RT1170 builds on 2 November 2026, builds not yet run. | Owner decision of 2 November 2026 (scope and A1 extension). Content is a desk result. |

## Owner decisions needed

The recommended options summarise the stage recommendations. Rows 8, 11, 12
and 15 have no stage recommendation and are left to the owner. For rows 8 and 11 a
recommendation was given on 7 November 2026 and accepted. Rows 16 to 18
come from Stages 6 and 8. On 2 November 2026 the owner accepted the
recommended options for rows 13, 16, 17 and 18, and on 7 November 2026 for
rows 1, 3, 4, 5, 6, 8, 9, 10 and 11. Rows 12 and 15 are not decided. Each row says
what was accepted. Acceptance is an owner decision. It is not an implementation.

| # | Decision | Recommended option | Source |
| --- | --- | --- | --- |
| 1 | Accept the constraint that no stock mode avoids NOR writes during XIP with test, confirm and recovery, or open a separate scope for bootloader customisation (variant b) | Accept the constraint for M8. **Accepted by the owner on 7 November 2026:** the constraint is accepted for M8. Bootloader customisation (variant b) stays a separate scope that is not started. | Stage 2 decision 1 |
| 2 | Ranked candidates for the trial build | Done: A1 approved on 1 November 2026 with both builds, rank 1 and rank 2 | Stage 2 decision 2 |
| 3 | Pursue RAM load into SDRAM as the M8 reference execution model, and when the reference composition may change relative to M7 | Pursue it, change at M8 start after M7 target validation. Stages 5 and 6 found no build blocker and support the timing (Inference). Owner decision 16 limits any M8 hardware step in which the bootloader writes. **Accepted by the owner on 7 November 2026:** RAM load into SDRAM is the M8 reference execution model, and the reference composition changes at the start of M8, after M7 target validation. | Stage 2 decision 3, Stage 6 section 4.1 |
| 4 | Require an application-side version policy (no older image, compared on major, minor and revision) | Require it, because the RAM load bootloader provides none. **Accepted by the owner on 7 November 2026:** an application-side check rejects an older image. | Stage 2 decision 4 |
| 5 | Before any network-facing update interface (M9), require authentication and authorisation of update requests and an application-side check before any NOR write | Require it. **Accepted by the owner on 7 November 2026:** authentication and authorisation of update requests, and an application-side check before any NOR write, are required before any network-facing update. | Stage 2 decision 5 |
| 6 | Whether the microSD may later hold a staging path (variant a), which would change the #147 card layout | Defer. Stage 2 rates variant (a) as an optional later variant. **Accepted by the owner on 7 November 2026:** deferred. | Stage 2 decision 6 |
| 7 | A2 licence review before any download of SPSDK and its binary dependencies, SEC or CST | Partly done: A2 given for the SPSDK download. Not yet used. SEC and CST stay without approval. | Stage 3 decision 1 |
| 8 | Whether a later, separately approved hardware step may read HAB events in open configuration, without fuse writes | Owner choice. **Accepted by the owner on 7 November 2026:** a later HAB event read in open configuration, without fuse writes, is allowed only as a separately approved hardware task, which may share a session with the ROM serial downloader check (#309). | Stage 3 decision 2 |
| 9 | Accept the plan corrections listed below | Accept. **Accepted by the owner on 7 November 2026:** the plan corrections below are accepted. | Stage 3 decision 3 |
| 10 | Whether BEE stays out of M8, since it needs customised MCUboot and overwrite-only | Keep it out. **Accepted by the owner on 7 November 2026:** BEE stays out of M8. | Stage 3 decision 4 |
| 11 | Whether to plan a dedicated board for future closed-configuration work | Owner choice. The EVKB is listed as no longer manufactured (Stage 3 E2). **Accepted by the owner on 7 November 2026:** deferred until a closed configuration decision. | Stage 3 decision 5 |
| 12 | Whether to adopt the #141 scope wording corrections in plan section 9 | Owner choice | Plan section 9 |
| 13 | Rank 1 needs either application code that marks the inactive slot for test, or images signed with trailer magic (imgtool `--pad`). Which one should the trial and M8 use? | **Accepted by the owner on 2 November 2026:** M8 test images are signed with `--pad` and placed with development tools. The application-driven update path is designed in M9, where application code that marks the inactive slot is preferred. Basis: both options are stock on source. Padding makes every written higher-version image a test request (Inference). A padded binary is the full slot size, so the network transfer, the SDRAM buffer and the NOR write become 3 MiB per update on RT1060, while the hex output is sparse (imgtool `image.py`:432-454). RAM load with revert does not imply `MCUBOOT_GENERATE_CONFIRMED_IMAGE`, unlike direct-XIP with revert, so confirmed images must be enabled explicitly (Zephyr `modules/Kconfig.mcuboot`:270-290 and 312). Build evidence (Stage 5 section 9): the trial sysbuild signed both applications without `--pad` and without `--confirm`, and `MCUBOOT_GENERATE_CONFIRMED_IMAGE` is not set. The unpadded signed application is 33,032 B against a 3 MiB padded image. This is build evidence of the default only, not a hardware result. | Independent review 1, F1. Independent review 2, N2. Stage 2 decision 7. Stage 5 section 9. |
| 14 | Extend A1 (the Stage 5 approval) to an RT1170 sysbuild configure and bootloader cross-build for rank 1 and rank 2 in the Dev Container | Done: the owner extended A1 on 2 November 2026. The RT1170 builds have not been run yet. | Stage 7 section 6 |
| 15 | Which RT1170 items, if any, #155 should cover first when hardware work is planned | Owner choice | Stage 7 section 6 |
| 16 | How to treat the #22 data-closure failure found in the MCUboot write path of both modes: record it as an open blocker for any bootloader write on hardware, or open a scope to remediate it (for example placing the FlexSPI configuration object in RAM, as the #22 PoC did for the application), with a decision on whether that is a Crux composition change or an upstream change | **Accepted by the owner on 2 November 2026:** record it as a blocker for bootloader writes on hardware. The remediation is a separate scope, tracked in [#165](https://github.com/wonny-labs/crux-pilot/issues/165) (not started). Its scope must be decided before any M8 hardware step that lets the bootloader write NOR. | Stage 6 section 3 |
| 17 | Use of the `storage` region (kept without owner, removed, or moved to microSD) and whether to reduce the 3 MiB slots | **Accepted by the owner on 2 November 2026:** keep `storage` declared with no owner and no writer in M8. Keep 3 MiB slots until a whole-slot erase has been timed on the board. Partition boundaries stay 64 KiB aligned. | Stage 6 sections 1.4 and 1.5 |
| 18 | Where the fixed development key for later board tests lives, whether the trial key is deleted, and whether the development public key may be committed (plan section 9, item 10) | **Accepted by the owner on 2 November 2026:** delete the trial key from the shared volume now that both builds are done. Generate a new key in a directory that is not shared when a hardware test is approved. Commit only the public key, at that point. The deletion is done by the owner on the local machine and is Not verified here. | Stage 8 sections 3 and 5 |

## Corrections to the plan

| Plan statement | Correction | Evidence |
| --- | --- | --- |
| Stage 3 input row and section 3.2: `requirements-extras.txt` | It is Zephyr's file at the pin (`scripts/requirements-extras.txt` lines 21 to 22, `spsdk >= 3.0.0`). crux-pilot has no such file, and the crux-pilot bootstrap does not install it. | Stage 3 B1 to B3 |
| Section 3.2: MCUboot `scripts/requirements.txt` names six entries "and one more entry (seven in total, to be listed in the record)" | The plan is not wrong. It deferred the seventh entry, which is `pytest` (test-only). The full list is `cryptography>=40.0.0`, `intelhex`, `click`, `cbor2`, `setuptools`, `pyyaml`, `pytest`. | Stage 3 B16 as corrected |
| Appendix A: sysbuild offers nine modes in lines 41 to 139 | Eleven modes, including RAM load with revert and single application RAM load, in lines 41 to 149 | Stage 1 A3 |
| Appendix A: CMake line ranges for ECDSA tinycrypt | Lines 25 to 37 and 164 to 189. The claim holds. | Stage 1 A6 |
| Appendix A: no BEE reference in the MCUboot tree | Verified only for the files read. Whole-tree absence is Not verified. | Stage 1 A8 |
| Section 3.2: `ext/mbedtls-asn1` licence not verified | Partly closed. The source files carry Apache-2.0 tags. No licence file was found for the directory. | Stage 1 L3 |
| Section 5.2: three evidence labels, with vendor documentation classed as Not verified | The record uses an extended label set: "Verified by public vendor document" (Stage 3), "Verified by artefact" and "Verified on source" (Stage 4), and "Inference". They were needed to separate a vendor's public statement, a committed build record and a reasoned conclusion from a plain Not verified. None of them means a Crux build or hardware result. | [Evidence labels](#evidence-labels) |
| Stage 1 verification: a second agent re-reads each cited line | Not done as a separate step. Independent review 1 spot-checked 47 rows. See Method. | Method |
| Evidence locations: `pinned-source-findings.md` and `review-record.md` | The Stage 1 files are `stage1-pinned-source-findings.md` and `stage1-answers.md`. No review record file is in the repository yet. Stage 9 will add it. | [Evidence index](evidence/boot-update/README.md) |
| Section 1.1 item 5, Stage 7, question 11 and the model and mapping tables: RT1170-EVK removed or deferred (1 November 2026) | Superseded on 2 November 2026. Owner decision: RT1170-EVK Rev A1 is back in scope as item 5, desk and build-only. Hardware verification is deferred to #155. The plan is kept as written with a note at its top. | Plan note, [stage7-rt1170-delta.md](evidence/boot-update/stage7-rt1170-delta.md) |
| Header: crux-pilot baseline `bb7f425` | Stage 0 read `f67e22a`. `origin/main` then moved to `f57ff22` when #147 merged. Stage 2 read `f57ff22`. Stages 3 and 4 cite `f67e22a`. The Stage 5 and 8 trial builds used `1d6ac4f`, an older ancestor of `f57ff22`, whose controller sample sources and Crux module files differ from `f57ff22`. | Stage 0, Stage 2 section 0, Stage 5 section 1 |
| Evidence locations: `inputs/`, `provenance-mcuboot.json`, `provenance-app.json`, and `reproducibility.txt` and `signing.txt` at the top of `docs/evidence/boot-update/` | The trial inputs, check outputs, hashes, `reproducibility.txt` and `signing.txt` are in `docs/evidence/boot-update/trial-inputs/`. No provenance JSON files exist: `scripts/record-build-provenance.py` was not run (plan section 4). Hashes, revisions and `.config` extracts replace them. | [Stage 5](evidence/boot-update/stage5-trial-builds.md) section 11 |
| Section 5.2: "Verified by build" reserved for Stages 5 and 8 | Also used in Stage 6, which analyses the Stage 5 outputs. Stage 6 includes a small static disassembly of the archived ELF files made outside the Dev Container. Section 4 asks for such results to be labelled, and Stage 6 labels it. | [Stage 6](evidence/boot-update/stage6-layout-and-write-analysis.md) section 3 |
| Section 1.3: A2 "Not given", A1 for RT1060 only | On 2 November 2026 the owner extended A1 to RT1170 builds and gave A2 for the SPSDK download. Neither has been used yet. | [Approvals](#approvals) |

Corrections between stages:

| Earlier statement | Later result |
| --- | --- |
| Stage 1 B8: MCUboot board files for RT1060 not found at guessed names, Not verified | Stage 3 D11 listed `boot/zephyr/boards/` at the pin. No RT1060 board file exists. Only an RT1050 hyperflash RAM load overlay. |
| Stage 2 rows marked "Pending Stage 3" | Answered as far as documents allow in Stage 3. HAB behaviour, BEE key default and SD boot remain Not verified. Recovery of a damaged bootloader was not answered by Stage 3 and remains Not verified. |
| Stage 2 V1 to V4: vendor documents labelled Not verified | Stage 3 reads the same documents and labels them "Verified by public vendor document" (vendor statement only). This record keeps both labels and treats neither as a Crux result. |
| Stage 3 C3 cites plan Appendix A for "no BEE in MCUboot" | The Stage 1 narrower label applies: verified for the listed files only. |
| Stage 1 B63: `boot_set_next` at 595-662 "for direct-XIP and RAM load" | Independent review 1: that variant is compiled only for direct-XIP with revert. RAM load with revert uses the swap variant. B63 corrected and B63a added for the test request path. |
| Stage 1 B68 cited for the rank 1 RAM link | Independent review 1: B68 applies to plain RAM load only. B68a added: sysbuild sets `CONFIG_XIP=n` and `CONFIG_FLASH_SIZE=0`. |
| Stage 1 B62: an image without a header is not booted | Independent review 1: the bootloader erases the whole slot of a selected image without GOOD trailer magic. |
| Stage 2 write extents: "full swap of both slots", "erase the whole primary slot" | Independent review 1: swap and overwrite stop at the image size. Only the revert paths of RAM load and direct-XIP erase the whole slot. |
| Stage 3 D10 and summary: the boot ROM runs the bootloader DCD, "Not verified by build" | Independent review 1: vendor boot ROM behaviour, Not verified. A build cannot show it. D11 "needs a Crux overlay" is an Inference. |
| Stage 3 D3 and summary: SW7 boot switch settings from the Zephyr board document | The Zephyr document uses EVK (Rev A) names. On the EVKB the boot switch is SW4, Serial Downloader is SW4[3:4] = `01` (read as `OFF-OFF-OFF-ON` with SW4[1:2] OFF, an Inference), `ON-OFF-ON-OFF` selects SD card boot, and SW7 is a reset button. Source: EVKB Board User Manual Rev. 0, Tables 9 and 10, read through a summarising fetch, not a full read. |
| Earlier record wording "RT1170-EVK is deferred" (target line and scope row 5, 1 November 2026) | Superseded on 2 November 2026 by the owner decision above. Stage 4 keeps its dated wording with a note. Stage 0 keeps the dated PR #148 line. |
| Stage 3 D5: SD boot switch settings not verified | Partly answered: the manual gives a switch setting. Whether SD boot works without fuse settings is still Not verified. |
| Stage 2 section 1 and earlier record: the bootloader executes in place from NOR, "Not verified by build" | Stage 5: Verified by build for both trial modes. |
| Stage 1 B56 and B87 (Inference): MCUboot RAM defaults to SDRAM | Stage 5 Build B: MCUboot RAM is 27,904 B in SDRAM from `0x80000000` by default. Verified by build. |
| Stage 2 and earlier record: which FlexSPI driver path MCUboot links is Not verified | Stage 5 and 6: the ITCM-relocated FlexSPI NOR driver, with its configuration object in NOR and the #22 data-closure failure in the busy-wait path. |
| Stage 1 B37 to B47 and earlier record: whether ECDSA P-256 signatures are randomised is Not verified | Stage 8: randomised for `imgtool` 2.4.0 with `cryptography` 50.0.2. RSA-PSS and Ed25519 remain Not verified. |

## Not verified

| Item | Needs |
| --- | --- |
| Effective trailer use on NOR (the 198 B, 48 B, 4,246 B and 4,096 B figures are MCUboot CMake estimates, not passed to imgtool), and whether the swap using offset estimate covers the one-sector offset (S2-9) | Source reading or a board run |
| RAM load application: indirect references into NOR (literal pointers, data pointers), and the link of a composition that uses ITCM or DTCM | Disassembly or pointer analysis of the RAM-linked ELF, and a RAM load build of such a composition |
| Shared-data survival across the bootloader-to-application jump | Hardware, outside #141 |
| Closure of the bootloader write path against the #22 matrix in every phase. The data-closure failure in the busy-wait path is found (Stage 6). | Remediation decision (owner decision 16), then a closure audit. Not expected within #141. |
| That the build path is the only cause of the ELF debug-section differences, and reproducibility in another container, host or date | A rebuild in the same path, a rebuild elsewhere |
| Trial results at the PR base `f57ff22` or the revision that starts M8 | A rebuild at that revision |
| Randomness of RSA-PSS salts and determinism of Ed25519 in the `cryptography` library | Library source reading |
| Whether mcumgr image management avoids the slot 1 problem (B63a). The trial sample has no update code, so the trial does not show how the inactive slot is marked. | Source reading of the mcumgr image group, or an application build with update code |
| Duration of a whole-slot revert erase under bootloader XIP, and watchdog behaviour during the RAM load copy and the erase. The trial MCUboot configures no watchdog (`CONFIG_WATCHDOG` and `BOOT_WATCHDOG_FEED` not set, `BOOT_WATCHDOG_SETUP_AT_BOOT` absent). | Hardware |
| Cache and MPU state at the bootloader-to-application jump on the Cortex-M7 after a CPU copy into write-back cached SDRAM. Both trial builds have `BOOT_DISABLE_CACHES=y` and `MCUBOOT_CLEANUP_ARM_CORE=y` (Verified by build). `MCUBOOT_CLEANUP_RAM` is not offered (B69). | Hardware |
| Rejection of an oversized image at the slot boundary | A deliberate oversized build |
| Configuration values of the current EVKB compositions and the latest `main` CI artefact contents. Partly closed: the controller sample at `1d6ac4f` was rebuilt as the Stage 5 baseline. | CI artefact access or a Dev Container rebuild at the current revision |
| The 65,964-byte image size in the M1 readiness record | Identification of its composition |
| Whole-tree absence of BEE, SD or MMC handling, SEMC initialisation and a non-volatile counter backend in MCUboot | Content search of the pinned tree |
| That the boot ROM runs the bootloader's DCD, SDRAM state across the bootloader-to-application jump, RAM load copy time | Hardware, outside #141 |
| Any revert, recovery or interrupted-update behaviour | Hardware, outside #141 |
| Which erase size the update path may rely on (4 KiB sector and 64 KiB block in the devicetree and storage assessment, 64 KiB in the LinkServer algorithm). 64 KiB alignment is a Stage 6 recommendation, not a finding. | Stage 6 layout, then a board measurement |
| Recovery of a corrupted bootloader through boot ROM paths, for example the serial downloader | Reference manual, hardware |
| RT1060 open-configuration HAB behaviour with blank SRK hash fuses, and event readback means | Security reference manual and a board |
| BEE key default (conflict between C2 and C4) and fuse needs | Security reference manual |
| SPSDK handling of a Zephyr image with FCB and IVT, and RT1060 HAB key types | A tool check under A2 (given for SPSDK, not yet used) |
| SD or eMMC ROM boot on EVKB without fuse settings. A switch setting is now known from the manual through a summarising fetch. | Reference manual and a full read of the board user manual |
| RT1060 fuse irreversibility and the closed-configuration procedure | Reference manual and security reference manual |
| Licences of SEC, CST and SPSDK binary dependencies, and SEC headless operation | Licence check within A2 (given for the SPSDK download). No result recorded yet. |
| RT1170: any sysbuild configure or bootloader cross-build, bootloader size and RAM cost with 1,792 sectors per slot, and a working RAM load overlay | A run in the Dev Container under the A1 extension of 2 November 2026 |
| RT1170: that the Rev A1 PCB is Zephyr revision `A` with the devicetree's IS25WP128 and SDRAM, and whether the named SDRAM part matches the 64 MiB node | Board inspection (#155), part data sheet |
| RT1170: boot ROM behaviour, including XMCD processing, HAB open configuration and event logging, and recovery of a corrupted bootloader | Reference manual, security reference manual, hardware (#155) |
| RT1170: Rev A1 boot and serial downloader switch settings | Rev A1 user guide, hardware (#155) |
| RT1170: OTFAD key source and fuse needs, which of OTFAD and IEE covers which memory, and any no-fuse evaluation | Security reference manual |
| RT1170: cause of the 133 MHz FCB clock against the 104 MHz devicetree maximum | Flash data sheet |
| RT1170: header addresses of a built image | RT1170 CI map or a rebuild |
| RT1170: erase size the update path may rely on | Board measurement (#155) |

### Rows closed or narrowed by the trial build

| Earlier Not verified row | Result | Source |
| --- | --- | --- |
| MCUboot size and RAM use per candidate mode, effective write alignment and real trailer sizes | Size, RAM and write alignment closed (Verified by build). Trailer sizes narrowed to CMake estimates, see the first row above. | Stage 5 sections 4 and 9 |
| RAM load application ELF has nothing used at runtime in NOR | Closed for sections, LOAD segments and symbols. Indirect references remain open (row above). | Stage 5 section 7 |
| Configuration-only fix for the SDRAM address conflict, and the share-data backend with a retained memory region | Build part closed: both configure and link without a source patch. Hardware part remains (shared-data survival row above). | Stage 5 section 5 |
| Which FlexSPI driver path the bootloader links, and its #22 closure | Driver path closed. Closure not closed, and a data-closure failure was found (row above). | Stage 5 section 8, Stage 6 section 3 |
| Reproducibility of unsigned images and signature verification of signed images | Closed for two clean builds in the same container. Same-path confirmation and other environments remain open (row above). | Stage 8 |
| Randomness of ECDSA nonces and RSA-PSS salts, determinism of Ed25519 | ECDSA closed: randomised in this toolchain. RSA-PSS and Ed25519 remain open (row above). | Stage 8 section 2 |
| Python version in the Dev Container | Closed: Python 3.12.3 in the container created from the repository Dev Container image | Results `environment.txt` |
| Rank 1 test request reaches the inactive slot in the trial configuration | Answered for the trial: it does not, because the sample has no update code and the default signed image has no trailer magic. mcumgr remains open (row above). | Stage 5 sections 5 and 9 |
| Watchdog and cache `.config` checks for Stage 5 | `.config` part closed. Hardware part remains (rows above). | Stage 5 section 6 |

## Next steps

| Step | Content |
| --- | --- |
| Stages 5, 6 and 8 | Done as build-only evidence on 2 November 2026. Independent review of the new stage files and of the record changes is needed. |
| Owner decisions 13, 16, 17 and 18 | Decide the rank 1 test-request method, the treatment of the bootloader data-closure failure, the storage region and slot size, and the development key handling |
| Trial key | Delete the development private key from the shared volume, or move it, as decided under owner decision 18 |
| Stage 7 | Independent review of the Stage 7 desk study. RT1170 sysbuild configure and bootloader cross-build under the A1 extension of 2 November 2026, in the Dev Container. |
| SPSDK | Optional tool check under A2 (given for SPSDK): offline HAB image structure of the bootloader, and whether SPSDK accepts a Zephyr image with FCB and IVT |
| Stage 9 | Independent review by a reviewer who did not author this record, recorded as PASS, REVISE or ESCALATE |
| A3, A4 | A3 is accepted in part (7 November 2026). The remaining rows are 12 and 15. Merge approval (A4) follows a narrow independent review of this update. |

This record does not claim that any image boots, that a layout is safe or
adopted, that MCUboot is adopted, that secure boot, a hardware root of trust or
anti-rollback is achieved, that HAB authentication was observed, that microSD
staging works, that Flash writes during XIP are safe, that the bootloader write
path is safe, that the build is reproducible beyond the tested comparison, or
that any M8 or M9 criterion is met. All trial results are cross-compilation
evidence only. #141 stays open until A3 is complete for rows 12 and 15 and until its acceptance evidence.
