# ADR-0005: How a consumer composes the Crux System service on native_sim with a test watchdog (proposal)

Status: **Proposed**. Nothing here is adopted. Baseline: `origin/main` at `5c09d431049adee62f87332968e0235bb53cc0aa`. Author: Claude Code, model `claude-sonnet-5-5` (a lower model than a design record deserves; the independent review is by the `reviewer` agent on Opus). The owner asked for this in the Crux Controller chat on 14 November 2026 ("Crux에 요청", relayed) and approved writing this proposal, and only the proposal, in the Crux Pilot main session chat on 14 November 2026 at about 17:50 UTC ("추천대로 승인할게", relayed by that session), after [ADR-0004](0004-product-composition-of-management-services.md) merged.

This record is documentation only. It adds no code, check, target, header, Kconfig symbol or test seam, adopts no public interface and changes no protected path. It does not edit an accepted ADR. Every change that it puts forward would be its own reviewed change, listed under [Follow-up changes](#follow-up-changes).

**Labels.** **Verified** means I read it at the baseline revision and cite `path:line`, or I ran it and say how. **Reported** means a named document or note says it and I did not re-check it. **Inference** is my reading. **Not verified** means no run or read exists. No Zephyr build, `native_sim` run or hardware command ran for this record: see [What was run](#what-was-run).

## The recommendation in brief

- **The gap is real, and it is narrow.** On `native_sim` the System binding returns no watchdog device and reports "not running" (`components/system/ports/zephyr/system_binding.c:5-17`), so `crux_watchdog_start` returns `CRUX_ERR_NOT_SUPPORTED` and nothing can feed or miss a feed. Crux's own tests fill that seam by wrapping two private symbols at link time (`tests/system/zephyr/CMakeLists.txt:8-12`). A consumer outside this repository has no public way to do the same. This matches the Controller session's reading at its pin `37c92b18`. The System component and its tests are unchanged between that pin and the baseline (**Verified**: `git diff --stat 37c92b18 HEAD` over `components/system`, `tests/system`, `tests/system_consumer` and `zephyr/Kconfig` lists only `zephyr/Kconfig`).
- **Recommended route: A**, a `native_sim`-only simulated watchdog that the Zephyr module provides and a Kconfig symbol selects, with a configure error on any other board. It adds one Kconfig symbol (an Experimental public interface under ADR-0001 D4), no header, no target and no function. The six composition rules do not apply, because no composition interface is published. **Alternative: C**, the existing `crux-watchdog0` devicetree alias widened to `native_sim` and pointed at Zephyr's own counter watchdog. It adds no Crux symbol but depends on facts I could not check (see section 2).
- **Not recommended: B**, a public test seam. It needs a new header, an INTERFACE target, a tier and a board guard, and it would make a product write a Zephyr watchdog driver. The Controller session reports that its boundary check refuses Zephyr watchdog headers in every tree (**Reported**, PR #45 body), so B is unusable for the product that asked.
- **What native_sim can show, and what it cannot.** It can show the boot cause line, a feed cycle and a missed feed that ends the simulated process or, with `CONFIG_NATIVE_SIM_REBOOT=y`, restarts it. It cannot show the `CRUX_RESET_CAUSE_WATCHDOG` bit: the simulator's reset-cause mask is power-on and software only, and a restart records software (**Verified** at the Zephyr pin, sections 1.1 and 3). It is not hardware evidence and says nothing about the board.
- **A negative test for a link-time wrap needs a guard change.** I ran the consumer guard on macOS: it refuses a wrap of the two binding symbols only by accident of their names, and accepts a wrap of `sys_reboot` or the HWINFO calls (**Verified**, section 4). Closing that is a protected-path change.
- **The reset loop is the product's to decide.** Crux System has no stop, no rearm and no boot journal by contract. The recommendation is no Crux change (section 5).
- **Nine questions** for the owner are at the end, each with a recommendation.

## Context

[ADR-0001](0001-application-boundary-and-api-stability.md) (D3, D4) adopted the [composition interface rules](../composition-interfaces.md) and the stability tiers. [ADR-0002](0002-platform-direction-zephyr-execution-crux-services.md) and [ADR-0003](0003-p9-result-services-keep-narrow-thread-sync.md) decided that product code uses the OS natively. ADR-0004 decided how a product composes the management services and named the System service in its scope, and its boundary-test fixture is the model here.

The Crux Controller product adopted the System service in its PR #45 (merge `9a906b1`, **Reported**): the reset cause is logged at boot, the watchdog is fed by the workload's producer thread, and the policy is plain C. Its issue #41 stays open because its acceptance criterion "a `native_sim` scenario shows the reset cause line and a feed cycle, and a missed feed is observed as the Crux contract describes" cannot be met by a product (**Reported**, issue #41 comment of 14 November 2026 17:15 UTC, and its findings note `docs/findings/s4-system-composition.md`, friction 1 and 7). I read all three with `gh api` and re-checked the Crux side below.

## 1. Current state

| # | Fact | Evidence | Status |
| --- | --- | --- | --- |
| 1 | Under `CONFIG_BOARD_NATIVE_SIM`, `crux_watchdog_zephyr_device()` returns `NULL` and `crux_watchdog_zephyr_running()` returns `false`. The comment says tests substitute a native driver object at this private binding seam | `system_binding.c:5-17` | Verified |
| 2 | The RT1060-EVKB branch needs the alias `crux-watchdog0` (`#error` at `:20`, `:22`) and pins the node to `wdog0` at `0x400b8000` (`BUILD_ASSERT` at `:28-30`). Any other board stops with `#error "Crux System unsupported Zephyr board"` | `system_binding.c:18-45` | Verified |
| 3 | A NULL device makes start, feed and the status probe return `CRUX_ERR_NOT_SUPPORTED` | `system_zephyr.c:40-46` (`watchdog_ready`), `:48-55` (probe) | Verified |
| 4 | The provider requires `CONFIG_WATCHDOG`, `CONFIG_HWINFO` and `CONFIG_REBOOT`, and refuses four other native owners, on `native_sim` too | `components/system/ports/zephyr/CMakeLists.txt:9-20` | Verified |
| 5 | `crux_watchdog_is_emulated()` returns `false` in the Zephyr provider. `refresh()` and `feed` change lifecycle on "emulated" only when the probe reports `expired`, and the Zephyr probe always reports `expired = false` | `system_zephyr.c:27-29`, `:52`, `system.c:69`, `:243` | Verified |
| 6 | Crux's own tests substitute the device by linking with `-Wl,--wrap=crux_watchdog_zephyr_device` and `--wrap=crux_watchdog_zephyr_running`, and define a fixture device with `DEVICE_DEFINE` | `tests/system/zephyr/CMakeLists.txt:8-12`, `tests/system/zephyr/src/main.c:59-68` | Verified |
| 7 | On `native_sim` the integrated test expects `watchdog_status == CRUX_ERR_NOT_SUPPORTED` and `CRUX_WATCHDOG_UNAVAILABLE`. The System consumer on `native_sim` only observes (cause is POR) and never starts the watchdog | `tests/m5_integrated/zephyr/main.c:425-431`, `tests/system_consumer/zephyr/main.c` | Verified |
| 8 | `tests/system_consumer` is a Crux-owned application in this repository, not an out-of-tree consumer | path | Verified |
| 9 | `crux-watchdog0` is named in no user-facing document. It appears in the System design spec, the binding, and tests and overlays | `grep` of tracked `*.md`, `*.overlay`, `*.c`, `*.txt`, `*.yml`, `*.py` outside `docs/evidence` | Verified |
| 10 | `<crux/system.h>` includes only `<crux/status.h>`, `<stdbool.h>` and `<stdint.h>`. It carries no `Status:` tier line, and no tier table exists yet | `components/system/include/crux/system.h:11-13`, `docs/compatibility-table.md` (a revision table, not a tier table) | Verified |
| 11 | `CONFIG_CRUX_SYSTEM` depends on `CRUX_PILOT_MODULE && MULTITHREADING` and the module adds its subdirectory under that symbol. The component links `crux_interrupt` only. System needs no frozen Thread, Sync, Queue or Serial symbol | `zephyr/Kconfig:223-228`, `zephyr/CMakeLists.txt:108-111`, `components/system/ports/zephyr/CMakeLists.txt` last two lines | Verified |

**Existing precedents for a `native_sim`-only switch.** `CRUX_FILE_SIMULATOR` depends on `BOARD_NATIVE_SIM` (`zephyr/Kconfig:230-234`). `CRUX_WEB_PLAIN_HTTP_FOR_TESTS` depends on `BOARD_NATIVE_SIM` and says "no other board can select it" (`zephyr/Kconfig:420-424`). `CRUX_SETTINGS_FILE_CONTROL` is a CMake variable that stops the configure step on any other board (`zephyr/CMakeLists.txt:142-146`). All three are **Verified** by reading.

**What the System contract says about this** (the accepted design, [System health design](../superpowers/specs/2026-10-24-system-health-design.md)):

- `native_sim` "uses the real Zephyr kernel/device watchdog dispatch with a controlled fixture device and controlled reset/fault hooks where needed", reuses the native HWINFO for the POR and SOFTWARE path, and "never call an actual machine reset" (`:301-305`).
- `EXPIRED` is observable only in the Host simulation. On real hardware an expiry can reset the processor before any status call (`:170-172`).
- No stop, no rearm, no multi-channel, and tokens do not survive a reboot (`:173-176`). There is no persistent boot journal (`:201`).
- The initial boot diagnostic owner reads the cause, keeps its copy, then may clear (`:213`).

### 1.1 The Zephyr side on `native_sim` (read at the manifest pin)

The manifest pins Zephyr `dccb09599635bdff17633fa7e9dab014b91dce90` (`west.yml:19`, v4.4.2). The local workspace checkout is `684c9e8f` (v4.4.0), but the pinned commit is present as a git object, so I read the files with `git show`. For the four files below the two revisions are identical (**Verified**, empty `git diff --stat` between them).

| Fact | File at the pin | Status |
| --- | --- | --- |
| The native HWINFO reports `RESET_POR \| RESET_SOFTWARE` as supported, starts with POR, and takes SOFTWARE only if the host environment variable `NATIVE_SIM_RESET_CAUSE` equals `SOFTWARE` (any other value falls back to POR with a warning) | `drivers/hwinfo/hwinfo_native.c:45-47`, `:85-111` | Verified |
| With `CONFIG_NATIVE_SIM_REBOOT=y`, `sys_reboot` ends in `sys_arch_reboot`, which marks "reboot on exit" and calls `posix_exit(0)`. At exit the board code sets `NATIVE_SIM_RESET_CAUSE=SOFTWARE`, prints a warning and re-executes `/proc/self/exe` with the same arguments. The symbol depends on `REBOOT`, defaults to off, and `native_sim_defconfig` does not set it | `boards/native/native_sim/Kconfig:31-34`, `CMakeLists.txt:14-17`, `reboot.c`, `reboot_bottom.c` (`maybe_reboot`) | Verified |
| **Without** that symbol, `sys_arch_reboot` is a weak stub that prints a warning and calls `posix_exit(1)`: the process ends with status 1 and does not restart. No file in this repository sets `CONFIG_NATIVE_SIM_REBOOT` | `arch/posix/core/cpuhalt.c:55-64`; `grep` of this repository | Verified |
| Zephyr has a counter-based watchdog (`zephyr,counter-watchdog`) that supports `WDT_FLAG_RESET_SOC` and calls `sys_arch_reboot(0)` from the counter alarm | `drivers/watchdog/wdt_counter.c:14`, `:63-66` | Verified |
| `native_sim` defines `counter0` with `zephyr,native-sim-counter` | `boards/native/native_sim/native_sim.dts:177-180` | Verified |
| The counter watchdog works with the native-sim counter at this pin, under the System provider's call sequence (`install_timeout` window 0 to N ms, flags `RESET_SOC`, `setup` with `WDT_OPT_PAUSE_HALTED_BY_DBG`) | no build or run | **Not verified** |
| A `native_sim` process re-exec keeps its console output in one Twister harness stream | no run | **Not verified** |

### 1.2 What the boundary guard does today (ran on macOS)

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

## 2. Routes

**Route A: a Kconfig-selected simulated watchdog in the Zephyr module.** A private source file under `components/system/ports/zephyr/`, built only when one new Kconfig symbol is set, defines a Zephyr watchdog device with `DEVICE_DEFINE`, the way the test fixture does (`tests/system/zephyr/src/main.c:59`). It implements `install_timeout` (one channel, minimum window 0, flag `RESET_SOC` only), `setup`, `feed`, and an expiry handler that prints one line and calls `sys_reboot`, as Zephyr's counter watchdog does (`wdt_counter.c:63-66`). The `native_sim` branch of the binding returns that device and its running state when the symbol is set, and `NULL` and `false` otherwise, so every existing expectation stays. The product sets the symbol in its `native_sim`-only configuration fragment. No header, target or devicetree node is added. Whether the symbol should also `select NATIVE_SIM_REBOOT`, so that an expiry restarts the process and does not end it, is S5.

*Fail closed.* A `depends on BOARD_NATIVE_SIM` line alone is not enough. On another board the symbol would be hidden, and **Reported** by ADR-0004 and by the Controller note (friction 6) that an unmet dependency only warns and the symbol becomes `n`. I did not re-check this at the pinned Zephyr. Worse, a CMake check cannot see a hidden symbol. **Inference:** declare the symbol without a board dependency, stop with `message(FATAL_ERROR)` in the component's CMake file when it is set on any board other than `native_sim` (the `CRUX_SETTINGS_FILE_CONTROL` precedent), and keep an `#error` in the binding as the second line. The negative tests of section 4 assert a non-zero exit and the symbol name in the message, whichever line fires first.

**Route B: a public test seam.** A composition interface `<crux/system_zephyr.h>` with an INTERFACE target, so a product can hand a Zephyr watchdog device to System. Two shapes: B1, publish the two existing declarations (`system_zephyr.h` today) as functions that **the product implements**; B2, a new bind function. B1 turns a module-implemented getter into a product-implemented port. That is the shape of ADR-0004 C3, which the six rules do not cover. B2 adds a function, which rule 4 does not allow without a decision (ADR-0004, A2 reading). Either way the header names `const struct device *` (rule 5 allows it), and it would have to be refused at compile time on every board but `native_sim`, so a product cannot bind a non-hardware device as its watchdog on the board. The target name must differ from the existing STATIC provider `crux_system_zephyr` (`components/system/ports/zephyr/CMakeLists.txt`), as File uses `crux_file_zephyr_composition`.

**Route C: the `crux-watchdog0` alias on `native_sim`.** The binding's `native_sim` branch uses `DT_ALIAS(crux_watchdog0)` when the node exists, and `NULL` otherwise, as the EVKB branch already does for its own node. The product's overlay declares a `zephyr,counter-watchdog` node on `counter0` and the alias, and sets `CONFIG_WDT_COUNTER=y`. No Crux symbol, no Crux device code, and Zephyr's own driver is reused (AGENTS.md prefers Zephyr-native facilities when suitable). The costs: the running state has no source (the counter watchdog has no status call), so the provider would keep a private flag set when `setup` succeeds, which loses detection of an already-running owner on the simulator; runtime behaviour of the counter watchdog on `native_sim` is **Not verified** (section 1.1); `watchdog_emulated` can only be derived from the node's compatible; and the alias, now spanning two boards, becomes a wider public interface.

**Route D: accept the gap.** The feed cycle and the missed feed stay covered by the Controller's Host test and the EVKB build. Issue #41's criterion is then amended on the Controller side. Nothing changes in Crux.

| Question | A | B | C | D |
| --- | --- | --- | --- | --- |
| New public symbol under D4 | One Kconfig symbol, **Experimental** (a test provider with no Zephyr composition outside tests; D4 lists Kconfig symbols as interface) | A header, an INTERFACE target, one or two functions, probably a symbol; tier for each | The alias meaning widens to `native_sim` (D4 lists devicetree aliases as interface); no symbol | none |
| Six rules (`composition-interfaces.md`) | **Do not apply.** No composition interface is published. Rule 2 itself says Kconfig selects providers | Apply: 1 placement and name, 2 INTERFACE target, 3 guard and negative test, 4 binds a device (B1 inverts direction, B2 adds a function), 5 `const struct device *` allowed, 6 tier. B1 is outside the rules, as C3 was | Do not apply | Do not apply |
| Fail closed on other boards | CMake `FATAL_ERROR` plus `#error` (inference above) | A compile-time refusal in the header and the binding | The existing per-board `#error` and `BUILD_ASSERT` (`system_binding.c:20-30, 44`) already refuse another node on the EVKB | n/a |
| What the product writes | One line in a `native_sim`-only fragment | A Zephyr watchdog driver in its own tree. The Controller note reports its check refuses `<zephyr/drivers/watchdog.h>` everywhere (**Reported**) | An overlay of about six lines and one fragment line | nothing |
| What Crux maintains | About one source file, plus tests | A public header, target, tier, guard rule, tests | A small binding branch, plus tests | nothing |
| Depends on an unverified fact | no | no | **yes**: the counter watchdog on the native-sim counter | no |
| Closes issue #41's criterion | yes | yes, but the product cannot use it | yes if the fact holds | no |

**Recommendation: A.** It is the smallest public cost that works for the product that asked, it follows three existing `native_sim`-only precedents, it needs no devicetree and no product-written driver, and it does not rest on an unverified Zephyr behaviour. It also lets the provider know at compile time that the device is a simulation, which section 3 uses. Its weak point is that Crux owns a small simulated driver where Zephyr already has one. **Alternative: C**, if the owner prefers zero new Crux symbols and upstream reuse. C's first step would be a CI-only spike that proves the counter watchdog under System's call sequence; the answer decides C. The two do not exclude each other: the A device can be dropped later if C proves out. **Against B:** it is the most public surface for the least gain, it inverts a provider getter into a product port, and the requesting product could not use it.

## 3. What the consumer scenario can show

The consumer is an application outside this repository that links `crux::system`, includes only `<crux/system.h>`, `<crux/status.h>` and `<crux/logging.h>`, and sets the three native facilities and the A symbol in a `native_sim` fragment. The expected observations, with route A:

| Observation | Shown on `native_sim`? | How, and the limit |
| --- | --- | --- |
| Reset cause line at boot | Yes, already today | First boot reads POR from the native HWINFO (`hwinfo_native.c:85-96`). The product reads, logs, then clears once |
| Start succeeds and the state is active | Yes | `crux_watchdog_start(2000, &token)` returns `CRUX_OK`, status shows `CRUX_WATCHDOG_ACTIVE` and `watchdog_timeout_ms == 2000`. The provider's range and step (500 to 64000 ms, 500 ms steps, `system_zephyr.c:32-37`) apply unchanged, because the validation is in the shared provider |
| A feed cycle | Yes | Repeated `crux_watchdog_feed` returns `CRUX_OK` and no restart follows within several intervals. `CRUX_OK` means native acceptance (`system.h`), so the observation is "accepted and no expiry", not hardware health |
| A missed feed and its reset | Yes. With `CONFIG_NATIVE_SIM_REBOOT=y` as a restart of the simulated process, without it as the end of the process with status 1 (section 1.1) | No feed for one interval. The simulated device prints its line and calls `sys_reboot`. The contract says a Zephyr expiry is seen as a reset, never as `EXPIRED` (`spec:170-172`), so the scenario must expect the restart or the exit, not a status value |
| The second boot | Only with `CONFIG_NATIVE_SIM_REBOOT=y` | A fresh System state (idle, token gone), and the cause line reads **SOFTWARE**, not WATCHDOG |
| The WATCHDOG cause bit | **No** | The simulator's supported mask is POR and SOFTWARE (`hwinfo_native.c:45-47`), and the restart sets SOFTWARE. The product's "warning for a watchdog reset" path stays covered by its Host test with injected causes only |
| Whether the simulation is marked as one | Needs a decision | `watchdog_emulated` is `false` in the Zephyr provider (`system_zephyr.c:27-29`). The contract defines it as "a development/test simulation" (`spec` status table). See S3 |
| Hardware timer independence, WDOG1 resolution, debug pause, flag accumulation, power loss | **No** | Nothing on `native_sim` models them. A simulated expiry runs in simulated kernel time. Whether a thread that never yields still lets the timer fire was not checked (**Not verified**) |

**A simulated watchdog is not hardware evidence.** A `native_sim` pass says the product calls System in the right order and handles the answers. It makes no claim about the RT1060-EVKB, its watchdog, its reset or its reset-cause register, and no board result may cite it.

**The process restart.** The spec says "never call an actual machine reset" for `native_sim` tests (`:305`). A re-exec of a simulated target is not a reset of the host machine, but the sentence was written when the plan was a controlled fixture. **Inference:** the restart is within its intent. It is S4 for the owner.

## 4. Boundary test plan

The model is `tests/build/test_file_zephyr_boundary.py` (a small CMake project run against the real guard and against a weakened copy, so a rejection is not vacuous) and ADR-0004's out-of-tree fixture ([ADR-0004 section 5](0004-product-composition-of-management-services.md#5-boundary-test-plan)). The fail-closed cases follow `tests/build/test_system_composition.py`, which is environment-gated and runs `west build --cmake-only` per case.

**T1, Host, no Zephyr SDK: a consumer cannot reach the private seam.** On the model of the table in section 1.2, fixed as a test: a consumer target that adds `components/system/ports/zephyr` or `components/system/src` is refused, and the public include root is accepted. This part passes at the baseline (**Verified** by the probe) and runs on macOS. It needs no change to the guard.

**T2, Host: the link-time wrap is refused.** The probes of section 1.2 show that only the two seam names are refused, by accident. A wrap of `z_impl_hwinfo_get_reset_cause` or `sys_reboot` is accepted. So a test that says "a link-time wrap of a System seam is refused" passes for two symbols and fails for the rest. Two options: (i) extend the guard with a narrow rule that refuses `-Wl,--wrap=` of any symbol that begins with `crux_` or `__wrap_crux_`, which catches the seam by identity and leaves a product's own wraps of Zephyr symbols alone; (ii) refuse every wrap on a consumer target, which is simple and may flag Crux's own fixtures (which targets call the guard was not traced: **Not verified**). The guard is a protected path, so it is a separate approved change (S6). Each case also runs against a weakened guard and must then pass, as the File test does.

**T3, native_sim, Zephyr shard: an out-of-tree consumer shows the three behaviours.** A fixture project copied outside the repository (as `tests/build/test_external_application.py:19` asserts with `assertNotIn(REPO, source.parents)`), built with `-DZEPHYR_EXTRA_MODULES=<repo>` as `scripts/test-product-template.sh` does for the template. It uses public targets only. Build assertions: every `-I` of its sources is a public include root; the generated `.config` has the A symbol, `CONFIG_BOARD_NATIVE_SIM` and `CONFIG_NATIVE_SIM_REBOOT`; the ELF contains the simulated device only when the symbol is set. Run assertions, in order, on the console: the cause line (POR), the start line, several feeds, the simulator's expiry line and the host's restart warning, then the cause line again with SOFTWARE and a fresh active state. Twister's handling of a re-exec is **Not verified**; if it does not hold, the fixture runs the binary directly and the fixture script reads its output.

**T4, negative cases that must fail** (configure or compile, with a message):

| Variant | Must fail because |
| --- | --- |
| The A symbol set for `mimxrt1060_evk@B//qspi` | the configure error of the component, or the `#error` of the binding |
| The A symbol set for another board that System does not support | the configure error, before the existing "unsupported Zephyr board" `#error` |
| The A symbol set with `CONFIG_WATCHDOG=n`, or with a refused native owner such as `WDT_SHELL` | the existing checks (`CMakeLists.txt:9-20`) still fire first |
| The fixture adds `-I components/system/ports/zephyr`, or includes `system_zephyr.h` | a private header (T1 refuses the path; without the path the header is not found) |
| The fixture adds `-Wl,--wrap=crux_watchdog_zephyr_device` | T2 (refused today by the name) |
| The fixture adds `-Wl,--wrap=sys_reboot` | T2 once the guard rule of S6 exists. Today it is accepted |
| The A symbol is set and `CONFIG_NATIVE_SIM_REBOOT` is forced off (only if S5 chooses the select) | the Kconfig assignment mismatch. If it does not fail, the run-time case below ends with status 1 and the scenario reports it |

**T5, provider absent by default.** On `native_sim` with the symbol off, the binding returns `NULL` and the System status is `CRUX_ERR_NOT_SUPPORTED` and `CRUX_WATCHDOG_UNAVAILABLE`, as `tests/m5_integrated/zephyr/main.c:425-431` expects; no simulated device appears in the ELF. This keeps every existing test unchanged (**Inference**: the symbol is off in them, and their link-time wraps of the two binding functions are untouched because they are in a separate translation unit from the callers).

**Where it runs.** T1 and T2 in the Host job. T3 to T5 in a Zephyr shard next to the System run in `.github/workflows/zephyr.yml` (`./scripts/test-system.sh`, the runtime shard). Per the owner's rule, push the Draft PR and let CI run the Zephyr parts. No local amd64 emulation.

## 5. The possible reset loop

Controller's note (friction 7, **Reported**): the watchdog is armed before the workload starts. If the workload start fails, or the producer thread exits on a clock error, nothing feeds, the board resets, and because the policy never stops the start after a watchdog reset, it arms again and keeps resetting.

What Crux guarantees, **Verified**: one activation per boot, a failed or uncertain start cannot be retried (`system.c:196-203`), and there is no stop or rearm (`system.h:22`). There is no boot journal (`spec:201`). On the RT1060 the cause flags may accumulate across resets (**Reported**, the spec, from earlier physical M3 evidence at `:19`). Nothing here ran on a board, and this record runs none.

| Option | What it does | Cost |
| --- | --- | --- |
| L1, accept and bound | Any board run is supervised and time-limited, with the operator able to cut power. The product documents the loop | No code. The loop stays possible in the field |
| L2, arm after the workload has started | A start that fails never arms | A short unprotected window at boot. It does not cover a workload that dies after arming |
| L3, a product-side loop guard | The product counts watchdog-cause boots in its own storage and, above a limit, does not arm and enters a safe state | The product needs persistence and a safe-state design. This is product policy. `native_sim` can test it, because the simulated flash disk keeps its contents across the process restart (`zephyr/Kconfig`, `CRUX_FILE_FATFS_SIMULATOR` help) |
| L4, a Crux boot journal or a disarm call | Crux breaks the loop | Contradicts the accepted contract (single activation, no stop, no journal) and adds public API. Not proposed |

**Recommendation: L1 now, and L2 with L3 as product decisions** that the Controller session records. Crux changes nothing in code. A docs-only note in the System documentation can state that arming order and loop policy belong to the product (S7). **Alternative:** L2 alone, if the owner wants no persistence in the product. On `native_sim` the scenario of section 3 shows the loop with no extra work: a fixture that never feeds restarts forever until the test timeout. The scenario must therefore tell the second boot apart (by the SOFTWARE cause) and feed on it, or the test never ends.

## 6. Interaction with ADR-0004 and the frozen headers

- **Phasing.** ADR-0004's phase 0 integrates the service cores into the module under Kconfig. System is already integrated (`zephyr/Kconfig:223`, `zephyr/CMakeLists.txt:108-111`), so this proposal does not wait for phase 0 and blocks none of phases 1a to 3. It is independent of ADR-0004's decisions C1 to C9. **Inference:** it can land as its own small change.
- **The fixture.** ADR-0004's T3 fixture is an out-of-tree `native_sim` consumer built like the product template. The System scenario should reuse that mechanism (S8). It needs none of the web, credential or Access targets, so it can ship first and ADR-0004's fixture can absorb it later.
- **Frozen headers.** `<crux/system.h>` includes none of `crux/thread.h`, `crux/sync.h`, `crux/queue.h` or `crux/serial.h` (**Verified**, section 1, row 10), and the System component needs none of their Kconfig symbols (row 11). Route A adds no header. Route B would add one, and it would have to stay free of them. The consumer scenario's own check (the template's `check_domain_includes.py --product`, **Reported** by ADR-0004) would still refuse them.
- **Related.** The reboot service reaches the System reset through `crux::system` (ADR-0004, hook table). A `native_sim` restart that works would let a later reboot scenario run there too (**Inference**; not part of this proposal).

## Follow-up changes

None is part of this record. Each needs its own reviewed change after the owner decides. Protected paths are listed so approval can be asked per change (ADR-0004 C9).

1. This record's acceptance or rejection, and the `docs/decisions/README.md` row. **Protected:** none.
2. Route A: the Kconfig symbol in `zephyr/Kconfig`, the CMake check, the private simulated device and the binding branch under `components/system/ports/zephyr/`, with T5 and the unit tests first. **Protected:** none expected (`zephyr/` and `components/` are not on the list).
3. The `watchdog_emulated` change, if S3 is accepted: it changes two existing assertions (`tests/system/zephyr/src/main.c:188` and the `m5_integrated` expectation) only for the new symbol. Needs the owner's explicit yes.
4. T1, T2 and T4 as Host and configure tests under `tests/build/` (not protected). The guard rule of S6 in `cmake/CruxConsumerBoundary.cmake` (**protected**).
5. The out-of-tree fixture and T3: new files under `tests/` and a Zephyr shard entry in `.github/workflows/zephyr.yml` (**protected**) or a new script `scripts/test-consumer-system.sh` (a new check script may be added within the task).
6. A docs note on arming order and loop policy (S7). **Protected:** none.
7. On the Controller side, a pin move to a Crux revision that holds the change, and the scenario that closes issue #41. Those are the Controller's decisions, not Crux's.

## Open questions for the owner

The recommendation comes first in each row.

| # | Question | Recommendation | Reason, and the alternative |
| --- | --- | --- | --- |
| S1 | Which route: A, B, C or D? | **A** | Smallest public cost that the requesting product can use, three precedents, no unverified fact. C if you want zero new Crux symbols and accept a CI spike first. B is refused by the product's boundary check. D leaves issue #41 open |
| S2 | May Crux add one Kconfig symbol for the simulated watchdog, at tier Experimental, with a name that says `native_sim` and test only (illustrative: `CRUX_SYSTEM_NATIVE_SIM_WATCHDOG`)? | **Yes** | Adopting a Kconfig symbol is a public interface decision (ADR-0001 D4). Experimental lets it change in any release with a release-note entry. Alternative: no symbol, which is route C |
| S3 | Should `watchdog_emulated` be `true` when the simulated device is bound? | **Yes, only for the A symbol** | The contract defines the field as a simulation marker, and the product could log it. No lifecycle effect, because the Zephyr probe never reports `expired`. It changes two existing assertions, so it needs your yes. Alternative: leave it `false` and rely on the log line and the documentation |
| S4 | Does "never call an actual machine reset" (System spec `:305`) allow the `native_sim` process restart on a missed feed? | **Yes**, recorded here and in a docs note | A re-exec of a simulated target does not reset the host. Alternative: the simulated device never restarts and only records the expiry, which cannot show "its reset" and so does not meet issue #41 |
| S5 | What does the simulated device do on expiry, and should the A symbol `select NATIVE_SIM_REBOOT`? | **Print one line, then `sys_reboot`, and select the symbol** | It matches Zephyr's own counter watchdog (`wdt_counter.c:63-66`) and needs no new contract. Without the select a missed feed ends the process with status 1 and the scenario has no second boot, and a product that forgets the symbol gets a surprising exit. The provider already requires `REBOOT`, so the select adds nothing else. Alternative: no select, and the product sets the symbol itself, which keeps the symbol list explicit but is easy to forget |
| S6 | Add a narrow rule to the consumer guard that refuses `--wrap` of `crux_`-prefixed symbols? | **Yes, as its own protected change** | It closes the seam by identity and does not touch a product's Zephyr wraps. Alternative: refuse every wrap (simple, may flag Crux fixtures), or leave T2 to the consumer's own check and test only the two names that the guard refuses today |
| S7 | Add a docs-only note that arming order and loop policy belong to the product? | **Yes** | It states the contract's limit and stops a future reader from looking for a Crux fix. Alternative: no note |
| S8 | Put the System scenario in ADR-0004's out-of-tree fixture mechanism, shipped first on its own? | **Yes** | One mechanism for both, and the System scenario needs no phase 0. Alternative: a separate script now |
| S9 | Approve the protected-path changes per follow-up, as ADR-0004 C9 did? | **Per follow-up** | Only follow-up 4 (the guard rule) and follow-up 5 (CI workflow) touch protected files |

## What was run

On native macOS, in the worktree at `5c09d431`: `git fetch`, reads of the files cited above, `grep`, `gh api` for the Controller issue, comment, PR and findings note, `gh pr list` to check that number 0005 was free, and `git show` and `git diff --stat` in the local Zephyr checkout (read-only) for the pinned revision. The consumer-guard probes of section 1.2: eight throwaway CMake projects in the session scratchpad, configured with `cmake` 4.4.5 against the unmodified guard. They changed nothing in the repository. Not run: `west`, any Zephyr build, Twister, `native_sim`, any test of the repository, any hardware command. The documentation link check is recorded in the pull request.

## Record

| Item | Value |
| --- | --- |
| Baseline | `5c09d431049adee62f87332968e0235bb53cc0aa` |
| Skills and agents used | `ecc:architecture-decision-records` (record shape, with this repository's convention and `docs/decisions/` taking precedence over its defaults), `ecc:search-first` (search of existing seams, tests, targets and precedents before proposing anything new, with no install or dependency) |
| Model | `claude-sonnet-5-5`, Claude Code |
| Independent review | The `reviewer` agent, verdict in the pull request |
| Approval | Proposal only, by the owner in the Crux Pilot main session chat on 14 November 2026 at about 17:50 UTC: "추천대로 승인할게", relayed by that session. The request came from the Crux Controller chat ("Crux에 요청"), relayed. Acceptance of this record is not given |
