# Boot and update investigation for RT1060-EVKB

| Field | Value |
| --- | --- |
| Issue | [#141](https://github.com/wonny-labs/crux-pilot/issues/141) "Investigate verified boot and firmware update feasibility for RT1060-EVKB" |
| Status | **Draft. Desk results only.** Stages 5, 6 and 8 are pending the A1 trial build (approved 1 November 2026, running separately in the Dev Container). No hardware or fuse operation was performed. Nothing in this record is adopted. Revised after independent review 1 of PR #151 (verdict REVISE, findings F1 to F15). |
| Target | RT1060-EVKB Rev B, `mimxrt1060_evk@B/mimxrt1062/qspi`. RT1170-EVK is deferred by owner decision of 1 November 2026. |
| Pinned upstream | Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0). MCUboot `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2`. `hal_nxp` `2c2f28ac333e995d7279777409817c4b4f92c1ec`. |
| crux-pilot read | `f67e22a` (Stages 0, 3 and 4) and `f57ff22` (Stage 2, after #147 merged) |
| Plan | [Pre-M8 investigation plan](plans/pre-m8-investigation-plan.md) |
| Evidence | [docs/evidence/boot-update/](evidence/boot-update/README.md) |

## Purpose and scope

#141 asks for the technical basis for verified boot and firmware update on
RT1060-EVKB Rev B before any implementation is planned. Its results inform the public
milestones M8 "Boot platform foundation and verified boot" and M9 "Secure
firmware update and recovery". It does not meet any M8 or M9 criterion.

| #141 scope item | Covered by | State in this draft |
| --- | --- | --- |
| 1. MCUboot against the adoption criteria | Stages 1, 2, 3 and 5 | Desk results recorded. Size and RAM cost pending Stage 5. |
| 2. Update mode and staging medium | Stage 2 | Recommendation recorded |
| 3. Flash writes during XIP for the candidate mode | Stages 2 and 6 | Desk summary recorded. Full write inventory pending Stage 6. |
| 4. Current placement and a fenced layout | Stages 4 and 6 | Placement re-verified from committed records. Layout pending Stage 6. |
| 5. (Removed on 1 November 2026, RT1170-EVK deferred) | None | Not applicable |
| 6. HAB open configuration without fuse writes | Stage 3 | Documentation result recorded. RT1060 behaviour not verified. |
| 7. Reproducible bootloader and signed image cross-build | Stages 5 and 8 | Pending. Desk findings on signing recorded. |

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
| A1 | Fetch MCUboot at the pin outside the repository, install imgtool dependencies in a separate virtual environment, generate development keys outside the repository | Given on 1 November 2026, for two trial builds: rank 1 (RAM load with revert) and rank 2 (swap using offset) |
| A2 | Licence review before any NXP signing tool is downloaded (CST, SPSDK, SEC) | Not given. Stage 3 stayed documentation-only. |
| A3 | Gate 1: accept or reject the recommendations in this record | Pending, after independent review |
| A4 | Gate 2: merge the PR | Pending, after A3 |
| A5 | Any manifest, module or Dev Container change | Not part of this investigation |

## Evidence labels

Labels are used exactly as each stage recorded them. They are never upgraded
in this summary.

| Label | Meaning |
| --- | --- |
| Verified on pinned source | Checked at a cited file and line of a pinned revision. Shows what the code allows, not what a board does. |
| Verified by public vendor document | Stage 3 only. A public vendor page or repository says so. It is the vendor's statement, not a Crux result. Where a document was read only through a summarising fetch, the label says "summarising fetch, not a full read". |
| Verified by artefact | Stage 4 only. Read from a committed build output, record or map. |
| Inference | Reasoned from verified lines. |
| Not verified | Not checked, not accessible or inferred. The reason is given in the evidence file. |
| Verified by build | Not used yet. Reserved for Stages 5 and 8. |

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
| Builds, tools, hardware | No build was run. No tool was downloaded or installed. No licence was accepted. No hardware was used. |

| Stage | Evidence |
| --- | --- |
| 0 Baseline | [stage0.md](evidence/boot-update/stage0.md) |
| 1 MCUboot capability on pinned source | [stage1-pinned-source-findings.md](evidence/boot-update/stage1-pinned-source-findings.md), [stage1-answers.md](evidence/boot-update/stage1-answers.md) |
| 2 Update mode and staging | [stage2-matrix.md](evidence/boot-update/stage2-matrix.md), [stage2-recommendation.md](evidence/boot-update/stage2-recommendation.md) |
| 3 HAB, BEE, boot media, signing tools | [stage3-findings.md](evidence/boot-update/stage3-findings.md), [stage3-summary.md](evidence/boot-update/stage3-summary.md) |
| 4 Placement | [stage4.md](evidence/boot-update/stage4.md), [placement-rt1060.json](evidence/boot-update/placement-rt1060.json) |
| 5, 6, 8 | Pending the A1 trial build |

Row identifiers in brackets below (for example B61 or S2-4) point to the
evidence files. A, B and L rows are in the Stage 1 findings. S2, V and R rows
are in the Stage 2 matrix. Stage 3 claims are written as "Stage 3 A13" and so
on.

## MCUboot against the adoption criteria

| Criterion | Result | Label |
| --- | --- | --- |
| Sysbuild integration | Sysbuild offers MCUboot for every board and builds it as a separate image from MCUboot `boot/zephyr/` (B1, B2). The board QSPI devicetree has `mcuboot`, `image-0` and `image-1` partitions and no scratch partition (A9, B5). MCUboot lists `mimxrt1060_evk/mimxrt1062/qspi` as a sample target, which means the default revision C (A2). MCUboot has an i.MX RT FlexSPI branch (B6). MCUboot has no RT1060 board file at the pin (Stage 3 D11). | Verified on pinned source. A listing is not a passing build. Build result Not verified. |
| Modes on XIP NOR | Sysbuild lists eleven modes, not nine as the plan said (A3). The default is swap using offset (A4, B11). Swap using scratch needs a devicetree change. | Verified on pinned source |
| SoC XIP encryption (BEE) | No BEE handling in the MCUboot and Zephyr files read (A8, B72, B73). MCUboot image encryption is a different feature: it protects a staged image at rest (B70). See [BEE](#bee-xip-encryption). | Verified for the listed files. Whole-tree absence Not verified. |
| Validation on every boot | On by default in swap modes (`BOOT_VALIDATE_SLOT0`). Unconditional in direct-XIP and RAM load. RAM load validates the copy in RAM (A5, B57). | Verified on pinned source |
| Crypto backend without Zephyr crypto modules | ECDSA P-256 with tinycrypt from MCUboot's own `ext/` directory needs no Zephyr crypto module. The sysbuild default is RSA, which needs Zephyr mbedTLS, so ECDSA P-256 must be set explicitly (A4, A6, B31, B32, B35). | Verified on pinned source |
| RAM and Flash cost | Not measured. The vendor SDK example reserves 256 KiB for its own MCUboot port (V1). That is a warning about headroom against the 128 KiB `mcuboot` partition, not evidence about the Zephyr build. | Not verified (Stage 5) |
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
| Remedies | Application code that opens the inactive slot (from `BLINFO_RUNNING_SLOT`) and calls `boot_set_next(fa, false, permanent)`, or images signed with trailer magic (imgtool `--pad`). With a padded image, writing the image is itself the test request. | Code paths Verified on pinned source. Effect of padding is an Inference. |
| mcumgr image management | Whether it avoids the problem was not checked. | Not verified |

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

## Flash writes during XIP (desk part)

The full write inventory by actor and phase belongs to Stage 6 and is pending.
The desk results so far:

- The bootloader keeps the NXP boot header and DCD and executes in place from
  QSPI NOR at `0x60000000` in every mode (A7, A9, and MCUboot
  `boot/zephyr/app.overlay`:1-5, which sets `zephyr,code-partition` to
  `boot_partition`). Every bootloader write therefore happens while it executes
  from the same NOR. Not verified by build.
- The [Flash XIP phase evidence](flash-xip-evidence.md) from #22 applies to the
  bootloader's FlexSPI NOR driver path as well as to the application. It
  records a concrete busy-phase data-closure failure in the inherited driver
  image and unknown exception, DMA and bus-master closure.
- In XIP modes, the application's update writes are exactly the case that #22
  found blocked. A network download adds Ethernet DMA, which #22 lists as an
  open bus-master item.
- In RAM load modes, the application-side hazard changes form. It reduces to
  showing that no code, data, vector or constant used during the write is in
  NOR. Bus masters that reference NOR, cache coherence and FlexSPI
  reconfiguration still need review. Not verified.
- Write extent differs by mode. Swap and overwrite write per update only up to
  the image size. The RAM load and direct-XIP revert paths erase the whole
  3 MiB slot under bootloader XIP. The duration of that erase and the watchdog
  behaviour during it are Not verified (Stage 6, then hardware).
- Which FlexSPI driver path MCUboot links is Not verified. Closure of the
  bootloader write path is not expected within #141.

## Current placement

Stage 4 re-verified the current placement from committed records only. The
latest `main` CI artefact could not be downloaded, and nothing was rebuilt.

| Composition | Source | Boot header addresses | Highest Flash load end | Label |
| --- | --- | --- | --- | --- |
| `reference-normal` (historical baseline `fd9e341`) | `docs/evidence/storage-configuration/verification.json` | FCB `0x60000000`, IVT `0x60001000`, boot data `0x60001020`, DCD `0x60001030` | `0x6001a560` (107,872 bytes) | Verified by artefact. `CONFIG_FLASH_LOAD_OFFSET=0`, `CONFIG_BOOTLOADER_MCUBOOT=n`, `CONFIG_NXP_IMXRT_BOOT_HEADER=y`. |
| `execution_runtime` (EVKB map) | `docs/evidence/m5-execution/` | Same addresses | `0x6000d148` (53,576 bytes) | Verified by artefact for addresses. Configuration values Not verified. |
| `portable_execution` (EVKB map) | `docs/evidence/m5-execution/` | Same addresses | `0x60010348` (66,376 bytes) | Verified by artefact for addresses. Configuration values Not verified. |
| Latest `main` CI EVKB builds (`f67e22a`) | CI artefact | Not read | Not read | Not verified |

The upstream partitions are `mcuboot` `0x000000` to `0x020000`, `image-0`
`0x020000` to `0x320000`, `image-1` `0x320000` to `0x620000` and `storage`
`0x620000` to `0x800000`, with 4 KiB erase blocks (A9). None of the three
images exceeds 128 KiB.

Open item for Stage 6: the real erase unit. The devicetree declares 4 KiB, and
the [storage assessment](storage-configuration-assessment.md) records 4 KiB
sectors and 64 KiB erase blocks for this NOR. The LinkServer Flash algorithm
used on this board detected the 8 MiB device as 128 sectors of 64 KiB and wrote
in 64 KiB sectors
([#14 comment of 30 October 2026](https://github.com/wonny-labs/crux-pilot/issues/14#issuecomment-6530776373)).
Keep partition boundaries 64 KiB aligned until the real erase unit is measured
on the board. The upstream partition boundaries are already 64 KiB aligned.
 The 65,964-byte image size in the
[M1 readiness record](platform-m1-readiness.md) matches no retained map or
record and is unreconciled. The fenced layout proposal is pending Stage 6.

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
debug access can replace the bootloader and its embedded public key. There is
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
| Application images | MCUboot `imgtool`, pinned in-tree version 2.4.0 | Apache-2.0. Already used by `west sign` and sysbuild (Stage 3 A9). Recommended for the A1 trial. |
| Scripted HAB signing of the bootloader | SPSDK `nxpimage hab`, 3.12.0 | BSD-3-Clause. Scripted YAML workflow without a GUI (B6). Python 3.10 or later and 34 required dependency entries. Licences of binary dependencies not checked. Deferred until A2. |
| Optional manual cross-check | MCUXpresso Secure Provisioning Tool (SEC) 26.09 | GUI front-end over SPSDK. Download needs an NXP account. Licence name not shown. Not a reproducibility path (B10 to B12). |
| Legacy HAB signer | NXP CST | Click-through licence LA_OPT36. Superseded by SPSDK (B6, B13). Not recommended. |

Stage 1 desk findings for the signed build (B37 to B47):

- Sysbuild's default signing key is MCUboot's public debug key, and the build
  warns about it. The trial must set its own development key.
- The public key is embedded through `imgtool.py getpub` into a generated
  source file in the build directory. The Zephyr port holds exactly one key.
- imgtool writes no timestamp. The unsigned payload and hash are therefore
  reproducible on source. Whether ECDSA P-256 signatures are randomised depends
  on the `cryptography` library and is Not verified. The plan's criterion does
  not need signature determinism: unsigned images must reproduce byte for byte
  and signed images must verify against the documented public key.
- Encrypted images are never byte-reproducible because the AES key is random.

Whether SPSDK accepts a Zephyr image that already has an FCB and IVT is Not
verified (Stage 3 A14).

## Boot media, SDRAM and DCD

| Point | Result | Label |
| --- | --- | --- |
| SDRAM initialisation | The board enables DCD and SDRAM by default. The DCD in the boot header programs the SEMC (B80 to B82). Zephyr's SoC code does not initialise SEMC (B84). | Verified on pinned source |
| Application built for MCUboot | Has no boot header and no DCD. It relies on the bootloader's DCD having run (A7, B86). | Build logic Verified on pinned source. That the boot ROM runs the bootloader's DCD is Not verified. A build cannot show it. Stage 3 D10 and the Stage 3 summary have been corrected to this label. |
| RAM load address conflict | The board `zephyr,sram` is 32 MiB SDRAM at `0x80000000`. Bootloader RAM and the RAM load address both default to it (B53 to B56). MCUboot has no RT1060 overlay to change it. Its RT1050 RAM load overlay places retained boot information in SDRAM at `0x80007f00` (Stage 3 D11). On this board that would overlap an image loaded at `0x80000000`. | Lines Verified on pinned source. Conflict and the need for a Crux overlay are Inferences. Not verified by build. |
| Boot from microSD as an eMMC analogue | Pinned Zephyr cannot build an SD or eMMC boot image (D1). SPSDK lists SD, MMC and eMMC boot layouts for this family (D4). The EVKB Board User Manual gives SW4 `ON-OFF-ON-OFF` for SD card boot (Table 10, D5 as corrected). | Switch setting: Verified by public vendor document (summarising fetch, not a full read). SD boot on EVKB without fuse settings: Not verified (D5 partly answered). |
| Boot switch names | The Zephyr board document uses EVK (Rev A) names: SW7 for the boot switch and SW9 for reset (D3). On the EVKB the boot switch is SW4. QSPI boot is `OFF-OFF-ON-OFF`, recorded for the physical board in [baseline](baseline.md). Serial Downloader is SW4[3:4] `OFF ON`, that is `OFF-OFF-OFF-ON` (manual Table 9). SW7 is a reset button on the EVKB. | QSPI setting: repository record. Serial Downloader and SD settings: Verified by public vendor document (summarising fetch, not a full read). SW7 role: same, summary only. Rev A naming: Inference. |
| Recovery of a corrupted bootloader | The bootloader has one copy. Recovery depends on boot ROM paths, for example the serial downloader. Stage 3 did not answer this, although Stage 2 marked it pending Stage 3. | Not verified |

## Execution model

**Recommendation: evaluate RAM load into SDRAM as the M8 reference candidate.
Keep the standalone QSPI XIP image as the reference until M7 target validation
is closed.** Confirm the RAM load choice only after Stages 5 and 6 show no
blocker. If they show one, fall back to XIP with swap using offset.

Reasons: it is the only stock path where the application's update writes do
not happen during its own XIP. It is a close analogue of SDRAM execution
loaded from block storage, which the platform keeps open. It keeps MCUboot image encryption available, which direct-XIP
does not.

Conditions before adoption (A3):

| Condition | Rows |
| --- | --- |
| Bootloader RAM separated from the image load window by configuration | B53 to B56 |
| Share-data backend and retained memory configured for the confirm path. The retention region must lie outside the image load window and the application's linked RAM. | B64, Stage 3 D11 |
| Upgrade request reaches the inactive slot: application code that marks it, or images signed with trailer magic (imgtool `--pad`) | B63a |
| Application ELF shows no code, data, vectors or constants in NOR. The RAM link comes from sysbuild `CONFIG_XIP=n` and `CONFIG_FLASH_SIZE=0`. `USE_DT_CODE_PARTITION=y` is expected in the application `.config` and does not mean a Flash link. | B68a, R2 |
| Bootloader size fits the `mcuboot` partition or a revised layout | A9, Stage 5 |
| Bootloader FlexSPI write path reviewed against the #22 matrix | R2, Stage 6 |

Effect on M6 "Device interfaces and platform configuration" and M7 "Controller
platform integration and validation" evidence:

| Effect | Detail |
| --- | --- |
| Image identity | Any bootloader composition changes the application link address, removes the application boot header and DCD, and changes ELF and binary hashes. Evidence tied to the standalone image hash does not transfer automatically. |
| Programming and debug | Procedures that program a standalone image at `0x60000000` would change. Two images, the bootloader and the signed application, must be programmed and identified. Under rank 1, a programmed application image must carry trailer magic and be confirmed (for example Zephyr's `*.signed.confirmed.hex`, Zephyr `cmake/mcuboot.cmake`:246-255), or the bootloader erases the slot when it selects it (MCUboot `loader.c`:2199-2211). |
| Peripheral behaviour | Driver behaviour should not change, but this is an inference. A defined smoke subset would need re-running. Not verified. |
| Timing | Execution from SDRAM instead of cached QSPI and ITCM can change timing. Timing evidence would need re-measurement. Which records depend on timing is Not verified. |
| SDRAM dependence | Current RAM is already SDRAM. RAM load makes code depend on it too. |

Timing recommendation: change the reference composition at the start of M8,
after M7 target validation, so that no milestone's evidence becomes invalid
part-way through. Stage 6 is to confirm this.

## Network-delivered updates

The bootloader verifies the signature at boot, so unsigned code cannot run. An
unauthenticated update request could still cause NOR writes and erases, mark an
image pending or permanent, or install an older signed image in modes without
rejection. Before any network-facing update interface (M9), update requests
must be authenticated and authorised, and the application should check
signature, version and size before it writes NOR. An application-side
signature check needs an application crypto provider (#142, unscheduled).
Source: [stage2-matrix.md](evidence/boot-update/stage2-matrix.md) section 6.

## Recommendations and decisions

Nothing in this record is adopted. The owner decisions for #141 are the
RT1170-EVK deferral, A0 and A1. All other rows are recommendations for A3.

| Item | Content | Status |
| --- | --- | --- |
| Bootloader | MCUboot at the Zephyr pin, ECDSA P-256 with tinycrypt | Recommendation |
| Update mode | Rank 1 RAM load with revert, rank 2 swap using offset, rank 3 direct-XIP with revert | Recommendation |
| Rank 1 test request | Application code that marks the inactive slot, or images signed with trailer magic | Recommendation, owner decision needed |
| Trial builds | Rank 1 and rank 2 | Approved (A1, 1 November 2026) |
| Staging medium | Variant (c), NOR slot, optionally through an SDRAM buffer | Recommendation |
| Execution model | RAM load into SDRAM as M8 reference candidate, after M7 target validation | Recommendation |
| Application version policy | Reject older images in the application before any NOR write | Recommendation, owner decision needed |
| Signing tools | `imgtool` for application images. SPSDK only after A2. SEC optional. No CST. | Recommendation |
| BEE | Not part of the candidate path | Recommendation, owner decision needed |
| HAB | Documentation result only. Event readback and closed configuration are later, separately approved work. | Recommendation |
| Fenced layout | Pending Stage 6 | Not yet available |

## Owner decisions needed

The recommended options summarise the stage recommendations. Rows 8, 11, 12
and 13 have no stage recommendation and are left to the owner.

| # | Decision | Recommended option | Source |
| --- | --- | --- | --- |
| 1 | Accept the constraint that no stock mode avoids NOR writes during XIP with test, confirm and recovery, or open a separate scope for bootloader customisation (variant b) | Accept the constraint for M8 | Stage 2 decision 1 |
| 2 | Ranked candidates for the trial build | Done: A1 approved on 1 November 2026 with both builds, rank 1 and rank 2 | Stage 2 decision 2 |
| 3 | Pursue RAM load into SDRAM as the M8 reference execution model, and when the reference composition may change relative to M7 | Pursue it, change at M8 start after M7 target validation, subject to Stages 5 and 6 | Stage 2 decision 3 |
| 4 | Require an application-side version policy (no older image, compared on major, minor and revision) | Require it, because the RAM load bootloader provides none | Stage 2 decision 4 |
| 5 | Before any network-facing update interface (M9), require authentication and authorisation of update requests and an application-side check before any NOR write | Require it | Stage 2 decision 5 |
| 6 | Whether the microSD may later hold a staging path (variant a), which would change the #147 card layout | Defer. Stage 2 rates variant (a) as an optional later variant. | Stage 2 decision 6 |
| 7 | A2 licence review before any download of SPSDK and its binary dependencies, SEC or CST | Keep documentation-only until needed | Stage 3 decision 1 |
| 8 | Whether a later, separately approved hardware step may read HAB events in open configuration, without fuse writes | Owner choice | Stage 3 decision 2 |
| 9 | Accept the plan corrections listed below | Accept | Stage 3 decision 3 |
| 10 | Whether BEE stays out of M8, since it needs customised MCUboot and overwrite-only | Keep it out | Stage 3 decision 4 |
| 11 | Whether to plan a dedicated board for future closed-configuration work | Owner choice. The EVKB is listed as no longer manufactured (Stage 3 E2). | Stage 3 decision 5 |
| 12 | Whether to adopt the #141 scope wording corrections in plan section 9 | Owner choice | Plan section 9 |
| 13 | Rank 1 needs either application code that marks the inactive slot for test, or images signed with trailer magic (imgtool `--pad`). Which one should the trial and M8 use? | Owner choice. Both are stock on source. Padding makes every written higher-version image a test request (Inference). | Independent review 1, F1. Stage 2 decision 7. |

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
| Header: crux-pilot baseline `bb7f425` | Stage 0 read `f67e22a`. `origin/main` then moved to `f57ff22` when #147 merged. Stage 2 read `f57ff22`. Stages 3 and 4 cite `f67e22a`. | Stage 0, Stage 2 section 0 |

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
| Stage 3 D3 and summary: SW7 boot switch settings from the Zephyr board document | The Zephyr document uses EVK (Rev A) names. On the EVKB the boot switch is SW4, Serial Downloader is `OFF-OFF-OFF-ON`, `ON-OFF-ON-OFF` selects SD card boot, and SW7 is a reset button. Source: EVKB Board User Manual Rev. 0, Tables 9 and 10, read through a summarising fetch, not a full read. |
| Stage 3 D5: SD boot switch settings not verified | Partly answered: the manual gives a switch setting. Whether SD boot works without fuse settings is still Not verified. |

## Not verified

| Item | Needs |
| --- | --- |
| MCUboot size and RAM use per candidate mode, effective write alignment and real trailer sizes | Stage 5 build |
| RAM load application ELF has nothing used at runtime in NOR | Stage 5 build and ELF analysis |
| Configuration-only fix for the SDRAM address conflict, and the share-data backend with a retained memory region | Stage 5 build, then hardware |
| Which FlexSPI driver path the bootloader links, and its #22 closure | Stage 6 ELF analysis. Closure is not expected within #141. |
| Reproducibility of unsigned images and signature verification of signed images | Stage 8 |
| Configuration values of the current EVKB compositions and the latest `main` CI artefact contents | CI artefact access or a Dev Container rebuild |
| The 65,964-byte image size in the M1 readiness record | Identification of its composition |
| Whole-tree absence of BEE, SD or MMC handling, SEMC initialisation and a non-volatile counter backend in MCUboot | Content search of the pinned tree |
| Randomness of ECDSA nonces and RSA-PSS salts, determinism of Ed25519 in the `cryptography` library | Library source reading |
| That the boot ROM runs the bootloader's DCD, SDRAM state across the bootloader-to-application jump, RAM load copy time | Hardware, outside #141 |
| Any revert, recovery or interrupted-update behaviour | Hardware, outside #141 |
| Rank 1 test request reaches the inactive slot in the trial configuration, and whether mcumgr image management avoids the slot 1 problem | Stage 5 or 6 check of the application path |
| Duration of a whole-slot revert erase under bootloader XIP, and watchdog behaviour during the RAM load copy and the erase (`BOOT_WATCHDOG_SETUP_AT_BOOT`) | Stage 6, then hardware |
| Cache and MPU state at the bootloader-to-application jump on the Cortex-M7 after a CPU copy into write-back cached SDRAM. MCUboot defaults `BOOT_DISABLE_CACHES=y` and `MCUBOOT_CLEANUP_ARM_CORE=y`. `MCUBOOT_CLEANUP_RAM` is not offered (B69). | Stage 5 `.config` check, then hardware |
| Real NOR erase unit on the board (4 KiB in the devicetree, 64 KiB in the LinkServer algorithm) | Stage 6 layout, then a board measurement |
| Recovery of a corrupted bootloader through boot ROM paths, for example the serial downloader | Reference manual, hardware |
| RT1060 open-configuration HAB behaviour with blank SRK hash fuses, and event readback means | Security reference manual and a board |
| BEE key default (conflict between C2 and C4) and fuse needs | Security reference manual |
| SPSDK handling of a Zephyr image with FCB and IVT, and RT1060 HAB key types | A2 and a tool check |
| SD or eMMC ROM boot on EVKB without fuse settings. A switch setting is now known from the manual through a summarising fetch. | Reference manual and a full read of the board user manual |
| RT1060 fuse irreversibility and the closed-configuration procedure | Reference manual and security reference manual |
| Licences of SEC, CST and SPSDK binary dependencies, and SEC headless operation | A2 |
| Python version in the Dev Container | Dev Container check |

## Next steps

| Step | Content |
| --- | --- |
| Stage 5 | Add the trial build inputs, provenance for the bootloader and each signed application, and the size, RAM and configuration results for rank 1 and rank 2. Check in the rank 1 application `.config` that `CONFIG_XIP=n`, `CONFIG_FLASH_SIZE=0` and `USE_DT_CODE_PARTITION=y`, and in the bootloader `.config` the values of `BOOT_DISABLE_CACHES`, `MCUBOOT_CLEANUP_ARM_CORE` and `BOOT_WATCHDOG_SETUP_AT_BOOT`. Record how the rank 1 trial marks the inactive slot for test. |
| Stage 6 | Add the fenced layout proposal with 64 KiB aligned partition boundaries, the NOR write inventory by actor and phase (including whole-slot revert erases), and the layout change timing against M6 and M7 evidence |
| Stage 8 | Add the reproducibility comparison, `imgtool verify` output, public key hash and the check that no private key is in the repository, logs or evidence |
| Stage 9 | Independent review by a reviewer who did not author this record, recorded as PASS, REVISE or ESCALATE |
| A3, A4 | Owner decision on the recommendations, then merge approval |

This record does not claim that any image boots, that a layout is safe or
adopted, that MCUboot is adopted, that secure boot, a hardware root of trust or
anti-rollback is achieved, that HAB authentication was observed, that microSD
staging works, that Flash writes during XIP are safe, or that any M8 or M9
criterion is met. #141 stays open until A3 and its acceptance evidence.
