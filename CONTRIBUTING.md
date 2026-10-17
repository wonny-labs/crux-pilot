# Contributing to Crux

**Status:** Draft for review

## Purpose and scope

This guide defines contribution practices for Crux, an embedded software platform. It covers platform code, samples, reference applications, tests, build and CI tooling, and documentation. The same practices apply to human and AI-assisted contributions. A human remains accountable for review and acceptance.

Use the [implementation plan](docs/plans/initial-implementation-plan.md), [Time API contract](docs/time-contract.md), [upstream reuse notes](docs/upstream-reuse.md), and [implementation status](docs/implementation-status.md) for current technical decisions and evidence. This guide complements those documents and does not override them. [AGENTS.md](AGENTS.md) provides additional repository-specific instructions for AI agents. Follow these practices as applicable; do not assume proposed or unconfigured automation is an active gate.

## Making changes

- Keep each change focused on one coherent purpose. Separate unrelated changes where doing so improves review and testing.
- Review the current architecture documents and applicable decisions before changing public contracts, dependency boundaries, module ownership or platform integration. Discuss material architecture changes before implementation, including alternatives and consumer impact. For accepted incompatible changes, explain the compatibility impact and required migration.
- Reuse suitable native Zephyr facilities and existing components rather than introducing speculative abstraction layers.
- Use `crux_` for Crux-owned public C functions, types and exported variables, `CRUX_` for macros, enum constants and public header guards, and `crux` as the root namespace for Crux-owned C++ APIs. These prefixes are fixed for Crux. Detailed naming patterns and language standards are selected for the relevant interfaces and modules. Do not rename native Zephyr, vendor or third-party identifiers.
- Follow the applicable language standard, formatting rules, compiler warning policy and configured static-analysis checks. Define memory ownership and relevant concurrency assumptions. Do not introduce blocking operations in interrupt context, and assess dynamic allocation and resource bounds where timing or predictability matters.
- Include appropriate tests and documentation changes when behaviour, public interfaces or supported configurations change. Document public capability contracts sufficiently for consumers to understand relevant inputs, outputs, errors, ownership, lifetime and execution-context or thread-safety guarantees. Do not require Doxygen or another specific documentation tool unless one is adopted.
- Treat third-party and generated code according to its applicable upstream rules. Record the source, licence and any maintained local patches. Follow upstream contribution requirements when submitting changes to upstream projects.
- Keep Crux independent of employer-owned software and confidential materials. Do not import employer-owned or other unauthorised proprietary code, restricted information, secrets or credentials.

## Git workflow

Use a short-lived branch based on the current default branch for substantive code or architecture changes. Open a pull request (PR) for review before integration. Trivial documentation corrections may use a lighter process where appropriate.

Keep branch names descriptive. No mandatory prefix list is defined. Before requesting review, inspect the complete diff, remove temporary changes and confirm the reported validation results.

**Proposed merge default:** Squash-merge a reviewed PR when it represents one coherent change. The final squash commit must follow the commit message convention below. If its subject comes from the PR title, format the PR title accordingly.

Retain individual commits when they represent independently meaningful changes whose history matters. Keep retained commits buildable and testable where practical. These are proposed practices, not claims that GitHub merge rules or branch protection have been configured.

## Commit messages

Use a small subset of the [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) format:

```text
<type>: <short summary>

[explanation when needed]

[optional footers]
```

Use one of the following types.

| Type | Use for |
| --- | --- |
| `feat` | A new feature, capability or developer-facing behaviour |
| `fix` | Correcting faulty behaviour |
| `refactor` | Restructuring without intended behaviour change |
| `perf` | A performance improvement supported by evidence |
| `test` | Adding or improving tests without changing product behaviour |
| `docs` | Documentation-only changes |
| `build` | Build definitions, toolchains and dependency configuration |
| `ci` | Continuous-integration workflows and related automation |
| `chore` | Repository maintenance that fits none of the above |
| `revert` | Reverting an earlier commit or change |

