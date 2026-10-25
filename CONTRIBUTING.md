# Contributing to Crux Pilot

## Purpose and scope

This guide defines the contribution and review practices for Crux Pilot. It applies to source code, samples, tests, documentation, build configuration and CI workflows in this repository.

Contributors must follow the technical decisions and validation procedures adopted for the current work. Approved GitHub issues and recorded repository decisions define the applicable scope and acceptance criteria. Implemented component contracts, such as the [Time API contract](docs/time-contract.md), and applicable verification evidence define supported behaviour. Consult the [upstream reuse notes](docs/upstream-reuse.md) for source assessments and reuse decisions.

The [initial Time Pilot plan](docs/plans/initial-implementation-plan.md) and [implementation-status record](docs/implementation-status.md) preserve historical work rather than defining the current roadmap. Working proposals become requirements only when adopted. The [README](README.md) provides developer commands, the [test guide](tests/README.md) covers validation, and [AGENTS.md](AGENTS.md) defines AI execution boundaries.

## Making changes

- Keep each change focused on one coherent purpose. Separate unrelated changes where doing so improves review and testing.
- Review adopted repository decisions and applicable contracts before changing public interfaces, dependency boundaries, module ownership or platform integration. Architecture proposals may inform the review, but do not constrain implementation unless this repository adopts them. Discuss material architecture changes before implementation, including alternatives and consumer impact. For accepted incompatible changes, explain the compatibility impact and required migration.
- Reuse suitable native Zephyr facilities and existing components rather than introducing speculative abstraction layers.
- Use `crux_` for Crux-owned public C functions, types and exported variables, `CRUX_` for macros, enum constants and public header guards, and `crux` as the root namespace for Crux-owned C++ APIs. These prefixes are fixed for Crux. Detailed naming patterns and language standards are selected for the relevant interfaces and modules. Do not rename native Zephyr, vendor or third-party identifiers.
- Follow the [Coding and Public API Documentation Guidelines](docs/coding-guidelines.md) for new or materially changed Crux-owned code, alongside the existing format, compiler warning and static-analysis checks. Define memory ownership and concurrency assumptions. Do not introduce blocking operations in interrupt context, and assess dynamic allocation and resource bounds where timing or predictability matters.
- Include appropriate tests and documentation changes when behaviour, public interfaces or supported configurations change. Document supported inputs, outputs, errors, ownership, lifetime and execution-context or thread-safety guarantees. Use Doxygen-compatible comments for new or materially changed public declarations, without requiring a generated reference website or retroactive conversion of untouched headers.
- Treat third-party and generated code according to its applicable upstream rules. Record the source, licence and any maintained local patches. Follow upstream contribution requirements when submitting changes to upstream projects.
- Keep Crux independent of employer-owned software and confidential materials. Do not import employer-owned or other unauthorised proprietary code, restricted information, secrets or credentials.

## Port and test names

Use the owning `ports/<os>/` directory for platform implementations. When a
filename distinguishes an OS implementation, use its OS name (for example,
`file_linux.c`); keep shared `*_port_*` interfaces platform-neutral. Retain
`native` for internal native-operation contracts and `host` for development/test
roles. Name tests by their purpose, such as port fault handling or OS integration.
Existing public targets, provider values and configuration names require a
separate compatibility decision before renaming.

## Public headers and module dependencies

Keep platform public headers in the owning component at
`components/<component>/include/crux/`. Repeated `include/crux/` directories are intentional:
components own their files while consumers use the shared `<crux/...>` include namespace.
Public paths must be unique across components; physical separation does not prevent collisions.
Do not create a second central copy of a public header.

- Publish only interfaces required by consumers. Keep private ports, implementation helpers and
  native OS/chip headers in the owning implementation directory, outside public include roots.
  Backends implementing an existing API do not each need a new public header tree.
- Keep test controls outside public headers and expose their directories only to the affected
  test targets. Sample-owned headers belong to samples, not the platform API.
- Applications consume public CMake usage targets, such as `crux::time`, instead of naming
  implementation/backend targets or adding another component's include directory themselves.
  The target propagates its public headers and required dependencies.
- Use target-scoped CMake requirements. Use PUBLIC/INTERFACE for requirements consumers need,
  and PRIVATE for implementation-only requirements. Avoid directory/global include paths that
  make undeclared Crux dependencies available everywhere. Keep native dependencies out of
  portable public contracts and do not expose private directories transitively.
