# Updating or recovering a deployed bootloader

| Field | Value |
| --- | --- |
| Issue | [#299](https://github.com/wonny-labs/crux-pilot/issues/299) "Investigate how a deployed bootloader is updated or recovered" |
| Status | **Draft. Desk investigation.** Nothing in this record is adopted. No build, hardware, debugger, fuse or manifest operation was performed, and no dependency checkout was altered. The record is under independent review (see [Review record](#review-record)). |
| Target | RT1060-EVKB Rev B (`mimxrt1060_evk@B/mimxrt1062/qspi`) for the NOR configuration. RT1170-EVK Rev A1 only where the public sources show a difference. The eMMC configuration is a set of expectations, not a result for any device. |
| Pinned upstream | Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0). MCUboot `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2`. `hal_nxp` `2c2f28ac333e995d7279777409817c4b4f92c1ec`. The pins are those of the #141 record. |
| Read on | 7 November 2026, against `main` at `3e0a5d7` |
| Related | [#141](https://github.com/wonny-labs/crux-pilot/issues/141) and its [record](https://github.com/wonny-labs/crux-pilot/blob/4fc5cc88b166deeb906e97ebd2b8c22bd17a5f72/docs/boot-update-investigation.md) (Draft PR [#151](https://github.com/wonny-labs/crux-pilot/pull/151), not merged). [#165](https://github.com/wonny-labs/crux-pilot/issues/165) and [#22](https://github.com/wonny-labs/crux-pilot/issues/22) for NOR writes. [#297](https://github.com/wonny-labs/crux-pilot/issues/297) for the update path. [#298](https://github.com/wonny-labs/crux-pilot/issues/298) for eMMC. |
| Evidence | [docs/evidence/boot-bootloader-update/](evidence/boot-bootloader-update/README.md) |

## Summary for the owner

The question: M8 and M9 as planned update the application image only. If the
deployed bootloader is defective, can it be corrected or recovered in the
field?

| Point | Result |
| --- | --- |
| Stock MCUboot | Cannot update itself, and the `mcuboot` partition is not an image slot. A project maintainer's stated view is that an updatable MCUboot only makes sense behind a separate, unmodifiable first stage. |
| Boot ROM recovery | The RT1060 manual says an error during internal boot falls through to the serial downloader. That is the main recovery path found for a damaged bootloader. SD manufacturing boot, an LPSPI recovery device and a ROM secondary image are also described, and they need an SD card, a second memory or fuses. Every path needs physical access, and in a closed security configuration the serial downloader needs a signed flashloader. A bootloader that is valid but defective is not caught by it. Whether a corrupt first sector counts as an error was not stated, and it was not tested. |
| Options for NOR | Five options compared. Only a two-stage design (N2: a small first stage that never writes flash, plus an updatable second stage in two banks) gives a safe field update without fuse writes. N3 (a ROM secondary copy) needs an irreversible fuse write and an unverified ROM algorithm. N1 (in-place rewrite) is unsafe. N2 is a custom bootloader and an architecture decision. Every option that writes NOR still depends on #165. |
| Options for eMMC | Two boot partitions and a selector byte make an A/B pattern possible at the eMMC level. Pinned Zephyr has no API for it. The RT1060 manual says the ROM reads the boot partition selector. Fallback between the two boot partitions was not found. Handed to #298 as expectations. |
| Trust | Without HAB in closed configuration, "immutable" is a convention. An updatable bootloader then adds an attack surface without adding a root of trust. HAB closed adds ROM enforcement and key revocation, and revocation needs a bootloader update path to be usable. |
| Recommendation | **No option becomes a milestone now.** Declare the bootloader's update and recovery level in the M9 results. Run a bounded ROM recovery check on the EVKB later, under its own approval. Keep the two-stage design as the candidate if a field-update requirement is confirmed. |
| Product decisions | Fuse programming, closed security configurations, key custody and revocation, eMMC boot partition protection, whether a field-updatable bootloader is required at all. |

The summary restates the labelled sections below. Each row's label is given in the section it comes from.

## Purpose, scope and boundaries

#299 is a desk investigation. It records options, costs, risks and what each
option needs from hardware or fuse settings, and it recommends whether any
option should become a milestone. It does not implement a bootloader update path.

| Not done here | Reason |
| --- | --- |
| Any hardware, debugger, reset, erase or fuse operation | #299 boundaries. Fuse programming and closed security configurations remain product decisions. |
| Any manifest, module or Dev Container change | Not authorised. |
| Any claim that an option works on hardware | None was run. Every row below is a source reading or an inference. |
| The eMMC investigation | [#298](https://github.com/wonny-labs/crux-pilot/issues/298) owns the five eMMC questions. This record only states what it expects, and links the rows #298 should check. |
| Statements about a specific product | The record covers the platform and evaluation boards only. It states no product storage or memory size. |

## Evidence labels and method

The labels are those of the #141 record. They are never upgraded.

| Label | Meaning |
| --- | --- |
| Verified on pinned source | Checked at a cited file and line of a pinned Zephyr, MCUboot or `hal_nxp` revision. Shows what the code allows, not what a board does. |
| Verified by public vendor document | A public vendor or project document, repository or tool says so. It is the author's statement, not a Crux result. |
| Inference | Reasoned from verified rows, which the row names. |
| Not verified | Not checked, not accessible, or contradicted by another source. The reason is given. |

Row IDs such as A-33, B-48 or C-17 point to the evidence files:
[A](evidence/boot-bootloader-update/a-mcuboot-zephyr-ecosystem.md) (MCUboot,
Zephyr and public designs), [B](evidence/boot-bootloader-update/b-nxp-rom-fuses-hab.md)
(NXP ROM, fuses, remap, HAB) and [C](evidence/boot-bootloader-update/c-emmc-expectations.md)
(eMMC). IDs such as "#141 A9" or "#141 B64" point to the #141 evidence rows.

| Item | How it was done |
| --- | --- |
| Sources | Public upstream source and documents, read through `gh api`, raw files at the pins and web fetches. Nothing was cloned into this repository. The full MCUboot tree at its pin was read as a tarball outside the repository and searched. |
| Delegation | Three research agents read the sources in parallel and wrote the three evidence files. The record's author then re-read a sample of high-impact rows at the source: MCUboot issue #1403 (A-33), the `IOMUXC_GPR` remap registers (B-46), the ROM API wrapper (B-30) and the MCUboot no-bootable-image path (A-22). All four matched. After the first review the author also read the manual's MMC boot details (B-84). The rest is not re-read by the author. The independent review covers it. |
| Absence claims | Limited to the files or paths each row names. |
| NXP reference manual | Read from a third-party-hosted copy of revision 3 (07/2021), because NXP's own download needs a login. Authenticity and the current revision were not checked. Rows from it say "manual" and are labelled as vendor document. |
| Not readable | RT1060 Security Reference Manual, AN12079, the Code Signing Tool guide, any RT1170 reference manual, the JEDEC eMMC standard. |

## What #141 records, and what this investigation adds

The #141 record says in three places that the bootloader has one copy and that
recovery of a corrupted bootloader through boot ROM paths, for example the
serial downloader, is Not verified (the Boot media section, the "Not verified"
table and #141 Stage 2 section 2.3). #141 also records that MCUboot keeps the
NXP boot header and executes in place from the `mcuboot` partition, and that the
partition is 128 KiB (51,760 B and 53,940 B used in the two trial builds).

| #141 statement | Status after #299 | Label |
| --- | --- | --- |
| The bootloader has one copy. | Unchanged. No stock mode or Zephyr option creates a second copy or updates the partition. Stock MCUboot opens only the primary, secondary and scratch areas of the image slots, so the `mcuboot` partition is unreachable because no code path names it, not because it is protected. | Verified on pinned source (A-1 to A-5, A-10, A-18). Reading as "unreachable by absence": Inference (A-5). |
| Recovery depends on boot ROM paths such as the serial downloader. | Partly answered from the manual. The ROM has several recovery paths, with conditions (next section). Which of them applies to a corrupted or defective bootloader on the EVKB is not tested. | Verified by public vendor document for the paths (B-1 to B-11). Inference for the application to a corrupt first sector (B-6). |
| Recovery was not verified. | Still not verified on hardware. The documentary basis is now recorded. | Not verified on hardware. |
| MCUboot validates images on every boot. | It validates slot images only. It never validates itself. With no bootable slot image it logs, then loops forever. No watchdog resets it unless one is enabled. | Verified on pinned source (A-20 to A-23). Watchdog effect: Inference (A-24). |
| The Zephyr port holds exactly one signing key (#141 Stage 1). | Confirmed by the upstream problem statement: a leaked signing key can only be answered by a new bootloader with other keys. | Verified by public vendor document (A-34, an open proposal, not a shipped feature). |

## What "update or recover" has to cover

A defect in a deployed bootloader is not one problem. The failure classes below
decide which option helps. They are the author's classification (Inference).

| Class | Description | Example |
| --- | --- | --- |
| D1 | The bootloader image is damaged and does not start. | A power cut during a rewrite, or flash corruption. |
| D2 | The bootloader starts but is defective. | It cannot find or start a valid application, or it loops with no reset. |
| D3 | The bootloader works but has a vulnerability found after deployment. | A flaw in signature or version checking. |
| D4 | The signing key embedded in the bootloader must be replaced. | Key compromise, or a planned rotation. |

Application defects (a bad or unconfirmed application image) are M9's subject
and are not part of this record.

## What the boot ROM offers for recovery on RT1060

All rows are from the manual copy and public tool data described above. None
was run on a board.

| # | Path | What it does | Needs | Evidence | Label |
| --- | --- | --- | --- | --- | --- |
| 1 | Boot mode pins to serial downloader | BOOT_MODE `01` sampled at power-on enters the serial downloader. BOOT_MODE `00` with `BT_FUSE_SEL` unset also enters it. Boot configuration comes from pads while `BT_FUSE_SEL` is unset. NXP recommends fuses for deployed products. | The mode pins reachable on the board. USB or UART host connection. | B-1, B-7, B-8 | Verified by public vendor document. A product that fixes the boot configuration by fuses still needs a way to reach mode `01` (Inference). |
| 2 | ROM fall-through | If an error occurs during internal boot, the ROM jumps to the serial downloader. In closed configuration, authentication errors abort the boot to the serial downloader. In open configuration, authentication errors are logged and do not change the boot flow. | Nothing beyond path 3 | B-3, B-4, B-5 | Verified by public vendor document. That a corrupt first sector or IVT counts as an error is not stated: Inference (B-6). |
| 3 | What the serial downloader can do | It loads an image into RAM and jumps to it. It has no flash command. Programming NOR needs a flashloader loaded this way, then `blhost`. NXP's own flow uses this. | A flashloader binary. Its source and licence were not read. | B-11 to B-14 | Verified by public vendor document (B-14: summarising fetch, not a full read). |
| 4 | Serial downloader in closed configuration | The downloaded image is authenticated like any other. NXP's flow loads a signed flashloader. A recovery of a closed device therefore needs the SRK hash fuses, a flashloader signed under that key tree, and a bootloader signed with its own signature structure. | Key custody that outlives the product's service life | B-17 to B-19 | Verified by public vendor document for the flow. The consequence: Inference (B-19). |
| 5 | Application re-enters the serial downloader | The ROM API `runBootloader` takes an argument word. One documented value enters the serial downloader over USB. Another selects the redundant image after an update. | A running application. Not available when the bootloader is defective. | B-30 to B-33 | Verified by public vendor document and Verified on pinned source for the wrapper. Use from an application: Inference. Pinned Zephyr does not enable the ROM API for RT10xx (B-38). |
| 6 | Recovery device on LPSPI | A serial NOR or EEPROM on an LPSPI port is tried if the primary boot device fails. | Fuses (disabled by default) and extra memory on the board. | B-9 | Verified by public vendor document. |
| 7 | SD manufacturing boot | The ROM tries SD/MMC manufacturing boot before the serial downloader when internal and recovery boot fail. | An SD card in the slot. The flow is gated by an "SDMMC MFG mode disabled?" check. The fuse behind it was not examined. #298 should check it. | B-5 | Verified by public vendor document (figure read as extracted text). |
| 8 | ROM secondary image | Fuse-configured secondary image offset for NOR (units of 256 KiB) and a persistent bit that selects the image. For SD and eMMC the manual gives the full rule. For NOR it gives the fuse fields only. | Irreversible fuse writes (see the option N3). | B-48, B-49, B-51 | Verified by public vendor document for the fields. The NOR algorithm: Not verified. |
| 9 | Fuses that remove recovery | `SDP_DISABLE`, `FORCE_INTERNAL_BOOT` and `BOOT_CFG_LOCK` can remove the pin-selected serial downloader or lock the boot configuration. | A product choice | B-20, B-21 | Verified by public vendor document for the fields. The effect on recovery: Inference. The polarity of the UART disable fuse differs between SPSDK and the manual: Not verified (B-22). |

Reading, all Inference:

- D1 (a damaged bootloader) can plausibly be recovered through paths 1 to 4,
  by a person with physical access. It is a service procedure, not an update.
  Its premise, that a damaged first sector falls through to the serial
  downloader, is the one result most worth testing on a board.
- D2 to D4 (a valid bootloader that is defective, vulnerable or trusts the
  wrong key) are not caught by the fall-through. The ROM boots a valid image.
  Only path 1 (by switch) recovers, again by physical access, and in a closed
  device only with the signing keys.
- None of the ROM paths is a remote update.

## Options for the NOR configuration

The NOR configuration is the #141 layout: the `mcuboot` partition holds the
bootloader with the NXP boot header, the slots follow, and `storage` is kept
without an owner (owner decisions 13, 16 and 17 of #141).

### Options

| ID | Option | What it is |
| --- | --- | --- |
| N0 | Single bootloader, declared service recovery | The #141 baseline. No bootloader update. Recovery is the ROM serial downloader (paths 1 to 4). Optionally prepare the path: keep the mode pins reachable, do not set `SDP_DISABLE`, and plan an application call of `runBootloader`. |
| N1 | Application rewrites the `mcuboot` partition in place | An update agent erases and programs the bootloader region while the device runs. Included only to record why it is rejected. |
| N2 | Two stages: a small first stage and an updatable second stage in two banks | The ROM boots a small first stage that verifies a second stage (the MCUboot of M8) and starts the valid bank with the newest version. The pattern of the Nordic immutable bootloader (A-27 to A-32) and TF-M BL1 and BL2 (A-37, A-38). |
| N3 | ROM-level redundancy with a second bootloader copy | Program the secondary-image offset fuse so the ROM can boot a second bootloader copy at a multiple of 256 KiB. |
| N4 | Vendor flash remap | The vendor SDK's direct-XIP MCUboot port, which uses the FlexSPI address remap to switch application slots. |

Variants of N2:

| ID | Variant | Difference |
| --- | --- | --- |
| N2a | One image per bank, executed in place | Each bank is linked to its own address, so there are two builds, as in Nordic's S0 and S1 variants (A-30, A-50). |
| N2b | The first stage copies the second stage to RAM and runs it there | One build, like TF-M BL1_2 copying BL2 to RAM (A-37). Feasibility with MCUboot on RT1060 is Not verified (A-51). |
| N2c | N2a with one linked image and the RT1060 address remap | The first stage sets the remap registers so one build runs from either physical bank. Uses the mechanism of N4 (B-46, B-54). |

### What the sources say about each option

| ID | Findings | Label |
| --- | --- | --- |
| N0 | The ROM paths are in the previous section. MCUboot's own recovery options (serial recovery, USB DFU, firmware loader) recover a bad application, not a bad bootloader (A-9, A-10, A-25). A bootloader that is defective but valid is not caught by the ROM fall-through, so only a switch or an application call reaches the serial downloader. With all slots invalid, MCUboot loops with no reset (A-22, A-23). | Verified on pinned source and by public vendor document |
| N1 | No stock path writes the `mcuboot` partition. mcumgr image management and MCUboot serial recovery reach slot areas only (A-10, A-18). It would be new code. The partition holds the FCB at offset 0 and the IVT at `0x1000` (#141 Stage 6 section 1.2), so an interrupted erase of the first sectors removes what the ROM needs. The device then depends on path 2 or 1. No A/B fallback exists, so a single power cut in the update window can leave the device with no bootloader. | Inference from A-5, A-10, A-18, B-3 and #141 Stage 6 |
| N2 | Upstream MCUboot has no first-class path (A-1, A-11, A-13). A maintainer suggests exactly this shape for an updatable MCUboot: a light first stage and a second stage (A-7, A-33). Zephyr's sysbuild offers no chain and `BOOTLOADER_MCUBOOT` does not apply to an MCUboot image (A-11, A-14), so the first stage and a second-stage MCUboot built for a bank need a custom configuration, with scripted signing (A-49). Public designs make the first stage immutable by hardware: ROM, a write lock set at each boot, keys in one-time memory (A-53). They update the second stage by either the first stage (Nordic) or a later stage (TF-M's first stage contains no update logic, A-38). None documents a guarantee for a power cut during the write of a new second stage (A-32, A-54). | Verified on pinned source and by public vendor document for the facts. The pattern reading: Inference |
| N3 | The RT1060 manual lists the secondary image offset fuse (`0x6E0` bits 23:16, units of 256 KiB) and a persistent bit that the ROM sets (B-48). For SD and eMMC the rule is given: a secondary image table at offset `0x200`, and in closed configuration the ROM switches to the secondary image after a failed primary authentication (B-49). For NOR the algorithm is not in the text read. The SDK guide says redundant boot needs a plugin boot application, which conflicts with the manual (B-50). | Verified by public vendor document for the fields. The NOR behaviour and the conflict: Not verified |
| N4 | The vendor port keeps one 256 KiB bootloader outside the remap window and switches the application slots by `IOMUXC_GPR` registers 30 to 32 on RT1060 (B-46, B-52 to B-54). No bootloader self-update, redundant bootloader or bootloader recovery path was found in the vendor documents searched (B-55). The vendor Zephyr note is marked draft and covers another SoC (B-56). | Verified by public vendor document. Absence over the named files: Not verified |

### Comparison

All cells are Inference from the cited rows unless a row says otherwise. "Layout" means the fenced layout proposed in the #141 record (`mcuboot` `0x000000` to `0x020000`, slots of 3 MiB each, `storage` last).

| | N0 | N1 | N2 | N3 | N4 |
| --- | --- | --- | --- | --- | --- |
| Failure classes helped | D1 to D4 by service, not remotely. By the mode pins (path 1), and for D1 possibly by the ROM fall-through (Inference). | None safely | A second stage that fails validation is rejected and the other bank starts (a D1-type failure of the second stage). D3 and D4 by update. A second stage that validates but is defective (D2) needs a confirm state (below). | D1 for a copy that the ROM rejects. D3 and D4 by update. | None. It updates the application only. |
| Field update of the bootloader | No | Yes, unsafe | Yes, of the second stage. Not of the first. | Yes, of the secondary copy and, if the roles can swap, the primary | No |
| Effect on layout | None | None | The region before the slots grows from 128 KiB to at least 192 KiB, see below. | The second copy starts at `0x40000` or later (multiples of 256 KiB). With 128 KiB per copy the region grows to about 384 KiB. | A 256 KiB bootloader region and 2 MiB slots in the vendor port, see below. |
| NOR writes of the bootloader itself | None added | The update rewrites the boot sectors (#22, #165 hazards, plus power-cut brick) | The first stage can stay write-free. Bank writes are done by the application or the second stage, see below. | Same as N2 for the copy | None added |
| Needs #165 closure first | Same as M8 | Yes, and more | Yes for every NOR writer (second stage, bank writes) | Yes for the copy writer | Same as M8 |
| Hardware and fuses | None. The mode pins must stay reachable. | None | None for the function. HAB closed (fuses) for a meaningful first-stage trust. NOR block protection to lock the first stage is not verified. | Irreversible fuse write (the secondary offset) and probably closed configuration for the trigger | None |
| Main cost and risk | A field visit per bootloader defect | Brick risk, new unreviewed code | Custom first stage, a new signing structure, two or three builds, more layout, key policy. First stage size not known. | Fuse decision, unverified ROM algorithm, 256 KiB granularity | Not an answer to this question |
| Needs a scope decision | No | No (rejected) | Yes: bootloader customisation, the #141 owner decision 1 (variant b) class | Yes: fuses are a product decision | No |

### Effect on the fenced layout

Arithmetic from the trial sizes in #141 (Verified by build there): MCUboot
uses 51,760 B and 53,940 B. The #141 decision keeps boundaries 64 KiB aligned
until the erase size is measured. One bank for MCUboot therefore takes 64 KiB.

| Option | Layout consequence | Label |
| --- | --- | --- |
| N2a, N2b, N2c | First stage with the boot header in the first 64 KiB (FCB at `0x0`, IVT at `0x1000`, vectors at `0x2000`), then two 64 KiB banks: `0x00000` to `0x30000`. Slots then start at `0x30000`, 64 KiB later than in #141. With 3 MiB slots, `image-1` ends at `0x630000` and `storage` shrinks by 64 KiB to 1,856 KiB. `storage` has no owner in M8, so nothing is lost. A bank larger than 64 KiB, or a first stage larger than the space before the banks, adds more. | Inference (arithmetic) |
| N2b | Only one bank image is needed, but RAM must hold the second stage. The MCUboot RAM region in the RAM load trial is a trimmed OCRAM2 region (#141 Stage 6 section 1.3), so RAM size is unlikely to be the limit. Whether code can run there with the ROM and boot flow is Not verified. | Inference. Feasibility: Not verified (A-51). |
| N3 | The ROM secondary offset is in 256 KiB units (B-48). The second copy therefore lies at `0x40000` or beyond. Whether the secondary copy needs its own FCB and IVT at fixed positions is Not verified. | Verified by public vendor document for the unit. Rest: Not verified. |
| N4 | The vendor port places the application slots at `0x40000` and `0x240000` after a 256 KiB bootloader region (B-52). That differs from #141's `0x20000` start, which shows the #141 layout is not the only one. | Verified by public vendor document |

The fenced layout of #141 does not need to change for N0. For N2 and N3 it
changes, and the decision is needed before the M8 layout is adopted if either
is to stay possible without a later move of the slots. The owner decision 17 of
#141 (keep `storage` unowned, keep 64 KiB boundaries) already keeps that
cheap. See the recommendation.

### Effect on the NOR write hazards (#22 and #165)

#22 recorded that writing NOR while the code, data or exception paths still
read the same NOR is unsafe until closed, and its phase matrix lists the
hazards ([docs/flash-xip-evidence.md](flash-xip-evidence.md)). #165 records that
the MCUboot write path shows the same data-closure failure and blocks any
bootloader NOR write on hardware.

| Question | Result | Label |
| --- | --- | --- |
| Does an updatable bootloader add NOR writers? | Yes. Whoever writes a new bootloader or bank is a new NOR writer, on top of the writers already in #141 (application slot writes and the bootloader's `copy_done` and revert erases). | Inference |
| Can a first stage avoid the hazard? | Only if it writes nothing. A first stage that selects, verifies and jumps has no write path, so it needs no #22 closure for writes. TF-M's first stage works this way (A-38). Nordic's documents say its immutable bootloader is responsible for upgrading the second stage but do not say which code writes the slot, so whether its first stage has a write path is unresolved (A-30, A-32). | Inference from A-30, A-38 |
| Who may write a bank? | A second stage that writes the other bank executes in place from NOR while it programs NOR, the exact #22 case. An application that executes from SDRAM, as in the #141 rank 1 RAM load, does not execute from NOR while it writes. It still needs the #22 items for bus masters, cache and FlexSPI state, which #141 lists as not verified. | Inference from #141 Stage 6 section 2 and #22 |
| Does it change the #165 scope? | It widens it. #165 asks which phases apply to the bootloader. A bank write adds a phase (write a candidate second stage while the running one is in NOR). A first stage with no write path leaves #165 unchanged for that stage. | Inference |
| What if power fails during the bank write? | The first stage must then reject the incomplete bank and fall back to the other. That requires full signature verification of the bank at every boot. The cost is one hash of the bank per boot. No source gives the guarantee. | Inference from A-32, A-37, A-38, A-54 |
| Where is "second stage confirmed" kept? | A second stage that starts but is defective (D2) is only caught if something records "not confirmed" and the first stage reverts. State in NOR needs a first-stage write path. State elsewhere (retained registers) was not examined. The ROM itself uses a persistent register bit (B-48). | Inference. Retained state: Not verified |
| Does N3 avoid these writes? | No. A second copy is written by someone, with the same hazards. | Inference |

## Options for an eMMC configuration (expectations for #298)

This section does not answer #298. It states what the public sources suggest,
so #298 can check each point. All rows are from [evidence C](evidence/boot-bootloader-update/c-emmc-expectations.md)
unless marked. The JEDEC standard was not read for these rows. The RT1060 reference
manual was read only where the B rows cite it (B-5, B-48, B-49, B-84).

### Options

| ID | Option | What it is |
| --- | --- | --- |
| E0 | Single bootloader copy, declared service recovery | As N0. The bootloader sits in one boot partition or in the user area. Recovery is the ROM serial downloader or another ROM path. |
| E1 | A/B bootloader in the two boot partitions | Write the inactive boot partition, then switch the boot partition selector, as the RAUC `boot-emmc` slot type and the SWUpdate handlers do. |
| E2 | Two bootloader copies in the user area with the ROM secondary image table | The manual gives a ROM redundancy rule for SD and eMMC: a secondary image table at offset `0x200` and a persistent bit (B-49). |
| E3 | Protected first stage in a boot partition plus an updatable second stage | A small first stage in a boot partition that is write protected after provisioning, and a second stage in the user area that is updated and verified by the first stage. The eMMC form of N2. |

### What the sources suggest

| Point | Expectation | Label | Rows |
| --- | --- | --- | --- |
| Two boot partitions and a selector | An eMMC has two boot partitions of equal size and one selector byte (`PARTITION_CONFIG`, byte 179, bits 5:3). Three independent projects (mmc-utils, U-Boot and RAUC) agree on the layout. | Verified by public vendor document | C-2, C-4 |
| A/B pattern | RAUC reads the active partition, writes the other one, switches the selector, and fails if the switch does not take effect. SWUpdate only switches. RAUC states that the switch is atomic and that this prevents a non-bootable system after an error or power loss during the update. SWUpdate makes no atomicity statement. Neither source states the power-fail behaviour of the image write itself. | Verified by public vendor document | C-16, C-17, C-21 |
| Write protection | Boot partitions can be write protected by a power-on setting, cleared only by a power cycle or a reset-pin toggle, or by a permanent setting that cannot be undone. No tool or kernel code read can clear either by software. | Verified by public vendor document for the semantics (a vendor white paper summary, not the standard). Absence of a clearing path: Inference | C-7 to C-12 |
| The A/B pattern needs unprotected boot partitions at update time | Because protection is set but not cleared by software, E1 and the second-stage update of E3 depend on the product's protection policy. Permanent protection of both boot partitions ends any software replacement of the bootloader in them. | Inference | C-18, C-46 |
| Reset pin | Whether power-on protection can be undone outside software depends on the reset pin wiring and the write-once reset function setting. | Inference | C-14, C-47 |
| Interrupted register write | Power-fail behaviour of the selector write is not stated in any source read. | Not verified | C-15 |
| Zephyr | The public MMC API has only block read, block write and ioctl. The stack writes only four EXT_CSD bytes (bus width, timing, power class, cache control). It does not parse or write the partition configuration or write protection. A boot partition update would be new code, probably through the host controller API, which the documentation says applications should not use directly. | Verified on pinned source. Use of the host API: Inference | C-27 to C-35 |
| `hal_nxp` | The `fsl_usdhc` driver has host-side MMC boot registers, not card partition selection. No MMC card middleware exists in the tree. | Verified on pinned source | C-36, C-37 |
| RT1060 ROM and eMMC | SPSDK data for the RT1060 lists SD, MMC and eMMC boot layouts through an alias to another device, and the RT1060 file itself lists only NOR memory configuration. Whether the aliased entries apply to the RT1060 is Not verified (C-39). The manual says that after card initialisation the ROM reads `BOOT_PARTITION_ENABLE` in EXT_CSD byte 179 and boots from that boot partition, or from the user area if none or the user area is selected. It ties fast boot and boot acknowledgement to boot configuration fuses (B-84). The manual also gives the SD and eMMC redundancy rule and the SD manufacturing boot order (B-5, B-49). What the ROM does when a boot partition is corrupt, and whether it falls back to the other one, was not found. | Verified by public vendor document for the selection, the rules and the tool data. Fallback between boot partitions and behaviour on hardware: Not verified | B-84, B-5, B-49, C-39 to C-44 |
| NOR hazards | #22 and #165 are about code that executes from the NOR it writes. An update written to eMMC through the uSDHC does not use FlexSPI. The corresponding hazards are DMA and cache coherence in the SDRAM-resident application, which are not part of #22. | Inference | |
| Product-owned settings | Permanent write protection, the reset function setting, protection disable bits, the first programming path and return handling are decisions outside firmware. No public source on return handling of permanently protected parts was found. | Inference for the ownership. No source: Not verified | C-45 to C-50 |

### Handoff to #298

| #298 question | What #299 expects it to settle for the bootloader update | Rows |
| --- | --- | --- |
| 1. What does the ROM load from the boot area, and is it authenticated? | The manual says the ROM boots from the partition named by `BOOT_PARTITION_ENABLE` or from the user area (B-84). #298 should check where the HAB container sits, and whether the ROM secondary image rule (B-49) applies to the boot partitions or to the user area. | B-5, B-49, B-60, B-84, C-39, C-41 |
| 3. How is a damaged bootloader recovered, given boot partitions and write protection? | Whether the ROM falls back between the two boot partitions when one is damaged, how the boot configuration fuses interact with `PARTITION_CONFIG` (B-84 names two; other i.MX forum threads suggest fuses or pins may also matter), and which protection state the product will use. | B-84, C-9 to C-14, C-25, C-26, C-44, C-46 |
| 2. Can the MCUboot port read slots from eMMC? | Not needed for the bootloader update. A bootloader in a boot partition is not an image slot of MCUboot, so a slot backend does not decide E1. | A-3, A-4 |
| 4. Which stage initialises SDRAM and the card controller? | A first stage that loads a second stage needs the same initialisation as the bootloader does today. | #141 Stage 3 D6 to D11 |
| 5. What does a transactional store prove about atomic activation? | For the bootloader, activation is the selector write or the secondary-table sector write, not a file system operation. | C-16, C-17, B-49 |

All cells of the handoff table are Inference from the rows named.

## Trust consequence of an updatable bootloader

| Point | Result | Label |
| --- | --- | --- |
| Today (#141) | The boot ROM does not enforce authentication of the bootloader in open configuration (errors are only logged, B-4). A Zephyr-built bootloader has no CSF. Anyone with flash or debug access can replace the bootloader and its embedded public key. | #141, Verified on pinned source and Inference |
| An updatable bootloader without HAB | The update path becomes a way to change the root of trust at run time, not only the application. Whoever can reach it can replace the embedded key. It improves availability and key rotation (D3, D4). It adds no enforcement. MCUboot's maintainers say a second MCUboot behind no unmodifiable first stage is mostly redundant. | Verified by public vendor document for the maintainer view (A-33). The reading: Inference |
| What HAB adds when closed | The ROM authenticates the first image against an SRK hash in fuses on every boot, and a failed authentication aborts the boot to the serial downloader, after any redundant-image retry (B-5). The key hash in fuses is the hardware root of trust that "immutable" needs. In open configuration, authentication errors are logged and the boot continues, so a signed bootloader is not enforced (B-4, B-69). | Verified by public vendor document (B-4, B-17). Open-configuration reading: Inference |
| What HAB can verify later | The ROM exposes a HAB API at `0x0020_0300`, so a first stage could authenticate a second stage. The calling convention for RT1060 was not read. | Verified by public vendor document for the entry point (B-63). Use: Inference (B-65) |
| Key revocation | HAB documents up to four root keys and revocation by fuse. Revocation is permanent. How many of the four RT1060 revoke bits are valid, and whether the last key is protected, is not confirmed. | Verified by public vendor document for the existence (B-24, B-61). Behaviour: Not verified (B-62) |
| Revocation needs an update path | After a revocation, the old bootloader signed with the revoked key does not boot. The device then needs a bootloader signed under another key, which only an update path or a service recovery delivers. Using revocation without a plan for installing the new bootloader strands units. | Inference from B-62 and B-19 |
| Version rollback of the bootloader | No ROM-checked version field appears in the IVT, boot data or CSF commands read. HAB offers key revocation, not version-based anti-rollback. The manual's security overview lists "image version control/image revocation (on-chip OTP-based)" as a feature and the pages read do not describe it (B-67). A second-stage downgrade check would be software, with a counter held in fuses or another persistent store. The vendor direct-XIP mode says it has no rollback support. | Inference (B-67, B-68). Counter choice: Not verified |
| Closed configuration and recovery | Closed configuration also gates the serial downloader. Recovery then needs the signing keys and a signed flashloader. Key custody must last as long as the service life. | Inference (B-19) |
| Separate keys | Nordic's design allows a different signing key for each bootloader stage, so a leaked application key does not allow a bootloader replacement. | Verified by public vendor document (A-31) |
| No ROM protection without fuses | Without fuse programming none of the ROM-side gains exist. The trust level stays that of #141. | Inference |

## What each option needs from hardware or fuse settings

None of this was exercised. Fuse writes are expected to be irreversible (Inference: the manual lists
the fuse array as one-time programmable and does not state it per field, B-26).
Every fuse write stays a product decision.

| Option | Hardware | Fuses | Other | Label |
| --- | --- | --- | --- | --- |
| N0 | Mode pins and a USB or UART connection reachable on the board. The EVKB switch settings are in #141. | None. Do not set `SDP_DISABLE`, `FORCE_INTERNAL_BOOT` or `BOOT_CFG_LOCK` without a recovery plan. Closed configuration also needs the SRK hash fuses, a signed flashloader and key custody. | For a hardware check of the ROM fall-through: a sacrificial NOR content, a saved copy of the old content, and owner approval. | Inference (B-8, B-19 to B-21) |
| N1 | None | None | Rejected | Inference |
| N2 | None for the function. A NOR part with block protection could lock the first stage, which was not examined. | None for the function. Closed configuration for the first stage to be enforced. | Custom first stage and scripted signing. Remap registers for N2c need no fuse (B-46), and their effect on reads and writes through the controller was not read. | Inference. Remap behaviour: Not verified |
| N3 | A board that may have fuses written. The EVKB is listed by NXP as no longer manufactured (#141 Stage 3 E2). | Secondary image offset fuse. Probably closed configuration for the fallback trigger. | Needs the unverified NOR algorithm confirmed on a board first. | Inference (B-48, B-49, B-51) |
| N4 | None | None | Application-slot mechanism only | Verified by public vendor document |
| E0 | An eMMC board with the mode pins reachable. Not the EVKB: its boot media is NOR and SD (#141). | Boot configuration fuses for MMC boot were not read. | #298 | Inference |
| E1 | An eMMC with boot partitions and unprotected boot partitions at update time. | The ROM boot configuration for eMMC (not read). | New application code for the selector. Reset pin wiring matters for power-on protection. | Inference (C-9 to C-18) |
| E2 | As E1 | Secondary image rule from the manual (B-49) | The ROM persistent bit and the table at offset `0x200` | Verified by public vendor document for the rule. Fit with eMMC: Not verified. |
| E3 | As E1, plus a protection policy for the first-stage partition | Permanent protection is one-way and a product decision. | A first stage loading a second stage from the user area | Inference (C-46) |

## Recommendation

### Should any option become a milestone?

**No option should become a milestone now.** Reasons:

1. Every option that gives a field update (N2, N3, E1 to E3) needs something the
   current planning does not have: a custom first stage, irreversible fuses, or
   a protection policy. Each is an architecture or product decision, not
   routine work.
2. Every option that writes NOR waits on #165, which is not scoped.
3. The key trust benefit needs closed configuration, which is a product
   decision, and without it an updatable bootloader adds surface without a root
   of trust.
4. The eMMC answer depends on #298, which has not reported.
5. The one result that supports the baseline, the ROM fall-through for a
   damaged bootloader, is Inference and has not been tested.

M8 and M9 should therefore stay as planned, and a bootloader update should not
become an M9 exit criterion.

### What to do instead

| # | Step | Authority needed | Why |
| --- | --- | --- | --- |
| 1 | State the bootloader update and recovery level in the M9 results, next to the anti-rollback and root-of-trust levels that #297 already requires. Wording: single copy, not field-updatable, recovery by service through the ROM serial downloader, with the verification state of that recovery. | A change to #297's acceptance criteria. The owner decides. This record changes no issue. | The gap is then declared and not hidden. |
| 2 | Include the bootloader version in the boot-result record of #297. MCUboot can share its version with the application when data sharing is on, and mcumgr's bootloader information reports no version (A-17, A-19). Data sharing is already needed for the RAM load confirm path (#141 B64). | Same as step 1. | A deployed fleet can then say which bootloader it runs, which any later decision needs. |
| 3 | Plan a bounded ROM recovery check on the EVKB in open configuration, without fuse writes: corrupt the first sector on a sacrificial NOR content, see whether the device enumerates in the serial downloader, and recover it with the NXP flashloader flow. Optionally also test the application call of `runBootloader`. | A separate approval for the hardware task, as AGENTS.md requires. The NOR content must be saved first. Not authorised by this record. | It turns the main Inference into evidence and defines the safety net of every option. |
| 4 | Keep N2 with these properties as the candidate design if a field-update requirement is confirmed: a first stage with no write path, a second stage in two banks, updates written by an application that runs from SDRAM, full verification of the bank at every boot, a separate signing key for the second stage, and a confirm state held outside NOR. | A scope decision on bootloader customisation (the #141 owner decision 1 class) and an architecture review. | It is the only option whose first stage can avoid the NOR write hazards. |
| 5 | Do not choose a layout in M8 that makes N2 expensive later. Keep `storage` unowned and keep 64 KiB boundaries (#141 owner decision 17), and do not assume that the `mcuboot` partition is exactly 128 KiB in tools or scripts. | Already within #141 decisions. | Moving the slots later costs a migration. |
| 6 | If a field-updatable bootloader becomes a requirement, create the work as an investigation and prototype after #165, #298 and the M8 hardware results. It would need a product decision on HAB closed configuration first. | Owner decision. | Keeps the order of dependencies. |

Nothing in this table is a request to change a milestone or an issue. The
owner decides which steps to take.

### What stays a product decision

- Fuse programming of any kind: SRK hash, closed configuration, serial
  downloader disable, forced internal boot, boot configuration lock, secondary
  image offset, key revocation.
- Whether a closed security configuration is used, and key custody, signing
  ceremony and revocation policy for the service life.
- Whether a field-updatable bootloader is required, or service recovery is
  acceptable. This sets the risk accepted for D2 to D4.
- Whether the product board keeps the boot mode pins or a service port
  reachable.
- The anti-rollback counter for a bootloader, if any, and where it is kept.
- eMMC boot partition write protection (power-on or permanent), the reset pin
  and its setting, and return handling.
- The choice of a NOR block protection policy, if the NOR part offers one.

### Open questions for the owner

| # | Question | Recommended answer |
| --- | --- | --- |
| 1 | Is a field-updatable bootloader a requirement for M8 and M9, or is service recovery accepted? | Accept service recovery for M8 and M9. Decide again after #165, #298 and the M8 hardware results. |
| 2 | Should #297 declare the bootloader update and recovery level, and carry the bootloader version in the boot-result record? | Yes. It is a declaration, not new scope. |
| 3 | May a later hardware task check the ROM fall-through on the EVKB in open configuration, without fuses? | Yes, as a separate bounded task after the M8 hardware step is planned. |
| 4 | Is closed security configuration in the product's plan? | Product decision. The record cannot advise on fuse programming. |
| 5 | If N2 is pursued: separate signing keys for the second stage and the application? | Yes, as Nordic's design allows. |
| 6 | For eMMC, which write protection state will the product use for boot partitions? | Product decision, to be known before #298 closes. |

## Not verified

| Item | Needs |
| --- | --- |
| That a corrupt FCB or IVT in NOR falls through to the serial downloader, in open and in closed configuration (B-6) | A board, saved NOR content, owner approval |
| What happens with the serial downloader disabled and a failed boot (B-21), and the polarity of the UART disable fuse (B-22) | The Security Reference Manual or a board |
| Calling `runBootloader` from an application, and the ROM API from XIP code (B-33, B-35) | A board and new code |
| The ROM secondary image algorithm for NOR and the conflict with the SDK guide's plugin statement (B-48, B-50) | The Security Reference Manual and a board |
| HAB event content with a blank SRK hash, the SRK revoke bit behaviour and the RT1060 HAB API layout (B-62, B-65, #141 A13) | The Security Reference Manual, AN12079 and a board |
| Whether MCUboot or Zephyr refuses a slot that overlaps the executing bootloader (A-8) | A targeted source read or a build |
| Minimal size of a first stage, and of MCUboot on a Cortex-M7 with ECDSA P-256 (A-52) | A build |
| Whether MCUboot can run from RAM through the ROM RAM loader on RT1060 (A-51, N2b) | A build and a board |
| Whether the RT1060 remap registers apply to program and erase commands, and their state across resets (B-46) | A board |
| Whether the EVKB NOR part offers block protection that could lock a first stage | The part data sheet |
| Where retained state for a second-stage confirm can live | The reference manual and a board |
| JEDEC eMMC semantics: `BOOT_CONFIG_PROT`, write-once protection disable bits, power-fail behaviour of register writes (C-6, C-13, C-15) | The standard or a data sheet |
| RT1060 ROM eMMC boot partition selection, fallback and fuse dependence (C-39 to C-44) | #298 |
| Everything about RT1170 ROM redundancy and fall-through (B-83) | The RT1170 reference manual |
| The author re-read five rows (A-22, A-33, B-30, B-46, B-84). The remaining rows of evidence A, B and C were not re-read by the author. Reviewers could not check rows whose source was not in their pack (see the Review record). | A later full re-read, or the independent review of a later revision |

This record does not claim that any option works on hardware, that the serial
downloader recovers a damaged bootloader, that a first stage of any size can be
built, that HAB or fuse settings behave as the sources describe, that eMMC boot
partitions can be used on RT1060, or that any M8 or M9 criterion is met.

## Review record

**Round 1, 7 November 2026.** Two independent reviewers, both the repository's
`reviewer` agent (model `claude-opus-5-5`, read-only tools, neither wrote the
record), each took one scope: (1) the NOR options, ROM recovery, trust and
evidence A and B, and (2) the eMMC section, evidence C and the record-wide
label and boundary checks. They read the files and checked the cited sources
in a downloaded source pack, because they had no shell. The author ran
`scripts/check_docs.py` (857 links, 0 errors). CI results are on the PR.

| Reviewer | Verdict | Blocking findings | Other findings |
| --- | --- | --- | --- |
| 1 | REVISE | 5: the summary called the serial downloader fall-through the only recovery path although three more paths are in the ROM table. The summary said only a two-stage design gives a field update, against the comparison table. The N0 cell omitted D3 and D4 that the reading covers. The trust table said the ROM does not authenticate the bootloader in open configuration, against B-4. Evidence B-48 claimed a manual search found only five places, but NAND text also matched. | 7: wording of "project position", D1 and D2 mix-up in the N2 cell, Nordic first-stage write path unresolved, irreversibility label, image version control line, redundant-image retry, a missing row cite |
| 2 | REVISE | 2: the claim that the RT1060 ROM boot partition selection was not found in public material, while the manual copy in use states it (new row B-84). A label stronger than its evidence on atomicity, and a wrong statement about SWUpdate. | 5: unlabelled handoff cells, "four tools" count, SPSDK alias caveat, evidence C header, minor row wording |

All blocking and other findings were fixed in the fix commit of this PR.
Where the fix added a statement (B-84), the author read the manual text
directly. Reviewer 1 could not check rows A-7, A-27 to A-32, A-33, A-34, A-37,
A-38, B-14, B-18, B-50, B-52 to B-56 and B-62, because their sources were not
in the pack (the author re-read A-33). Reviewer 2 did not check C-1, C-3, C-5,
C-6, C-15, C-19, C-20, C-22 to C-26, C-29, C-31, C-32, C-40 to C-50 beyond label
fit. These rows stay as the research agents recorded them.

**Round 2 (narrow re-check of the fix commit only):** pending.