Begin the summary with a concise, lowercase imperative verb. Preserve the usual capitalisation of proper names and acronyms, and omit the final full stop. Aim for fewer than 72 characters, including the type. Describe the change rather than the work undertaken.

**Scope policy.** Omit scopes for now. Crux does not yet have a maintained mapping between scope names and components, so do not invent free-form values. If scopes are introduced, define an explicit list based on component ownership, including how cross-cutting changes are handled, before enforcing it. A scope should not be a filename, work-item number or arbitrary technical term.

For substantial changes, use the commit body to explain **why** the change is needed, including significant behaviour changes or trade-offs. Put detailed test results in the PR rather than duplicating them. When reverting a change, identify the original commit and explain why.

For intentional incompatible changes, include a `BREAKING CHANGE:` footer. An optional `!` may also appear after the type. These markers do not by themselves set a Crux release version.

Examples:

```text
feat: add a portable time capability contract
fix: reject invalid RTC timestamps
refactor: simplify provider binding in time service
test: cover time capability failure paths
build: pin Zephyr module revisions
ci: run native host tests on pull requests
docs: clarify capability ownership
revert: undo unsupported storage configuration
```

Example with context:

```text
fix: reject invalid RTC timestamps

Reject out-of-range values before updating the clock to avoid
silently accepting an unusable system time.
```

Example of an intentional incompatible change:

```text
feat!: change the time capability contract

BREAKING CHANGE: Consumers must use the new time representation.
```

## Pull requests and review

A substantive PR must explain **what changed, why it is needed and how it was validated**. If a relevant check was not run, state why. Include the following details where they matter:

- **Impact:** affected capability contracts, modules, environments, compatibility and known limitations.
- **Risks and dependencies:** outstanding uncertainty, follow-up work or changes required for integration.
- **Validation context:** relevant toolchain, configuration or target details needed to interpret results.

Review should consider correctness, architectural boundaries, ownership, public API documentation, test adequacy, portability where promised, resource and execution-context effects where relevant, and maintainability. Crux-owned code must meet the applicable compiler warning policy and configured static-analysis checks. Report checks that are not yet available or were not run rather than claiming they passed. Address material review feedback and re-run affected checks. A green CI result does not replace engineering review.

For AI-assisted contributions, the responsible human reviews the proposed plan and completed changes at the gates required by the active workflow. AI-generated claims about tests or target execution must be supported by actual results. Do not treat AI assistance as approval or as evidence of correctness.

## Testing and validation

Choose tests according to the change and the claims being made, rather than requiring every environment for every PR.

| Change | Expected validation approach |
| --- | --- |
| Portable logic or a public capability contract | Native host tests and applicable contract/conformance cases |
| Zephyr integration or configuration | Applicable Zephyr build, `ztest`/Twister or `native_sim` checks |
| BSP, peripheral, timing or physical behaviour | Relevant physical-target validation and measured evidence |
| Samples or reference applications | Build and run applicable scenarios, including failure cases where useful |
| Documentation-only change | Review technical accuracy, links and consistency |

Use the checks implemented in repository scripts and the [`host.yml`](.github/workflows/host.yml) and [`zephyr.yml`](.github/workflows/zephyr.yml) workflows as the available validation baseline. Consult the [README](README.md), [implementation status](docs/implementation-status.md), and [build evidence](docs/baseline.md) for commands and recorded results. Do not assume a proposed test tool, command or CI gate is implemented until it is present and working. Record failures and tests not run transparently. Security-sensitive changes require verification proportionate to their risk.

## Documentation

Keep documentation clear and consistent with the current architecture decisions. Use relative links for general cross-document references, without embedding the referenced document’s version.

Use Git history for document revisions rather than maintaining document-specific version numbers or last-updated dates. Preserve meaningful document status, approval records and historical decision dates. Use a Git commit or tag when a particular historical baseline must be reproduced.
