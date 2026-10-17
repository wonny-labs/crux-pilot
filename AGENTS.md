# Crux development agent instructions

## Purpose and context

This is the source repository for Crux, an independent, reusable embedded software platform. Work from the existing implementation, not from an empty-project assumption. The current development focus is **M1 — Engineering and Target Foundation** in the ongoing Crux Platform Development Plan.

Read the following before planning or changing code:

- [CONTRIBUTING.md](CONTRIBUTING.md) for code ownership, public interfaces, Git/PR conventions and review requirements.
- [README.md](README.md) for workspace setup, build commands and supported developer workflows.
- The current **Crux Platform Vision, Architecture Concept, Architecture Design and Development Plan** in the separate private `wonny-labs/crux-platform` repository, preferably from a read-only sibling checkout. Locate and read them rather than assuming an outdated local copy is current.
- Applicable component contracts, tests and implementation evidence in this repository.

The earlier Time Pilot plan in `docs/plans/initial-implementation-plan.md` and its **M0–M5** acceptance identifiers are historical **Pilot milestones**, not the new platform roadmap's M1–M5. `docs/implementation-status.md` and `docs/baseline.md` record what was actually verified in that pilot; they do not authorise unrelated work or prove later platform milestones complete. Do not merge the two milestone systems or re-run completed work without a specific reason.

Current implemented contracts and verified source behaviour are the factual baseline. Working architecture proposals guide development but become implementation requirements only when adopted for the current work. Report material conflicts between the platform documents and existing implementation instead of silently rewriting either. If the private planning repository is inaccessible, report that limitation and avoid decisions that depend on its unread contents.

## Engineering approach

- Preserve the existing C17 `components/base` and `components/time`, shared host/Zephyr sources, public `crux::time` usage target, and applicable test evidence unless changes are justified.
- Keep portable components independent of Zephyr, board and vendor headers. Select environment-specific implementations explicitly and use Zephyr-native drivers, networking, storage and kernel facilities when suitable.
- Evaluate a documented POSIX subset or a narrow capability-specific interface where actual portable consumers need one. Do not pre-emptively create a universal HAL/OSAL, generic service registry, or pass-through wrappers for native APIs.
- Preserve public interface naming, header ownership, dependency visibility and contribution rules defined in `CONTRIBUTING.md`. Do not promote Time-specific range, session or thread-safety constraints into platform-wide rules.
- Separate application policy from platform mechanisms. Do not introduce product-specific runtime integration, speculative capabilities or broad repository restructuring as part of M1.
- Reuse existing build and test tools. Keep the west workspace separate from this Git repository; retain upstream Zephyr, vendor modules, SDKs and caches outside this source tree.

## Autonomous work and review boundaries

For an approved milestone goal or issue, inspect the current code, tests, repository status and recorded evidence. Identify satisfied requirements before creating or changing work items. Break remaining work into small, independently verifiable tasks, implement within the agreed scope, run applicable checks and record the results.

AI may propose, create and maintain GitHub Issues, dependencies and progress when access is available. It may carry out routine implementation and fixes within approved scope without seeking approval for each edit. Avoid duplicate tickets and do not close work whose acceptance evidence is missing.

**Escalate before adopting** significant public API or architecture changes, breaking compatibility, major dependency/toolchain upgrades, scope expansion, unsafe hardware operations or security-sensitive design decisions. Present the proposal and trade-offs for review. Prepare substantive changes on a dedicated development branch and submit a PR with actual validation evidence for human review. Do not push directly to `main`, merge a PR or change protected settings without explicit authorisation. Follow `CONTRIBUTING.md` for commits and PR content.

For M1, prioritise remaining foundation work: verify the committed baseline and hosted CI, maintain formatting/static-analysis and dependency gates, and assess Ethernet, peripheral-test access and QSPI/XIP/boot-image feasibility. Record blockers and support limits rather than implementing M2–M5 features early. Continue with available independent work when another task is blocked.

## Validation and evidence

Use the existing repository checks as applicable:

- `scripts/check-host.sh` and `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh` for host behaviour and sanitizers.
- `scripts/check-format.sh`, `scripts/analyze-host.sh` and `scripts/check-dependencies.sh` for formatting, static analysis and architecture boundaries.
- `scripts/check-zephyr-smoke.sh` and `scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'` for Zephyr builds, `native_sim` and tests.

Use the Linux Dev Container and documented west workspace configuration for supported commands. Choose tests appropriate to the change; do not rerun costly unrelated checks without a reason. Distinguish passing local tests, passing GitHub Actions, successful EVKB compilation and **physical-board test evidence**. None implies the others. Record the tested revision, commands, configurations, failures, skipped checks and relevant artefacts in the issue/PR or applicable evidence document. Do not report unexecuted or blocked tests as passing.

## Safety and workspace boundaries

Verify Git status and the baseline commit before modifying work. Preserve unrelated or uncommitted changes. Do not use destructive Git resets, force pushes, clean-ups or file deletion to resolve environmental problems without explicit approval.

Physical EVKB programming, reset and other hardware operations require explicit authorisation for the **current task**, verified board/probe/Flash configuration and a safe execution setup. Past Pilot approvals do not authorise unattended hardware operations.

Use original work and appropriately licensed upstream material only. Do not import employer-owned or other confidential code, documents, credentials or restricted assets. Keep secrets and unrelated personal paths outside the repository and container. Do not mount the user home directory or Docker socket by default. Do not modify the separate `crux-platform` planning repository unless explicitly requested.
