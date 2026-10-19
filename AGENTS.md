# Crux development agent instructions

## Purpose and context

This is the source repository for Crux, an independent, reusable embedded software platform. Work from the existing implementation and verified evidence. Determine the active scope from the current user request and applicable approvals rather than assuming a fixed milestone.

Read the following before planning or changing code:

- [CONTRIBUTING.md](CONTRIBUTING.md) for code ownership, public interfaces, Git/PR conventions and review requirements.
- [README.md](README.md) for workspace setup, build commands and supported developer workflows.
- The current **Crux Platform Vision, Architecture Concept, Architecture Design and Development Plan** in the separate private `wonny-labs/crux-platform` repository, preferably from a read-only sibling checkout. Locate and read them rather than assuming an outdated local copy is current.
- Applicable component contracts, tests and implementation evidence in this repository.

The earlier Time Pilot plan in `docs/plans/initial-implementation-plan.md` and its **M0–M5** acceptance identifiers are historical **Pilot milestones**, separate from the current platform roadmap. Consult approved GitHub issues, PRs and applicable validation records for current delivery status. `docs/implementation-status.md` and `docs/baseline.md` retain historical Time Pilot evidence. Historical results do not authorise unrelated work or prove a current milestone complete. Do not merge the two milestone systems or re-run completed work without a specific reason.

Current implemented contracts and verified source behaviour are the factual baseline. Working architecture proposals guide development but become implementation requirements only when adopted for the current work. Report material conflicts between the platform documents and existing implementation instead of silently rewriting either. If the private planning repository is inaccessible, report that limitation and avoid decisions that depend on its unread contents.

## Engineering approach

- Preserve the existing C17 `components/base` and `components/time`, shared host/Zephyr sources, public `crux::time` usage target, and applicable test evidence unless changes are justified.
- Keep portable components independent of Zephyr, board and vendor headers. Select environment-specific implementations explicitly and use Zephyr-native drivers, networking, storage and kernel facilities when suitable.
- Evaluate a documented POSIX subset or a narrow capability-specific interface where actual portable consumers need one. Do not pre-emptively create a universal HAL/OSAL, generic service registry, or pass-through wrappers for native APIs.
- Preserve public interface naming, header ownership, dependency visibility and contribution rules defined in `CONTRIBUTING.md`. Do not promote Time-specific range, session or thread-safety constraints into platform-wide rules.
- Separate application policy from platform mechanisms. Keep implementation and restructuring proportional to the approved outcome; do not introduce product-specific runtime integration or speculative capabilities without an agreed need.
- Reuse existing build and test tools. Keep the west workspace separate from this Git repository; retain upstream Zephyr, vendor modules, SDKs and caches outside this source tree.

## Scope and authorisation

The user's current request and applicable prior approvals define authorised work. For an approved issue, use its agreed scope, acceptance criteria, exclusions and dependencies to plan execution. Identify any material conflict with the user request or adopted repository decisions before acting on it.

**Publishing, assigning or scheduling an Issue, or creating a Goal, does not itself authorise investigation, implementation, hardware operation or integration.** A Goal tracks an approved objective and its completion criteria; it does not expand that approval or satisfy a technical decision gate. An issue description, proposed plan or AI recommendation alone is not evidence of user approval. Record or reference required user decisions explicitly.

Respect technical decision gates and issue dependencies. Where a result requires user review before adoption, prepare the evidence and recommendation, then obtain that decision before beginning dependent implementation. Continue independent work within the approved scope while another task is blocked. Do not adopt a new public interface or architectural commitment merely because a probe or prototype demonstrates it.

## Autonomous work and review boundaries

For an approved milestone goal or issue, inspect the current code, tests, repository status and recorded evidence. Identify satisfied requirements before creating or changing work items. Break remaining work into small, independently verifiable tasks, implement within the agreed scope, run applicable checks and record the results.

Within user-authorised work, AI may propose, create and maintain GitHub Issues, dependencies and progress when access is available. It may carry out routine implementation and fixes within approved scope without seeking approval for each edit. Avoid duplicate tickets and do not close work whose acceptance evidence is missing.

**Escalate before adopting** significant public API or architecture changes, breaking compatibility, major dependency/toolchain upgrades, scope expansion, unsafe hardware operations or security-sensitive design decisions. Present the proposal and trade-offs for review. Prepare substantive changes on a dedicated development branch and submit a PR with actual validation evidence for human review. Do not push directly to `main`, merge a PR or change protected settings without explicit authorisation. Follow `CONTRIBUTING.md` for commits and PR content.

Record satisfied acceptance criteria, remaining gaps, blockers and support limits against the active work. Distinguish investigation findings, recommended decisions, approved decisions and implemented behaviour. Select representative probes and tests that resolve material uncertainties; do not interpret a broad investigation as a requirement to verify every API combination. Completion claims must match the approved acceptance criteria and available evidence.

## Independent review and historical approvals

When the active work requires independent review, use a reviewer that did not author the proposal or implementation. The reviewer must inspect governing requirements, applicable source/configuration and actual test evidence rather than relying only on the author's summary. Record a scoped PASS, REVISE or ESCALATE result and resolve material findings before passing the corresponding review gate. An independent PASS does not itself authorise hardware operations, architectural commitments, merges or milestone closure.

The bounded M2 #6/#7 delegation and PR #9 hardware/merge authorisations are historical, retained in their issues and PR. They grant no authority for subsequent tasks. Follow the active work's recorded approvals and decision gates.

## Validation and evidence

Use the existing repository checks as applicable:

- `scripts/check-host.sh` and `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh` for host behaviour and sanitizers.
- `scripts/check-format.sh`, `scripts/analyze-host.sh` and `scripts/check-dependencies.sh` for formatting, static analysis and architecture boundaries.
- `scripts/check-zephyr-smoke.sh`, `scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'`, `scripts/test-controller.sh` and `scripts/test-execution.sh` for applicable Zephyr builds, `native_sim` and execution tests.
- `scripts/test-m3.sh` for M3 software integration and EVKB cross-builds. It does not operate or qualify the physical board.
- [tests/README.md](tests/README.md) and the active issue/PR for additional suites and their actual validation status.

Use the Linux Dev Container and documented west workspace configuration for supported commands. Choose tests appropriate to the change; do not rerun costly unrelated checks without a reason. Distinguish passing local tests, passing GitHub Actions, successful EVKB compilation and **physical-board test evidence**. None implies the others. Record the tested revision, commands, configurations, failures, skipped checks and relevant artefacts in the issue/PR or applicable evidence document. Do not report unexecuted or blocked tests as passing.

## Safety and workspace boundaries

Verify Git status and the baseline commit before modifying work. Preserve unrelated or uncommitted changes. Do not use destructive Git resets, force pushes, clean-ups or file deletion to resolve environmental problems without explicit approval.

Physical programming, reset and other hardware operations require explicit authorisation for the **current task**, verified target/probe/Flash configuration and a safe execution setup. Past hardware approvals do not authorise unattended operations in a new task.

Use original work and appropriately licensed upstream material only. Do not import employer-owned or other confidential code, documents, credentials or restricted assets. Keep secrets and unrelated personal paths outside the repository and container. Do not mount the user home directory or Docker socket by default. Do not modify the separate `crux-platform` planning repository unless explicitly requested.
