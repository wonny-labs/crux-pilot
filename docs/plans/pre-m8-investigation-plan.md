# Pre-M8 boot and update investigation plan

| Field | Value |
| --- | --- |
| Status | Investigation plan for #141. The plan itself authorises nothing. The owner gave approval A0 and, on 1 November 2026, approval A1 (see the [decision record](../boot-update-investigation.md#approvals)). A2 to A5 are not given. |
| Tracks | wonny-labs/crux-pilot#141 (open, no milestone, no comments on 1 November 2026) |
| Related | #14 (microSD storage decision), #22 (XIP software safety gates, closed), #34 (M7 MVP), #142 (PSA Crypto, unscheduled) |
| Public baseline read | crux-pilot `main` at `bb7f425` |
| Revision | 1 November 2026: RT1170-EVK content removed after the owner decision to defer RT1170-EVK. Removed items keep their numbers as placeholders so that #141 numbering and later stages stay valid. |
| Public copy | Published with the decision record. Corrections found during the investigation are recorded in the [decision record](../boot-update-investigation.md#corrections-to-the-plan), not edited into this plan. |
| Pinned upstream | Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0). MCUboot `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2`, which matches the `mcuboot` entry in the pinned Zephyr `west.yml` (path `bootloader/mcuboot`, group `bootloader`). |

## 1. Scope

### 1.1 Restated from #141

The investigation is build-only. RT1060-EVKB Rev B is the only target. RT1170-EVK is out of scope. It is deferred by owner decision (1 November 2026). Porting Crux to another board is a candidate for a later milestone.

1. Evaluate MCUboot against the adoption criteria. These are sysbuild integration, modes on XIP NOR (swap, overwrite-only, direct-XIP), compatibility with SoC XIP encryption, validation on every boot, RAM and Flash cost, and, per mode, test, confirm, recovery and older-image rejection with its comparison basis.
2. Select a candidate update mode and staging medium, including whether microSD staging avoids application writes to XIP NOR.
3. Analyse Flash writes during XIP execution for the candidate mode.
4. Re-verify the current RT1060-EVKB image placement and propose a fenced layout.
5. (Removed on 1 November 2026. RT1170-EVK is deferred.)
6. Determine whether NXP HAB can be evaluated in open configuration without fuse writes.
7. Produce a reproducible cross-build of the bootloader and a signed reference image, with development keys outside the repository.

### 1.2 Out of scope