- Module/composition build definitions may wire their own implementation targets and private
  paths, including instrumented test variants. A reusable sample helper may receive public
  declarations without binding a backend; the final application/test composition selects one
  backend. Such wiring is internal and is not a supported consumer interface.

Header placement supports ownership; dependency declarations and review enforce the boundaries.
It does not establish binary ABI stability or require separate repositories/packages per module.
See the [Time contract](docs/time-contract.md#header-and-build-ownership) for the current mapping.
The [native-free consumer policy](docs/native-interface-policy.md) defines the
application/runtime/provider roles, standard-API limits and required source/build
checks when introducing portable consumers or public usage targets.

## Git workflow

Use a short-lived branch based on the current default branch for substantive code or architecture changes. Open a pull request (PR) for review before integration. Trivial documentation corrections may use a lighter process where appropriate.

Keep branch names descriptive. No mandatory prefix list is defined. Before requesting review, inspect the complete diff, remove temporary changes and confirm the reported validation results.

**Proposed merge default:** Squash-merge a reviewed PR when it represents one coherent change. The final squash commit must follow the commit message convention below. If its subject comes from the PR title, format the PR title accordingly.

Retain individual commits when they represent independently meaningful changes whose history matters. Keep retained commits buildable and testable where practical. These are proposed practices, not claims that GitHub merge rules or branch protection have been configured.

Requirements in this guide apply even when GitHub settings do not enforce them. Automated gates are limited to checks configured in the repository workflows; recommendations and proposals are not claims about configured enforcement.

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

Owned Host C/C++ targets use `-Wall -Wextra -Wpedantic -Werror`. Owned Zephyr
component libraries and sample/test application targets use private
`-Wall -Wextra -Werror` options. Zephyr's native headers use GNU extensions, so
the Host pedantic policy is not applied to that composition. These options do
not propagate to upstream Zephyr, vendor or generated targets. The pinned native
warning selection is retained, including its disabled format-zero-length,
pointer-sign and unused-but-set-variable categories; these are explicit initial
baseline limits for the Zephyr composition, not claims of exhaustive analysis.
No additional suppression is introduced for Crux code. Any future suppression must
be scoped and justified in review; a successful build with upstream warnings
is not permission to ignore owned-code warnings.

Choose tests according to the change and the claims being made. The table guides which evidence is appropriate; it does not require every environment to run for every PR.

| Change | Validation to select when applicable |
| --- | --- |
| Portable logic or a public capability contract | Native host tests and applicable contract/conformance cases |
| Zephyr integration or configuration | Applicable Zephyr build, `ztest`/Twister or `native_sim` checks |
| BSP, peripheral, timing or physical behaviour | Relevant physical-target validation and measured evidence |
| Samples or reference applications | Build and run applicable scenarios, including failure cases where useful |
| Documentation-only change | Review technical accuracy, links and consistency |

Use repository scripts for local validation. The [`host.yml`](.github/workflows/host.yml) and [`zephyr.yml`](.github/workflows/zephyr.yml) files define the automated CI checks currently configured. Consult the [README](README.md) and [test guide](tests/README.md) for commands, and the applicable approved issues and linked validation records for evidence. Do not assume a proposed test tool, command or CI gate is implemented until it is present and working. Record failures and tests not run transparently. Security-sensitive changes require verification proportionate to their risk.

## Documentation

Keep documentation clear and consistent with the current architecture decisions. Use relative links for general cross-document references, without embedding the referenced document’s version.

Check local Markdown links and section anchors with `python3 scripts/check_docs.py`. Install the pinned lychee executable once with `python3 scripts/check_docs.py --install`, or use `--list` to inspect the selected documents and exclusion reasons. The [selection policy](doc-links.toml) includes maintained platform and Pilot guidance and accepted contracts, but excludes frozen evidence and historical plans as scan inputs. Links *from* maintained documents *to* those records remain checked. External URLs are not verified by this offline check.

Use Git history for document revisions rather than maintaining document-specific version numbers or last-updated dates. Preserve meaningful document status, approval records and historical decision dates. Use a Git commit or tag when a particular historical baseline must be reproduced.
