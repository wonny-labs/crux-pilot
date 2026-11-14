# ADR-0002: Platform direction: Zephyr for execution, Crux for services

Status: **Accepted** on 13 November 2026. **Amended by [its P9 result](#amendment-the-p9-result)** on 14 November 2026. See [the owner's decision](#the-owners-decision). Proposed: 13 November 2026. Baseline: main `828021cf`. It amends [ADR-0001](0001-application-boundary-and-api-stability.md) by reference, as [What this amends](#what-this-amends-in-adr-0001) lists. ADR-0001's body is unchanged.

This record is documentation only. It changes no code, check, contract or protected path. Each decision that it puts into effect needs its own reviewed change afterwards, listed under [Follow-up changes](#follow-up-changes). The evidence is in the [platform direction review](../research/platform-direction-review.md) and is not copied here.

## Context

ADR-0001 settled three layers for a product, a stability policy and an experiment (D10) that would show whether Crux earns its boundary. The owner then asked "Zephyr only, or Crux APIs?" Two consumers supply the evidence. The review splits the question into three decisions:

1. What product code uses.
2. What Crux's own services use inside.
3. Whether Crux keeps Host providers, so that domain code runs without Zephyr. That is hypothesis H2 of ADR-0001.

The review found, in brief:

- Crux Thread cannot set priorities while a thread runs, and Crux Sync offers no recursive lock, which would be an extra layer on its non-recursive mutex. A consumer that ports a real-time runtime therefore uses native Zephyr threads and locks, and Crux's own services run beside them.
- The open Draft PR #174 fixes a Sync priority defect by running Sync metadata with the scheduler locked. One call can delay every other thread, and that cost is unmeasured.
- The evidence does not separate "Zephyr only" (O2) from "Zephyr for execution, Crux for services" (O4). Both score the same within the review's margin. The first decision can be settled now. The other two need D10's results.

**Corrected H2 evidence.** ADR-0001 ([ADR-0001, Decision 5](0001-application-boundary-and-api-stability.md#decision-5-crux-value-and-the-evidence-that-would-show-it), the H2 row, `:347` at this head) cites `crux-controller`'s Host tests as evidence for H2. Those tests link `workload` and `net_client` only, and no Crux provider. No product consumer uses a Crux Host provider today, so H2 has no product evidence. D10 phase 1 is what can show it.

## Decisions

1. **P1. One rule for product code, in three questions.**
   - OS and hardware facilities (threads, priorities, locks, queues, timers, interrupts, devices, buses, pins) go to Zephyr, called from the product's Zephyr code.
   - Platform services that Crux provides go to Crux.
   - Product behaviour is plain C in the domain, behind ports that the product owns. It calls no Zephyr and owns no thread.
   - The rule is the same under O2 and O4. It goes into the documentation and the product template now.
2. **P2. Freeze Thread, Sync, Queue and Serial for new product code.** No new features and no real-time extension. Existing users keep working. The product template's domain include check rejects these four headers, and `CONTRIBUTING.md:45` changes, which is a protected path. **D10's Build B is exempt from that check**, because it uses Crux Thread in its domain by design.
3. **P3. Per-service priority Kconfig** for Crux's workers and SSH threads, defaulting to today's priorities.
4. **P4. The shared-state pattern in `templates/product`** before any product migrates: one owning thread, reads by snapshot, writes by message, locks only in adapters. Service callbacks run on session, HTTP server and reboot worker threads, and a status read runs on the caller's thread.
5. **P5. D10 phase 1** runs Build B as approved, plus **Build B'**: Build B's domain without Crux Thread, behind a Zephyr shell. A `native_sim` sanitizer probe and a spike of a slimmed internal Sync are added. B against B' measures O1 against O4. A against B' measures O4 against plain Zephyr. The GPLv2 limit that applies to Build B applies to B' too.
6. **P6. #174 is not merged as it is.** The defect is fixed by slimming internal Sync, or by #174's third alternative, with no scheduler lock. The pool maximum of 256 is dropped and #174's test is kept. The runtime port's agreement that its recursive locks stay native is to be obtained.
7. **P7. Linux providers stay as test providers.** None is added without a Crux test need. The rest is decided under P9.
8. **P8. `README.md:5` is reworded:** Embedded Linux is not planned for the foreseeable future, and if needed it would be designed at the service boundary.
9. **P9. The later decisions wait for D10, the probe and the spike**, and the rule, proposed in P9 and approved with it, is fixed before the runs:
   - **Deprecations.** Deprecate Thread, Sync, Queue and Serial for products if Build B' stays within 1.3 times Build B's effort and its domain tests run on Host.
   - **Internals.** Choose O2 if `native_sim` sanitizers run within twice the `host-sanitize` time and the services' concurrency tests can move to them. Otherwise keep the slim shim (O4).
   - The Internal tier and the Linux providers follow from these two results.
10. **P10. This record** states P1 to P9 and the corrected H2 evidence, and amends ADR-0001 by reference.

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

The review's section 9 carries the full wording and trade-offs. The options O1 to O5 are in its section 4.

## What this amends in ADR-0001

- **D1** (three layers) is narrowed by P2: new product code does not use the four frozen headers.
- **D10** gains Build B' (P5), the probe and the spike. Phase 1 only stays as approved. Phase 2 and board use are not touched.
- **Decision 5, option c** (`:357-359` at this head) is **not adopted**. The staged path freezes, measures, then decides. The result of D10 can reopen it, as ADR-0001 says.
- **Decision 5, H2 evidence** (`:347`) is corrected as above.
- **D2 to D9** are kept. D6 rule 5 applies to the new priority Kconfig.

## Departures from recorded plans

- The runtime port recorded a plan to return to Crux locks once the defect is fixed in Crux. P6 keeps its recursive locks native instead. The reason is the scheduler lock's cost, not the non-recursive mutex contract, which is older than that plan. The change needs the runtime port's agreement, and it is not assumed.
- Wording that treated Embedded Linux as a planned product target changes under P8.

## Consequences

- Product code has one rule that holds under both O2 and O4, so the later decision changes only Crux's inside.
- `crux-controller` keeps working. Its threads and queue stay on frozen headers until P9 decides. If the headers are deprecated, it migrates first, with its own session.
- The runtime port's timer wait may move to Zephyr, after its agreement or under P9, a small change in its own repository. P2 lets existing users keep working.
- No Crux priority API is added. Real-time threads are native.
- Frozen headers carry a documentation burden until P9. Their users see no break.
- The result of D10 phase 1 is no longer a confirmation test: B' is a direct rival to B.
- Nothing here lets a product mix Zephyr into its domain. The domain still has no Zephyr.

## Follow-up changes

None of these is part of this ADR. Each is its own reviewed change. Protected paths need the owner's approval of that change, and merging any follow-up pull request needs the owner's authorisation under `AGENTS.md` (Gate 2), or a standing merge rule of the owner that is recorded outside the repository. P1 to P10 set no other decision maker. Where an owner is named below, it is a proposal.

1. Documentation: `docs/architecture.md`, `docs/native-interface-policy.md`, `docs/thread-sync.md`, `docs/messaging-contract.md`, `docs/serial-contract.md`, `docs/component-readiness.md`, `README.md:5` (P8) and `README.md:9,14-15`.
2. `templates/product`: the include check, the shared-state example, their tests.
3. Per-service priority Kconfig, by test-driven development.
4. `CONTRIBUTING.md:45` (protected).
5. #174: the slim-Sync fix or the third alternative, after the spike.
6. D10 phase 1 with Build B', the probe and the spike, in the repository outside `crux-pilot`.
7. The runtime port's agreement on native locks and its timer wait. Proposed owner: the runtime port's owner.
8. P9's decision, after the runs, by the rule proposed in P9 and approved with it.

## Amendment: the P9 result

P9 left the internals decision to the reboot pilot and fixed its rule before the runs. The pilot has run, so P9's decision is made here. The body above is unchanged. Where it says that P9 is undecided, or words P8 as "not planned", this amendment applies. It changes no code, check, contract or protected path.

1. **Result: O4.** Crux's services keep a narrow Crux Thread and Sync internally, with Zephyr and Linux providers. The services do not move to Zephyr kernel objects (O2).
2. **Evidence.** The records are [Stage A](../research/reboot-native-sim-stage-a.md), which is merged, and Stage B, `docs/research/reboot-zephyr-direct-stage-b.md` on branch `pilot-reboot-zephyr-direct` (Draft PR #399), which is not merged. Every figure below comes from those two records. They are local figures from arm64 containers. Neither record contains a CI figure, and nothing ran on a physical board.
   - Stage A ran the 79 `reboot` cases on `native_sim` over the unchanged Crux providers: 79 cases, 0 failed.
   - Stage B moved the `reboot` service to `k_mutex`, `k_sem`, `k_thread` and `k_msleep`. The service core changed by 47 lines added and 31 removed, and the flake bar passed (100 of 100 runs, then 200 of 200).
   - The D-D rule allows O2 only if every criterion holds. Four fail, two of them (M4 and M6) on one suite. On the same suites the `native_sim` run takes about 19 to 23 times the Host `host-sanitize` run, against a limit of 2. The 9 checks of `sleep_provider` no longer run, which fails M4 and M6 narrowly. Blockers B1 to B4 have no owner answer. D-D also keeps the slim shim if M2 is above 2, and on the like-for-like basis it is.
   - Not measured: ThreadSanitizer suites (B4) and any physical board. Neither the Host nor the `native_sim` build has a race detector for these suites, so neither shows a data race.
3. **Principle.** Product code uses the OS natively: Zephyr products call Zephyr, Linux products call Linux. Crux's services use the narrow Crux Thread and Sync. Other OS facilities (drivers, networking, storage) are used natively in adapters. This is P1 with the services' inside named.
4. **Target state.** Crux Thread and Sync become internal to Crux. That switch is a later, separate pull request. It needs the owner's approval, and it comes after CODESYS, `crux-controller` and the samples have migrated. Queue and Serial are decided separately, because `cli` uses Serial. Deprecating and removing the public headers is part of that later decision. The P2 freeze stays until then.
5. **Known exception.** `ssh` uses native threads.
6. **P8, new wording.** An Embedded Linux integration is expected. It will be designed at the service boundary when its first product is defined. No date is set. The Linux providers remain test providers (P7) until then. The text of `README.md:5` and `:37` follows this wording in its own change.
7. **O5, candidate.** Moving the services' internals to a documented POSIX subset is evaluated when a Linux product is defined. Known blocker: Zephyr's POSIX layer needs picolibc or newlib, and the workspace has neither (Stage A). That evaluation must also check that CODESYS's timing needs are met: distinct run-time priorities and sub-millisecond waits.

The owner approved this result as recommended in the Crux Pilot main session chat on 14 November 2026 at 09:06:27 UTC. The time comes from the session transcript, and the owner's time zone was not recorded. The owner replied:

> 추천대로 승인할게

This means "I will approve as recommended". The statement that an Embedded Linux integration is expected is the owner's, from earlier in the same session. The switch to internal-only, hardware, tags and merging stay outside this approval.

Statements elsewhere that still say P9 is undecided or that Embedded Linux is not planned (`README.md`, `docs/architecture.md`, `docs/thread-sync.md` including its freeze notice and the remark at `:150` that workers may become native threads under P9, and the freeze notices of `docs/messaging-contract.md` and `docs/serial-contract.md`) follow in a separate documentation change.

## The owner's decision

The owner approved P1 to P10 as recommended in the Crux Pilot main session chat on 13 November 2026 at 08:12:55 UTC. The time comes from the session transcript, and the owner's time zone was not recorded. The main session had asked "P1~P10을 추천대로 승인할까요?" ("Approve P1 to P10 as recommended?"). The owner replied:

> 추천대로 승인

This means "approve as recommended". Hardware, phase 2 of D10 and merging stay outside it.

After the approval, P2, P4 and P6 in the review's section 9 were reworded to apply the independent re-check's non-blocking points. No decision changed: Build B's exemption follows from P5's "Build B as approved", and the requirement for the runtime port's agreement was already in sections 4.2 and 5.6 of the review at `b43d40ba`.

## Record

- Author: Claude Code, model `claude-sonnet-5-5`, from the review, whose recommendations the owner approved.
- Review: the repository's `reviewer` agent passed the review document before the owner's decision. This record's own review is in its pull request.
- Acceptance: recorded from the owner's words as the Crux Pilot main session relayed them.
