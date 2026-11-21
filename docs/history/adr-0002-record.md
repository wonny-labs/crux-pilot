# ADR-0002: drafting and approval record

> **Historical record.** Moved from [0002-platform-direction-zephyr-execution-crux-services.md](../decisions/0002-platform-direction-zephyr-execution-crux-services.md) when the documentation was restructured as a developer entry point. Kept as written; not current guidance. The link check does not read this folder.

Each section below names the section of the record that the text came from. Where the record now says the same thing in current terms, the heading says "rewritten in place".

## Status line (rewritten in place)

Status: **Accepted** on 13 November 2026. **Amended by [ADR-0003](../decisions/0003-p9-result-services-keep-narrow-thread-sync.md)** on 14 November 2026. See [the owner's decision](../decisions/0002-platform-direction-zephyr-execution-crux-services.md#the-owners-decision). Proposed: 13 November 2026. Baseline: main `828021cf`. It amends [ADR-0001](../decisions/0001-application-boundary-and-api-stability.md) by reference, as [What this amends](../decisions/0002-platform-direction-zephyr-execution-crux-services.md#what-this-amends-in-adr-0001) lists. ADR-0001's body is unchanged.

## Opening paragraph (rewritten in place)

This record is documentation only. It changes no code, check, contract or protected path. Each decision that it puts into effect needs its own reviewed change afterwards, listed under [Follow-up changes](../decisions/0002-platform-direction-zephyr-execution-crux-services.md#follow-up-changes). The evidence is in the [platform direction review](../research/platform-direction-review.md) and is not copied here.

## Decisions: the recommendation table (rewritten in place without the recommendation column)

| # | Recommendation | Reason | Alternative |
| --- | --- | --- | --- |
| P1 | Approve | Fits the real-time evidence, is the same under O2 and O4, and is reversible | O4a: keep Crux Thread open for non-real-time product work |
| P2 | Approve | Stops growth of a second kernel API without breaking `crux-controller` before D10 | Leave them open until D10 (O1 as is) |
| P3 | Approve | Products must place Crux threads, and per-service settings survive O2 and O4 | Three per-class settings: simpler, but the public interface is tied to the shim |
| P4 | Approve | Service callbacks run on session, server, worker or caller threads (F17), and the domain owns no lock | An exclusive-area lock port per product |
| P5 | Approve | Measures O1 against O4 directly, without turning the test into a confirmation | B only; or replace B with B', which tests only the chosen direction |
| P6 | Approve | Removes the defect's cause and an unmeasured system-wide delay | Merge as is and document one CPU and the unmeasured delay |
| P7 | Approve | The providers carry the services' Host suites, and removing them now preempts the probe | Remove them now (O2) |
| P8 | Approve | Uses the owner's own wording in ADR-0001 | Keep the text, which reads against ADR-0001, or delete it |
| P9 | Approve | Decides with evidence, as on 12 November | Decide the deprecations now, which takes up option c untested |
| P10 | Approve | Keeps the accepted record intact and the change traceable | A dated amendment section in ADR-0001 |

## Departures from recorded plans

#### Departures from recorded plans

- The runtime port recorded a plan to return to Crux locks once the defect is fixed in Crux. P6 keeps its recursive locks native instead. The reason is the scheduler lock's cost, not the non-recursive mutex contract, which is older than that plan. The change needs the runtime port's agreement, and it is not assumed.
- Wording that treated Embedded Linux as a planned product target changes under P8.

## Follow-up changes: opening paragraph (rewritten in place)

None of these is part of this ADR. Each is its own reviewed change. Protected paths need the owner's approval of that change, and merging any follow-up pull request needs the owner's authorisation under `AGENTS.md` (Gate 2), or a standing merge rule of the owner that is recorded outside the repository. P1 to P10 set no other decision maker. Where an owner is named below, it is a proposal.

## The owner's decision

#### The owner's decision

The maintainer approved P1 to P10 as recommended on 13 November 2026.

This means "approve as recommended". Hardware, phase 2 of D10 and merging stay outside it.

After the approval, P2, P4 and P6 in the review's section 9 were reworded to apply the independent re-check's non-blocking points. No decision changed: Build B's exemption follows from P5's "Build B as approved", and the requirement for the runtime port's agreement was already in sections 4.2 and 5.6 of the review at `b43d40ba`.

## Record

#### Record

- Author: Claude Code, from the review, whose recommendations the maintainer approved.
- Review: the repository's `reviewer` agent passed the review document before the owner's decision. This record's own review is in its pull request.
- Acceptance: accepted by the maintainer on 13 November 2026.
- Revision: 18 November 2026: chat quotes, relay wording and model names replaced with neutral wording, with the maintainer's approval; no decision, option, consequence or decision code changed.
