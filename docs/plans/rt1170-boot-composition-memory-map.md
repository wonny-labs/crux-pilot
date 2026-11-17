# RT1170-EVK boot composition: memory-map choices (design note)

| Field | Value |
| --- | --- |
| Status | **Starting values approved.** The project owner approved the starting values C1 to C5 and the four choices D1 to D4 as recommended on 15 November 2026. The values are starting values, to be corrected by the first CI build. The RT1170 composition itself is not built or verified, and nothing was run on a board. The files and the check are in a Draft pull request that is not merged. |
| Date | 15 November 2026 |
| Work package | RT1170 build-only evidence of M8 ([#296](https://github.com/wonny-labs/crux-pilot/issues/296)), Phase 1 of the [secure boot development plan](secure-boot-development-plan.md) |
| Builds on | [Boot composition](../boot-composition.md) (RT1060-EVK Rev B, merged), the RT1170 desk study [Stage 7](../evidence/boot-update/stage7-rt1170-delta.md) of the [boot and update investigation](../boot-update-investigation.md) |
| Evidence labels | **Verified by build** (read from the output of a build that exited 0), **Verified on source (v4.4.0)** (read from a file of a Zephyr v4.4.0 tree, it shows what the configuration allows, not what a board does), **Inference**, **Not verified**. No RT1170 statement in this note is Verified by build; the one Verified by build row describes the merged RT1060 build. |

## 1. Question

The RT1060-EVK Rev B composition needs four board-level files: MCUboot configuration and overlay, application configuration and overlay. They fix where MCUboot runs, where the image may be loaded and where the boot information is retained. Which values would the same four files hold for the RT1170-EVK, if the goal is a build-only result (configure, build, sign, `imgtool verify`) as on the RT1060?

This note proposes values and lists what a first CI build has to settle. It does not choose a value for the owner.

## 2. Facts that bound the choice

Read in a local Zephyr tree at v4.4.0, the version the Stage 7 study used (it pins `684c9e8f32e4373a21098559f748f06915f950c9`; the tree read here was identified by its version file only). Paths are relative to that tree. **Not verified at v4.4.2**, the revision Crux pins now; the first build would re-read them.

| Fact | Value | Source (Zephyr tree) | Label |
| --- | --- | --- | --- |
| Board and revision | `mimxrt1170_evk@A//cm7` is a valid target: the board lists revisions A and B, with B the default, and revision B has its own overlay. That the Rev A1 PCB is Zephyr revision A is not shown. | `boards/nxp/mimxrt1170_evk/` | Verified on source (v4.4.0); the PCB mapping is **Not verified** (Stage 7) |
| SDRAM | 64 MiB at `0x80000000` (`sdram0`, Winbond W9825G6KH) | `mimxrt1170_evk_mimxrt1176_cm7.dts` | Verified on source (v4.4.0) |
| Application RAM | `zephyr,sram = &sdram0`: the board's default RAM is the SDRAM. | same | Verified on source (v4.4.0) |
| External flash | IS25WP128, 16 MiB, partitions `mcuboot` 128 KiB, `image-0` and `image-1` 7 MiB each at `0x20000` and `0x720000`, no scratch partition | `mimxrt1170_evk.dtsi` | Verified on source (v4.4.0) |
| On-chip RAM | `ocram` 256 KiB at `0x20200000` (also the CM4 region, `zephyr,cpu1-region`), `ocram1` 512 KiB at `0x20240000`, `ocram2` 512 KiB at `0x202c0000`, plus ITCM and DTCM | `dts/arm/nxp/imxrt/nxp_rt11xx.dtsi`, `nxp_rt1170.dtsi` | Verified on source (v4.4.0) |
| Upstream MCUboot board files | None for the EVK. The MCUboot sample target resolves to revision B. | Stage 7 | Verified on source (v4.4.0) (Stage 7) |
| RT1060 solution | MCUboot's RAM moved to OCRAM2 (trimmed), executable window set to the SDRAM, a 256 B retained region at the end of OCRAM2 shared by both images | [boot composition](../boot-composition.md) | Verified by build (RT1060) |

## 3. Choices and proposed values

| # | Choice | Proposal | Reason | Label |
| --- | --- | --- | --- | --- |
| C1 | Executable window | `0x80000000`, size the whole SDRAM (64 MiB, `67108864`) | The same mechanism as the RT1060 (there the whole 32 MiB). A smaller window would need a reason that a build cannot give. | Inference |
| C2 | MCUboot's own RAM | `ocram2` (`0x202c0000`, 512 KiB), trimmed by the retained region, as the RT1060 uses OCRAM2. MCUboot must not use the SDRAM for its own RAM, because it loads the image there. Alternatives: `ocram1` (512 KiB at `0x20240000`, also not the CM4 region) or DTCM (Stage 7, row S7-L12). | Same clash as the RT1060 (SDRAM is the board default RAM on both boards). `ocram2` keeps the mapping closest to the RT1060 solution. | Inference |
| C3 | Retained region | 256 B (`0x100`) at the end of `ocram2`: `0x2033ff00`. Declared identically in both overlays. It lies outside the load window (SDRAM) and outside the application's RAM (SDRAM). | Same size and role as the RT1060 (`0x2027ff00`). | Inference |
| C4 | Slots | The upstream partitions unchanged (7 MiB slots) | No reason to move them. A padded image is the full 7 MiB slot (the RT1060: 3 MiB). Upstream also gives the CM4 core's image to slot 1 (`nxp,m4-partition = &slot1_partition`, used with `SECOND_CORE_MCUX`); that matters only for a dual-core build, which this note does not propose. | Verified on source (v4.4.0) for the sizes |
| C5 | Board revision | `@A`, as Stage 7 and the RT1170 feasibility record (issue #33) name it. Not `@B`. | The Rev A1 PCB mapping is open, so the choice follows the records, not a guess. | **Not verified** |
| C6 | Application | Open, see D2 below | `samples/controller/zephyr` has no RT1170 board configuration. | Inference |

Files would live in `zephyr/boot/boards/mimxrt1170_evk_mimxrt1176_cm7_A/` with the same four names as for the RT1060, next to the RT1060 folder.

## 4. What a build cannot show

A build-only result for the RT1170 would show that the arguments configure, MCUboot and the application build, the application is signed and `imgtool verify` accepts it. It would **not** show that the boot ROM accepts the layout. Everything about the RT1170 boot ROM is Not verified, because every NXP reference manual, security manual and application note and the Rev A1 user guide were unreadable for Stage 7: the FCB offset (`0x400`, against `0x0` on the RT1060), the XMCD that starts the SDRAM (the RT1060 uses a DCD), the boot switch settings, OTFAD in place of BEE, and any HAB or fuse behaviour. None of this is addressed by the proposed files.

## 5. Decisions

| # | Question | Recommendation |
| --- | --- | --- |
| D1 | Are C1 to C5 acceptable as the starting values, to be corrected by the first CI build? | Yes. They are the RT1060 values mapped to the RT1170 memory map, and the first build is what tests them. |
| D2 | Which application does the RT1170 build use? The controller sample has no RT1170 board configuration. | A minimal board configuration for the controller sample, only if it needs no change to the sample's source. If it does, a small Zephyr sample first (the claim is then about the composition, not about the Controller). |
| D3 | Where does the RT1170 build run in CI? | A separate step or job, not inside the RT1060 step of the `runtime` shard, so that an RT1170 failure does not hide an RT1060 result. Adding it changes `.github/workflows/zephyr.yml`, a protected path: the change is additive and needs a human approval that covers it. Alternative: one step that loops over both boards, which hides which board failed. |
| D4 | How does the check script handle a second board? | A second script that reuses the same checks, because a new check script may be added within the task and no existing protected file is edited. Alternative: a parameter of the existing `scripts/test-boot-composition.sh`, which keeps one script. That is an edit of an existing protected file; `AGENTS.md` lets an approved work package edit the test runner of a component it changes if no suite, board, build option or warning flag is removed, so it may need no separate approval, but the owner decides whether that reading applies. |

**Answer.** D1 to D4 were approved as recommended by the project owner on 15 November 2026: D1, the starting values C1 to C5 as proposed; D2, a minimal board configuration for the controller sample only if it needs no source change, otherwise a small Zephyr sample; D3, a separate step or job for the RT1170 in the Zephyr workflow, additive and not inside the RT1060 step; D4, a second, new check script.

## 6. Not verified

- All RT1170 boot ROM, XMCD, OTFAD, HAB and fuse behaviour (Stage 7).
- That the Rev A1 PCB corresponds to Zephyr revision A and carries the flash and SDRAM the devicetree names.
- That MCUboot and the application build for `mimxrt1170_evk@A//cm7` under sysbuild at v4.4.2 with these values. The upstream MCUboot sample target resolves to revision B, so a revision-A build may need more than the four files.
- The memory-map facts of section 2 at Zephyr v4.4.2.
- Whether the retained region at `0x2033ff00` is free of any other use in the application at v4.4.2.

## 7. Next step

The four files, the second check script and the CI step are prepared in a Draft pull request that is built only by CI, with a reviewer PASS before any merge, as for the RT1060. The first CI run may fail on the memory-map assumptions of this note. That is expected information, and the values are corrected from it. No board, no key, no fuse operation.
