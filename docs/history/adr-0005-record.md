# ADR-0005: drafting and approval record

> **Historical record.** Moved from [0005-native-sim-test-watchdog-for-system-consumers.md](../decisions/0005-native-sim-test-watchdog-for-system-consumers.md) when the documentation was restructured as a developer entry point. Kept as written; not current guidance. The link check does not read this folder.

Each section below names the section of the record that the text came from. Where the record now says the same thing in current terms, the heading says "rewritten in place".

## Status line (rewritten in place)

Status: **Accepted** on 14 November 2026, as recommended: S1 to S9. See [the owner's decisions](../decisions/0005-native-sim-test-watchdog-for-system-consumers.md#the-owners-decisions). Baseline: `origin/main` at `5c09d431049adee62f87332968e0235bb53cc0aa`. Author: Claude Code. The maintainer asked for this while working on `crux-controller` on 14 November 2026 and approved writing this proposal, and only the proposal, with the order that the work starts after [ADR-0004](../decisions/0004-product-composition-of-management-services.md) merges (it merged as #403).

## Opening paragraph (rewritten in place)

This record is documentation only. It adds no code, check, target, header, Kconfig symbol or test seam, adopts no public interface and changes no protected path. It does not edit an accepted ADR. Every change that it puts forward would be its own reviewed change, listed under [Follow-up changes](../decisions/0005-native-sim-test-watchdog-for-system-consumers.md#follow-up-changes).

## Labels paragraph (rewritten in place)

**Labels.** **Verified** means I read it at the baseline revision and cite `path:line`, or I ran it and say how. **Reported** means a named document or note says it and I did not re-check it. **Inference** is my reading. **Not verified** means no run or read exists. No Zephyr build, `native_sim` run or hardware command ran for this record: see [What was run](../decisions/0005-native-sim-test-watchdog-for-system-consumers.md#what-was-run).

## The recommendation in brief (replaced by "In brief")

#### The recommendation in brief

- **The gap is real, and it is narrow.** On `native_sim` the System binding returns no watchdog device and reports "not running" (`components/system/ports/zephyr/system_binding.c:5-17`), so `crux_watchdog_start` returns `CRUX_ERR_NOT_SUPPORTED` and nothing can feed or miss a feed. Crux's own tests fill that seam by wrapping two private symbols at link time (`tests/system/zephyr/CMakeLists.txt:8-12`). A consumer outside this repository has no public way to do the same. This matches the Controller session's reading at its pin `37c92b18`. The System component and its tests are unchanged between that pin and the baseline (**Verified**: `git diff --stat 37c92b18 HEAD` over `components/system`, `tests/system`, `tests/system_consumer` and `zephyr/Kconfig` lists only `zephyr/Kconfig`).
- **Recommended route: A**, a `native_sim`-only simulated watchdog that the Zephyr module provides and a Kconfig symbol selects, with a configure error on any other board. It adds one Kconfig symbol (an Experimental public interface under ADR-0001 Decision 4), no header, no target and no function. The six composition rules do not apply, because no composition interface is published. **Alternative: C**, the existing `crux-watchdog0` devicetree alias widened to `native_sim` and pointed at Zephyr's own counter watchdog. It adds no Crux symbol, but Zephyr's counter watchdog refuses the setup option that the System provider always passes, so C needs a change in the shared provider (**Verified**, section 1.1) and the saving over A is small.
- **Not recommended: B**, a public test seam. It needs a new header, an INTERFACE target, a tier and a board guard, and it would make a product write a Zephyr watchdog driver. The Controller session reports that its boundary check refuses Zephyr watchdog headers in every tree (**Reported**, PR #45 body), so B is unusable for the product that asked.
- **What native_sim can show, and what it cannot.** It can show the boot cause line, a feed cycle and a missed feed that ends the simulated process or, with `CONFIG_NATIVE_SIM_REBOOT=y`, restarts it. It cannot show the `CRUX_RESET_CAUSE_WATCHDOG` bit: the simulator's reset-cause mask is power-on and software only, and a restart records software (**Verified** at the Zephyr pin, sections 1.1 and 3). It is not hardware evidence and says nothing about the board.
- **A negative test for a link-time wrap needs a guard change.** I ran the consumer guard on macOS: it refuses a wrap of the two binding symbols only by accident of their names, and accepts a wrap of `sys_reboot` or the HWINFO calls (**Verified**, section 4). Closing that is a protected-path change.
- **The reset loop is the product's to decide.** Crux System has no stop, no rearm and no boot journal by contract. The recommendation is no Crux change (section 5).
- **Nine questions** for the owner are at the end, each with a recommendation.

## Context: second paragraph (rewritten in place)

The Crux Controller product adopted the System service in its PR #45 (merge `9a906b1`, **Reported**): the reset cause is logged at boot, the watchdog is fed by the workload's producer thread, and the policy is plain C. Its issue #41 stays open because its acceptance criterion "a `native_sim` scenario shows the reset cause line and a feed cycle, and a missed feed is observed as the Crux contract describes" cannot be met by a product (**Reported**, issue #41 comment of 14 November 2026 17:15 UTC, and its findings note `docs/findings/s4-system-composition.md`, friction 1 and 7). I read all three with `gh api` and re-checked the Crux side below.

## 1.1 The Zephyr side on native_sim: opening paragraph (rewritten in place)

The manifest pins Zephyr `dccb09599635bdff17633fa7e9dab014b91dce90` (`west.yml:19`, v4.4.2). The local workspace checkout is `684c9e8f` (v4.4.0), but the pinned commit is present as a git object, so I read the files with `git show`. For `hwinfo_native.c`, `wdt_counter.c`, `reboot.c` and `reboot_bottom.c` the two revisions are identical (**Verified**, empty `git diff --stat` between them). `cpuhalt.c`, the native_sim `Kconfig`, `CMakeLists.txt`, `native_sim.dts` and the watchdog `Kconfig` were read at the pin only.

## 1.2 What the boundary guard does today (ran on macOS) (rewritten in place)

##### 1.2 What the boundary guard does today (ran on macOS)

`crux_assert_portable_consumer` (`cmake/CruxConsumerBoundary.cmake:157`) inspects a consumer target's own include directories, link options and the rest. I ran it on `app` in throwaway CMake projects in my scratchpad, with `cmake` 4.4.5 on macOS, against the unmodified guard:

| Probe | Result |
| --- | --- |
| nothing added | accepted |
| `-I components/system/include` | accepted |
| `-I components/system/ports/zephyr` | **refused** (`components/<c>/ports` pattern, `:130`) |
| `-I components/system/src` | **refused** |
| `-Wl,--wrap=crux_watchdog_zephyr_device` | **refused**, by the token `zephyr` in the name (`:125`) |
| `-Wl,--wrap=crux_watchdog_zephyr_running` | **refused**, same reason |
| `-Wl,--wrap=z_impl_hwinfo_get_reset_cause` | **accepted** |
| `-Wl,--wrap=sys_reboot` | **accepted** |

All eight are **Verified** by that run. The guard is a function that a consumer's CMake must call. Whether a given consumer calls it is the consumer's choice (Controller has its own Python check, **Reported**). So today a wrap of the seam is refused only by the spelling of two names, and the guard has no rule about link-time wraps.

## 2. Routes: recommendation paragraph (rewritten in place)

**Recommendation: A.** It is the smallest public cost that works for the product that asked, it follows three existing `native_sim`-only precedents, it needs no devicetree and no product-written driver, and it does not rest on an unverified Zephyr behaviour. It also lets the provider know at compile time that the device is a simulation, which section 3 uses. Its weak point is that Crux owns a small simulated device where Zephyr already has a counter watchdog; but that driver cannot take the provider's call sequence as it is (section 1.1), so reusing it moves the simulator's special cases into the shared provider instead of removing them. **Alternative: C**, if the owner prefers zero new Crux symbols and upstream reuse. C's first step would be a CI-only spike that shows the counter watchdog running on the native-sim counter with the changed setup options and a channel count of 1; the answer decides C. The two do not exclude each other: the A device can be dropped later if C proves out. **Against B:** it is the most public surface for the least gain, it inverts a provider getter into a product port, and the requesting product could not use it.

## 3. What the consumer scenario can show: the process restart (rewritten in place)

**The process restart.** The spec says "never call an actual machine reset" for `native_sim` tests (`:305`). A re-exec of a simulated target is not a reset of the host machine, but the sentence was written when the plan was a controlled fixture. **Inference:** the restart is within its intent. It is S4 for the owner.

## 4. Boundary test plan: where it runs (rewritten in place)

**Where it runs.** T1 and T2 in the Host job. T3 to T5 in a Zephyr shard next to the System run in `.github/workflows/zephyr.yml` (`./scripts/test-system.sh`, the runtime shard). Per the owner's rule, push the Draft PR and let CI run the Zephyr parts. No local amd64 emulation.

## 5. The possible reset loop: recommendation paragraph (rewritten in place)

**Recommendation: L1 now, and L2 with L3 as product decisions** that the Controller session records. Crux changes nothing in code. A docs-only note in the System documentation can state that arming order and loop policy belong to the product (S7). **Alternative:** L2 alone, if the owner wants no persistence in the product. On `native_sim` the scenario of section 3 shows the loop with no extra work: a fixture that never feeds restarts forever until the test timeout. The scenario must therefore tell the second boot apart (by the SOFTWARE cause) and feed on it, or the test never ends.

## Follow-up changes: opening paragraph (rewritten in place)

None is part of this record. Each needs its own reviewed change after the owner decides. Protected paths are listed so approval can be asked per change (ADR-0004 C9).

## Open questions for the owner

#### Open questions for the owner

The recommendation comes first in each row.

| # | Question | Recommendation | Reason, and the alternative |
| --- | --- | --- | --- |
| S1 | Which route: A, B, C or D? | **A** | Smallest public cost that the requesting product can use, three precedents, no unverified Zephyr fact (one simulator timing behaviour stays **Not verified**, section 3). C if you want zero new Crux symbols, accept a change in the shared provider, and accept a CI spike first. B is refused by the product's boundary check. D leaves issue #41 open |
| S2 | May Crux add one Kconfig symbol for the simulated watchdog, at tier Experimental, with a name that says `native_sim` and test only (illustrative: `CRUX_SYSTEM_NATIVE_SIM_WATCHDOG`)? | **Yes** | Adopting a Kconfig symbol is a public interface decision (ADR-0001 Decision 4). Experimental lets it change in any release with a release-note entry. Alternative: no symbol, which is route C |
| S3 | Should `watchdog_emulated` be `true` when the simulated device is bound? | **Yes, only for the A symbol** | The contract defines the field as a simulation marker, and the product could log it. No lifecycle effect, because the Zephyr probe never reports `expired` and the Zephyr feed never returns `CRUX_ERR_TIMEOUT`. It changes two existing assertions, so it needs your yes. Alternative: leave it `false` and rely on the log line and the documentation |
| S4 | Does "never call an actual machine reset" (System spec `:305`) allow the `native_sim` process restart on a missed feed? | **Yes**, recorded here and in a docs note | A re-exec of a simulated target does not reset the host. Alternative: the simulated device never restarts and only records the expiry, which cannot show "its reset" and so does not meet issue #41 |
| S5 | What does the simulated device do on expiry, and should the A symbol `select NATIVE_SIM_REBOOT`? | **Print one line, then `sys_reboot`, and select the symbol** | `sys_reboot` is the path `crux_system_reset` already uses (`system_zephyr.c:91`), so it adds no new contract; Zephyr's counter watchdog uses `LOG_PANIC()` and `sys_arch_reboot(0)` directly (`wdt_counter.c:63-66`), which is the alternative if the owner wants the panic flush. Without the select a missed feed ends the process with status 1 and the scenario has no second boot, and a product that forgets the symbol gets a surprising exit. The provider already requires `REBOOT`, so the select adds nothing else. Alternative: no select, and the product sets the symbol itself, which keeps the symbol list explicit but is easy to forget |
| S6 | Add a narrow rule to the consumer guard that refuses `--wrap` of `crux_`-prefixed symbols? | **Yes, as its own protected change** | It closes the seam by identity and does not touch a product's Zephyr wraps. Alternative: refuse every wrap (simple, may flag Crux fixtures), or leave T2 to the consumer's own check and test only the two names that the guard refuses today |
| S7 | Add a docs-only note that arming order and loop policy belong to the product? | **Yes** | It states the contract's limit and stops a future reader from looking for a Crux fix. Alternative: no note |
| S8 | Put the System scenario in ADR-0004's out-of-tree fixture mechanism, shipped first on its own? | **Yes** | One mechanism for both, and the System scenario needs no phase 0. Alternative: a separate script now |
| S9 | Approve the protected-path changes per follow-up, as ADR-0004 C9 did? | **Per follow-up** | Only follow-up 4 (the guard rule) and follow-up 5 (CI workflow) touch protected files |

## Open questions for the owner: subsection "The owner's decisions"

##### The owner's decisions

The maintainer accepted this record as recommended on 14 November 2026. The recommendation had pointed out S4 and S6 for a second look.

| # | Decision |
| --- | --- |
| S1 | Route A: a `native_sim`-only simulated watchdog that the Zephyr module provides and one Kconfig symbol selects, with a configure error on any other board |
| S2 | Crux may add one Kconfig symbol for it, at tier Experimental, with a name that says `native_sim` and test only (`CRUX_SYSTEM_NATIVE_SIM_WATCHDOG` is illustrative) |
| S3 | `watchdog_emulated` is `true` only when the simulated device of the A symbol is bound |
| S4 | The `native_sim` process restart on a missed feed is allowed under the System spec's "never call an actual machine reset" |
| S5 | The simulated device prints one line and then calls `sys_reboot`, and the A symbol selects `NATIVE_SIM_REBOOT` |
| S6 | A narrow rule in the consumer guard refuses `--wrap` of `crux_`-prefixed symbols, as its own protected-path change |
| S7 | A docs-only note says that arming order and loop policy belong to the product |
| S8 | The System scenario uses the out-of-tree fixture mechanism of ADR-0004 and ships first on its own |
| S9 | Protected-path changes are approved per follow-up change |

Accepting this record decides these questions. It changes no code, check, target, header, Kconfig symbol or protected path, and it authorises no hardware operation. Each follow-up under [Follow-up changes](../decisions/0005-native-sim-test-watchdog-for-system-consumers.md#follow-up-changes) is its own reviewed change and needs its own approval, including for every protected path that it touches ([the convention](../decisions/README.md#convention)).

## What was run

#### What was run

On native macOS, in the worktree at `5c09d431`: `git fetch`, reads of the files cited above, `grep`, `gh api` for the Controller issue, comment, PR and findings note, `gh pr list` to check that number 0005 was free, and `git show` and `git diff --stat` in the local Zephyr checkout (read-only) for the pinned revision. The consumer-guard probes of section 1.2: eight throwaway CMake projects in the session scratchpad, configured with `cmake` 4.4.5 against the unmodified guard. They changed nothing in the repository. Not run: `west`, any Zephyr build, Twister, `native_sim`, any test of the repository, any hardware command. The documentation link check is recorded in the pull request.

## Record

#### Record

| Item | Value |
| --- | --- |
| Baseline | `5c09d431049adee62f87332968e0235bb53cc0aa` |
| Skills and agents used | `ecc:architecture-decision-records` (record shape, with this repository's convention and `docs/decisions/` taking precedence over its defaults), `ecc:search-first` (search of existing seams, tests, targets and precedents before proposing anything new, with no install or dependency) |
| Independent review | The `reviewer` agent, verdict in the pull request |
| Approval | Proposal only, by the maintainer on 14 November 2026. The request came from the `crux-controller` project. This row records the approval to write the proposal only; acceptance is the next row |
| Acceptance | The maintainer, as recommended (S1 to S9), on 14 November 2026. See [the owner's decisions](../decisions/0005-native-sim-test-watchdog-for-system-consumers.md#the-owners-decisions) |
| Amendment | The owner amended the T4 row of section 4 (line 145) that read `-Wl,--wrap=sys_reboot` so that it matches S6 as merged in #434 (the row now reads `-Wl,--wrap=crux_system_probe`, any `crux_` symbol, and says that a wrap of a Zephyr symbol is accepted on purpose). Approved by the maintainer on 15 November 2026 for the row, and for recording it as a dated line here with the in-place edit, which is an exception to the rule of `docs/decisions/README.md` that an accepted record is not rewritten. Nothing else in this record changed. Pull request #442 |
| Revision | 18 November 2026: chat quotes, relay wording and model names replaced with neutral wording, with the maintainer's approval; no decision, option, consequence or decision code changed. |
