# Boot and update investigation evidence (#141)

These files are the stage records behind the
[boot and update investigation record](../../boot-update-investigation.md).
They are **desk results only**. They come from reading pinned upstream source,
public vendor documents and committed crux-pilot records. No build was run for
these files, no hardware was used and no fuse was touched. Stages 5, 6 and 8
(trial build, layout and write analysis, reproducibility and signing) will add
their own files here.

The files were collected on 1 November 2026. They are frozen records. Notes
added when the record was assembled are marked as corrections, and the original
rows are kept. Where a later stage disagrees with an earlier one, the later
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

## Evidence labels

The labels are used exactly as each stage recorded them. They are never
upgraded when the record is assembled.

| Label | Used in | Meaning |
| --- | --- | --- |
| Verified on pinned source | Stages 1 to 3 | Checked at a cited file and line of a pinned Zephyr, MCUboot, `hal_nxp` or crux-pilot revision. Shows what the code allows, not what a board does. |
| Verified by public vendor document | Stage 3 | Read from a public vendor page or repository, with URL and date. It is the vendor's statement, not a Crux result. Stage 2 cites some of the same documents as rows V1 to V4 and labels them Not verified. |
| Verified by artefact | Stage 4 | Read from a committed build output, record or map in this repository. |
| Verified on source | Stage 4 | Read from a pinned upstream file or a repository test assertion, not from a built output. |
| Inference | Stages 1 and 2 | Reasoned from verified lines. Not checked directly. |
| Not verified | All | Not checked, not accessible, or inferred. The reason is given. |
| Verified by build | None yet | Reserved for Stage 5 and Stage 8 outputs with provenance. |

No file uses "verified on hardware".

## Sources and access

| Item | Value |
| --- | --- |
| Zephyr | `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0) |
| MCUboot | `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2` (from the pinned Zephyr `west.yml`) |
| `hal_nxp` | `2c2f28ac333e995d7279777409817c4b4f92c1ec` |
| crux-pilot | `f67e22a` for Stages 0, 3 and 4. `f57ff22` for Stage 2, after #147 merged. |
| Access | The GitHub contents API was refused for the upstream repositories. Files were read as raw files at the pinned commits and from read-only blobless Git fetches outside the repository. Local copies are not committed. NXP documents on `www.nxp.com/docs/` could not be read and are cited by title only. |
