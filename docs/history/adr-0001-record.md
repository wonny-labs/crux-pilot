# ADR-0001: drafting and approval record

> **Historical record.** Moved from [0001-application-boundary-and-api-stability.md](../decisions/0001-application-boundary-and-api-stability.md) when the documentation was restructured as a developer entry point. Kept as written; not current guidance. The link check does not read this folder.

Each section below names the section of the record that the text came from. Where the record now says the same thing in current terms, the heading says "rewritten in place".

## Status line (rewritten in place)

Status: **Accepted** on 12 November 2026, with D10 limited to phase 1. **Amended by [ADR-0002](../decisions/0002-platform-direction-zephyr-execution-crux-services.md)** on 13 November 2026. See [the owner's decision](../decisions/0001-application-boundary-and-api-stability.md#the-owners-decision). Proposed: 12 November 2026. Baseline: main `54cc83e`.

## Opening paragraph (rewritten in place)

This record is documentation only. It changes no code, check, contract or protected path, and nothing in it binds until the owner accepts it. Each decision that it would put into effect needs its own change afterwards, listed under [Follow-up changes](../decisions/0001-application-boundary-and-api-stability.md#follow-up-changes). The facts and sources behind it are in the [research note](../research/app-boundary-and-api-stability.md).

## The recommendation in brief: heading (renamed "In brief")

#### The recommendation in brief

## The recommendation in brief: closing paragraph

The [owner decisions](../decisions/0001-application-boundary-and-api-stability.md#owner-decisions) at the end list ten items. Eight are "approve the recommendation". Two need the owner's input: D7 asks for a number, and D10 needs hardware and a place to run the experiment.

## Context: the review (rewritten in place)

**The review.** A colleague reviewed Crux on 12 November 2026. A fact-check against `54cc83e` found the claims largely true:

## Context: the owner's decision of 12 November 2026 (rewritten in place)

**The maintainer's decision of 12 November 2026.** Crux will not be ported to an operating system other than Zephyr for the foreseeable future. Portability between operating systems is therefore no longer a reason for any Crux API. Linux Host stays a development and test environment. Three reasons remain for Crux to publish an interface:

## Decision 1: recommendation line (rewritten in place)

**Recommendation: B.**

## Decision 2: recommendation line (rewritten in place)

**Recommendation: b and c together.**

## Decision 3: recommendation line (rewritten in place)

**Recommendation: c.**

## Decision 3: approval sentence (rewritten in place)

Removing a public API needs the owner's explicit approval under `AGENTS.md`, so it is listed as its own decision, D4.

## Decision 4.1: recommendation line (rewritten in place)

**Recommendation: c.** The tiers apply to every public interface: headers, `crux::` targets, Kconfig symbols, Devicetree aliases such as `crux-i2c0`, and composition interfaces. Private headers, ports and test controls are **Internal**: no promise, as today.

## Decision 4.3: recommendation line (rewritten in place)

**Recommendation: d.** The mechanism, a follow-up change:

## Decision 4.4: release notes row (rewritten in place)

| Release notes sections | Listed breaking changes, and the compatibility table (the owner's decision of 8 November 2026). No release exists yet | Add sections for deprecations and tier changes |

## Decision 4.5: criteria heading (rewritten in place)

**Proposed criteria for `v1.0`:**

## Decision 5: secure management row (rewritten in place)

| Secure management has never run on a board | Agreed. It is the largest value claim, and it is unproven until milestones M10 to M12 produce board evidence |

## Decision 5: recommendation line (rewritten in place)

**Recommendation: b.** If the result refutes H2 and H3, option c becomes the next decision. In that case Crux would keep the services and the tooling, and stop wrapping kernel facilities.

## Decision 5: phase 2 (rewritten in place)

- Phase 2 runs on the RT1060-EVKB with an ADS112C04 breakout. It needs the part, and the owner's approval for the board under `AGENTS.md`. Where it runs and who runs it are D10.

## Follow-up changes: opening paragraph (rewritten in place)

None of these is part of this ADR. Each is its own reviewed change after acceptance, and the ones on protected paths need the owner's approval of that change. None had started when the owner accepted this record. Item 7 covers phase 1 only, as [the owner's decision](../decisions/0001-application-boundary-and-api-stability.md#the-owners-decision) records.

## Owner decisions, and its subsection "The owner's decision"

#### Owner decisions

| # | Decision | Recommendation |
| --- | --- | --- |
| D1 | Adopt the three layers: domain, adapters, Crux services | Approve |
| D2 | Zephyr only in adapters, composition, providers, samples' Zephyr folders and tests. Crux keeps its checks, and products get an include check and a Host build of the domain through the template | Approve |
| D3 | The composition interface rules as the one formal route into Crux providers, including Zephyr types where a binding needs them | Approve |
| D4 | No device or driver model. Freeze the I2C, SPI and GPIO APIs now as Experimental, announce deprecation at the first tag, remove them in the next tagged release. This removes public API, so it needs your explicit yes | Approve |
| D5 | Zephyr's four tier names with Crux's rules, labelled from the first tag. Capabilities without a Zephyr composition outside `tests/` start Experimental. Nothing Stable before `v1.0` | Approve |
| D6 | The six change rules, including "zero means default, unless zero must mean unset" (security and safety fields stay fail-closed) and "unknown results are failures" | Approve |
| D7 | **Deprecation window:** after `v1.0`, at least two minor releases and at least six months; before `v1.0`, one tagged release for Unstable | Approve, or give other numbers |
| D8 | Compatibility evidence: contract tests per provider, `crux-controller` built against main (as today) and at each tag, a record of the public surface, fuller release notes | Approve |
| D9 | The three new rules before `v1.0` in 4.5: every breaking change raises `y` with a migration note, tiers from the first tag, one release of notice for Unstable. Also the five criteria for `v1.0` | Approve |
| D10 | **The experiment:** run phase 1 (Host and `native_sim`, no hardware) in a new repository outside `crux-pilot`. Phase 2 needs an ADS112C04 breakout and board time | Approve phase 1. For phase 2, please decide whether to buy the part and when the board is free. Neither of those authorises the board run: phase 2 needs your explicit authorisation for that run when it happens |

##### The owner's decision

The maintainer accepted the recommendations on 12 November 2026, as recommended. The list D1 to D10 carried the recommendation "D1 to D9 approve, D7 with the recommended numbers, D10 phase 1 only", and flagged D4 as needing an explicit yes.

The first sentence means "Proceed as recommended". The second is about ECC environment variables and is not part of this record.

| # | Decided |
| --- | --- |
| D1 to D6, D8, D9 | Approved as recommended |
| D7 | Approved with the recommended numbers: after `v1.0`, at least two minor releases and at least six months; before `v1.0`, one tagged release for Unstable |
| D10 | Phase 1 only: the Host and `native_sim` experiment, without hardware, in a new repository outside `crux-pilot`. Phase 2 is **not approved**. Buying the ADS112C04 breakout and the board time stay undecided, and no board run is authorised |

The approval of D4 is read from the acceptance of the whole list as recommended, given after a list that flagged D4 as needing an explicit yes. The removal itself is follow-up change 4, which touches `scripts/` and `.github/` and so needs the owner's approval of that change.

## Record

#### Record

- Author: Claude Code.
- Research: three subagents on the Sonnet model alias (exact model ID not reported), each limited to primary sources.
- Review: the repository's `reviewer` agent; its verdict is recorded in the pull request.
- Acceptance: accepted by the maintainer on 12 November 2026. The pull request that records it holds its review.
- Revision: 18 November 2026: chat quotes, relay wording and model names replaced with neutral wording, with the maintainer's approval; no decision, option, consequence or decision code changed.
