# ADR-0002: Platform direction: Zephyr for execution, Crux for services

Status: **Accepted** on 13 November 2026. See [the owner's decision](#the-owners-decision). Proposed: 13 November 2026. Baseline: main `828021cf`. It amends [ADR-0001](0001-application-boundary-and-api-stability.md) by reference, as [What this amends](#what-this-amends-in-adr-0001) lists. ADR-0001's body is unchanged.

This record is documentation only. It changes no code, check, contract or protected path. Each decision that it puts into effect needs its own reviewed change afterwards, listed under [Follow-up changes](#follow-up-changes). The evidence is in the [platform direction review](../research/platform-direction-review.md) and is not copied here.

## Context

ADR-0001 settled three layers for a product, a stability policy and an experiment (D10) that would show whether Crux earns its boundary. Two consumers then raised the question "Zephyr only, or Crux APIs?" The review splits it into three decisions:

1. What product code uses.
2. What Crux's own services use inside.
3. Whether Crux keeps Host providers, so that domain code runs without Zephyr. That is hypothesis H2 of ADR-0001.

The review found, in brief:

- Real-time threads and recursive locks cannot be made through Crux Thread and Sync. A consumer that ports a real-time runtime therefore uses native Zephyr threads and locks, and Crux's own services run beside them.
- The open Draft PR #174 fixes a Sync priority defect by running Sync metadata with the scheduler locked. One call can delay every other thread, and that cost is unmeasured.
- The evidence does not separate "Zephyr only" (O2) from "Zephyr for execution, Crux for services" (O4). Both score the same within the review's margin. The first decision can be settled now. The other two need D10's results.

**Corrected H2 evidence.** ADR-0001 (`docs/decisions/0001-application-boundary-and-api-stability.md:345`) cites `crux-controller`'s Host tests as evidence for H2. Those tests link `workload` and `net_client` only, and no Crux provider. No product consumer uses a Crux Host provider today, so H2 has no product evidence. D10 phase 1 is what can show it.

## Decisions

1. **P1. One rule for product code, in three questions.**
   - OS and hardware facilities (threads, priorities, locks, queues, timers, interrupts, devices, buses, pins) go to Zephyr, called from the product's Zephyr code.
   - Platform services that Crux provides go to Crux.
   - Product behaviour is plain C in the domain, behind ports that the product owns. It calls no Zephyr and owns no thread.
   - The rule is the same under O2 and O4. It goes into the documentation and the product template now.
2. **P2. Freeze Thread, Sync, Queue and Serial for new product code.** No new features and no real-time extension. Existing users keep working. The product template's domain include check rejects these four headers, and `CONTRIBUTING.md:45` changes, which is a protected path. **D10's Build B is exempt from that check**, because it uses Crux Thread in its domain by design.
3. **P3. Per-service priority Kconfig** for Crux's workers and SSH threads, defaulting to today's priorities.
4. **P4. The shared-state pattern in `templates/product`** before any product migrates: one owning thread, reads by snapshot, writes by message, locks only in adapters. Service callbacks run on Crux threads, and a status read runs on the caller's thread.
5. **P5. D10 phase 1** runs Build B as approved, plus **Build B'**: Build B's domain without Crux Thread, behind a Zephyr shell. A `native_sim` sanitizer probe and a spike of a slimmed internal Sync are added. B against B' measures O1 against O4. A against B' measures O4 against plain Zephyr. The GPLv2 limit that applies to Build B applies to B' too.
6. **P6. #174 is not merged as it is.** The defect is fixed by slimming internal Sync, or by #174's third alternative, with no scheduler lock. The pool maximum of 256 is dropped and #174's test is kept. The runtime port's agreement that its recursive locks stay native is obtained.
7. **P7. Linux providers stay as test providers.** None is added without a Crux test need. The rest is decided under P9.
8. **P8. `README.md:5` is reworded:** Embedded Linux is not planned for the foreseeable future, and if needed it would be designed at the service boundary.
9. **P9. The later decisions wait for D10, the probe and the spike**, and the rule is fixed before the runs:
   - **Deprecations.** Deprecate Thread, Sync, Queue and Serial for products if Build B' stays within 1.3 times Build B's effort and its domain tests run on Host.
   - **Internals.** Choose O2 if `native_sim` sanitizers run within twice the `host-sanitize` time and the services' concurrency tests can move to them. Otherwise keep the slim shim (O4).
   - The Internal tier and the Linux providers follow from these two results.
10. **P10. This record** states P1 to P9 and the corrected H2 evidence, and amends ADR-0001 by reference.

## What this amends in ADR-0001

- **D1** (three layers) is narrowed by P2: new product code does not use the four frozen headers.
- **D10** gains Build B' (P5), the probe and the spike. Phase 1 only stays as approved. Phase 2 and board use are not touched.
- **Decision 5, option c** (`:355-357`) is **not adopted**. The staged path freezes, measures, then decides. The result of D10 can reopen it, as ADR-0001 says.
- **Decision 5, H2 evidence** (`:345`) is corrected as above.
- **D2 to D9** are kept. D6 rule 5 applies to the new priority Kconfig.

## Departures from recorded plans

- The runtime port recorded a plan to return to Crux locks once the defect is fixed in Crux. P6 keeps its recursive locks native instead. The reason is the scheduler lock's cost, not the non-recursive mutex contract, which is older than that plan. The change needs the runtime port's agreement, and it is not assumed.
- Wording that treated Embedded Linux as a planned product target changes under P8.

## Consequences

- Product code has one rule that holds under both O2 and O4, so the later decision changes only Crux's inside.
- `crux-controller` keeps working. Its threads and queue stay on frozen headers until P9 decides. If the headers are deprecated, it migrates first, with its own session.
- The runtime port's timer wait moves to Zephyr, a small change in its own repository after its agreement.
- No Crux priority API is added. Real-time threads are native.
- Frozen headers carry a documentation burden until P9. Their users see no break.
- The result of D10 phase 1 is no longer a confirmation test: B' is a direct rival to B.
- Nothing here lets a product mix Zephyr into its domain. The domain still has no Zephyr.

## Follow-up changes

None of these is part of this ADR. Each is its own reviewed change. Protected paths need the owner's approval of that change.

1. Documentation: `docs/architecture.md`, `docs/native-interface-policy.md`, `docs/thread-sync.md`, `docs/messaging-contract.md`, `docs/serial-contract.md`, `docs/component-readiness.md`, and the `README.md` wording of P8. Decided by: owner's approval of the pull request.
2. `templates/product`: the include check, the shared-state example, their tests. Decided by: the Crux Pilot session, with review.
3. Per-service priority Kconfig, by test-driven development. Decided by: the Crux Pilot session, with review.
4. `CONTRIBUTING.md:45` (protected). Decided by: the owner, on the change itself.
5. #174: the slim-Sync fix or the third alternative, after the spike. Decided by: the Crux Pilot session, with the owner's approval to merge.
6. D10 phase 1 with Build B', the probe and the spike, in the repository outside `crux-pilot`. Decided by: the session that runs D10.
7. The runtime port's agreement on native locks and its timer wait. Decided by: the runtime port's owner.
8. P9's decision, after the runs. Decided by: the owner, on the evidence and the rule fixed above.

## The owner's decision

The owner approved P1 to P10 as recommended in the Crux Pilot main session chat on 13 November 2026 at 08:12:55 UTC. The time comes from the session transcript, and the owner's time zone was not recorded. The main session had asked "P1~P10을 추천대로 승인할까요?" ("Approve P1 to P10 as recommended?"). The owner replied:

> 추천대로 승인

This means "approve as recommended". Hardware, phase 2 of D10 and merging stay outside it.

## Record

- Author: Claude Code, model `claude-sonnet-5-5`, from the review that the Crux Pilot main session had approved.
- Review: the repository's `reviewer` agent passed the review document before the owner's decision. This record's own review is in its pull request.
- Acceptance: recorded from the owner's words as the Crux Pilot main session relayed them.
