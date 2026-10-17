# Crux implementation status

Last evidence update: 2026-10-17. M0–M3 software and software-only M5 checks are recorded;
both hosted CI workflows passed for clean source `df48e61`. Physical M4 acceptance now also
passes for the identified EVKB QSPI sample: retained UART pytest 5/5, target Ztest 2/2,
readiness, progression, transaction bounds, boundary/rejection and reset-session evidence.
The tested production image and test-image source identities are distinguished in the
[physical baseline](baseline.md#m4-physical-uart-and-rtc-acceptance-2026-10-17). This bounded
verification does not establish clock drift, backup-power retention or concurrent API use.

## Current platform roadmap M1

The M0–M5 tables below describe the **historical Time Pilot**. The current Platform
Development Plan uses different milestones. Its M1 readiness assessment is recorded
in [platform M1 readiness](platform-m1-readiness.md), against planning source
`7d2287bdb5a252ee5e1fc51c7142813ba37800d6`.

The follow-up for issues [#2](https://github.com/wonny-labs/crux-pilot/issues/2) and
[#3](https://github.com/wonny-labs/crux-pilot/issues/3) preserves the Pilot implementation
and adds owned-target compiler-warning controls in Zephyr. Local verification and
integration status are recorded in the [M1 exit evidence](baseline.md#platform-m1-exit-verification-2026-10-17).
M2 implementation has not started as part of this work.

## Authorised M0–M1 work

| AC | Status | Evidence / next action |
| --- | --- | --- |
| M0-01 | pass | Inspected primary repo, branch, initial uncommitted state, parent workspace, and no remote. See [baseline](baseline.md#repository-and-host-state). No existing files were overwritten. |
| M0-02 | pass | Inspected host tools, architecture, Docker resources and dependency access. First-push preparation observed 19 GiB free; this is below the advisory 30–40 GiB working-headroom target. No user files were removed. See baseline. |
| M0-03 | pass | Zephyr `v4.4.0` and example reference commits, GoogleTest `v1.15.2` commit/archive hash, installed SDK `v1.0.1`, west `1.4.0`, CMake/Ninja/clang-format/Clang identities and immutable base-image digest are recorded in [baseline](baseline.md#selected-pinned-inputs). |
| M0-04 | pass | RTC startup write/sample metadata, Shell/C++ sample, module split, RT1062 alias, RTC fields and generic counter conversion assessed in [upstream-reuse](upstream-reuse.md). DTS tracing identifies the `zephyr,rtc-counter` child under `nxp,imx-snvs-rtc` and MCUX SNVS counter provider; no build is claimed. |
| M0-05 | pass | User confirmed the physical board is MIMXRT1060-EVKB revision B, SW4 is OFF–OFF–ON–OFF, and no HyperFlash rework was performed. J-Link PLUS S/N 602003106 and UART `/dev/cu.usbmodem11102` are recorded. See [hardware assessment](baseline.md#hardware-and-range-assessment) and [QSPI download evidence](baseline.md#m4-qspi-download-and-breakpoint-verification-2026-10-17). |
| M0-06 | pass | Source-level 2000–2099 assessment, including 32-bit seconds guard and `time_t` conversion, is recorded in [baseline](baseline.md#hardware-and-range-assessment). Native simulation covers the post-2038 conversion; physical post-2038/upper-range acceptance subsequently passed under M4-05. Source inspection is not target proof. |
| M1-01 | pass | C17 libraries and a C++17 GoogleTest executable build through root CMake; actual Time API tests consume public C headers. A separate generic smoke fixture is no longer maintained. The host-only library configure also succeeds without enabling C++ or fetching GoogleTest; see [2026-10-17 architecture verification](baseline.md#architecture-boundary-cleanup-2026-10-17). |
| M1-02 | pass | `scripts/check-host.sh` configures/builds/tests on Linux with Zephyr unavailable and the pinned GoogleTest already cached. That recorded check passed 26/26 CTest cases and the host sample scenario; final review and hosted verification later passed 27/27; current evidence is in [2026-10-17 architecture verification](baseline.md#architecture-boundary-cleanup-2026-10-17). |
| M1-03 | pass | `scripts/check-zephyr-smoke.sh` passed in the pinned container with `CRUX_BOARD_TARGET=mimxrt1060_evk@B//qspi`: native_sim Hello World built, upstream Ztest/Twister passed 1/1, C++ Hello World built, and EVKB Hello World cross-compiled. No flashing or hardware operation occurred. The initially missing `cmsis_6` import and its fix are recorded in [baseline](baseline.md#current-build-policy-2026-10-16). |
| M1-04 | pass | `zephyr/module.yml`, separate `zephyr/CMakeLists.txt`/Kconfig, pinned `west.yml` (including Zephyr's pinned `cmsis_6` dependency), and explicit `ZEPHYR_EXTRA_MODULES` are in place. Module discovery was observed in native_sim and EVKB builds; the current message is `Crux Zephyr integration module discovered`; host root has no `find_package(Zephyr)`. |
| M1-05 | pass | Built pinned linux/amd64 Dev Container (latest image ID `sha256:dbbf1293a2032554bd9174c44059859e3cf8bc4f1b05b30e2fb093858b8dbbd7`). Docker CLI reproduced configured amd64, 4 GiB/2 CPU limits, parent bind to `/workspaces/crux`, primary Git root `/workspaces/crux/crux-pilot`, west topdir `/workspaces/crux`, visible sibling projects and persistent named cache volume. SDK path is set through `ZEPHYR_SDK_INSTALL_DIR`; exact mounted Git paths, including `cmsis_6`, are marked safe in the container post-create command. CMake Tools is declared as a Dev Container extension; VS Code CMake preset UI itself was not exercised. See [baseline execution evidence](baseline.md#current-build-policy-2026-10-16). |
| M1-06 | pass | Bootstrap/check scripts use `set -euo pipefail`; host checks are offline; target script only builds and contains no flash/reset call. A fake CMake exit-37 probe confirmed `scripts/bootstrap-host.sh` propagates the failure; local checks and workflow parsing passed. The first authenticated push and both hosted workflows passed (see M5-03); their downloaded reports and provenance are recorded below. |

## Authorised M2 work

| AC | Status | Evidence / next action |
| --- | --- | --- |
| M2-01 | pass | [`docs/time-contract.md`](time-contract.md) defines shared statuses, module-owned state, public Time API, private backend boundary, link selection, state transitions and error precedence. No duplicate Time error type or speculative service was added. |
| M2-02 | pass | The C++ `TimeApiTest.*` and `TimeCalendar.*` GoogleTests include public C headers and call the C17 APIs in the host test composition. The 2026-10-17 host run passed 26/26 discovered CTest cases. No per-header fixtures or dedicated link-only executable are maintained. |
| M2-03 | pass | Calendar GoogleTests cover UTC anchors, dates after 2038, 2099 bounds, leap years, malformed fields, null arguments, unchanged outputs and round trips. See `TimeCalendar.*` cases in the 28-test CTest run recorded in [M2 evidence](baseline.md#m2-time-host-implementation-verification-2026-10-16). |
| M2-04 | pass | `TimeApiTest` verifies repeated init preserves valid time, rejected writes do not reach the backend or invalidate time, and failed writes invalidate session validity. |
| M2-05 | pass | Test-only backend advance changes time by exactly five seconds. Private controls are absent from exported headers; the dependency/header check passes. |
| M2-06 | pass | The same portable command handler is exercised in GoogleTests and the host stdin/stdout process. The integration script confirms the simulation banner/help, unset status, set/get output and no natural clock progression. |
| M2-07 | pass | Temporarily injected `#include <zephyr/kernel.h>` into common Time source; `scripts/check-dependencies.sh` rejected it with exit 1. The include was removed, and the checker passed afterward. No copied production implementation or weak-symbol backend discovery is present. |
| M2-08 | pass | `scripts/analyze-host.sh` passed after removing the deleted-source reference and analyzing owned component/sample/test headers. Other selected checks remain enabled; the configuration excludes the `memcpy`/`snprintf` checker that flags bounded standard C calls and the enum-size suggestion, which cannot be applied to a C17 public enum without changing its representation. |
| M2-09 | pass | `crux::base` is an independent header-only target. The expanded dependency check covers base, public/common Time, portable commands, backend identities and shared source ownership. API no-data maps to the documented CLI `ERR NOT_SET`. |

### M1 scaffold and actual checks

- The initial M1 generic C17/C++17 build smoke fixture was removed after the real Time API GoogleTests covered C++ consumption. Root CMake now has an optional C-only library path that does not configure tests or samples.
- `git ls-remote` for Zephyr v4.4.0, example-application v4.4.0 and GoogleTest v1.15.2 succeeded. The GoogleTest archive was streamed and hashed; no repository dependency cache was populated.
- Docker Desktop is available and the pinned container, required parent workspace mount, tool identities and mount safety were verified via Docker CLI. Earlier macOS host CMake/CTest results remain historical; current CMake and Zephyr build scripts are Linux-only. Linux CMake/CTest, ASan/UBSan, clang-format, dependency checks, clang-tidy, native_sim and C++ Hello World builds passed; Twister passed 1/1, and Crux module discovery was observed in both configurations. West 1.4.0 and pinned Zephyr/CMSIS/NXP HAL checkouts are present. SDK v1.0.1 is installed.
- JSON/YAML parsing, shell syntax, whitespace and dependency-boundary checks passed. Those checks validate the scaffold, not target behavior.
- M0–M1 are complete. Board identity and QSPI configuration are confirmed, and the identified image was downloaded and debugged through J-Link. The user also reports successful guided UART operation. Formal physical acceptance remains tracked under M4; hosted workflow execution remains M5-03. Historical environment/storage observations are preserved in the baseline.

## Authorised M3 Zephyr integration

| AC | Status | Evidence / remaining limitation |
| --- | --- | --- |
| M3-01 | pass | The current common source list and sample-owned commands compile in both native_sim configurations, the native_sim sample and the EVKB sample. Both sample compilation databases show all six owned translation units use C17. See [manifest resolution and current verification](baseline.md#zephyr-manifest-blocker-resolution-2026-10-17). |
| M3-02 | pass | The backend binds `DT_ALIAS(rtc)` and calls Zephyr's standard `rtc_get_time()`/`rtc_set_time()` API. EVKB generated DTS resolves that alias to upstream SNVS/Counter RTC integration and the QSPI target build compiles. Retained physical M4 UART results now verify progression, timing bounds, range/rejection and reset-session behaviour. |
| M3-03 | pass | The standard `samples/time` Zephyr application was rebuilt from pristine native_sim and EVKB build directories with the revised module/sample composition. No copied BSP/SDK or upstream patch was required. |
| M3-04 | pass | The current C adapter compiled and its registered Shell command passed the trailing-argument rejection in both Twister configurations. A native_sim sample execution also showed explicit startup/readiness, `ERR NOT_SET`, rejection of excess arguments, successful set and advancing read-back. `CONFIG_RTC_SHELL` is disabled in the generated target config. See [current evidence](baseline.md#zephyr-manifest-blocker-resolution-2026-10-17). |
| M3-05 | pass | Current C17 Ztests passed 4/4 executed cases in 2/2 native_sim configurations, covering native status mapping, backend/command behavior, missing RTC and actual Shell execution through the dummy backend. The earlier manifest error was caused by incomplete Git trust under the manual root build user; the configured `vscode` user resolves it. Reports and diagnostic transcripts are linked in [baseline](baseline.md#zephyr-manifest-blocker-resolution-2026-10-17). |
| M3-06 | pass | Host tests cover strict timestamp syntax, invalid dates, range errors, trailing input, token overflow, stable errors and no write on rejection. The actual Shell adapter's trailing-argument response is included in M3-05 evidence. |
| M3-07 | pass | The clean-source EVKB QSPI sample build passed with C17 and board-specific RTC init priority 61. Linker allocation is 65,964 B FLASH and 8,448 B RAM. Current ELF SHA-256 is `3b75f71b…68aa84a`. This software verification did not flash; the earlier downloaded image is identified separately under M4-01. See [first-push verification](baseline.md#first-push-baseline-preparation-2026-10-17). |
| M3-08 | pass | [Current evidence](baseline.md#zephyr-manifest-blocker-resolution-2026-10-17) distinguishes passing native simulation/Shell execution, target compilation and unexecuted physical SNVS tests, and preserves the original manifest failures and their cause. |

## M4 Physical EVK verification

| AC | Status | Evidence / next action |
| --- | --- | --- |
| M4-01 | pass | Reconfirmed the connected J-Link S/N 602003106, Cortex-M7 and ~3.33 V against the previously confirmed EVKB B / QSPI setup. Downloaded hosted clean-source `df48e61` ELF `362ccac5…a9ab889`; final rom_start/text/rodata comparisons matched. See [physical evidence](baseline.md#m4-physical-uart-and-rtc-acceptance-2026-10-17). |
| M4-02 | pass | Final pytest captured readiness after reset and initial `ERR NOT_SET`, with ELF/manifest identity and verified flash log. Physical target Ztest 2/2 includes successful repeated init after valid set/read. The generic marker is associated externally with the image, not treated as a unique build ID. |
| M4-03 | pass | Four physical UART reads advanced 1792238400→1792238403 over 3.056239 monotonic host seconds, non-decreasing and within the 5-second limit. Raw RX/TX and assertions retained in `build/evidence/m4/uart-run-02/`. |
| M4-04 | pass | Seven measured set-to-get intervals 0.101427–0.110739 s; all read deltas 0 s, within the measured interval plus the fixed one-second RTC tick allowance. 34 command responses 0.050255–0.055399 s, bounded by a 2-second command timeout. This includes host serial polling, not CPU execution timing. |
| M4-05 | pass | Physical UART rejected malformed syntax, invalid calendar dates, trailing Z/arguments and 1999/2100 bounds; the last valid time continued. Post-2038 and 2099-12-31T23:59:50 set/read passed, followed by safe-date restoration. |
| M4-06 | pass | Established a valid time, issued native Commander reset/go, captured a fresh readiness marker, then observed `ERR NOT_SET`. Safe date was restored afterward. No backup-power retention claim is made. |
| M4-07 | pass | Added standard pytest/pyserial UART suite with kernel TIOCEXCL and pyserial exclusivity, explicit 2 s command / 10 s readiness / 20 s reset timeouts, new evidence directories and complete raw RX/TX/reset/result logs. Final run passed 5/5 without warnings; initial warning and failing target diagnostic runs remain retained. No implicit flash in pytest. |
| M4-08 | pass | [Physical baseline](baseline.md#m4-physical-uart-and-rtc-acceptance-2026-10-17) records board/configuration, probe/UART, tools, image/source hashes, commands, actual timing, raw logs and diagnostic failures. The current sample runs from the safe test date; drift, persistence and other boards remain outside the verified scope. |

## Software-only M5 reproduction and architecture exit review

| AC | Status | Evidence / remaining limitation |
| --- | --- | --- |
| M5-01 | pass | Latest final-review Host Debug and ASan/UBSan checks each passed 27/27 and the sample scenario. Hosted clean-checkout verification for commit `df48e61` passed Host 27/27, native_sim 4/4, and native_sim/EVKB sample builds. The review corrected a test-only successful-read injection path. See [final source review](baseline.md#final-source-review-before-publication-2026-10-17) and [hosted evidence](baseline.md#first-publication-and-hosted-ci-2026-10-17). |
| M5-02 | pass | A temporary malformed C++ source made `scripts/check-format.sh` fail with exit 1 and line diagnostics; the probe was removed, then the normal format check passed. The log is retained under ignored `build/evidence/`. |
| M5-03 | pass | First push published local history through `df48e61`. [host-foundation](https://github.com/wonny-labs/crux-pilot/actions/runs/39064900805) and [zephyr-software](https://github.com/wonny-labs/crux-pilot/actions/runs/39064901047) both completed successfully for that SHA. Both artifact bundles were downloaded: Host JUnit reports 27 tests/0 failures, and Zephyr provenance reports 4 tests/0 failures/errors/skips. All 19 recorded artifact/report hashes and sizes match. See [hosted evidence](baseline.md#first-publication-and-hosted-ci-2026-10-17). |
| M5-04 | pass | Hosted provenance identifies clean/recoverable source commit `df48e61`, 4 pinned clean dependencies, runner image identity and matching successful tests. All 19 artifact/report hashes and sizes were verified after download. Earlier mismatch/failure/invalidation probes remain recorded. The hosted ELF hash differs from the local build; BIN hashes match, without a claim of bit-identical ELF reproduction. See [hosted evidence](baseline.md#first-publication-and-hosted-ci-2026-10-17). |
| M5-05 | pass | The review supports independent C-only host use, C++ consumption, shared C17 code, private native types and proportional complexity, with documented single-instance/session/range/serialization constraints. Host-only presentation and SNVS priority now belong to their composition/board layer. Physical M4 UART pytest and on-target Ztest now support the timing/range/reset and repeated-init acceptance criteria. |
| M5-06 | pass | Hardware, external CI, complete clean reproduction and artefact provenance gaps and next actions are recorded. The manifest blocker is resolved with before/after evidence. No policy was generalised beyond this Time slice. |


## Consumer build interface follow-up (2026-10-17)

M1-04, M3-03 and M5-05 remain **pass**, with Host and Zephyr consumers now using the
same `crux::time` target. Public headers and the selected backend propagate through
that target; application code does not link internal implementation targets.
The existing module/build entry points retain backend selection responsibility.

Host Debug and ASan/UBSan each passed 26/26 tests and their sample scenario; native_sim
passed 2/2 configurations, 4/4 cases; native_sim and EVKB samples built from pristine
outputs. A temporary separate C consumer configured, linked and executed through the
public target without manual include paths or test dependencies. Format/dependency and
whitespace checks passed. One mistyped sanitizer preset was corrected; its original
error is retained. See [commands, results and limitations](baseline.md#shared-consumer-build-target-2026-10-17).
Firmware hashes are unchanged. This run used uncommitted inputs and is not a new
clean-commit acceptance bundle. Physical M4 and hosted M5-03 status are unchanged.


Module header review: M5-05 remains **pass**. Contributor rules now specify component-owned
public headers, private/native/test visibility and target-scoped dependencies. Current header
and CMake inspection found no required implementation correction; intentional sample/test
composition wiring is documented. Dependency and whitespace checks passed; no build was repeated
for this documentation-only follow-up. See [review evidence](baseline.md#module-header-and-dependency-review-2026-10-17).

Private helper naming follow-up: shortened the Zephyr helper header to
`components/time/zephyr/time_port_internal.h`; the Zephyr-qualified board `.conf` filename
remains unchanged for automatic board/SoC/flash-variant matching. After the header rename,
Twister passed 4/4, native_sim and EVKB builds passed, and dependency/whitespace checks passed.
Firmware hashes are unchanged. See [verification evidence](baseline.md#private-helper-filename-review-2026-10-17).
The post-rename format blocker is now **resolved**. The configured formatter is the installed
`clang-format` 18.1.8, not `clang-format-18`. The failure came from an absent old pathname
returned by Git during an unstaged rename. The script now skips nonexistent inputs, still
rejects malformed untracked source, and passes the repository-wide check. See the
[corrected diagnosis and verification](baseline.md#format-check-after-unstaged-renames-2026-10-17).


## Final code review (2026-10-17)

The bounded Time/platform review found and fixed one test-backend issue: a successful injected
read status previously left its output uninitialised. It now reads the stored value, with a
focused regression case. Host Debug and ASan/UBSan each passed 27/27 and their sample;
format, dependency boundaries, configured analysis and 4 provenance checks passed.
The earlier native_sim 4/4 and EVKB build results remain applicable to unchanged Zephyr code.
No unresolved source finding blocks an initial private push. M4 and hosted M5-03 remain
pending until their actual evidence is collected. See [review scope and results](baseline.md#final-source-review-before-publication-2026-10-17).


## First publication and hosted CI (2026-10-17)

Published the reviewed history to private `wonny-labs/crux-pilot/main`; remote SHA matched
`df48e61fe96e120d1e911c594b13214053fcd6c1`. Both hosted workflows passed and both artifact
bundles were inspected. M5-03 is now **pass**. Earlier entries saying hosted CI was pending
are historical. The evidence-document follow-up does not change compiled code. Physical
M4-02 through M4-08 still require their retained measurements/logs and remain **pending**.


## M4 completion (2026-10-17)

M4-01 through M4-08 are **pass** with retained physical results: sample pytest 5/5 and
on-target Ztest 2/2. The Ztest was corrected to validate public session-unset semantics
instead of assuming every native RTC returns NO_DATA before set; repeated-init preservation
was added. The production sample was restored and its final UART suite passed. See the
[full evidence and reproduced diagnostics](baseline.md#m4-physical-uart-and-rtc-acceptance-2026-10-17).
Earlier pending entries are historical. The new tests and evidence do not change production APIs.
The corrected Zephyr tests also passed native_sim 4/4; format, dependency and shell/diff
checks passed. The original raw timeout and native-backend-assumption failure are retained.
