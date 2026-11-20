# HAB signing in the open configuration: plan

> **Status:** Proposed. A plan for the project owner. It adopts nothing beyond the decisions it cites. Since 16 November 2026 one narrow approval is recorded (per-run throwaway keys in a software test, see section 8); it approves no hardware operation, fuse operation, persistent key or other CI change. Section 8 lists what the owner must decide or approve.
> **Verified:** Only that the offline structure checker named in section 3 passes its host tests on synthetic images. Everything else is a plan or a citation of a desk record.
> **Not verified:** (As of writing the plan; the software signing test added later is described on the status page.) That any tool named here accepts a Zephyr-built first-stage image, produces an image the boot ROM accepts, or yields any HAB event on a board; every HAB behaviour on the RT1060; the layouts the checker assumes. No key was generated, no tool was installed or run, and no board was used for this plan.
> **Read this first / then:** the [HAB status page](../boot-hab-status.md), then the [secure boot development plan](secure-boot-development-plan.md) (phases 4 and 5, sections 3 and 6).

| Field | Value |
| --- | --- |
| Status | **Proposed.** |
| Date | 16 November 2026 |
| Plan phase | Phase 4 of the [secure boot development plan](secure-boot-development-plan.md): "HAB in open configuration". Trust level T2 at most. T2 is defined as T1 plus a HAB-signed first stage, and neither T0 nor T1 has been reached, so a Phase 4 result alone cannot claim T2: it can only add the Phase 4 statements of section 1 to whatever lower level is shown. |
| Issues | [#440](https://github.com/wonny-labs/crux-pilot/issues/440) (eMMC first-stage layout), [#296](https://github.com/wonny-labs/crux-pilot/issues/296) (NOR layout, "a build path for a HAB-signed bootloader image"), [#309](https://github.com/wonny-labs/crux-pilot/issues/309) (ROM serial downloader check, which may share a hardware session) |
| Records it builds on | [HAB status page](../boot-hab-status.md); [boot and update investigation](../boot-update-investigation.md) (#141); [eMMC boot investigation](../boot-emmc-investigation.md) (#298); [bootloader update investigation](../boot-bootloader-update-investigation.md) and its [evidence B](../evidence/boot-bootloader-update/b-nxp-rom-fuses-hab.md) (#299, rows cited as B-n) |
| Author model | `claude-sonnet-5-5` (tool: Claude Code). Independent review: see the pull request. |

Labels follow the boot records: Verified on pinned source, Verified by build, Verified by public vendor document, Inference, Not verified. A statement without a label is a proposal of this plan or a citation of a decision record. Documents under a non-disclosure agreement were not used and are not cited.

## 1. Purpose, scope and what a result would mean

**Purpose.** Show, offline and then on one board in open configuration, that a HAB-signed first-stage image can be built reproducibly from documented inputs, that its HAB structures are well formed, and what the ROM reports about it. This answers the open parts of #296 ("a build path for a HAB-signed bootloader image", "HAB open-configuration event evidence or the recorded deferral") and #440, in that order of risk.

**Scope.** Open configuration only. Offline signing with throwaway development keys. A structure check. A HAB event read on a board as a separately approved hardware block.

**Not in scope.** Fuse programming of any kind, including SRK hash fuses and SEC_CONFIG; closed configuration; production keys; key custody for products; SRK rotation and revocation; image confidentiality; the NOR secondary-image fuses; any change to the boot ROM serial downloader policy; a root-of-trust claim.

**What a result would mean.** In open configuration the ROM runs HAB, logs authentication errors and boots the image anyway, even without authentication data (B-4, Verified by public vendor document). So a successful boot of a signed image proves nothing about its signature. A signed first stage in open configuration adds checking and reporting but no root of trust (B-69, Inference). Whether a signature check can pass at all with blank SRK hash fuses is Not verified for the RT1060 (#141 stage 3 A13). The most this plan can reach is level T2, with these statements: "the image has a well-formed CSF", "the structure check passes", and, after the hardware block, "the ROM reported these statuses". It must never say "authenticated" or "secure".

## 2. Decisions and constraints in force

| Item | Source |
| --- | --- |
| A later HAB event read in open configuration, without fuse writes, is allowed only as a separately approved hardware task, which may share a session with the ROM serial downloader check (#309). | #141 owner decision 8 (7 November 2026), plan section 2 |
| Closed configuration is planned for production (D1), anti-rollback is application-side until D1 (D4), recovery stays signed-only and is decided with D1 (D5). None approves a fuse operation. | Plan section 6, owner answers of 15 November 2026 |
| HAB key algorithm and size, key custody (D6): RSA-3072 or larger, offline keys in an HSM, up to four SRK slots; which slots are revocable stays Not verified. D6 creates and provisions no key. | Plan section 6 |
| No production keys; development keys stay outside the repository. Throwaway keys that a software test creates in a temporary directory and deletes were approved on 16 November 2026 (second-hand quote on the [HAB status page](../boot-hab-status.md#software-signing-test-throwaway-keys-no-board)); no other key generation is approved. | #296 boundaries; #141 owner decision 18 (as the plan cites it); this plan |
| The SPSDK download was approved under #141 A2 (2 November 2026); since 16 November 2026 SPSDK 3.13.0 is used for the software signing test only. CST and the Secure Provisioning tool have no approval. | [Boot and update investigation](../boot-update-investigation.md), A2 |
| The RT1060-EVKB is open and stays open. A dedicated board for closed-configuration work is deferred. | Plan section 9; #141 decision 11 |
| A new check script may be added within a task; an edit to an existing workflow, script or other protected path needs an explicit human approval; a new evidence record may be added within the task. | `AGENTS.md`, protected paths |
| Physical programming, reset and other hardware operations need explicit authorisation for the current task, a verified target and a safe setup. | `AGENTS.md` |

## 3. Work blocks

Blocks A to F are board-free. Block G is the hardware block. Block H is not part of this plan.

### Block A: structure checker (done in the change that adds this plan)

`tests/target/check_hab_structure.py` with host tests in `tests/build/test_hab_structure.py`, on synthetic images only. It checks the IVT position (NOR `0x1000`, SD and eMMC `0x400`), the IVT header, the boot data, the self pointer, the DCD pointer and header, the CSF pointer and header against a declared CSF presence, and region overlap, and prints a verdict. It verifies no signature and parses no DCD or CSF command. See the [HAB status page](../boot-hab-status.md#how-to-run-the-checker). Follow-up items, all board-free and small:

- A1: a medium for the file layout that the ROM serial downloader receives, needed by Block G. The IVT offset inside that file is Not verified (`0x0` in SPSDK device data, B-15; `0x400` in a BD file the SDK guide shows, B-18 as summarised). Add it only after the pinned tool's documentation has been read.
- A2: run the checker on the first image a real tool produces, and record any disagreement. Until then every assumption it makes is Inference.

### Block B: tool decision and pin

| Option | Role | Licence and approval state |
| --- | --- | --- |
| NXP SPSDK `nxpimage hab` (`export`, `parse`, `verify` where the pinned version has it) | Primary. Scripted, no GUI, pip-installable, a candidate for CI with throwaway keys. | BSD-3-Clause on PyPI as read; licences of binary dependencies Not verified. The download was approved under A2; the install and first run are scheduled by the owner's approval in section 8. |
| NXP Code Signing Tool (CST) | Fallback only if SPSDK output cannot be produced or processed. | Click-through licence; superseded by SPSDK as read; no approval. A licence review and a separate approval come first. |
| MCUXpresso Secure Provisioning (GUI and `securep`) | Not used. Not in CI or the repository; not a reproducibility path. | Account and EULA. |

Pin SPSDK by exact version and by the hash of the downloaded artefact in a requirements file (the software signing test pins SPSDK and its dependencies by version only, without hashes; a hash lock is a possible follow-up) added outside the protected paths. Committing that file adds a dependency pin to the repository and is part of O2. The version read in #299 was 3.13.0 (published 3 November 2026); the pin is decided at implementation time and recorded. Install into a virtual environment outside the repository and outside any container that mounts the user home directory (`AGENTS.md`). Disk space and Python 3.10 or later are preconditions (SPSDK's own requirement as read in stage 3).

Questions the first run answers offline, each Not verified today:

- Q1: whether `nxpimage hab` accepts a Zephyr-built image that already has an FCB and an IVT (#141 A14), or needs the plain binary and writes its own headers, which would change the offsets of the Zephyr layout. Partly answered: it accepts a plain binary for `mimxrt1060` and writes a bare container (IVT at file offset 0). The Zephyr-image case is Not tested.
- Q2: whether the SPSDK RT1060 device data is correct for the images it writes. The data is an alias chain, and its memory map disagrees with the manual (B-16, Inference). Any output must be compared with the layout the repository records (FCB `0x0`, IVT `0x1000`, boot data `0x1020`, DCD `0x1030` for NOR).
- Q3: whether the tool fully replaces CST for the RT1060 (#141 B6).

### Block C: throwaway development keys

Nothing in this block may start before the owner approves it (section 8, item O1).

- **What they are.** Keys made only to exercise the signing path. They are never programmed into any fuse, never used for a product, never used to sign anything that leaves the bench, and are named "throwaway" in their file names and in the record.
- **Where they are held.** Outside every repository and outside any container, in one directory the author names in the record, readable by the owner's account only, excluded from cloud synchronisation and backups where the host allows, and never pasted into chat, an issue, a PR or a log. A record states the path class (not the user name) and the creation time. Private keys are deleted when the task ends unless the owner asks to keep them until Phase 5.
- **How they are made.** By the pinned tool's own key and certificate generation, run by the author of the task, on the owner's host, with the command line recorded in the record and the output not printed. The exact set (for example an SRK table and the CSF and image signing keys and certificates the pinned flow needs) is read from the pinned tool's documentation first; it is Not verified here.
- **Algorithm.** See O1: RSA-3072 to exercise the class D6 recommends, not RSA-2048. The RT1060 manual copy lists RSA 1024 to 4096 for HAB (B-17, B-61); whether the pinned tool and the ROM accept 3072 for the RT1060 is Not verified.
- **What may enter the repository.** Nothing from the keys. The record may carry the SHA-256 of each public certificate and the tool's output of `parse`, but not an SRK hash presented as a fuse value, so that nobody mistakes the record for a fuse plan.

### Block D: offline signing and structure verification

1. **Inputs.** A first-stage binary built by the repository's composition (for NOR the first stage MCUboot of the [boot composition](../boot-composition.md); the eMMC first-stage image of plan issue B does not exist yet, so this block starts with NOR). Recorded: the source revision, the build command, the toolchain and the image SHA-256.
2. **Description.** A YAML or BD file for the pinned tool in the repository, without any key path: addresses and offsets only. NOR values the records give for the tool data: start `0x60000000`, IVT offset `0x1000`, initial load size `0x2000` (B-66, B-15, Verified by public vendor document as tool data). SD and eMMC: IVT `0x400`, initial load size `0x1000` (B-15); the CSF placement for them is Not verified (#298 X7), which is why eMMC stays last (Block F).
3. **Signing.** `nxpimage hab export` with the throwaway keys held outside the repository. Output goes outside the repository. Nothing printed contains key material.
4. **Verification, in this order.** (a) The tool's own `parse` of the output; (b) `tests/target/check_hab_structure.py check <image> --medium nor --csf present --dcd present`; (c) the same checker on the unsigned input with `--csf absent` as a control, and on a copy whose csf pointer is zeroed as a negative control (the checker must fail it); (d) the MCUboot inspector is **not** applicable to a HAB image and is not run.
5. **Record.** Tool name and exact version, command lines, the input and output SHA-256, the checker's JSON, the open questions Q1 to Q3 answered or still open. The record says "structure verified offline" and never "signature verified" or "authenticated".

Reproducibility: the unsigned input must reproduce byte for byte (the existing reproducibility check). Whether the signed output is byte-reproducible depends on the signature scheme and is Not verified; the criterion follows #141: the unsigned image reproduces, and the signed image's structure verifies against the documented public certificate. Two signed outputs that differ only in the signature bytes are not a failure.

### Block E: a check script and a CI option

A new check script (for example `tests/target/check_hab_signing.sh`) would sign a synthetic or build image with **ephemeral** keys that it creates in a temporary directory and deletes, then run the structure checker. Writing the script is a new file and needs no approval, but **writing it does not authorise running it: every run, locally or in CI, generates keys and needs the owner's approval (O1 covers the one throwaway key set of Block C, and O5 covers the ephemeral per-run keys of this script)**. Running it in CI also means editing `.github/workflows/`, a protected path, which needs explicit approval too. CI would use only ephemeral keys, never a stored secret. Recommendation: do not propose the CI step until Block D has run once by hand.

### Block F: eMMC layout

Waits for the first-stage image (plan issue B), the validation target (D3) and the adoption decision (D2). It reuses Blocks B to D with `--medium emmc` and IVT `0x400`. Open point: the SDK guide's signed-image examples use `0x1000` while the manual gives `0x400` for SD and eMMC; the manual copy is read as authoritative and the guide's examples as NOR (Inference, vendor documents V8).

### Block G: hardware block H-HAB-1, a HAB event read in open configuration

Separately approved, not part of any approval this plan asks for in sections 8 O1 to O5. It is written so that the combined bench runbook can carry it as one block.

**Goal.** Record what the ROM reports when it processes three images in open configuration, with no flash write and no fuse write.

**Route (proposed; Inference, from B-4, B-11, B-12 and B-15).** The ROM serial downloader receives an image in RAM through `sdphost` (`write-file`, `jump-address`, `error-status`). The ROM text says the SDP commands return the security configuration and, on failure, the HAB error status (B-11). Nothing is written to NOR, eMMC or fuses on this route, so the board's stored content is unchanged. Whether open configuration reports any HAB event data to the host with a blank SRK hash is Not verified (#141 A13). If it reports none, the route yields "no event visible through SDP", which is a valid recorded outcome. Reading the event through the ROM HAB API from code on the target is the alternative; the RT1060 layout of that API is Not verified from public sources (B-63 to B-65), so it needs its own decision and is not planned here.

| Field | Content |
| --- | --- |
| ID | H-HAB-1 |
| Prerequisites | Owner approval for this task alone, naming the board, the probe or USB path, the time window and who holds the board (who holds the board is agreed before the task). Blocks B to D done and their record merged or attached. The structure checker passes all three images. The board is in open configuration: before anything else, read SRC_SBMR2 (SEC_CONFIG bits 1:0, B-2) with `sdphost read-register` and **stop if it does not read as open**. The register address is taken from the pinned `hal_nxp` header `PERI_SRC.h` at task time and the open value from the reference manual copy; neither is recorded in this repository, so the task writes both down and has them checked before the board is touched. If `read-register` is refused (the SDP_READ_DISABLE fuse restricts it, B-20), stop; the security configuration that `write-file` returns (B-11) may be used instead only if the task approves it. The boot-medium save and recovery path of plan section 9 are not preconditions here; see "Departure from the plan's hardware preconditions" below. The three images are built and frozen with recorded hashes, following the [freeze and capture design](../rt1060-bench-freeze-and-capture-design.md), before the task. |
| Inputs | I1 unsigned probe image (csf pointer zero); I2 the same probe signed with the throwaway keys; I3 a copy of I2 with one byte changed inside the authenticated range, chosen in constant data that the probe never executes or reads, so that altered code cannot run (in open configuration a modified image still runs, B-4). Each in the layout the ROM serial downloader needs (A1). The probe is a minimal RAM-resident image with an observable marker, for example a user LED pattern or one UART line; it is **not** the first stage and carries no DCD unless the task shows the DCD is needed and safe. It is written board-free and reviewed before the task. |
| Tools | SPSDK `sdphost` at the pinned version, outside the repository; no `blhost`, no flashloader, no fuse tool. |
| Setup | Boot mode: serial downloader through the board's boot-mode switch (the EVKB SW4 values are recorded in #141 D3; check them again at the bench). Power cycle and the USB path the ROM uses (USB1, B-10). Record the switch photo or the readback. |
| Steps | 1. Verify identity, switch state and open configuration (`sdphost read-register`). 2. For each of I1, I2, I3 in a fixed order: reset to the serial downloader, `sdphost write-file` to the image's start address, record the response, `sdphost jump-address` with the image's IVT address, record the response, `sdphost error-status` and the marker observation, a screenshot or log of the host output. 3. Power cycle after each image; no state carries over. 4. Restore the switch to its recorded position and confirm the board's normal boot is unchanged. |
| Observations to record per image | The raw response bytes, the status values returned by each command, whether the marker appeared, whether the board re-enumerated, the time. Nothing is interpreted in the record's table; interpretation is a separate section labelled Inference. |
| Pass criterion for the block | Not a trust claim. The block passes when all three images were run in order, every response was recorded, no stop condition fired, and the record says which of "events visible", "events not visible" and "route not usable" holds. |
| Stop conditions | The board does not read as open; the readback differs from what the task approved; a command other than the four `sdphost` commands of this block (`read-register`, `write-file`, `jump-address`, `error-status`) would be needed; the host tool offers a fuse or flash command as the next step; any unexpected USB device state; the marker or a response suggests the image did something other than the probe; the operator is unsure. Stop and report; do not retry past one repeat of the same image. |
| Forbidden | Fuse read-write tools, `efuse-program-once`, `fuse-program`, any flash erase or write, closed configuration, programming through the probe, a flashloader, key material on the board. |
| Hand-back | The raw captures, the tool versions, the three image hashes, the board state before and after, and a statement that no fuse was written and no flash content changed. |

**Departure from the plan's hardware preconditions (an owner question, O6).** [Section 9 of the secure boot plan](secure-boot-development-plan.md) says every hardware step needs its own approval, a saved image of the boot medium and a proved recovery path first (#296 requires the recovery path before any physical boot experiment). H-HAB-1 runs a probe image on the board but only in RAM through the ROM serial downloader, writes no NOR, eMMC or fuse, and a power cycle with the boot-mode switch restored returns the board to its stored content. On that argument this plan proposes no saved boot-medium image and no recovery path as preconditions. That is a departure from the plan, not a finding that the preconditions are unnecessary: the owner decides it in O6. Saving the NOR content needs a flash read tool, and the flashloader flow that #309 documents is itself a write path, so requiring them first would put #309 before H-HAB-1.

**Sharing a session with #309.** Decision 8 allows it. The two checks are independent: #309 corrupts the first NOR sector and recovers with the flashloader (a flash write, which H-HAB-1 does not do). Run H-HAB-1 first so the board's NOR content is untouched, or run it in the same sitting but never interleave the steps.

### Block H: not in this plan

Closed-configuration work (SRK hash fuses, SEC_CONFIG, a signed flashloader, revocation, debug lockdown), anything on the NOR secondary-image fuses, and production signing. They need the product decisions of Phase 5 and are not approved.

## 4. Order and dependencies

| Step | Needs |
| --- | --- |
| Block A | Done. A1 waits for Block B's documentation reading. |
| Block B | O2. |
| Block C | O1. |
| Block D | Blocks B and C; O3. |
| Block E | Block D run once; O5 for a CI step. |
| Block G (H-HAB-1) | Blocks B to D, the probe image, and O6. |
| Block F | Plan issue B, D3, D2. |

## 5. Evidence and records

- Blocks B to D: a new record under `docs/evidence/boot-hab/` (a new evidence record may be added within the task, `AGENTS.md`) or the PR description, with the tested revision, commands, tool versions, hashes, the structure-check output, the skipped checks and the questions Q1 to Q3.
- Block G: the same folder, separate from the offline record, with raw captures. Board results are never implied by offline results, and the reverse.
- Each record states the trust level reached (T2 at most), the exact words "structure verified offline" or "ROM status recorded in open configuration", and what was not shown.

## 6. Risks

| Risk | Mitigation |
| --- | --- |
| SPSDK does not accept a Zephyr image with an FCB and an IVT, or rewrites its layout | Q1 is the first question; fall back to the plain binary and re-derive the offsets, checked by the structure checker; CST only after its own licence review and approval. |
| The checker's layout reading is wrong | A2: run it on the first real tool output; treat disagreement as a finding on the checker first. |
| A throwaway key is mistaken for a product key or programmed into a fuse | Naming, location and deletion rules in Block C; no SRK hash is recorded as a fuse value; no fuse tool is installed for this work. |
| Open configuration reports nothing | A valid outcome; the plan does not assume events are visible. |
| An SDP-loaded image misbehaves on the board | The probe is minimal, RAM-only, reviewed; the stop conditions; the board's stored content is not written on this route. |
| The result is read as a security claim | Section 1 wording; every record states T2 at most. |
| Tool licences block CI use | Block E waits; ephemeral keys only; licensed tools stay on signing hosts. |
| Disk space or Python version blocks the install | Check both first; do not install into the repository or a mounted home directory. |

## 7. Not decided here

The key algorithm for products and key custody (D6, approved as a recommendation only); the SRK slots and revocation policy; the fuse plan; whether a CI step is wanted; the eMMC signing layout; where the SDP-downloaded file's IVT sits. Each is named so that it is decided by someone with the evidence, not inherited from this page.

## 8. Decisions and approvals the owner must give

Nothing below is given, except the narrow part recorded after the table. Each row gives my recommendation, the reason and the alternative. "Approve" means approve the item as the row words it and nothing more.

| # | Item | Recommendation | Reason | Alternative |
| --- | --- | --- | --- | --- |
| **O1** | **Generate throwaway development keys** (Block C): one key set made by the pinned tool on the owner's host, RSA-3072, held outside every repository and outside any container, never programmed into a fuse, deleted when the task ends. | **Approve, limited to that task and to throwaway use.** | Without a key no HAB-signed image can be built and Block D, A2 and Block G cannot happen; the plan's open questions (Q1 to Q3) stay open. RSA-3072 exercises the class D6 recommends, so a result is not specific to a weaker setting. Open configuration cannot use the keys as a root of trust, so a leak costs nothing beyond a throwaway. | (a) RSA-2048: smaller and faster, but a pass says less about the intended class. (b) Do not generate keys: keep only the structure checker and synthetic fixtures. No signing evidence is produced, Phase 4 stays at planning. |
| O2 | **Install and run the pinned SPSDK** in a virtual environment outside the repository, offline signing only (A2 covers the download), and commit the version and hash pin file (a dependency pin in the repository). | Approve after the licence points that matter for the owner's use are read: the licences of the binary dependencies. | The download approval is not an approval to run it, and the dependency licences are Not verified. | CST: needs its own licence review and is superseded; not recommended. |
| O3 | **Run offline signing** with the O1 keys and produce signed images outside the repository; commit only the YAML or BD description, the check script and the record. | Approve together with O1 and O2, as one task. | Keys without the run give nothing; the run without keys is impossible. | Do it in separate tasks. |
| O4 | Where the record goes: a new folder `docs/evidence/boot-hab/`. | Approve. | Evidence records may be added within a task and the folder keeps offline and board results apart. | Record in the PR description only. |
| O5 | Running the Block E check script, which creates and deletes ephemeral keys on every run, and a CI step that does so (an edit to a protected workflow). Writing the script is not covered by this row. | **Do not approve now.** | Block D has not run once by hand; running the script or a CI step before that fixes assumptions that are Not verified. | Approve after Block D, with the exact step reviewed. |
| **O6** | **The hardware block H-HAB-1**: serial downloader, three images, no flash write, no fuse write, on the RT1060-EVKB in open configuration. | **Approve later, as its own task**, after Blocks B to D, the probe image and an independent review of the block text, and **accept the departure from the plan's saved-image and recovery-path preconditions for this RAM-only route** (see the paragraph above); the task names the board, the time window and its holder. It may share a session with #309. | Decision 8 allows it only as a separately approved task, and the route's value is unknown until the offline blocks show the images are well formed. | (a) Require the saved NOR image and the recovery path of plan section 9 first, which puts #309 and the recovery note (#459) before H-HAB-1. (b) Approve now in advance: not recommended, the images do not exist. (c) Skip the event read and record the deferral in #296, as #296's acceptance criteria allow ("HAB open-configuration event evidence or the recorded deferral"). |
| O7 | Whether an SDP-layout medium is added to the structure checker (A1) before H-HAB-1. | Approve; it is board-free and small, but only after the pinned tool's documentation fixes the IVT offset. | The checker is the gate that stops a malformed image from being sent to the board. | Skip the structural gate for the three probe images: not recommended. |

**Status of 16 November 2026.** The owner approved, second-hand, one narrow part of O1, O2 and O5: ephemeral per-run throwaway keys in a software test with a pinned SPSDK, in a new workflow, discarded after the test and never committed ([HAB status page](../boot-hab-status.md#software-signing-test-throwaway-keys-no-board) for the quote and the conditions). That is implemented as `tests/build/test_hab_signing.py` and `.github/workflows/hab-signing.yml`. The persistent key set of Block C, signing the real first-stage image, O4, O6 and O7 are still open.

Already decided and not asked again: D1, D4, D5, D6 (section 2). Not asked, not approved: any fuse operation, any production key, closed configuration, a tag.
