# Crux pilot agent instructions

Read `docs/plans/codex-start-instructions.md`, then the v0.5 plan and readiness review. The M0–M1 handoff is historical; this file and `docs/implementation-status.md` define active scope. Preserve AC identifiers and record evidence in `docs/implementation-status.md`.

## Current scope

M0–M3 are implemented and their evidence is recorded in `docs/implementation-status.md`. The user has authorized continuing development, including M4 physical verification on the NXP MIMXRT1060-EVKB with SEGGER J-Link. Complete software-only M5 review and the authorized M4 checks when the board and probe are connected. Record commands and actual outcomes; do not mark physical criteria passed until observed on hardware.

Preserve milestone AC identifiers and report evidence in `docs/implementation-status.md`. Do not start unrelated modules or change contracts without evidence.

Use this repository as the primary working directory. The parent `crux-workspace/` is a west workspace, not a Git repository. The VS Code Dev Container should mount that parent at `/workspaces/crux` while opening `/workspaces/crux/crux-pilot` as `workspaceFolder`; verify `west topdir` resolves to `/workspaces/crux`. Keep upstream Zephyr, west projects, SDKs and caches outside this Git tree. The future internal `zephyr/` directory contains Crux integration metadata, not upstream source.

## Architecture and quality

- Preserve independent host CMake, C17 production compilation and C++17 consumption. Reuse official Zephyr example-application and samples from the selected baseline.
- Use common `crux_status_t` by default; modules own domain states, data and necessary specialised results. Do not duplicate common codes in a Time-specific status type.
- Use GoogleTest/CTest for host checks, Ztest/Twister for Zephyr checks and pytest for external integration/functional tests at their applicable milestones. Do not implement later capability tests prematurely.
- Transferable APIs and sample output use Crux naming; keep host presentation and board configuration in their owning composition layers. Do not promote prototype Time constraints into platform-wide policy.
- Keep changes proportional: no Unity, custom AI Harness, universal HAL/OSAL, service registry or speculative services.
- Run checks appropriate to the active milestone. Record commands and actual results. Missing executions remain pending/fail, never passed by assumption.

## Ownership and execution boundaries

This is a private personal experiment. Use original work and appropriately licensed public upstream material. Do not copy company code, BSPs, slides, internal documents, Harness assets or credentials into the repo/container. Record upstream provenance and preserve licences.

Remote creation/push and repository renaming remain outside this handoff. Physical operation is authorized only for the user's MIMXRT1060-EVKB and SEGGER J-Link verification described in the current task; verify the board marking, flash configuration, probe, and connections before programming. Do not delete user files to recover space. Do not mount the user home, company source, credentials or Docker socket into the container by default. Discover tools/resources first; report required host installations or authentication that cannot be completed through the available execution environment. Do not copy company-owned materials into the repository or container.
