# ADR-0003: P9 result: services keep a narrow Crux Thread and Sync

Status: **Accepted** on 14 November 2026. See [the owner's decision](#the-owners-decision). Baseline: main `907acd67`. It decides P9 of [ADR-0002](0002-platform-direction-zephyr-execution-crux-services.md) and gives P8 new wording, so it **amends ADR-0002** as [What this amends](#what-this-amends-in-adr-0002) lists. ADR-0002's body is unchanged.

This record is documentation only. It changes no code, check, contract or protected path. Each change that it puts into effect needs its own reviewed change afterwards, listed under [Follow-up changes](#follow-up-changes).

## Context

P9 of ADR-0002 left one choice open until evidence existed: keep a slim internal Crux Thread and Sync (O4), or move the services to Zephyr kernel objects (O2). It fixed the rule before the runs. The reboot pilot supplies the evidence:

- [Stage A](../research/reboot-native-sim-stage-a.md), merged, ran the 79 `reboot` cases on `native_sim` over the unchanged Crux providers: 79 cases, 0 failed.
- Stage B, `docs/research/reboot-zephyr-direct-stage-b.md` on branch `pilot-reboot-zephyr-direct` (Draft PR #399, not merged), moved the `reboot` service to `k_mutex`, `k_sem`, `k_thread` and `k_msleep`. The service core changed by 47 lines added and 31 removed, and the flake bar passed (100 of 100 runs, then 200 of 200).

Every figure here comes from those two records. They are local figures from arm64 containers. Neither record contains a CI figure, and nothing ran on a physical board.

Against the D-D rule, O2 needs every criterion to hold, and four fail, two of them (M4 and M6) on one suite:

- **M2.** On the same suites the `native_sim` run takes about 19 to 23 times the Host `host-sanitize` run, against a limit of 2.
- **M4 and M6, narrowly.** The 9 checks of `sleep_provider` no longer run.
- **Blockers B1 to B4** have no owner answer.

D-D also keeps the slim shim if M2 is above 2, and on the like-for-like basis it is. Not measured: the ThreadSanitizer suites (B4) and any physical board. Neither the Host nor the `native_sim` build has a race detector for these suites, so neither shows a data race.

## Decisions

1. **R1. P9 result: O4.** Crux's services keep a narrow Crux Thread and Sync internally, with Zephyr and Linux providers. The services do not move to Zephyr kernel objects (O2).
2. **R2. Principle.** Product code uses the OS natively: Zephyr products call Zephyr, Linux products call Linux. Crux's services use the narrow Crux Thread and Sync. Other OS facilities (drivers, networking, storage) are used natively in adapters. This is P1 with the services' inside named.
3. **R3. Target state.** Crux Thread and Sync become internal to Crux. That switch is a later, separate pull request. It needs the owner's approval, and it comes after CODESYS, `crux-controller` and the samples have migrated. Queue and Serial are decided separately, because `cli` uses Serial. Deprecating and removing the public headers is part of that later decision. The P2 freeze stays until then.
4. **R4. Known exception.** `ssh` uses native threads.
5. **R5. P8, new wording.** An Embedded Linux integration is expected. It will be designed at the service boundary when its first product is defined. No date is set. The Linux providers remain test providers (P7) until then. The text of `README.md:5` and `:37` follows this wording in its own change.
6. **R6. O5, candidate.** Moving the services' internals to a documented POSIX subset is evaluated when a Linux product is defined. Known blocker: Zephyr's POSIX layer needs picolibc or newlib, and the workspace has neither (Stage A). That evaluation must also check that CODESYS's timing needs are met: distinct run-time priorities and sub-millisecond waits.

## What this amends in ADR-0002

- **P8** is replaced by R5. ADR-0002 words it as "not planned for the foreseeable future".
- **P9** is decided by R1 and R3. Its deprecation rule, which depended on Build B', is not applied here. Deprecation and removal are part of R3's later decision.
- **P7** is kept, and R5 repeats it.
- **P1 to P6 and P10** are kept. P2's freeze stays.

## Consequences

- Product code keeps the one rule of P1 under O4, and R2 states it for both OSes.
- `crux-controller` keeps working. Its threads and queue stay on the frozen headers until it migrates.
- Crux's Thread and Sync providers keep their Host and Zephyr tests. No service is rewritten.
- Embedded Linux is expected but not scheduled, so nothing about it is built or promised now.

## Follow-up changes

None of these is part of this ADR. Each is its own reviewed change, and merging any of them needs the owner's authorisation (`AGENTS.md`, Gate 2).

1. Documentation that still says P9 is undecided or that Embedded Linux is not planned: `README.md:5,37`, `docs/architecture.md`, `docs/thread-sync.md` (its freeze notice and the remark at `:150` that workers may become native threads under P9), and the freeze notices of `docs/messaging-contract.md` and `docs/serial-contract.md`.
2. The switch that makes Thread and Sync internal, with the decision for Queue and Serial. It needs the owner's approval and follows the migrations in R3.
3. The O5 evaluation, when a Linux product is defined.

## The owner's decision

The owner approved recording the P9 result as O4, with the P8 wording, as recommended in the Crux Pilot main session chat on 14 November 2026 at 09:06:27 UTC. The time comes from the session transcript, and the owner's time zone was not recorded. The owner replied:

> 추천대로 승인할게

This means "I will approve as recommended". The statement that an Embedded Linux integration is expected is the owner's, from earlier in the same session. The switch to internal-only, hardware, tags and merging stay outside this approval.

## Record

- Author: Claude Code, model `claude-sonnet-5-5`, from the two pilot records.
- Review: this record's own independent review is in its pull request.
- Acceptance: recorded from the owner's words as the Crux Pilot main session relayed them.
