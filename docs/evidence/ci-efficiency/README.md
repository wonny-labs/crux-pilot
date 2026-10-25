# CI efficiency

## Scope and baseline

Started from `main` `b32b858f911b22df10894c8ea922fd3ef6366b2e` after PR #64.
No test commands, steps, tool versions, jobs, caches, permissions or required
check names (`host`, `zephyr`) change. Host Debug, ASan/UBSan, native_sim,
independent consumers, dependency checks and EVKB cross-builds all remain.
No hardware, storage safety changes, change-based skips or PR merge are included.

The directly inspected baseline is [Zephyr PR run 40141278550](https://github.com/wonny-labs/crux-pilot/actions/runs/40141278550),
HEAD `89b673524cc843c76d91c435134c23ba5d6d0ce5`, artifact `12579792550`.
Its workflow and test/build scripts are unchanged at the starting main.
ZIP SHA-256: `6bdc3a3d81f8426041abdf9a22b9a6b317259a8a249a849fabb83232bdd779af`.

| Measurement | Baseline |
| --- | ---: |
| Files in downloaded ZIP | 230,339 |
| Downloaded ZIP bytes | 619,862,107 |
| Uncompressed file bytes | 2,630,087,809 |
| Upload step elapsed | 465 s |
| Complete job elapsed | 2,239 s |
| Run created to updated | 2,242 s |

Times are GitHub step/job/run timestamps, including compression/collection in the
upload step, not isolated network transfer. Counts and bytes come from ZIP central
directory entries. MB here means decimal bytes / 1,000,000. Archive size includes
ZIP metadata; summed per-entry compressed bytes do not.

| Dominant removed tree | Files | Uncompressed bytes |
| --- | ---: | ---: |
| `twister-m3` | 131,462 | 1,470,747,600 |
| `twister-portable-execution` | 32,846 | 352,520,286 |
| `twister-serial` | 24,675 | 277,849,703 |
| `twister-messaging` | 24,620 | 266,237,929 |
| `twister-execution` | 16,468 | 186,917,097 |

These complete trees included intermediate object/archive files, generated header
forests, native simulator executables and duplicate simulator link objects under
`zephyr/NSI/`. They are rebuildable from pinned sources and are not the physical
qualification firmware handoff. Long repeated paths also inflate ZIP metadata.

## Retained evidence

Use the existing upload action's path allowlist rather than a custom packager:

- Every Twister suite's root JSON and XML reports, including early `testplan.json`.
- Recursive raw `.log` and `.err` files, CMake configure YAML/cache, compilation
  databases, generated `.config` and DTS; Twister maps remain for resource diagnosis.
- Generated `production_formats.h` / `production_phase.h`: the M3 output verifiers
  need them together with per-scenario configuration and handler logs to replay checks.
- Existing explicit EVKB ELF/BIN, maps, configuration, DTS, STAT, ROM/RAM reports,
  resource-evidence directories and provenance. No existing explicit image is removed.
- Generated MDIO correction source and hash metadata, naming the two actual files
  instead of recursively collecting its directory.
- Existing input manifests plus pending/after captures left behind by failed runs.
- Existing composition-failure logs, consumer JUnit results and scenario logs.

`include-hidden-files: true`, `if: always()` and 14-day retention remain.
A failed configuration can lack `.config`; its configure/build log and pending
inputs must still upload. Compiler errors also remain in the GitHub job log.
This collection is evidence, not a self-contained build SDK: use the source SHA,
pinned workspace inputs and build commands to rebuild. Native intermediate objects
are not retained. The existing M5 preparation CI does **not** use `--freeze`, so it
produces no frozen physical image/ABI manifest; this change adds no such claim or
hardware authority. Existing explicit qualification images and provenance stay intact.

## Events and cancellation

Both workflows run on unchanged default PR integration events, pushes to `main`,
and manual dispatch. Feature-branch pushes no longer duplicate PR validation.
PR checkout continues to test GitHub's integration ref; main pushes validate the
actual merged source. Manual dispatch remains the branch-only validation option.
No paths filter or draft-PR skip is added.

Concurrency includes the workflow name and `pr-<number>` only for PR events.
All other runs use unique `run-<run_id>` groups and do not cancel in progress.
A newer run can supersede only the same PR's same workflow. It cannot cancel
another PR, the other workflow, main validation or a manual dispatch. Unique
non-PR groups also prevent GitHub's single-pending-run replacement on main.

## Verification and measured result

Final hosted measurements and independent assessments are recorded in the Draft
PR, with links to the exact tested HEAD. Do not infer current passing CI from the
historical baseline above. The target is <=100 MB per Zephyr artifact, conditional
on retaining the evidence described here. Total-duration comparisons are observations
from individual runs; runner setup, scheduling and network variation limit causal
claims. Baseline process-exit delays remain unresolved.

Reproduction: download the run's artifact ZIP, count non-directory entries with
Python `zipfile.ZipFile.infolist()`, sum `file_size`, and use the downloaded ZIP's
file size. Compare the `started_at`/`completed_at` timestamps of the upload step
and complete job from GitHub's run/jobs APIs. Evaluate selected paths with the
same `@actions/glob` matching semantics as upload-artifact, including hidden files.

References: [upload-artifact v4](https://github.com/actions/upload-artifact/tree/v4)
and [workflow events/concurrency](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax).
