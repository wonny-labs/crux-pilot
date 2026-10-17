# Original Codex Start Instructions (M0–M1; superseded)

This file preserves the original M0–M1 execution handoff. Those milestones are complete; it no longer sets the active scope. Read `AGENTS.md` and `docs/implementation-status.md` for current authorization and evidence.

## Execution inputs

- Primary directory: `/Users/jiwonkim/Projects/crux-workspace/crux-pilot`.
- West workspace: `/Users/jiwonkim/Projects/crux-workspace`; its root stays outside Git.
- Repository: `crux-pilot`, initialised on `main`, no initial commit at handoff preparation.
- Remote: none configured. Eventual intent is private GitHub; remote owner/access and creation/push are not authorised by this handoff.
- Board: actual EVK model/revision/flash configuration, probe, UART and reset method remain unverified. Discover available evidence; missing hardware must not block independent host work.
- Physical execution: disabled. Do not flash, reset or otherwise operate a board.
- Progress: planning and handoff files only. Their presence does not establish successful setup or tests.

## Implementation request: M0–M1 only

Read `docs/plans/initial-implementation-plan.md` v0.4 and `docs/plans/implementation-readiness-review.md`. Implement and verify M0–M1. This handoff authorises bounded local development setup and required dependency resolution, not later capability implementation or remote publication.

1. Inspect files and Git state. Recheck host tools, Docker availability/architecture, memory, storage and network/dependency access. Earlier observations are hints, not current facts. Do not delete files to make space. Report host installation/authentication steps requiring user interaction; continue independent scaffold work.
2. Resolve and record compatible pinned host tools, GoogleTest, Zephyr/example reference and SDK per M0. Assess the plan's v4.4.0 candidate; do not follow moving main. Record upstream RTC/Counter and C++ constraints rather than assuming RT1060 sample coverage.
3. Prepare the VS Code Dev Container using the plan's initial amd64/resource defaults and persistent caches. The host west workspace is `/Users/jiwonkim/Projects/crux-workspace`; `crux-pilot/`, `.west/`, `zephyr/` and `modules/` are siblings below it. Keep `crux-pilot/` as the primary VS Code/Codex folder, but bind-mount its parent into the container so west can see sibling checkouts. In `.devcontainer/devcontainer.json`, set `workspaceMount` to `source=${localWorkspaceFolder}/..,target=/workspaces/crux,type=bind` and `workspaceFolder` to `/workspaces/crux/crux-pilot` (adjust only if the actual folder name/path differs, and record the reason). Verify inside the container that `west topdir` is `/workspaces/crux`, `crux-pilot/` is the Git root, and `.west/`, `zephyr/` and selected `modules/` are visible. Keep caches in explicit named volumes or documented external paths. Do not mount the user's home, company materials, credentials or Docker socket. Separate host and Zephyr bootstrap. Fetch upstream as workspace siblings, not inside this Git tree.
4. Establish root host CMake/presets, a minimal C17 library/C++17 GoogleTest smoke fixture, GoogleTest/CTest execution, formatting, focused analysis and minimum CI/check entry points. These are foundation fixtures, not Time behaviour. Do not add temporary public capability APIs to make smoke fixtures look complete.
5. Establish upstream native_sim Hello World/Ztest smoke paths, C++ Hello World and applicable EVK Hello World compilation per M1. Demonstrate Crux module discovery without routing Zephyr into host root CMake. Missing exact target inputs keep the affected check pending.
6. Write `docs/baseline.md`, `docs/upstream-reuse.md` and update `docs/implementation-status.md` with versions, commands, outcomes, blockers and reproducible evidence.

Preserve independent C17 host compilation and C++17 consumption. Use common `crux_status_t` by default; services own domain states, data and necessary specialised results. The later Time implementation must preserve API `CRUX_ERR_NO_DATA` versus CLI `ERR NOT_SET`; implementing that behaviour is outside this task.

Use official Zephyr example-application and samples as references. Do not introduce Unity, an AI Harness, a universal HAL, service framework or unrelated modules. Do not copy company materials from Work sources. Resolve routine setup choices inside the plan without repeatedly requesting approval.

## Completion and stop condition

Finish with reviewable M0–M1 changes, AC-to-evidence links and actual commands/results. Every M0/M1 AC needs a pass/fail/pending/not-applicable status and rationale. Missing required executions prevent completion claims for the affected AC. Do not claim firmware builds or board results that have not run.

Stop before M2 Time production implementation. Propose its next bounded scope using established evidence. Do not create a remote, push code or operate hardware. A later instruction may enable M2; its contract-review gate still applies before the corresponding native capability implementation.

## Subsequent authorization update (2026-10-16)

The original M0–M1 handoff above is historical. Later user instructions authorized M2 and then the remaining development. The active software scope is M3 Zephyr integration and software-only M5 reproduction/architecture review, using the v0.5 plan while preserving its AC identifiers. The original prohibitions on creating a remote, pushing, company materials and physical board operation remain in force. M4 board evidence must stay pending unless separately authorized and executed.