- Any hardware programming, reset, erase, debugger download or fuse operation. This includes reading HAB events from a board.
- Production keys, private security configuration or key material in the repository.
- Implementation of update, image-state, recovery or downgrade-prevention features.
- Bootloader customisation, such as a microSD flash-area backend. The plan may describe the need but must not implement it.
- Changes to upstream Zephyr, MCUboot or `hal_nxp` sources.
- Adoption of a manifest change, a Flash layout or a bootloader. The investigation recommends. Adoption is a later decision.
- HAB closed-configuration enforcement testing. It needs fuse programming on a dedicated board and a separate approved procedure. It is later work and not an M8 exit criterion. The record only states what it would require.
- Development of a provisioning tool, production key management or manufacturing steps. NXP tools are evaluated instead.
- PSA Crypto qualification (#142). The bootloader uses its own cryptographic configuration.
- Any change to separately maintained planning materials. The public decision record must be understandable without them.

### 1.3 Approvals required

#141 states that it is a planning item. It does not authorise development, hardware operations or merging. The owner must give the following approvals. Each one covers only the stages listed.

| ID | Approval | Covers | Notes |
| --- | --- | --- | --- |
| A0 | Start the #141 work package within the boundaries in section 1.2 | Stages 0 to 4 (read-only analysis and builds of existing compositions with the current manifest), branch and Draft PR creation | AGENTS.md allows Draft PR creation within an approved work package. |
| A1 | Fetch MCUboot at the pinned revision outside the repository, install imgtool Python dependencies in a separate virtual environment, and generate development signing keys outside the repository | Stages 5, 6 and 8 | No change to `west.yml` or `.devcontainer/`. See section 3. |
| A2 | Licence review before any NXP signing tool is downloaded or installed (CST or SPSDK) | Optional part of Stage 3 | Only if the owner wants a HAB-signing tool check beyond documentation. Default is documentation-only. |
| A3 | Gate 1: accept or reject the decision record recommendation | After Stage 9 review | Material technical decision. A review PASS is not this approval. |
| A4 | Gate 2: merge the Draft PR | After A3 | Green CI and review PASS are not merge approval. |
| A5 | Any manifest, module or Dev Container change | Not part of this plan | A separate proposal, probably at M8 start. |

### 1.4 Pilot scope and paths kept open

The investigation evaluates the RT1060-EVKB Rev B pilot board with Zephyr-default components. Many controller designs execute the application from external SDRAM loaded from block storage, boot from eMMC or use a different TLS and cryptography stack. The pilot does not validate these paths.

The investigation must not choose a design that rules them out. Where a close analogue exists on the evaluation kit at low cost, the record evaluates it and states its limits. Examples are MCUboot RAM load as the analogue of SDRAM execution, and microSD as the analogue of eMMC, because both use the uSDHC controller as non-XIP block media.

## 2. Staged work breakdown

The order puts cheap, decisive questions first. Stages 1 to 3 are desk analysis on pinned sources and can stop the investigation before any new dependency is fetched. Most Stage 1 facts were already checked while drafting this plan and are recorded in Appendix A. Stage 1 then becomes a short independent re-check rather than new work.

### Evidence locations

Follow the existing `docs/` and `docs/evidence/<topic>/` conventions.

| Artefact | Location |
| --- | --- |
| Decision record | `docs/boot-update-investigation.md` |
| Pinned-source findings with file and line citations | `docs/evidence/boot-update/pinned-source-findings.md` |
| Current placement re-verification | `docs/evidence/boot-update/placement-rt1060.json` |
| Trial build inputs (scratch manifest or module path, Kconfig fragments, overlays) | `docs/evidence/boot-update/inputs/` |
| Build provenance for each image | `docs/evidence/boot-update/provenance-<image>.json` |
| Signed-image and key identity, `imgtool verify` output | `docs/evidence/boot-update/signing.txt` |
| Development public key (only if the owner agrees to commit it) | `docs/evidence/boot-update/dev-public-key.pem` |
| Reproducibility comparison | `docs/evidence/boot-update/reproducibility.txt` |
| Independent review record | `docs/evidence/boot-update/review-record.md` |

### Stage 0: Baseline and boundaries

| Item | Content |
| --- | --- |
| Inputs | Approval A0, #141, AGENTS.md, CONTRIBUTING.md, current `main`. |
| Steps | Record the crux-pilot revision, `west.yml` hash and the Zephyr and MCUboot pins. Confirm Git status is clean. Confirm that no open PR already covers this work (PR #63 and #70 are historical M4 ROM experiments and are not reused). |
| Evidence | Header table of the decision record. |
| Verification | `git rev-parse HEAD`, `sha256sum west.yml`, pinned Zephyr `west.yml` lines 337 to 341. |
| Stop condition | A0 not given, or the working tree contains unrelated uncommitted changes that cannot be preserved. |

### Stage 1: MCUboot capability check on pinned source (scope item 1, desk part)

| Item | Content |
| --- | --- |
| Inputs | Pinned MCUboot `boot/zephyr/Kconfig`, `boot/zephyr/sample.yaml`, `boot/zephyr/CMakeLists.txt`, `boot/bootutil/src/loader.c`, `bootutil_loader.c`, `bootutil_public.c`, `image_validate.c`. Pinned Zephyr `share/sysbuild/images/bootloader/Kconfig`, `cmake/mcuboot.cmake`, `soc/nxp/imxrt/Kconfig`. |
| Steps | Re-check each Appendix A row against the pinned file and line. Answer: Is sysbuild MCUboot available for RT1060-EVKB? Which modes exist? Is validation on every boot the default? Which crypto backend avoids new Zephyr crypto modules? What is the older-image comparison basis in each mode? What does the bootloader provide for SoC XIP encryption? |
| Evidence | `pinned-source-findings.md`, one row per claim, labelled with an evidence status from section 5.2. |
| Verification | A second agent (Haiku) re-reads each cited line and confirms or rejects the row. |
| Stop condition | Escalate if RT1060-EVKB is absent from MCUboot's Zephyr support at the pin, or if every signature option needs a Zephyr crypto module. Both would change the dependency question and overlap #142. |

### Stage 2: Update mode and staging medium fit (scope items 1 and 2)

| Item | Content |
| --- | --- |
| Inputs | Stage 1 findings, #14 decision, `docs/flash-xip-evidence.md`, `docs/platform-m1-readiness.md`. |
| Steps | Build one matrix of modes (swap using offset, swap using move, swap using scratch, overwrite-only, direct-XIP, direct-XIP with revert, and RAM load as a full candidate because it matches SDRAM execution) against: who writes NOR and when, how test and confirm work, recovery from an unconfirmed or failed image, older-image rejection and its basis, slot sizing, compatibility with address-bound XIP encryption. Then assess three microSD staging variants: (a) application copies from microSD into the secondary NOR slot, (b) bootloader reads the image from microSD, (c) no microSD staging. Record which variants need bootloader customisation. For RAM load, record how test, confirm, recovery and older-image rejection work, where the image is copied, and how much of the XIP write analysis still applies to the bootloader. Record whether the evaluation kit SDRAM size allows RAM load of a representative image. |
| Evidence | Mode matrix and staging assessment in the decision record. |
| Verification | Each matrix cell cites a Stage 1 row or is marked "Not verified". |
| Stop condition | Escalate if no stock mode avoids both application and bootloader NOR writes during XIP while still providing test, confirm and recovery. This is the expected result (see section 6) and becomes a recorded constraint for the owner, not a reason to design a custom bootloader. |

Working expectation, to be confirmed in this stage: image-state writes go to the slot trailers on NOR. `boot_set_pending` writes the secondary slot and `boot_set_confirmed` writes the primary slot (verified on pinned source, `bootutil_public.c` lines 690 to 765). Stock microSD staging therefore cannot remove all application writes to the XIP NOR in any mode with test and confirm.

### Stage 3: HAB, XIP encryption and tooling constraints (scope item 6 and part of item 1)

| Item | Content |
| --- | --- |
| Inputs | Publicly accessible NXP documentation (RT1060 reference manual, application notes on HAB and BEE), Zephyr board documentation, pinned `hal_nxp` and Zephyr SoC sources, the Zephyr `scripts/requirements-extras.txt` at the pin (lists `spsdk >= 3.0.0`, lines 21 to 22. It belongs to Zephyr and the crux-pilot bootstrap does not install it), MCUXpresso Secure Provisioning Tool (SEC) release notes, SPSDK and MCUboot `imgtool` documentation. |
| Steps | Record what open-configuration HAB does with an unprogrammed SRK hash. Record whether HAB event logs can be read without fuse writes and what they could show. Record what can be verified on RT1060 without writing the SRK hash fuses, and what cannot. Vendor answers for other i.MX RT parts are inputs only and not RT1060 results. Record which tool can produce a HAB-signed bootloader image and its licence and access terms (NXP CST, SPSDK and SEC). Evaluate the signing toolchain by role: SEC as a GUI for an initial check, SPSDK as the scripted command-line path, and `imgtool` for application images. For each, record the supported version for RT106x, Python and other dependencies, and a command-line workflow that can be reproduced without a GUI. SEC builds on SPSDK, so they are assessed as complementary, not as competitors. GUI output is never used as reproducibility evidence. Record the key source for RT1060 BEE and whether it needs fuse provisioning. Record what MCUboot slot movement means for address-bound encryption. |
| Evidence | HAB and XIP encryption sections of the decision record, each claim with source and status. |
| Verification | Every claim links to an accessible public document or a pinned source line. Documents behind an NXP account are cited by title only and marked "Not verified". |
| Stop condition | Do not download or accept the licence of an NXP tool without A2. If the needed security reference manual is not accessible, record the question as "Not verified" with the reason and continue. |

### Stage 4: Re-verify current RT1060-EVKB placement (scope item 4, first part)

| Item | Content |
| --- | --- |
| Inputs | Existing EVKB compositions built by current scripts (for example `scripts/test-controller.sh`, `scripts/test-controller-integration.sh`, `scripts/test-m5-integrated-smoke.sh`), their CI artefacts, `scripts/record-build-provenance.py`. |
| Steps | First read retained CI artefacts (map, `.config`, DTS) for the latest `main` run. Rebuild in the Dev Container only if artefacts are missing or expired. For each composition record `CONFIG_FLASH_LOAD_OFFSET`, `CONFIG_USE_DT_CODE_PARTITION`, `CONFIG_BOOTLOADER_MCUBOOT`, `CONFIG_NXP_IMXRT_BOOT_HEADER`, the FCB, IVT, boot data and DCD addresses, the highest Flash load address and load segments from `readelf -lW`. Compare with the upstream partitions (`mcuboot` 0x000000, `image-0` 0x020000, `image-1` 0x320000, `storage` 0x620000 to 0x800000). |
| Evidence | `placement-rt1060.json` and a summary table in the decision record. |
| Verification | Values match the ELF and `.config` hashes recorded in provenance. |
| Stop condition | None expected. If a composition already exceeds 128 KiB, record that it would overlap `image-0` in a bootloader layout. |

Gate after Stage 4: report the Stage 1 to 4 findings to the owner with the candidate mode for the trial build. Continue to Stage 5 only with A1.

### Stage 5: Trial bootloader and signed image cross-build (scope items 1 cost and 7)

| Item | Content |
| --- | --- |
| Inputs | A1, Stage 2 candidate mode, MCUboot at the pinned revision, development key outside the repository (for example under `/opt/crux-cache/keys/`), a minimal existing sample (the controller sample or Zephyr `hello_world`). |
| Steps | Use the evaluation route in section 3.3. Build with sysbuild for `mimxrt1060_evk@B/mimxrt1062/qspi`, with `SB_CONFIG_BOOTLOADER_MCUBOOT=y`, the candidate `SB_CONFIG_MCUBOOT_MODE_*`, `SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y` and `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` set to the external key. Never use the default sample keys in the MCUboot tree. Record the MCUboot and application Flash and RAM use from the linker. Optionally build a second mode if Stage 2 leaves two candidates. |
| Evidence | `inputs/`, `provenance-mcuboot.json`, `provenance-app.json`, signed artefact hashes in `signing.txt`. |
| Verification | MCUboot `.config` contains the expected mode, `CONFIG_BOOT_VALIDATE_SLOT0=y` and the tinycrypt backend. The application `.config` has `CONFIG_BOOTLOADER_MCUBOOT=y` and no NXP boot header. The bootloader image has the boot header at 0x60000000. |
| Stop condition | Stop and escalate if the build needs an upstream source patch, a Zephyr crypto module, or a key inside the repository. Stop if the Dev Container runs out of memory or disk and record the limit rather than switching to an undocumented environment silently. |

### Stage 6: Fenced layout proposal and XIP write analysis (scope items 3 and 4)

| Item | Content |
| --- | --- |
| Inputs | Stage 4 placement, Stage 5 ELF files, `docs/flash-xip-evidence.md` phase matrix. |
| Steps | Propose a fenced layout for the candidate mode, including bootloader size headroom, slot sizes, any swap-offset sector requirement, trailer space and a storage region decision (kept, removed or moved to microSD). List every NOR write in the candidate mode by actor (application, bootloader) and phase (download, pending, swap or copy, confirm, revert). For each bootloader write, record whether MCUboot executes from the same NOR and which FlexSPI driver path is linked, using the same phase headings as the #22 matrix. Record the effect of the proposed layout on current RT1060-EVKB images: the changed application link address, programming and debug scripts, and target evidence that M6 and M7 rely on. Recommend when the reference layout should change relative to M7 target validation, so that evidence is not invalidated during a milestone. |
| Evidence | Layout table and write inventory in the decision record. |
| Verification | Layout offsets checked against DTS and the built partition map. Write inventory checked against Stage 1 citations and the MCUboot ELF. |
| Stop condition | Do not claim closure. Mark bootloader write-path closure "Not verified" unless the ELF analysis covers it fully, which is not expected in this investigation. |

### Stage 7: (removed, RT1170 deferred)

Removed on 1 November 2026 together with #141 scope item 5. The stage number is kept so that Stages 8 and 9 and the tables keep their numbers.

### Stage 8: Reproducibility and signature verification (scope item 7, acceptance)

| Item | Content |
| --- | --- |
| Inputs | Stage 5 builds. |
| Steps | Rebuild the same commit from a clean build directory and compare ELF and unsigned binary hashes. Signed images are not expected to be byte-identical when the signature scheme is randomised, such as ECDSA P-256. For signed images, compare the image header and payload and verify the signature instead. Record the signature algorithm and whether it is deterministic. Explain any difference (for example the embedded `git rev-parse` revision found in the RT1170 CI comparison). Run `imgtool verify --key <public key>` on the signed image. Record the SHA-256 of the public key and confirm the bootloader embeds the matching key. Use two kinds of development key. A temporary key generated for each CI run is acceptable for build and signing tests, but the bootloader must be rebuilt with its public key in the same run. A fixed development key kept outside the repository is used for any later board test, and its public key may be stored in the repository. Production signing is out of scope. Confirm that no private key is in the repository, the build directory under the repository, the logs or the evidence files. |
| Evidence | `reproducibility.txt`, `signing.txt`. |
| Verification | Hash comparison output and `imgtool verify` exit status. `git grep` for PEM private-key headers returns nothing. |
| Stop condition | If hashes differ for reasons other than a documented embedded revision, record "not reproducible" and stop the claim. Do not adjust the build to hide differences. |

### Stage 9: Decision record, Draft PR and independent review

| Item | Content |
| --- | --- |
| Inputs | All earlier evidence. |
| Steps | Write `docs/boot-update-investigation.md` using section 5. Run `python3 scripts/check_docs.py`. Open a Draft PR as in section 7. Request independent review. Address REVISE findings and request re-review. |
| Evidence | Decision record, `review-record.md`. |
| Verification | Docs check passes. Review records PASS, REVISE or ESCALATE with reasons. |
| Stop condition | ESCALATE or material disagreement goes to the owner. No merge without A4. |

### Mapping of scope items to stages

| #141 scope item | Stages |
| --- | --- |
| 1 Adoption criteria | 1, 2, 3 (XIP encryption), 5 (cost) |
| 2 Mode and staging medium | 2 |
| 3 XIP write analysis | 6 |
| 4 Placement and fenced layout | 4, 6 |
| 5 (removed) | None |
| 6 HAB open configuration | 3 |
| 7 Reproducible signed build | 5, 8 |

## 3. Dependency and manifest handling

### 3.1 Current state (verified on pinned source)

The crux-pilot `west.yml` imports Zephyr `684c9e8` with the allowlist `cmsis`, `cmsis_6`, `hal_nxp`, `littlefs`, `fatfs`. It has no `mcuboot`, `mbedtls`, `tf-psa-crypto` or `zcbor`. The pinned Zephyr `west.yml` provides `mcuboot` at `ee39e2d` (lines 337 to 341).

### 3.2 What the investigation needs

| Need | Why | Change if adopted later | Approval |
| --- | --- | --- | --- |
| MCUboot source at `ee39e2d` | Sysbuild needs `ZEPHYR_MCUBOOT_MODULE_DIR`. Signing needs `scripts/imgtool.py` (Zephyr `cmake/mcuboot.cmake` stops with an error when imgtool is not found). | Add `mcuboot` to the `name-allowlist`. | A1 for trial. A5 for adoption. |
| Git safe directory for the new checkout | The Dev Container lists each module path in `postCreateCommand`. The `cmsis_6` addition needed the same step (`docs/baseline.md`). | Add `/workspaces/crux/bootloader/mcuboot` to `.devcontainer/devcontainer.json`. | A5. |
| imgtool Python dependencies | MCUboot `scripts/requirements.txt` lists `cryptography>=40.0.0`, `intelhex`, `click`, `cbor2`, `setuptools`, `pyyaml` and one more entry (seven in total, to be listed in the record). Only `intelhex` and PyYAML are in Zephyr `requirements-base.txt`, which `scripts/bootstrap-zephyr.sh` installs. | Pinned additions to the bootstrap, or a documented separate step. | A1 for a separate trial venv. A5 for adoption. |
| No Zephyr crypto module | ECDSA P-256 with the tinycrypt backend uses MCUboot's own `ext/tinycrypt` and `ext/mbedtls-asn1` (Kconfig lines 308 to 321, CMake lines 25 to 36 and 168 to 188). The default RSA type selects `MBEDTLS` and would need `mbedtls` or `tf-psa-crypto`. | None, if ECDSA P-256 with tinycrypt is kept. | Escalate if another type is required, because it overlaps #142. |
| CI evidence layout review | `docs/evidence/ci-efficiency/` records that the evidence collection assumes single-image `build/zephyr/<name>` directories and that sysbuild needs a review. | Only if a CI job builds sysbuild images. | Not part of this plan. |

Licences checked on the pinned source: MCUboot is Apache-2.0. The bundled TinyCrypt carries Intel's BSD-style licence (`ext/tinycrypt/LICENSE`). The licence of `ext/mbedtls-asn1` is not verified yet and Stage 1 must check it. TinyCrypt maintenance status upstream is not verified.

### 3.3 Evaluating without changing the manifest

Preferred route, cheapest first. Both need A1 and leave `west.yml` and `.devcontainer/` unchanged.

1. **External module path.** Clone MCUboot at `ee39e2d` outside the repository (for example `/opt/crux-cache/trial/mcuboot`) and pass it with `-DEXTRA_ZEPHYR_MODULES=<path>` alongside Crux. Whether sysbuild discovers MCUboot this way is not verified. Stage 5 checks it with a configure-only run before any full build.
2. **Scratch west workspace.** If route 1 fails, create a separate workspace with a scratch manifest that is the repository `west.yml` plus `- mcuboot`. Store a copy and its SHA-256 in `docs/evidence/boot-update/inputs/`. This costs a second Zephyr and `hal_nxp` checkout, so check disk space first. Do not reuse another existing workspace unless the owner confirms it.

The investigation PR does not change `west.yml`. Reproducibility is from the documented trial inputs. Adoption of the manifest change is an M8 decision (A5).

## 4. Execution environment

| Work | Where |
| --- | --- |
| Stages 0 to 3, Stage 6 layout reasoning | Read-only. Any host with Git and HTTPS access to pinned sources. No build tools needed. |
| Stage 4 rebuilds (if CI artefacts are missing) | Documented Linux Dev Container, existing scripts and workspace. |
| Stages 5 and 8 | Documented Linux Dev Container with the A1 additions in a separate venv (for example `/opt/crux-cache/venv-boot-trial`) so the baseline venv is unchanged. |

Constraints to expect:

- The Dev Container is configured for 2 CPUs and 4 GiB. A sysbuild run builds MCUboot and the application in sequence. Memory pressure is likely modest but is not verified. Use the default job count and record peak memory if a build fails.
- A scratch workspace (route 2) roughly doubles the Zephyr and `hal_nxp` disk use. Check free space in `/opt/crux-cache` first.
- `scripts/record-build-provenance.py` expects `<build>/zephyr/zephyr.elf` and does not record signed outputs. Run it once per sysbuild image directory and record signed-image hashes separately in `signing.txt`. Its suitability for sysbuild subdirectories is not verified. No new tooling is planned.
- Results from any environment other than the documented Dev Container are labelled as such and are not substituted for Dev Container evidence.
- No `west flash`, `west debug`, pyOCD, LinkServer or J-Link command is run in any stage.

## 5. Decision record outline

### 5.1 Questions the record must answer

1. **Bootloader fit.** Does MCUboot at the pin meet each adoption criterion for RT1060-EVKB Rev B? Each criterion gets a result and an evidence status.
2. **Update mode.** Which mode is recommended, and why are the others rejected?
3. **Staging medium.** NOR secondary slot or microSD? Which NOR writes remain in each case? Is bootloader customisation needed for microSD?
4. **Comparison basis for older images.** Per mode: version triple `major.minor.revision`, optional build number, or security counter. How are equal values treated? Where in the path is the check made?
5. **Test, confirm and recovery.** Per mode: who writes what, where, and what happens after an unconfirmed or failed image.
6. **XIP write analysis.** Every NOR write in the candidate mode, by actor and phase, with the open closure items.
7. **Layout.** Current placement findings and the proposed fenced layout.
8. **Trust level achievable without fuse programming.** Expected answer: development-key image verification by the bootloader. The boot ROM does not authenticate the bootloader. To be confirmed.
9. **HAB open configuration.** Is an evaluation possible without fuse writes? What could it show and what could it not show? Which tool and licence terms apply? What can be checked on RT1060 without SRK hash fuse writes? HAB closed-configuration enforcement is recorded as later work that needs a dedicated board and a separate approval.
10. **SoC XIP encryption.** Compatibility of the candidate mode with BEE (RT1060), and the key provisioning such encryption needs.
11. (Removed on 1 November 2026. RT1170-EVK is deferred.)
12. **Cost.** Static Flash and RAM of the bootloader and the slot overhead. Boot time and runtime stack are not measured.
13. **Dependencies.** The exact manifest, Dev Container and Python changes adoption would need.
14. **Open items.** What M8 must verify physically.
15. **Signing toolchain and keys.** Roles and versions of SEC, SPSDK and `imgtool`, the reproducible command-line workflow, the signature algorithm, the reproducibility criterion (unsigned images reproduce byte for byte, signed images verify against the documented public key) and the split between temporary CI keys and fixed development keys.
16. **Layout change timing.** The effect of the proposed layout on current images and M6 and M7 evidence, and a recommended point for changing the reference layout.
17. **Execution model.** XIP from QSPI NOR or RAM load into SDRAM as the reference pilot configuration for M8, with the effect on M6 and M7 evidence and on the SDRAM initialisation path (boot ROM DCD or bootloader).
18. **Block-media boot analogue.** Whether the RT1060 boot ROM can boot the bootloader from microSD on the evaluation kit without fuse writes, for example through boot mode switches, as an analogue of eMMC boot. Desk study only.

### 5.2 Evidence status labels

| Label | Meaning |
| --- | --- |
| Verified on pinned source | Checked against a cited file and line at the pinned Zephyr, MCUboot or `hal_nxp` revision. Shows what the code allows, not what it does on a board. |
| Verified by build | Observed in a recorded build output (`.config`, map, ELF, signed image) with provenance. Shows that it configures and links, not that it boots. |
| Not verified | Not checked, inaccessible, or from vendor documentation without source or build confirmation. Gives the reason. |

No claim in the record uses "verified on hardware". This investigation produces none.

## 6. Risks, unknowns and escalation

### 6.1 Known points from pinned source

| Point | Evidence |
| --- | --- |
| MCUboot `docs/design.md` (lines 1407 to 1413) says software downgrade prevention is only available with overwrite-only. This is out of date. The Kconfig and loader also support it in swap modes. | `boot/zephyr/Kconfig` lines 1133 to 1162. `loader.c` lines 1672 to 1711 (`check_downgrade_prevention`) and 1838 to 1845 (called for test and permanent swaps, not for revert). Overwrite-only path at `loader.c` lines 615 to 632. |
| Software downgrade prevention is unavailable in direct-XIP. | `MCUBOOT_DOWNGRADE_PREVENTION` `depends on !BOOT_DIRECT_XIP` (Kconfig line 1135). Direct-XIP boots the highest valid version (`loader.c` lines 2118 to 2140). An older image is not selected while a newer valid one exists, but nothing rejects it when written. |
| Equal security counters are accepted. | `image_validate.c` lines 465 to 466 reject only a lower counter. Kconfig help at lines 1143 to 1149 and 1153 to 1162 states it. |
| Equal versions are also accepted by software downgrade prevention. The Kconfig help ("greater major or greater minor") is stricter than the code. | `boot_compare_version` in `bootutil_loader.c` lines 200 to 245 returns 0 for equal versions, and the callers reject only a negative result. The revision field is compared. The build number is compared only with `BOOT_VERSION_CMP_USE_BUILD_NUMBER`. |
| Hardware downgrade prevention needs a platform security counter. No Zephyr implementation exists in the MCUboot tree. | No `boot_nv_security_counter_get` under `boot/zephyr`. A trusted counter on RT1060 would need fuses, which are out of scope. |
| microSD staging likely needs bootloader customisation. | MCUboot reads slots through the flash-area API. An SD card is a disk device. No disk-backed flash-area backend was found at the pin (not verified exhaustively). |
| Application test and confirm write NOR in every mode with test and confirm. | `bootutil_public.c` lines 690 to 765. |
| MCUboot image encryption is unavailable with direct-XIP in sysbuild. This is separate from SoC XIP encryption. | Zephyr `share/sysbuild/images/bootloader/Kconfig` line 208. |
| The default signing keys are public sample keys inside the MCUboot tree. | Sysbuild Kconfig `BOOT_SIGNATURE_KEY_FILE` defaults to `root-*.pem` in the module. |

### 6.2 Main unknowns

- **Bootloader writes during its own XIP execution.** In swap and overwrite modes MCUboot writes the QSPI NOR it executes from. The #22 phase matrix found open closure items in the FlexSPI NOR driver XIP path. These apply to the bootloader as well. Not verified.
- **SoC XIP encryption.** Neither MCUboot nor the pinned imxrt SoC Kconfig mentions BEE. Address-bound encryption is likely incompatible with swap and move unless images are re-encrypted per slot. Keys likely need fuse provisioning. All not verified.
- **HAB open configuration.** With unprogrammed SRK hash fuses, open-configuration HAB probably reports authentication events but cannot show a successful authentication. Reading events needs a board. The security reference manual and CST may need an NXP account. Not verified.
- **SDRAM and DCD.** The application built for MCUboot has no NXP boot header. If it needs SDRAM, something must initialise SEMC first. Not verified.

### 6.3 Stop and escalate

Stop the affected stage and report to the owner if any of these occurs:

- A step would need hardware access, a fuse operation or a debugger command.
- A build needs an upstream source patch, a Zephyr crypto module or a manifest change in the repository.
- A private key would enter the repository, a log or an evidence file.
- An NXP tool needs a licence acceptance or account without A2.
- The results show that a #141 acceptance criterion cannot be met as written. Record the reason and propose a correction. Do not reinterpret the criterion silently.
- The trial build cannot run in the documented Dev Container.

## 7. Review plan

- **Gate.** Independent review applies under AGENTS.md because the decision record supports a material technical decision (bootloader, update mode, layout). The reviewer must not have authored the record or the evidence. The reviewer re-checks pinned-source citations, build provenance, signing verification and the claim boundaries independently, and records PASS, REVISE or ESCALATE with reasons. REVISE needs correction and re-review. A PASS satisfies only the review gate. Gate 1 (A3) and Gate 2 (A4) remain with the owner.
- **Draft PR content.** Branch from current `main`, for example `docs/boot-update-investigation`. Title: `docs: record boot and update feasibility for RT1060-EVKB`. Files: `docs/boot-update-investigation.md`, `docs/evidence/boot-update/*`, and at most a one-line cross-reference from the ROM section of `docs/platform-m1-readiness.md`. The PR body states what changed, why, the tested revision, the commands and environment, tests not run (all hardware tests), and the approvals still needed. No change to `west.yml`, `.devcontainer/`, scripts, CI or source.
- **The PR must not claim** that any image boots, that a layout is safe or adopted, that MCUboot is adopted, that secure boot, hardware root of trust or anti-rollback is achieved, that HAB authentication was observed, that microSD staging works, that Flash writes during XIP are safe, that the build is reproducible beyond the tested comparison, or that any M8 or M9 criterion is met. It must not include content from separately maintained planning materials, and it must not close #141 without A3 and the acceptance evidence.

## 8. Model and effort per stage

| Stage | Model | Effort | Reason |
| --- | --- | --- | --- |
| 0 Baseline | Haiku | Low | Read-only verification |
| 1 Pinned-source check | Haiku re-check, Opus write-up | Low, then moderate | Most facts already gathered. Re-verification is mechanical. |
| 2 Mode and staging fit | Opus | High | Design trade-offs with safety consequences |
| 3 HAB, encryption, tooling | Opus | Moderate | Interpretation of vendor material and licences |
| 4 Placement re-verification | Haiku (artefact reading), Sonnet if rebuilds are needed | Low | Mechanical extraction |
| 5 Trial build | Sonnet | Moderate | Executing approved build steps |
| 6 Layout and XIP write analysis | Opus | High | Cross-component reasoning on hazards |
| 7 (removed) | None | None | Stage removed, RT1170 deferred |
| 8 Reproducibility and signing | Sonnet | Low to moderate | Executing approved checks |
| 9 Decision record | Opus | Moderate to high | Decision record |
| 9 Independent review | Opus, separate context, not the author | High | Required independent review |

## 9. Issues with the current scope

| # | Issue in #141 | Suggested minimal correction |
| --- | --- | --- |
| 1 | Scope item 7 needs MCUboot and imgtool dependencies before any change is approved, but the acceptance criterion only says changes must be approved "before adoption". It is unclear whether a trial fetch outside the manifest is allowed. | State that a trial fetch outside the repository needs explicit approval (A1) and that adoption into `west.yml` is a separate decision. |
| 2 | Scope item 2 asks whether microSD staging avoids application writes to XIP NOR. On pinned source, test and confirm write slot trailers on NOR in every mode that supports them. With bootloader-read staging, the writes move to the bootloader, which also executes from the same NOR. | Reword to "identify which NOR writes remain, and by which actor, for each staging option". |
| 3 | Scope item 6 cannot be fully answered build-only. Reading HAB events needs a board. With blank SRK hash fuses an open-configuration evaluation likely cannot show successful authentication. Key NXP documents and CST may need an account. | Reword to "determine from accessible documentation whether a non-fuse evaluation path exists, and what it could and could not show". Physical event readback is a later, separately approved step. |
| 4 | "Compatibility with SoC XIP encryption" is ambiguous. It can mean BEE, or MCUboot image encryption, which is a different feature and is unavailable with direct-XIP. SoC XIP encryption also likely needs fuse-provisioned keys. | Name BEE (RT1060) explicitly and accept a source and documentation analysis as the build-only result. |
| 5 | The mode list omits RAM load. On the EVKB it may move application execution off the NOR during application writes. | Add RAM load to the desk comparison only. No extra build unless it becomes the candidate. |
| 6 | Scope item 1 asks for older-image rejection in each mode. In direct-XIP, MCUboot software downgrade prevention is unavailable, so rejection would have to come from the update path in the application. | Allow the record to state "not provided by the bootloader in this mode" as a valid result. |
| 7 | "RAM and Flash cost" cannot include runtime stack, heap or boot time in a build-only investigation. | Limit to static linker figures and slot overhead. |
| 8 | "Reproducible" is not defined. The RT1170 record shows that the embedded `git rev-parse` revision changes hashes between commits. | Define it as identical hashes from two clean builds of the same commit and inputs, with any embedded revision explained. |
| 9 | "Current compositions" in scope item 4 is not named. | Name the EVKB images that current scripts and CI build, or the controller and integrated-smoke compositions as a minimum. |
| 10 | The acceptance criterion asks for verification "against the documented public verification key" but does not say where the key is documented. | Allow the development public key, or at least its SHA-256, to be committed under `docs/evidence/boot-update/`, marked development-only. |

## Appendix A: Facts checked while drafting

All rows are "Verified on pinned source" unless marked otherwise. Stage 1 re-checks them.

| Fact | Source |
| --- | --- |
| MCUboot at `ee39e2d` is the Zephyr-pinned revision | Zephyr `west.yml` lines 337 to 341 |
| MCUboot Zephyr sample targets include `mimxrt1060_evk/mimxrt1062/qspi` | MCUboot `boot/zephyr/sample.yaml` line 26. A listing is not a passing build (Not verified by build). |
| Sysbuild offers single app, swap using offset, move and scratch, overwrite, direct-XIP, direct-XIP with revert, RAM load, firmware updater | Zephyr `share/sysbuild/images/bootloader/Kconfig` lines 41 to 139 |
| Sysbuild default mode is swap using offset. Default signature is RSA. | Same file, lines 37 and 181 |
| Validation of the primary slot on every boot is on by default | MCUboot Kconfig line 539 (`BOOT_VALIDATE_SLOT0`, default y). Direct-XIP validates the active slot at `loader.c` line 2320. |
| ECDSA P-256 defaults to tinycrypt bundled in MCUboot | MCUboot Kconfig lines 308 to 321, `boot/zephyr/CMakeLists.txt` lines 25 to 36 and 168 to 188 |
| An application built for MCUboot omits the NXP boot header | Zephyr `soc/nxp/imxrt/Kconfig` line 27 |
| No BEE reference in MCUboot or the imxrt SoC Kconfig | Search of the pinned MCUboot tree and `soc/nxp/imxrt/Kconfig`, `soc/nxp/imxrt/imxrt10xx/Kconfig` |
| RT1060 QSPI upstream partitions: `mcuboot` 128 KiB, `image-0` and `image-1` 3 MiB each, `storage` 1,920 KiB, 4 KiB erase blocks | Zephyr `boards/nxp/mimxrt1060_evk/mimxrt1060_evk_mimxrt1062_qspi.dts` lines 44 to 72 |
| imgtool is required for signing | Zephyr `cmake/mcuboot.cmake` lines 83 to 85 |
