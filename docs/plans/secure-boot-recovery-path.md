# Secure boot recovery path: options and recommendation

> **Status:** Draft proposal. It adopts nothing. The decisions it cites were taken by the project owner and are recorded in the pages it links; everything else here is a recommendation or an option for the project owner to accept or reject.
> **Verified:** What exists on `main` on 15 November 2026: the MCUboot composition builds and signs in CI (Verified by build), and the earlier bench records show that the EVKB can be programmed and restored through a debug probe (Verified in repository). The option descriptions repeat the labelled rows of the boot pages and add no new source reading.
> **Not verified:** Everything that needs a board in the boot configuration. Nothing has been booted: no recovery path of this note has been run with MCUboot, the ROM serial downloader check (#309) has not been run, and no revert, confirm or update has been exercised.
> **Read this first / then:** the [boot overview](../boot-overview.md), then section 1 and the table in section 3 of this note, then the [secure boot development plan](secure-boot-development-plan.md) (section 2 for the decisions already taken, section 6 for the open ones).

| Field | Value |
| --- | --- |
| Status | **Draft.** A design note with options and a recommendation, no code. State on 15 November 2026. |
| Phase and milestones | Phase 1 of the plan (the non-destructive recovery path that is proved before any physical boot experiment), with M8 [#296](https://github.com/wonny-labs/crux-pilot/issues/296) and M9 [#297](https://github.com/wonny-labs/crux-pilot/issues/297). eMMC points cross-reference M14 and [#298](https://github.com/wonny-labs/crux-pilot/issues/298). |
| Records it builds on | [Boot and update investigation](../boot-update-investigation.md), [Updating or recovering a deployed bootloader](../boot-bootloader-update-investigation.md), [eMMC boot and update feasibility](../boot-emmc-investigation.md), [boot composition](../boot-composition.md), the [image state contract proposal](image-state-contract-proposal.md), [image profiles for the program and restore path](../board-image-profiles.md) and the [baseline log](../baseline.md). |
| Not in this note | Fuse programming, closed-configuration behaviour that only a confidential vendor manual describes, any hardware operation, any code. The confidentiality boundary of the plan (its section 10) applies: statements about the boot ROM, HAB and fuses come only from the repository pages and keep their labels. |

## Evidence labels

The labels are those of the boot pages. They are never upgraded here.

| Label | Meaning |
| --- | --- |
| Verified by build | Read from the output of a build that exited 0. Shows that something configures, builds and signs, never that a board boots. |
| Verified by command | The same for a `west` command, a configure or a test. |
| Verified on pinned source | Checked at a file and line of a pinned Zephyr, MCUboot or `hal_nxp` revision. Shows what the code allows, not what a board does. |
| Verified by public vendor document | A public vendor or project document says so (for the RT1060 reference manual, a third-party-hosted copy that was not compared with the vendor's own). It is the author's statement, not a Crux result. |
| Verified in repository | A record in this repository says so, for example a bench record or a decision row. It is as strong as that record. |
| Inference | Reasoned from rows that the sentence names. |
| Not verified | Not checked, not accessible, or contradicted by another source. |

A statement without a label is a recommendation of this note or a citation of a decision row.

## 1. Purpose and what "recovery" covers

The plan requires a non-destructive recovery path to be proved before any physical boot experiment on a board (plan section 4, Phase 1, and the acceptance text of #296). This note lays out the candidate paths, says what each protects against and what it does not, and recommends what to prove first. It is an input to the board test procedure (being written), not a replacement for it.

"Recovery" is three different problems. The paths in section 3 help with different ones, so the cases are kept apart throughout.

| Case | What went wrong | Example | Decided scope |
| --- | --- | --- | --- |
| R1: a bad application image | The bootloader is intact. The application image is defective, never confirmed, or an update was interrupted. | A signed image that starts but never confirms; a power cut during an update. | In M9's subject ("Secure firmware update and recovery", #297). The update-mode ranking that provides test, confirm and revert is a recommendation of #141, not an adopted decision, although the first composition builds it (plan section 2, last paragraph). The update path itself is Phase 2 and is not started. |
| R2: a bad or damaged bootloader | The bootloader image is damaged and does not start, or it starts but is defective or vulnerable. | A corrupted first sector; a flaw in signature or version checking found after deployment. | **Decided for M8 and M9** on 7 November 2026: a single bootloader copy that is not field-updatable, with service recovery through the ROM serial downloader (plan section 2, row of #297 and #299). The project owner will decide again after #165, #298 and the M8 hardware results. Which service route actually works is Not verified (section 2). |
| R3: a lost or compromised key | An application signing key is leaked or lost, or a HAB root key must be replaced or revoked. | Key compromise, or a planned rotation. | **Open.** No decision covers it. Production keys are out of scope of the plan, and key custody (D6) and the closed configuration (D1) are open decisions. The Zephyr port holds exactly one application signing key (read at the Zephyr v4.4.0 pin of the investigation; the composition now uses v4.4.2, and the row was not re-read there), so a leaked key can only be answered by a new bootloader with other keys (Verified by public vendor document, row A-34 of the [evidence A file](../evidence/boot-bootloader-update/a-mcuboot-zephyr-ecosystem.md), described there as an open proposal and not a shipped feature). R3 therefore reduces to R2 plus key custody (Inference). |

The bootloader record's classes D1 to D4 (damaged, defective, vulnerable, wrong key) are the author's classification (Inference). This note's R2 covers D1 to D3 and R3 covers D4. (The plan's decisions D1 to D10 are a different list. In this note "D" with a number means a decision of the plan, and the failure classes are called R1 to R3.)

## 2. What exists today

| Item | State | Label |
| --- | --- | --- |
| MCUboot composition | The RAM load with revert composition for the RT1060-EVK Rev B is merged. The published argument set configures, builds, signs the application with MCUboot's public test key and passes `imgtool verify` in CI. Nothing was programmed or booted. | Verified by build ([boot composition](../boot-composition.md); [WP1a evidence](../evidence/boot-composition/wp1a-evidence.md)) |
| Boot on a board | No signed image has been booted. The ROM accepting the MCUboot layout, the jump into the application, confirm and revert are all open. | Not verified |
| One bootloader copy | MCUboot sits in a single 128 KiB `mcuboot` partition. Stock MCUboot cannot update itself and never validates itself. With no bootable slot image it loops forever, and no watchdog resets it unless one is enabled. | Verified on pinned source (rows A-1 to A-5, A-20 to A-23 of evidence A). The effect of a watchdog: Inference (A-24). |
| Security configuration | The plan keeps the EVKB in the open configuration, where HAB errors are logged and the image runs. No record shows the fuse state of the board, because no fuse was read. | Plan statement: Verified in repository. Fuse state: Not verified. |
| ROM serial downloader as the service route | The RT1060 reference manual copy says an error during internal boot falls through to the serial downloader, and that boot-mode pins can select it. It loads an image into RAM and has no flash command, so programming NOR needs a flashloader loaded this way. That a corrupt first sector or IVT counts as an error is not stated. | Verified by public vendor document for the paths (rows B-1 to B-14). Application to a corrupt first sector: Inference (B-6). |
| Planned check #309 | [#309](https://github.com/wonny-labs/crux-pilot/issues/309) would corrupt the first sector of sacrificial NOR content on the EVKB in open configuration without fuse writes, observe whether the device enumerates in the serial downloader and recover it with the NXP flashloader flow. It is open and has not been run. It needs its own hardware approval. The scope is that of step 3 of the recommendations in the [bootloader update investigation](../boot-bootloader-update-investigation.md#what-to-do-instead), and the plan names the check as planned. | Verified in repository |
| Debug-probe programming | The bench records show an external SEGGER J-Link used with the EVKB: image download and section comparison against the QSPI range (17 October 2026), and a `restore` that loaded and verified the normal image over flash content that was not known (8 November 2026). These were not MCUboot layouts, and no damaged boot header was involved. | Verified in repository ([baseline](../baseline.md); [networking physical preparation](../networking-physical-preparation.md), "First real restore"). The device-control tool has never been run against hardware ([tools/device_control/README.md](../../tools/device_control/README.md)). |
| Boot switch | QSPI boot is SW4 `OFF-OFF-ON-OFF` on the EVKB (recorded for the physical board). The Serial Downloader setting is SW4[3:4] = `01`, which with SW4[1:2] OFF reads `OFF-OFF-OFF-ON`. | QSPI setting: Verified in repository. Serial Downloader value: Verified by public vendor document (summarising fetch); the four-position string is an Inference ([boot and update investigation](../boot-update-investigation.md), "Boot switch names"). |
| Bootloader NOR writes | The MCUboot FlexSPI write-path closure failure is a blocker for any bootloader NOR write on hardware ([#165](https://github.com/wonny-labs/crux-pilot/issues/165); plan section 2). RAM load with revert still makes the bootloader write `copy_done` and erase a whole slot while it executes from NOR. | Decision row: Verified in repository. Write behaviour: Verified on pinned source. |
| eMMC | Only desk expectations and a simulator replay of MCUboot trailer sequences. The flash-API driver is a Draft prototype that is not merged. No eMMC-bearing target exists. | Verified in repository ([plan](secure-boot-development-plan.md), section 4, Phase 3) |

### Where the sources do not yet agree

- **Ordering of #296 and #309, to settle.** #296 requires the recovery path to be proved before any physical boot experiment is run, and the plan says the steps that #309 documents can be reused for that path. #309 lists as a dependency that the M8 hardware step should be planned first. These fit together: the M8 hardware step is planned, then a recovery path is proved, then the boot experiment runs. What remains open is which check proves the path: #309, or another path such as option (a). Section 4 recommends an answer.
- **Test images, `--pad` and the confirmed image, to reconcile.** Decision 13 of the boot and update investigation, accepted by the project owner on 2 November 2026, settles that M8 test images are signed with `--pad`, which gives them trailer magic. That is not reopened here. Two statements still have to be reconciled with it. First, the composition page says a revert test uses the unconfirmed `zephyr.signed.bin`, and that file is unpadded; the build at the pinned Zephyr v4.4.2 writes no padded, unconfirmed image, only a padded, confirmed one (`zephyr.signed.confirmed.bin`). Second, the plan (section 9) asks for a follow-up on the `--pad` images and on that confirmed image, and how MCUboot treats the confirmed image is Not verified. This matters for options (a) and (c).

## 3. Options

Six options. For each: how it works, what it needs, what it protects against, what it does not, the risks and the evidence status. "Physical access" means a person at the board.

| | Option | Cases helped | Where it applies | Evidence status in one phrase |
| --- | --- | --- | --- | --- |
| (a) | Debug-probe reprogramming of the QSPI NOR | R1, R2 and R3 on a development board, by a person | Development bench | Probe download and restore Verified in repository; for damaged boot headers and MCUboot layouts Not verified |
| (b) | ROM serial downloader with a flashloader | R2, R3 by service | Development and, if the pins stay reachable, product | Documented in vendor text; the premise is untested; #309 not run |
| (c) | MCUboot revert of an unconfirmed application | R1 only | Field | Source behaviour verified; build-only; not booted |
| (d) | Second bootloader copy or two-stage design | R2 (defective, vulnerable) and R3 by update | Field | Candidate only; no milestone; not started |
| (e) | eMMC boot partitions as redundancy | R2 on eMMC products | Product, later | Expectations only |
| (f) | Factory or service procedure for a closed configuration | R2, R3 in closed configuration | Product | Out of scope until decided |

### (a) Debug-probe reprogramming of the QSPI NOR

This is the baseline path for the bench. It is development only and needs physical access.

| Point | Description | Label |
| --- | --- | --- |
| How it works | An external SEGGER J-Link on the EVKB's J2 header (with J9 and J10 removed to isolate the on-board SWD circuit, as the baseline records the NXP guidance and the bench setup) connects over SWD. J-Link Commander or a GDB run loads a binary or ELF into the QSPI range at `0x60000000` and compares it back. The bench's `restore` mode loads the whole accepted normal image, verifies it twice and only then starts it, and is described as independent of what the flash holds. | Verified in repository ([baseline](../baseline.md), M4 sections; [image profiles](../board-image-profiles.md), failure orders) |
| What it needs | The probe and the board at the bench; the board in QSPI boot (SW4 `OFF-OFF-ON-OFF`); a host that holds the probe; the exact image files with their hashes; a saved copy of what the NOR holds before a change; and a hardware approval for the task (`AGENTS.md`). For the MCUboot layout, two images must be programmed at known offsets (bootloader at `0x0`, signed application at `0x20000` in the trial layout; the slot sizes of the composition are provisional), and a debugger download of the application ELF would not program the slot, because its LOAD segments are in SDRAM. The images signed with MCUboot's public test key must never be programmed to a board, so a run with the MCUboot layout needs an image signed with a development key that is generated and kept outside every repository. Under RAM load with revert, a probe-programmed application must carry trailer magic and be confirmed (for example a `*.signed.confirmed.hex`), or MCUboot erases the slot when it selects it; a restore of the application slot therefore has to write a confirmed image, and the flash content after a boot is not the content that was programmed, because the bootloader writes `copy_done` (Inference from the same row and from the image state proposal). | Offsets, ELF and trailer rule: Verified on pinned source and Inference ([boot and update investigation](../boot-update-investigation.md), "Programming and debug" row). Key rule: Verified in repository ([boot composition](../boot-composition.md)). |
| Protects against | A bootloader or application image that does not start, a slot that holds the wrong content, and a key replacement, on a board that has a working probe connection. It does not depend on the ROM falling through or on MCUboot running. | Inference |
| Does not protect against | Field failures (it is not remote). A board whose probe port is not available; whether it is available in a closed configuration follows the product's fuse plan and is not shown here. It does not exercise the ROM or MCUboot recovery paths, so it proves nothing about the service route of a product. | Plan T3 row: Verified in repository. Closed-configuration probe use: Not verified. |
| Risks | The target halts when the probe attaches. A load that is cut part-way leaves the flash partly written and not bootable (the profile record states this for a timeout mid-flash), so the answer to a failed load is a new, approved restore, not a retry in the same run. A mistaken target or image overwrites the saved content. | Verified in repository (image profiles, failure table) |
| Evidence status | Probe download and section comparison on the EVKB: recorded (October and November 2026). Restore over unknown content: recorded. Programming with the MCUboot layout, recovery from a damaged first sector, a saved copy of the whole NOR (the method is not recorded; the bench records compare against known files), and the device-control tool: Not verified. | Verified in repository for the first two; Not verified for the rest |

### (b) ROM serial downloader with a loader

This is the service route that the project owner decided on for M8 and M9. It is unproven here.

| Point | Description | Label |
| --- | --- | --- |
| How it works | The boot ROM enters the serial downloader when the boot-mode pins select it, or when internal boot fails with an error. A host sends a flashloader into RAM over USB or UART, and `blhost` then programs the NOR through the flashloader. In the open configuration, authentication errors are logged and do not change the boot flow. | Verified by public vendor document (rows B-1 to B-5, B-11 to B-14). Open-configuration reading: Inference. |
| What it needs | Physical access to the boot switches (or a service port on a product); a USB or UART host connection; the NXP SPSDK tools `sdphost` and `blhost` and the NXP flashloader binary. Where the flashloader comes from and under what licence is Not verified, so it stays outside the repository and CI. A saved copy of the NOR content before any test. | Tools: Verified by public vendor document. Flashloader source and licence: Not verified (plan section 7). |
| Protects against | A damaged bootloader (R2, D1), provided that a damaged first sector counts as a boot error; and, by the boot-mode pins, a bootloader that is valid but defective, vulnerable or trusting the wrong key (R2 and R3), since the pins reach the downloader whatever the flash holds. | Inference (B-6, boot bootloader investigation, "Reading") |
| Does not protect against | A bootloader that is valid but defective, if only the fall-through is used: the ROM boots a valid image. Anything remote. A bad application image (that is MCUboot's job). A product that fixes the boot configuration by fuses and has no way to reach the downloader. | Inference |
| Risks | The premise is untested. Fuses that remove recovery exist (`SDP_DISABLE`, `FORCE_INTERNAL_BOOT`, `BOOT_CFG_LOCK`); setting them without a recovery plan strands units. In a closed configuration the downloaded image is authenticated, so recovery needs a flashloader signed under the device's key tree (the third point is an Inference). The polarity of the UART disable fuse differs between SPSDK and the manual copy. | Verified by public vendor document for the fields (B-20 to B-22). Consequence: Inference (B-19). |
| Evidence status | Planned check #309 is open and not run. Its acceptance criteria would record whether the device enumerates and for which kind of corruption, document the recovery steps so that the M8 recovery path can reuse them, and record that no fuse was written. | Not verified |

### (c) Revert by MCUboot for a bad application image

Recovery of a bad application image (R1) only. It is built into the first composition and is part of M9.

| Point | Description | Label |
| --- | --- | --- |
| How it works | RAM load with revert: MCUboot copies the chosen slot image to SDRAM, validates it there and jumps to it. A newer image that is not confirmed by the application is erased from its slot at the next reset, and the previous confirmed image runs. A newer image that fails validation does not run, and the previous image runs; the failed slot is erased by the following reset. | Verified on pinned source ([image state contract proposal](image-state-contract-proposal.md), "What the bootloader does with the result"; [boot and update investigation](../boot-update-investigation.md)). |
| What it needs | An update agent that marks the inactive slot for a test and confirms the running slot (Phase 2, not started); MCUboot's share-data backend, a retained region and a boot-information node, which the board files place; images signed with trailer magic for a test request, a point that is open (section 2); a reset after a hung image (an application watchdog, or a power cycle). The bootloader configures no watchdog in the trial builds. | Verified on pinned source and by build (boot composition); watchdog: Verified in repository (boot and update investigation, "Not verified" table) |
| Protects against | An application image that is correctly signed but is never confirmed, for instance because it hangs, and a newer image that fails validation, while a confirmed image remains in the other slot. | Inference from the rows above |
| Does not protect against | A damaged or defective bootloader (R2): MCUboot never validates itself. A confirmed image that is bad. The case where no slot image validates: MCUboot logs and loops, and no watchdog resets it unless one is enabled. A lost key (R3). The padded, confirmed image that the pinned build also writes may be treated as already confirmed (an Inference); flows must not use it for a revert test. | Verified on pinned source (A-20 to A-23). Confirmed-image treatment: Inference; Not verified. |
| Risks | The revert erases the whole 3 MiB slot while the bootloader executes from NOR, so it depends on #165 before any NOR write on hardware. Duration of that erase and a power cut during it are Not verified. After a revert nothing in flash says an update was attempted, so the outcome needs an attempt record written by the application (the image state proposal's I3). | Verified on pinned source for the behaviour; Inference for the consequence |
| Evidence status | Composition builds and signs; the build check also shows that the signing tool refuses another key type and a changed image. Boot, confirm, revert and power loss have not run on a board. The image state contract is Proposed, not adopted. | Verified by build for the first part; Not verified on a board |

### (d) A second bootloader copy or a two-stage design

The investigation's option N2 (two stages) is the candidate for a field-updatable bootloader. N3 (ROM secondary image) is the alternative that needs fuses. Neither is started, and no milestone is proposed for them.

| Point | Description | Label |
| --- | --- | --- |
| How it works | N2: the ROM boots a small first stage that never writes flash, verifies a second stage (the MCUboot of M8) and starts the valid bank with the newest version. The second stage sits in two banks and is updated by an application that runs from SDRAM; the first stage verifies the whole bank at every boot. N3: a fuse-programmed secondary image offset lets the ROM boot a second bootloader copy at a multiple of 256 KiB. | Pattern and rows: Verified by public vendor document (A-27 to A-32, A-37, A-38, B-48, B-49). The design reading: Inference. |
| What it needs | N2: a custom first stage and a new signing structure; a separate signing key for the second stage; a confirm state held outside NOR (where retained state can live is Not verified); two or three builds and more layout (the region before the slots grows from 128 KiB to at least 192 KiB, which moves the slots 64 KiB later; the arithmetic is an Inference); a scope decision on bootloader customisation (the class of #141 owner decision 1, which is not started); #165 closed for every NOR writer. N3: an irreversible fuse write, and an NOR algorithm that is not stated in the text read. | Inference (boot bootloader investigation, "Comparison" and "Effect on the fenced layout") |
| Protects against | N2: a defective, vulnerable or key-compromised second stage (R2, R3) by update, and a second stage that fails validation (the other bank starts). A second stage that validates but is defective needs a confirm state. N3: a copy the ROM rejects, and updates of the secondary copy. | Inference |
| Does not protect against | N2: a defect in the first stage, which is not updatable. Both: nothing protects the bank write against a power cut except full verification at every boot, and no source gives a guarantee. N3 does not avoid the NOR write hazards, and a closed configuration is probably needed for the fallback trigger. | Inference |
| Risks | Cost, a custom bootloader (an architecture decision), a larger attack surface without a root of trust unless HAB is closed, and the dependency on D7. The first stage's size is not known. N2b (the first stage copies the second stage to RAM and runs it there) is not shown feasible with MCUboot on the RT1060. | Verified by public vendor document for the maintainer's view that an updatable MCUboot makes sense only behind an unmodifiable first stage (A-33). Rest: Inference; Not verified. |
| Evidence status | Desk only. The investigation concluded that no option should become a milestone now and that N2 stays the candidate if a field-update requirement is confirmed. | Verified in repository |

### (e) eMMC boot partitions as redundancy

Expectations only. eMMC adds no M8 or M9 exit criterion (project owner decision of 7 November 2026, not revised on 14 November 2026), and no eMMC-bearing RT1060 target exists.

| Point | Description | Label |
| --- | --- | --- |
| How it works | An eMMC has two boot partitions and a selector byte. A/B bootloader copies would write the inactive partition and then switch the selector (the RAUC pattern). The ROM follows one `BOOT_PARTITION_ENABLE` value and the manual text read has no fallback from one boot partition to the other, so software would switch the field. A first stage that is write-protected in a boot partition plus an updatable second stage is the eMMC form of N2. | Verified by public vendor document for the selector and the ROM text; the switching by software: Inference ([eMMC investigation](../boot-emmc-investigation.md), section 3; bootloader investigation, E0 to E3) |
| What it needs | An eMMC target with boot partitions that are not write protected at update time; new code for the selector (pinned Zephyr has no API for boot partitions or write protection); a protection policy and the reset-pin wiring that decide whether power-on protection can be undone. The ROM secondary image rule (a table at offset `0x200`) is an alternative whose fit with eMMC boot partitions is Not verified. | Verified on pinned source (Zephyr stack); Inference; Not verified |
| Protects against | A damaged boot partition, if the ROM can be pointed at the other one by a service step or by software. | Inference |
| Does not protect against | Permanent protection of both boot partitions ends any software replacement of the bootloader in them. Whether the ROM falls back between boot partitions is not shown. The atomicity of the selector write under power loss is Not verified. | Inference; Not verified |
| Risks | Several EXT_CSD fields are one-time programmable; the product's settings are inputs (D8) that are not known. A bootloader update that goes through eMMC does not use FlexSPI, so the #22 and #165 hazards do not apply to it in the same way (Inference); the cache and DMA concerns are others. | Inference |
| Evidence status | Desk expectations. A simulator replay of MCUboot trailer sequences is merged; it says nothing about the ROM, boot partitions or recovery. | Verified in repository |

### (f) Factory or service procedure for a closed configuration

Out of scope until the project owner decides on the closed configuration. This note records what would have to be decided and does not recommend an answer.

| Point | Description | Label |
| --- | --- | --- |
| How it works | In a closed configuration the ROM authenticates the first image against a key hash in fuses on every boot and aborts to the serial downloader on a failure; the downloader authenticates what it loads. A service or factory procedure then needs the signing keys, a flashloader signed under the device's key tree and a bootloader signed under it. Revocation of a root key leaves the old bootloader unable to boot, so an update path or a service recovery must install a bootloader signed under another key. | Verified by public vendor document for the flow (B-4, B-5, B-17 to B-19, B-24, B-61). Consequences: Inference (B-19; B-62 for how many revoke bits are valid, which is Not verified). |
| What would have to be decided | D1 (is the closed configuration used, and when), D5 (recovery versus lockdown: serial download, boot-mode pins, service port), D6 (HAB algorithm, size and key custody, which has to outlive the product's service life), D4 (hardware anti-rollback source), the debug-port policy of the product, and, for eMMC, the boot partition protection state. Fuse writes are irreversible and are always a product decision. | Verified in repository (plan section 6; boot bootloader investigation, "What stays a product decision") |
| Protects against | Not assessed. | |
| Does not protect against | Not assessed. Without the closed configuration none of the ROM-side gains exist and the trust level stays that of T0 or T1. | Inference |
| Risks | Stranded units if recovery paths are fused away or keys are lost. Closed-configuration behaviour is shown only on boards that may be locked; the EVKB stays open. | Verified in repository (plan sections 6 and 9) |
| Evidence status | None. Closed configuration, fuse behaviour and the HAB event content with a blank key hash are Not verified, and the vendor manuals that would settle them are not available to this repository. | Not verified |

## 4. Recommendation for the next steps

The recommendation stays inside what the plan and the bootloader investigation already recommend. Where this note adds something, it is labelled **Proposal of this note**.

| # | Recommendation | Reason | Rejected alternative and why |
| --- | --- | --- | --- |
| 1 | **Prove option (a) first, on the bench, before any boot experiment** (Proposal of this note as to which option: the plan requires "a non-destructive recovery path" and names the steps of #309, and it does not name the probe). Scope of the proof: record the target, probe and Flash configuration; save the NOR content; program a known image over SWD; verify it; restore the saved content; verify again. Use the bench's gated program and restore path as the model, not commands typed ad hoc. The existing profiles cannot carry it as they are: the S2 profile refuses MCUboot images, and no mode of the driver reads out the NOR (the modes are identify, program, restore and watch, and they compare against known files), so a new, reviewed profile and a new read-out step are both needed. Any change under `tools/` is a protected-path change that needs the project owner's explicit approval (`AGENTS.md`). Keep the safety rules of the board test procedure (being written): one use per step, bounded commands, no retry, hardware approval for the task. | The bench already holds a recorded probe download and restore on this EVKB, so (a) is the shortest path to something that works whatever the flash holds, in the open configuration. It is also the net under every other experiment, including #309. | Skipping a recovery path and booting MCUboot first: the plan forbids it. Writing a separate programming tool: the repository already has a gated path (the image profiles), and a copy would copy the gates and let them drift. |
| 2 | **Run the ROM serial downloader check #309 (option b) as its own bounded task after step 1**, under its own approval, and require it before M9 declares the service recovery level as verified. It may share a bench visit with the separately approved HAB event read (decision 8 of #141, accepted 7 November 2026). (Proposal of this note as to the timing.) | #309 turns the main Inference (that a damaged first sector reaches the serial downloader) into evidence, and its recovery steps can be documented for reuse, as the plan wants. It corrupts the boot sectors on purpose, which is only safe with the step 1 net in place. | Making #309 a precondition of the first boot experiment. #309 corrupts the boot sectors on purpose, so it is safest to run with a proved net under it, and the recovery steps it documents are an output of the check, not a prerequisite for it. Requiring #309 first would also make the first boot experiment wait for a check that has its own approval and scheduling. |
| 3 | **Keep the decided posture: a single bootloader copy and service recovery for M8 and M9** (D7). Do not start N2, N3 or eMMC redundancy. Decide again after #165, #298 and the M8 hardware results, as the decision says. | The investigation found that every field-update option needs something the plan does not have (a custom first stage, irreversible fuses or a protection policy), every NOR-writing option waits on #165, and the trust benefit needs the closed configuration. | Starting the two-stage design now: it is a custom bootloader and a scope decision (variant b of #141) that is not started, and it would add NOR writers before #165. Choosing a layout that makes N2 expensive later is also avoided: keep `storage` unowned, the 3 MiB slots and the 64 KiB boundaries (#141 decision 17), and do not assume the `mcuboot` partition is exactly 128 KiB in tools or scripts (step 5 of the recommendations in the bootloader update investigation). |
| 4 | **Treat (c) as M9 work and not as the recovery for the first boot experiment.** Decision 13 already settles that test images are signed with `--pad`. Before a revert test can run, reconcile that with the composition page's instruction to use the unpadded `zephyr.signed.bin` and with the padded, confirmed image that the pinned build writes (section 2; the plan's follow-up in section 9). (The ordering is a Proposal of this note.) | The revert path is built but unbooted, its erase is a bootloader NOR write that #165 blocks, and the update agent and the image state contract do not exist yet. Option (a) restores a bad application image on the bench without any of that. | Relying on revert as the only way back for the first boot experiment: a hung or invalid image with no valid slot would loop in MCUboot, and the revert erase is the blocked write. |
| 5 | **State the recovery level and the open cases in the M9 declaration**: single copy, not field-updatable, recovery by service through the ROM serial downloader with the verification state of that route; and, as a Proposal of this note, that the loss or compromise of the application signing key is recoverable only by service replacement of the bootloader (Inference from row A-34), until D1 and D6 are decided. The bootloader version in the boot-result record is already decided. | The gap is then declared and not hidden. Case R3 has no decision, so the declaration should say so. | Leaving R3 unstated: a reader would assume that a leaked key can be answered in the field. |
| 6 | **Leave (e) and (f)** until the project owner decides on D1, D5, D6, D3 and D8, and an eMMC target exists. | They depend on product decisions and hardware that do not exist, and the project owner decided that eMMC adds no M8 or M9 exit criterion. | Writing a closed-configuration or eMMC recovery procedure now from desk text: it would read as an adopted procedure while nothing was shown, and fuse-level steps are irreversible. |

What to decide first: the approval of the bench task of step 1 (a hardware approval for the current task, as `AGENTS.md` requires). Nothing else blocks it. The other decisions in the next section can wait for later phases.

## 5. Decisions the team must make

Decisions D1 to D10 are those of the [plan](secure-boot-development-plan.md), section 6, with their numbers. Rows Q1 to Q4 are new in this note.

| # | Decision | Current recommendation | Needed before |
| --- | --- | --- | --- |
| Q1 | Approve a bounded bench task to prove option (a): save, program a known image, verify, restore, verify again, with the EVKB in the open configuration and no fuse operation | Yes, as one reviewed plan with its own approval; the net comes before any boot experiment | The first boot experiment on a board |
| Q2 | Whether option (a) alone satisfies "a non-destructive recovery path proved before any physical boot experiment" for the open-configuration EVKB | Yes, with #309 (b) following as its own task (section 4, steps 1 and 2) | Q1 |
| Q3 | When #309 runs relative to the M8 boot experiment | After step 1 and before M9 declares the service route as verified | The M9 declaration |
| Q4 | Whether the M9 declaration states the open case of a lost or compromised application key | Yes (section 4, step 5) | The M9 declaration |
| D7 | A field-updatable bootloader | Keep service recovery; decide again after #165, #298 and the M8 hardware results. Two-stage design (N2) stays the candidate | After Phase 3 |
| D5 | Recovery versus lockdown in production (serial download, boot-mode pins, service port) | Keep a signed-only recovery path; decide with D1 | Phase 5 |
| D1 | Will products use the closed security configuration, and when | Plan for it in production, keep development and the EVKB open | Phase 5 |
| D6 | HAB key algorithm and size, key custody | RSA-3072 or larger, offline keys in an HSM, up to four root key slots; which slots are revocable is Not verified | Phase 4 |
| D4 | Hardware anti-rollback source, if any | Application-side check only until D1 | Phase 5 |
| D3, D8 | eMMC validation hardware and the product's eMMC parameters | As in the plan: an EVKB with an eMMC adapter for software tests of the stack and user area, and a product-class board for ROM boot from the boot partition; the product's parameters are collected first. Not decided; eMMC recovery stays expectations until both exist | Any eMMC recovery work |
| Plan section 9 follow-up | Reconcile decision 13 (`--pad`, which settles how the magic is obtained) with the composition page's unpadded `zephyr.signed.bin` and with the padded, confirmed image of the pinned build, and find out how MCUboot treats that image | Settle before the first revert test and before an MCUboot-layout restore (section 4, step 4) | The first revert test |

## 6. Evidence needed and what validation would look like

Evidence is recorded separately for each kind: a build result, a Host or `native_sim` result, CI, and a physical-board result. None of them implies another. The procedure that would carry the board steps is the board test procedure (being written); nothing in this note authorises a hardware step.

| Option | What would show it works | Evidence not enough on its own |
| --- | --- | --- |
| (a) probe | A recorded run on the EVKB: target, probe identity and voltage, saved content with a hash, a known image programmed and compared read-back, the saved content restored and compared, the log of each command, and a statement that no fuse was written. A second run with the MCUboot layout (bootloader and an application signed with a development key at their offsets, the application written as a confirmed image) restored from the saved content. | The earlier bench restores (different images, intact boot headers); a successful build |
| (b) ROM serial downloader | #309's acceptance record: whether the device enumerates and for which kind of corruption, the flashloader steps documented, the saved content booting again, and no fuse written. Proposal of this note: repeat for each kind of corruption of the boot sectors (first sector, boot header), since the fall-through premise is shown per kind; a valid but defective bootloader is not caught by the fall-through, so it is covered only by the boot-mode-pin route, which would be tried as a separate case. | Vendor text; a source reading |
| (c) revert | On a board: an unconfirmed image reverts on the next reset and the confirmed image runs; an invalid image is refused and the previous one runs; the confirmed image of the pinned build is handled as expected; the duration of the whole-slot erase; a power cut during an update and during the erase (needs a power fixture and its own approval, #297). Prerequisite: #165 closed. | The CI build check and `imgtool verify`; the pinned-source reading |
| (d) two stage / second copy | Only after a requirement is confirmed: a prototype with its own investigation after #165, #298 and the M8 hardware results, then board runs of a defective second stage, a half-written bank and a first stage that rejects both banks. | The desk comparison |
| (e) eMMC | A target with an eMMC and boot partitions, the product's protection state, board evidence of the ROM's partition selection and fallback, and the selector write under power loss. | The simulator replay; expectations |
| (f) closed configuration | A board that may be locked, a fuse plan decided under D1, D5 and D6, and evidence of the serial downloader and probe behaviour in that state. The EVKB is not that board. | Anything on an open board |

## 7. Not verified

| Item | Needs |
| --- | --- |
| That the RT1060 ROM falls through to the serial downloader on a corrupt first sector or IVT, in the open and in the closed configuration (B-6) | #309: a board, saved NOR content, approval |
| That a probe download restores a board whose boot header is damaged, and that it works with the two-image MCUboot layout | The bench task of step 1 |
| A method to save the whole NOR content before a change | The bench task of step 1 |
| Where the NXP flashloader comes from and under what licence | A read of NXP's terms |
| Boot, jump, confirm, revert and power loss with the RAM load composition; the treatment of the padded confirmed image at the pinned Zephyr | A board, #165 closed, a power fixture |
| Duration of a whole-slot erase under bootloader XIP and watchdog behaviour | A board |
| Whether a probe port is available in a closed configuration | A board that may be locked; the product's fuse plan |
| HAB event content with a blank key hash, which root key slots are revocable, and the RT1060 HAB API layout (B-62, B-65; #141 A13) | The vendor security documents, which are confidential and are not in this repository, and a board |
| The ROM secondary image algorithm for NOR and its conflict with the SDK guide (B-48, B-50) | The vendor manual and a board |
| Whether the ROM falls back between the two eMMC boot partitions, and whether its secondary image rule fits them | A board, #298 |
| The atomicity of the eMMC selector write under power loss | A data sheet and a board |
| Retained state that can hold a second stage's confirm flag; the minimal size of a first stage | The reference manual, a build and a board |
| RT1170 ROM fall-through and redundancy | The RT1170 reference manual |

This note does not claim that any recovery path works on hardware, that the serial downloader recovers a damaged bootloader, that a probe recovers a board in the MCUboot layout, that a two-stage design can be built, that eMMC boot partitions can be used on an RT1060, or that any M8 or M9 criterion is met.
