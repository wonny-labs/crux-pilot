# Boot and update investigation evidence (#141)

These files are the stage records behind the
[boot and update investigation record](../../boot-update-investigation.md).
Stages 0 to 4 and 7 are **desk results only**. RT1170-EVK Rev A1 is covered by Stage 7 as a desk item. RT1060-EVKB Rev B is the primary target. The desk files come from reading pinned upstream source,
public vendor documents and committed crux-pilot records. No build was run for
them. Stages 5, 6 and 8 record the A1 trial builds of 2 November 2026 for
RT1060-EVKB. They are **cross-compilation (build-only) evidence**: no image was
run. No hardware was used and no fuse was touched for any file here.

The desk files of Stages 0 to 4 were collected on 1 November 2026. The
Stage 5 to 8 files were written on 2 November 2026. They are frozen records. Notes
added when the record was assembled, and after independent review 1 of PR #151,
are marked as corrections. Rows changed after that review say so in the row.
Other original rows are kept. Where a later stage disagrees with an earlier one, the later
stage is preferred. The correction is noted in the earlier file or in the
decision record, or in both.

"Plan" in these files means the
[pre-M8 investigation plan](../../plans/pre-m8-investigation-plan.md).

## Files

| File | Stage | Content |
| --- | --- | --- |
| [stage0.md](stage0.md) | 0 | Baseline: crux-pilot revision, `west.yml` hash, Zephyr and MCUboot pins, open PRs |
| [stage1-pinned-source-findings.md](stage1-pinned-source-findings.md) | 1 | One row per claim, with file and line at the pin. Re-check of plan Appendix A, then rows B1 to B89 and licences L1 to L5 |
| [stage1-answers.md](stage1-answers.md) | 1 | Answers to the Stage 1 questions, built from the rows above |
| [stage2-matrix.md](stage2-matrix.md) | 2 | Mode matrix, microSD staging variants, execution model, network delivery, interaction with #147 |
| [stage2-recommendation.md](stage2-recommendation.md) | 2 | Constraint, ranked candidate modes, staging and execution-model recommendations |
| [stage3-findings.md](stage3-findings.md) | 3 | HAB, signing tools, BEE, boot media and DCD claims with sources |
| [stage3-summary.md](stage3-summary.md) | 3 | Summary of Stage 3 and its owner decisions |
| [stage4.md](stage4.md) | 4 | Re-verification of the current RT1060-EVKB image placement |
| [placement-rt1060.json](placement-rt1060.json) | 4 | Machine-readable placement values and their sources |
| [stage5-trial-builds.md](stage5-trial-builds.md) | 5 | A1 trial builds for RAM load with revert and swap using offset: inputs, commands, sizes, the two RAM load configuration problems and their solutions, FlexSPI window check, linked Flash driver, boot header, trailer and signing arguments, deviations and failure record. Added on 2 November 2026. |
| [stage6-layout-and-write-analysis.md](stage6-layout-and-write-analysis.md) | 6 | Fenced layout proposal (not adopted), NOR write inventory by actor and phase, bootloader write path against the #22 phase matrix, effect on M6 and M7 evidence and the timing recommendation. Added on 2 November 2026. |
| [stage7-rt1170-delta.md](stage7-rt1170-delta.md) | 7 | RT1170-EVK Rev A1 layout and boot differences from RT1060, applicability of the findings, items for #155. Added on 2 November 2026. |
| [stage8-reproducibility-and-signing.md](stage8-reproducibility-and-signing.md) | 8 | Reproducibility of the trial builds, signed-image comparison, `imgtool verify`, public key and embedded key, private-key search, key kinds. Added on 2 November 2026. |
| [trial-inputs/](trial-inputs/) | 5, 8 | Small inputs and text outputs copied from the trial results: Build A Kconfig fragments and overlays, the two build commands, the check outputs, imgtool arguments, `hashes.txt`, `reproducibility.txt` and `signing.txt`. No ELF, binary, map, private key or PEM file. |

## Evidence labels

The labels are used exactly as each stage recorded them. They are never
upgraded when the record is assembled.

| Label | Used in | Meaning |
| --- | --- | --- |
| Verified on pinned source | Stages 1 to 3 | Checked at a cited file and line of a pinned Zephyr, MCUboot, `hal_nxp` or crux-pilot revision. Shows what the code allows, not what a board does. |
| Verified by public vendor document | Stages 3 and 7 | Read from a public vendor page or repository, with URL and date. It is the vendor's statement, not a Crux result. Stage 2 cites some of the same documents as rows V1 to V4 and labels them Not verified. Where a document was read only through a summarising fetch, the label adds "summarising fetch, not a full read" (EVKB switch settings in Stage 3 D3 and D5). |
| Verified by artefact | Stage 4 | Read from a committed build output, record or map in this repository. |
| Verified in repository | Stage 7 | Read from a committed crux-pilot document. Weaker than "Verified by artefact" when the record is not a build output. |
| Verified on source | Stage 4 | Read from a pinned upstream file or a repository test assertion, not from a built output. |
| Inference | Stages 1 to 3 and 5 to 8 | Reasoned from verified lines or build outputs. Not checked directly. |
| Not verified | All | Not checked, not accessible, or inferred. The reason is given. |
| Verified by build | Stages 5, 6 and 8 | Read directly from the A1 trial outputs (`.config`, devicetree, linker output, map, ELF, signed image, `imgtool` output), with the results archive SHA-256 `8e2e9839eae863b79a42a7a294a97e39f093a83c3f48d0598f9fb582f01cd3ed` as provenance. Shows that an image configures and links, not that it boots. Stage 6 also includes a small static disassembly of the archived ELF files made outside the Dev Container, and labels it so. |

No file uses "verified on hardware".

## Sources and access

| Item | Value |
| --- | --- |
| Zephyr | `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0) |
| MCUboot | `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2` (from the pinned Zephyr `west.yml`) |
| `hal_nxp` | `2c2f28ac333e995d7279777409817c4b4f92c1ec` |
| crux-pilot | `f67e22a` for Stages 0, 3 and 4. `f57ff22` for Stage 2, after #147 merged. `47da9e5` (`main`) for Stage 7. `1d6ac4f6cd5b4e4013f2ce0c59375beb3cdad9f2` for the Stage 5 and 8 trial builds (an older ancestor of `f57ff22`). |
| Trial results | Archive `d8f53cd7-results.tar.gz`, SHA-256 `8e2e9839eae863b79a42a7a294a97e39f093a83c3f48d0598f9fb582f01cd3ed`, supplied by the owner. Not committed. Small extracts are in [trial-inputs/](trial-inputs/). Built in a container created from the repository Dev Container image, with MCUboot and `imgtool` 2.4.0 from a pinned clone outside the repository. |
| Access | The GitHub contents API was refused for the upstream repositories. Files were read as raw files at the pinned commits and from read-only blobless Git fetches outside the repository. Local copies are not committed. NXP documents on `www.nxp.com/docs/` could not be read and are cited by title only. |
