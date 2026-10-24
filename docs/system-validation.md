# System Health and Reset — software validation

Issue [#30](https://github.com/wonny-labs/crux-pilot/issues/30), approved contract
[`72e5295`](superpowers/specs/2026-10-24-system-health-design.md). This is Pilot
software qualification, not physical watchdog/reset acceptance or production
safety qualification. The Issue stays open; hardware, merge and closure require
separate human approval.

The later integration with the accepted optional/external build model is tracked
[separately](evidence/m5-system-integration/README.md). The original results below
retain their original source identity; new final-HEAD CI is reported on PR #64.

## Scope and reproducibility

Six public C17 operations in `<crux/system.h>` / `crux::system`, usable from
C++17. System reports mechanisms and observed watchdog lifecycle; application
health/readiness, freshness and recovery remain sample policy. No background
worker, stop/reconfigure, native control API, automatic feed/reset/clear or
persistent boot journal was added.

The approved design started at `main` `bf048c1` (Logging PR #61). Pinned native
sources: Zephyr `684c9e8f32e4373a21098559f748f06915f950c9`, NXP HAL
`2c2f28ac333e995d7279777409817c4b4f92c1ec`. RT1060 uses
`drivers/watchdog/wdt_mcux_imx_wdog.c` and MCUX `drivers/wdog01`, not the unrelated
`wdog` driver. Linux Dev Container: Clang/clang-tidy 18, clang-format18.1.8,
CMake3.28.4, Ninja, SDK1.0.1. Native tests use the pinned host-GNU toolchain;
EVKB cross-build is `mimxrt1060_evk@B//qspi`.

From the documented Linux west workspace, run:

```sh
scripts/bootstrap-host.sh
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
scripts/test-system.sh --host-only
scripts/check-format.sh
scripts/analyze-host.sh
scripts/check-dependencies.sh
scripts/test-system.sh
```

During implementation only affected System targets/CTest cases, consumer probes
and native gates were run. Final full Host/Zephyr validation is delegated to the
existing PR/push workflows on one stable final HEAD. No unrelated baseline
investigation or physical operations are part of these commands. Public logs normalize trailing whitespace and use
`$REPO` / `$WORKSPACE` placeholders for checkout locations.

## Evidence and acceptance coverage

| Requirement | Executed evidence | Boundary |
| --- | --- | --- |
| Lifecycle, errors, ownership, failed outputs | Common 16 + actual Linux 9 = 25/25; ASan/UBSan 25/25 | Controlled native boundary failures; actual pthread mutex and monotonic-clock wrapper, fresh process per lifecycle |
| Independent public C17/C++17 runtime | Actual Host/controller integration + independent consumer policy 2/2, sanitizer 2/2 | Separately authored runtime helpers; public-symbol doubles only test consumer choices |
| Controller policy | Time 500/501 ms and network 16000/16001 ms boundaries; failed required work withholds feed | Existing joint health/readiness remains sample-owned; no physical scheduling claim |
| Native mapping/context/error/reset/cause | 2/2 configurations, 3/3 test cases passed | Real Zephyr kernel/device dispatch and HWINFO; controlled watchdog physical effects and nonreturn reset hook |
| Ownership/configuration rejection | 10 unaffected cases passed; corrected disabled-binding case passed on rerun | Known M3/native shell/task-watchdog owners, required facilities and selected binding |
| Native-free boundary | System source rejection, invalid provider and actual facade private-include leakage tests 3/3; dependency policy passes | Bounded lexical/build rules, not proof of arbitrary third-party portability |
| Format/static analysis | Affected sources pass; no broad suppressions | Native ABI wrapping uses narrow comments where necessary |
| EVKB composition/resources | Six-operation image linked; Flash 40,848 B / RAM 7,616 B | Compile/link and configured/static resources only |
| Final hosted CI | External gate: require four SUCCESS results on final published HEAD; inspect PR checks for current status | Host PR/push and Zephyr PR/push, all four required before acceptance |

The independent runtime consumer was written separately from the platform
implementation and its expectations. C and C++ helpers retain service/cause
errors, preserve output on failed top-level observations, and feed only after
caller-reported successful work. They do not elect a supervisor, start, clear or
reset automatically.

## Static resource observation

The final selected image retains all six public functions through explicit
linker undefined-symbol roots without executing start/feed/clear/reset at boot.
Linker whole-image totals are Flash 40,848 B and RAM 7,616 B; they are not System-only
deltas or measured stack/timing guarantees. GNU size reports text 40,764 B,
data 84 B and bss 7,483 B (linker RAM includes alignment). Native mutex occupies 20 B;
common boot metadata consists of three 1 B booleans, one 1 B lifecycle enum under the
target short-enum ABI and one 4 B last-successful timeout. No Crux worker or heap
allocation was introduced. Main/ISR/idle stacks are configured 4096/2048/320 B;
target symbols allocate 4160/2112/384 B including guard/alignment. The existing
system-workqueue stack configuration is 1024 B, not an additional Crux worker.
Native selected facilities are WATCHDOG/WDT_MCUX_IMX_WDOG, HWINFO/HWINFO_MCUX_SRC
and REBOOT. Resource script checks all six linked symbols and retains config,
module identities and SHA-256 of config/devicetree/ELF for reproduction.

Raw affected logs and native configuration diagnostics are retained under
[System evidence](evidence/m5-system/). Hosted reports retain their exact CI
commit/run identity on the PR.

## TDD and corrections

The common tests failed first on the missing public header; Linux tests failed
on the missing real provider; native tests failed on the missing System Kconfig
selection. A subsequent Host assertion exposed absent cause-service clear
returning INVALID_STATE. The implemented precedence is NOT_SUPPORTED for absent
services, otherwise successful explicit cause get is required before clear.
A private pure availability query resolves this without another public API.

Independent review found that the initial Controller helper used 1000/1000 ms
freshness rather than the approved 500/16000 ms. The new threshold assertion failed
before correction; corrected Host tests independently distinguish stale Time and
network boundaries. Review also prompted explicit System source/usage negative
tests and a cross-task reset-lock assertion. Zephyr's reset child now remains
suspended in its nonreturn hook while the parent observes System: same-task
recursive-mutex entry would not demonstrate lock release.

Initial native tests returned unavailable because PRIVATE test link options on
Zephyr's static app target did not reach the final linker. Inspecting link flags
and symbols identified that fixture wiring error; PUBLIC wrapping options fix
it without production test switches. Disabling the selected target watchdog initially
failed on a missing HAL header before the intended disabled-resource diagnostic.
The negative gate caught that ordering issue; the binding now checks alias/enabled
state before including the selected HAL header. A consumer configure/compiler-setup failure
from overlapping same-output CMake invocations is retained as a development
failure, not counted as feature RED evidence.

## Independent review

[Reviewer and Challenger](evidence/m5-system/reviews.md) independently returned
PASS for source `042086b` and the inspected local software evidence. Their
material freshness, boundary-test and reset-lock evidence findings are resolved.
Subsequent review-record changes affect documentation/log whitespace only; the
implementation and tested source hashes are unchanged. Hosted CI remains a
separate final-HEAD acceptance criterion, with current results on the PR.

## Supported behaviour and native limits

- Watchdog: one activation per boot, zero-invalid delegatable logical-owner token;
  unready may retry before install, uncertain installation/setup or lost native
  ownership quarantines until reboot. No disable/rollback. The native driver
  accepts a feed; it does not acknowledge physical timing independently.
- Nominal500..64000ms /500ms resolution is the initial RT1060 provider profile;
  Linux emulation mirrors it for conformance. It is not a universal platform
  range or measured production guarantee. Native expiry may reset before status
  can observe it; only Host lazily reports EXPIRED/TIMEOUT without any real reset.
- Cause query validates native supported/reported consistency before publication,
  maps advertised unknown reasons to OTHER, preserves combined flags and never
  clears automatically. RT1060 SOFTWARE/CPU_LOCKUP may be ambiguous; latched flags
  may accumulate. Zero is not proof of no prior reset.
- The declared boot diagnostic owner must capture, retain and distribute initial
  evidence before elective clear. Only explicit successful public get opens a
  one-clear-attempt permission; status probes do not. Failed clear consumes the
  permission and success requires zero reported flags on readback. Tokens and
  this ordering are not authentication or persistent storage.
- Normal Host reset/cause services are NOT_SUPPORTED; it never resets/exits the
  process or machine. Zephyr releases metadata before native cold reboot dispatch;
  success normally does not return, but a failed native reboot can hang instead.
  Public reset never returns OK. It does not drain logging, save state or establish
  Flash/QSPI/application safe state.
- Task context after startup only; detected ISR/pre-kernel use is unsupported.
  Serialization can block. No worst-case call, feed, scheduling or fairness bound.
  No allocation, new thread or timer worker; native mutex metadata is boot-local.
- M3's existing native composition remains separate and unchanged. CRUX_SYSTEM
  rejects CRUX_CONTROLLER_M3 even with development commands disabled, WDT/HWINFO
  shell and task-watchdog hardware fallback. Unknown direct native clients are
  outside the exclusive composition contract; token values do not arbitrate them.

Previously recorded local process-exit delays remain unresolved. Successful
System runs do not establish their root cause, fix or connection to this change.
The earlier evidence remains in the existing execution/messaging/serial/logging
records. No endurance, sustained load, oscillator tolerance, reset propagation,
physical latch persistence or worst-case latency qualification is claimed.

## Minimal physical verification proposal — not authorized or performed

After separate approval, identify the exact revision-B target, probe, firmware,
partition/Flash-safe operation procedure and bounded operator window. Boot once
with a designated diagnostic owner and retain/distribute initial cause/support
flags before any explicit clear. Verify explicit clear/readback. Run the existing
100 ms supervisor observation,500 ms Time / 16 s network freshness and nominal 2000 ms
watchdog scenario; feed only on fresh successful required work with margin.
Under a separately approved reset step, withhold a required feed once, observe
actual reset and retain fresh boot evidence, noting ambiguous/multiple flags.
If software reset is separately authorized, verify it and its fresh boot report
independently. Inspect real supervisor stack headroom, timing margin and watchdog
behaviour with debugger pause considered. A missing reset or unsafe state stops
the procedure; no automatic repeat or new Flash/partition operation is implied.
