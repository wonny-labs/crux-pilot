# ADR-0003: drafting and approval record

> **Historical record.** Moved from [0003-p9-result-services-keep-narrow-thread-sync.md](../decisions/0003-p9-result-services-keep-narrow-thread-sync.md) when the documentation was restructured as a developer entry point. Kept as written; not current guidance. The link check does not read this folder.

Each section below names the section of the record that the text came from. Where the record now says the same thing in current terms, the heading says "rewritten in place".

## Status line (rewritten in place)

Status: **Accepted** on 14 November 2026. See [the owner's decision](../decisions/0003-p9-result-services-keep-narrow-thread-sync.md#the-owners-decision). Baseline: main `907acd67`. It decides P9 of [ADR-0002](../decisions/0002-platform-direction-zephyr-execution-crux-services.md) and gives P8 new wording, so it **amends ADR-0002** as [What this amends](../decisions/0003-p9-result-services-keep-narrow-thread-sync.md#what-this-amends-in-adr-0002) lists. ADR-0002's body is unchanged.

## Opening paragraph (rewritten in place)

This record is documentation only. It changes no code, check, contract or protected path. Each change that it puts into effect needs its own reviewed change afterwards, listed under [Follow-up changes](../decisions/0003-p9-result-services-keep-narrow-thread-sync.md#follow-up-changes).

## Context (rewritten in place)

P9 of ADR-0002 left the internals choice open until evidence existed: keep a slim internal Crux Thread and Sync (O4), or move the services to Zephyr kernel objects (O2). It fixed the rule before the runs, and the pilot's exit criteria (D-D, section 3.5 of the pilot report, approved on 13 November 2026) refine it. The reboot pilot supplies the evidence:

- [Stage A](../research/reboot-native-sim-stage-a.md), merged, ran the 79 `reboot` cases on `native_sim` over the unchanged Crux providers: 79 cases, 0 failed.
- Stage B, `docs/research/reboot-zephyr-direct-stage-b.md` on branch `pilot-reboot-zephyr-direct` (Draft PR #399, not merged), moved the `reboot` service to `k_mutex`, `k_sem`, `k_thread` and `k_msleep`. The service core changed by 47 lines added and 31 removed, and the flake bar passed (100 of 100 runs, then 200 of 200).

Every figure here comes from those two records. They are local figures from arm64 containers. Neither record contains a CI run of the pilot suites. The only CI number in them, 268 s for the whole `host-sanitize` preset, is used as a context denominator. Nothing ran on a physical board.

Against the D-D rule, O2 needs every criterion to hold, and four fail, two of them (M4 and M6) on one suite:

- **M2.** On the same suites, the like-for-like basis that the pilot protocol fixed, Stage B's `native_sim` run takes about 19 to 23 times the Host `host-sanitize` run (Stage A, under load: about 33 and 52), against a limit of 2.
- **M4 and M6, narrowly.** The 9 checks of `sleep_provider` no longer run.
- **Blockers B1 to B4** have no owner answer.

D-D also keeps the slim shim if M2 is above 2, and on that basis it is. Dividing instead by the whole `host-sanitize` preset gives 0.19 to 0.69, but that divides different test sets, and the records do not count it as a pass. Not measured: the ThreadSanitizer suites (B4) and any physical board. Neither the Host nor the `native_sim` build has a race detector for these suites, so neither shows a data race.

## Decisions: source sentence (rewritten in place)

Reasons and alternatives are taken from the pilot records, ADR-0002 and the [platform direction review](../research/platform-direction-review.md). The recommendation was put to the maintainer, who approved it as stated ([the maintainer's decision](../decisions/0003-p9-result-services-keep-narrow-thread-sync.md#the-owners-decision)).

## Decisions: table header (rewritten in place)

| # | Recommendation | Reason | Alternative |

## Decisions: R6 (rewritten in place)

6. **R6. O5, candidate.** Moving the services' internals to a documented POSIX subset is evaluated when a Linux product is defined. Known blocker: Zephyr's POSIX layer needs picolibc or newlib, and the workspace has neither (Stage A). That evaluation must also check that the PLC runtime's timing needs are met: distinct run-time priorities and sub-millisecond waits.

## Follow-up changes: opening paragraph (rewritten in place)

None of these is part of this ADR. Each is its own reviewed change, and merging any of them needs the owner's authorisation (`AGENTS.md`, Gate 2).

## The owner's decision

#### The owner's decision

The maintainer approved recording the P9 result as O4, with the P8 wording, as recommended on 14 November 2026.

This means "I will approve as recommended". The statement that an Embedded Linux integration is expected is the owner's, from earlier in the same session. The switch to internal-only, hardware, tags and merging stay outside this approval.

## Record

#### Record

- Author: Claude Code, from the two pilot records.
- Review: this record's own independent review is in its pull request.
- Acceptance: accepted by the maintainer on 14 November 2026.
- Revision: 18 November 2026: chat quotes, relay wording and model names replaced with neutral wording, with the maintainer's approval; no decision, option, consequence or decision code changed.
