# Crux implementation status

Last evidence update: 2026-10-17. The repository contains the M0–M3 software implementation;
software-only reproduction and architecture checks passed for the committed baseline; hosted CI
remains outstanding. The original M0–M1 handoff is historical. AC identifiers
are retained from the v0.5 implementation plan. Build commands, exact tool context, and actual
results are recorded in [baseline](baseline.md). The user authorized M4 verification on the
MIMXRT1060-EVKB with SEGGER J-Link. Download/debugging passed, and the user reports that the
guided UART scenarios worked. Full M4 acceptance still requires the specified timing, boundary,
reset and raw-log evidence. GitHub Actions execution remains pending the first push.

## Authorised M0–M1 work

| AC | Status | Evidence / next action |
| --- | --- | --- |
| M0-01 | pass | Inspected primary repo, branch, initial uncommitted state, parent workspace, and no remote. See [baseline](baseline.md#repository-and-host-state). No existing files were overwritten. |
| M0-02 | pass | Inspected host tools, architecture, Docker resources and dependency access. First-push preparation observed 19 GiB free; this is below the advisory 30–40 GiB working-headroom target. No user files were removed. See baseline. |
| M0-03 | pass | Zephyr `v4.4.0` and example reference commits, GoogleTest `v1.15.2` commit/archive hash, installed SDK `v1.0.1`, west `1.4.0`, CMake/Ninja/clang-format/Clang identities and immutable base-image digest are recorded in [baseline](baseline.md#selected-pinned-inputs). |
| M0-04 | pass | RTC startup write/sample metadata, Shell/C++ sample, module split, RT1062 alias, RTC fields and generic counter conversion assessed in [upstream-reuse](upstream-reuse.md). DTS tracing identifies the `zephyr,rtc-counter` child under `nxp,imx-snvs-rtc` and MCUX SNVS counter provider; no build is claimed. |
| M0-05 | pass | User confirmed the physical board is MIMXRT1060-EVKB revision B, SW4 is OFF–OFF–ON–OFF, and no HyperFlash rework was performed. J-Link PLUS S/N 602003106 and UART `/dev/cu.usbmodem11102` are recorded. See [hardware assessment](baseline.md#hardware-and-range-assessment) and [QSPI download evidence](baseline.md#m4-qspi-download-and-breakpoint-verification-2026-10-17). |
| M0-06 | pass | Source-level 2000–2099 assessment, including 32-bit seconds guard and `time_t` conversion, is recorded in [baseline](baseline.md#hardware-and-range-assessment). Native simulation covers the post-2038 conversion; physical upper-range acceptance remains pending under M4-05. Source inspection is not target proof. |
| M1-01 | pass | C17 libraries and a C++17 GoogleTest executable build through root CMake; actual Time API tests consume public C headers. A separate generic smoke fixture is no longer maintained. The host-only library configure also succeeds without enabling C++ or fetching GoogleTest; see [2026-10-17 architecture verification](baseline.md#architecture-boundary-cleanup-2026-10-17). |
| M1-02 | pass | `scripts/check-host.sh` configures/builds/tests on Linux with Zephyr unavailable and the pinned GoogleTest already cached. The current check passed 26/26 CTest cases and the host sample scenario; current evidence is in [2026-10-17 architecture verification](baseline.md#architecture-boundary-cleanup-2026-10-17). |
| M1-03 | pass | `scripts/check-zephyr-smoke.sh` passed in the pinned container with `CRUX_BOARD_TARGET=mimxrt1060_evk@B//qspi`: native_sim Hello World built, upstream Ztest/Twister passed 1/1, C++ Hello World built, and EVKB Hello World cross-compiled. No flashing or hardware operation occurred. The initially missing `cmsis_6` import and its fix are recorded in [baseline](baseline.md#current-build-policy-2026-10-16). |
| M1-04 | pass | `zephyr/module.yml`, separate `zephyr/CMakeLists.txt`/Kconfig, pinned `west.yml` (including Zephyr's pinned `cmsis_6` dependency), and explicit `ZEPHYR_EXTRA_MODULES` are in place. Module discovery was observed in native_sim and EVKB builds; the current message is `Crux Zephyr integration module discovered`; host root has no `find_package(Zephyr)`. |
| M1-05 | pass | Built pinned linux/amd64 Dev Container (latest image ID `sha256:dbbf1293a2032554bd9174c44059859e3cf8bc4f1b05b30e2fb093858b8dbbd7`). Docker CLI reproduced configured amd64, 4 GiB/2 CPU limits, parent bind to `/workspaces/crux`, primary Git root `/workspaces/crux/crux-pilot`, west topdir `/workspaces/crux`, visible sibling projects and persistent named cache volume. SDK path is set through `ZEPHYR_SDK_INSTALL_DIR`; exact mounted Git paths, including `cmsis_6`, are marked safe in the container post-create command. CMake Tools is declared as a Dev Container extension; VS Code CMake preset UI itself was not exercised. See [baseline execution evidence](baseline.md#current-build-policy-2026-10-16). |
| M1-06 | pass | Bootstrap/check scripts use `set -euo pipefail`; host checks are offline; target script only builds and contains no flash/reset call. A fake CMake exit-37 probe confirmed `scripts/bootstrap-host.sh` propagates the failure; local checks and workflow parsing passed. GitHub Actions execution remains unverified (see M5-03); `origin` is configured, but authenticated remote workflow evidence is not available in this checkout. |

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
| M3-02 | pass | The backend binds `DT_ALIAS(rtc)` and calls Zephyr's standard `rtc_get_time()`/`rtc_set_time()` API. EVKB generated DTS resolves that alias to upstream SNVS/Counter RTC integration and the QSPI target build compiles. The user reports successful basic SNVS-backed UART operation; formal timing, range and reset evidence remains pending under M4. |
| M3-03 | pass | The standard `samples/time` Zephyr application was rebuilt from pristine native_sim and EVKB build directories with the revised module/sample composition. No copied BSP/SDK or upstream patch was required. |
| M3-04 | pass | The current C adapter compiled and its registered Shell command passed the trailing-argument rejection in both Twister configurations. A native_sim sample execution also showed explicit startup/readiness, `ERR NOT_SET`, rejection of excess arguments, successful set and advancing read-back. `CONFIG_RTC_SHELL` is disabled in the generated target config. See [current evidence](baseline.md#zephyr-manifest-blocker-resolution-2026-10-17). |
| M3-05 | pass | Current C17 Ztests passed 4/4 executed cases in 2/2 native_sim configurations, covering native status mapping, backend/command behavior, missing RTC and actual Shell execution through the dummy backend. The earlier manifest error was caused by incomplete Git trust under the manual root build user; the configured `vscode` user resolves it. Reports and diagnostic transcripts are linked in [baseline](baseline.md#zephyr-manifest-blocker-resolution-2026-10-17). |
| M3-06 | pass | Host tests cover strict timestamp syntax, invalid dates, range errors, trailing input, token overflow, stable errors and no write on rejection. The actual Shell adapter's trailing-argument response is included in M3-05 evidence. |
| M3-07 | pass | The clean-source EVKB QSPI sample build passed with C17 and board-specific RTC init priority 61. Linker allocation is 65,964 B FLASH and 8,448 B RAM. Current ELF SHA-256 is `3b75f71b…68aa84a`. This software verification did not flash; the earlier downloaded image is identified separately under M4-01. See [first-push verification](baseline.md#first-push-baseline-preparation-2026-10-17). |
| M3-08 | pass | [Current evidence](baseline.md#zephyr-manifest-blocker-resolution-2026-10-17) distinguishes passing native simulation/Shell execution, target compilation and unexecuted physical SNVS tests, and preserves the original manifest failures and their cause. |

## M4 Physical EVK verification

| AC | Status | Evidence / next action |
| --- | --- | --- |
| M4-01 | pass | User confirmed EVKB revision B, QSPI boot switch configuration, and no HyperFlash rework. ELF SHA-256 `20f6c93d…e40a4fc` was downloaded through J-Link at 400 kHz. Actual VS Code F5 reached the startup entry; continuing reached the source breakpoint at `main.c:7`, and F10 advanced to line 9. Both `rom_start` and `text` comparisons matched. See [VS Code failure and fix](baseline.md#m4-vs-code-download-failure-and-fix-2026-10-17). |
| M4-02 | pending | User reports the guided UART commands worked, including the initial unset check. A retained boot/readiness transcript tied to the identified image is still missing; do not infer it from a debugger breakpoint. |
| M4-03 | pending | User reports successful set/get and advancing time. The prescribed monotonic-host interval and sampled progression log have not been retained. |
| M4-04 | pending | No physical transaction timing was collected. |
| M4-05 | pending | User reports rejection of the guided invalid-date command and continued valid reads. Physical post-2038, upper-range and out-of-range cases with raw results remain unverified. |
| M4-06 | pending | GDB issued a reset before downloading the image, but there was no known valid time set beforehand and no post-reset UART/API check. This does not satisfy the session-unset behavior criterion. |
| M4-07 | pending | No hardware UART pytest fixture or raw UART transaction logs exist. Retained J-Link/GDB logs verify download/debug transport, not the required UART test ownership and timeout behavior. |
| M4-08 | pending | Board setup, J-Link download/debugging and user-reported UART success are recorded. Timing bounds, automated transaction logs, range/reset cases and association with a specific tested image remain outstanding. |

## Software-only M5 reproduction and architecture exit review

| AC | Status | Evidence / remaining limitation |
| --- | --- | --- |
| M5-01 | pass | Clean host Debug and ASan/UBSan builds each passed 26/26 CTest cases and the sample scenario. Fresh Twister configurations passed 4/4 cases; native_sim sample and pristine EVKB builds passed for clean commit `bbf3d08`. The native_sim sample also passed readiness/unset, post-2038 set/get/progression and invalid-date preservation. Later changes are confined to test-report invalidation safeguards and documentation; compiled component/sample sources are unchanged. See [first-push verification](baseline.md#first-push-baseline-preparation-2026-10-17). |
| M5-02 | pass | A temporary malformed C++ source made `scripts/check-format.sh` fail with exit 1 and line diagnostics; the probe was removed, then the normal format check passed. The log is retained under ignored `build/evidence/`. |
| M5-03 | pending | Host and Zephyr workflows support push, pull request and manual dispatch with read-only contents permission. Host CI runs format, dependency, tests and analysis. Zephyr CI installs pinned inputs, runs native_sim and EVKB builds, then uploads focused reports/logs and the provenance manifest for 14 days. Actionlint and local equivalent checks/builds pass. `origin` is configured, but a successful remote Actions run has not been recorded in the evidence reviewed here; inspect both workflow runs and artifacts before marking this passed. See [CI and provenance evidence](baseline.md#ci-and-build-artifact-provenance-2026-10-17). |
| M5-04 | pass | Corrected provenance was verified against clean source commit `bbf3d08`: 19 artifact/report hashes and sizes, 4 actual pinned/clean dependency HEADs, container image identity and successful matching tests. Mismatched source/test inputs were rejected without a manifest. Incremental and clean commands invalidate earlier provenance; full test runs invalidate it before replacing reports. Dirty fingerprints explicitly do not claim source recoverability. Accepted firmware remains associated with that recorded source commit, not later documentation/tool-only commits. See [first-push verification](baseline.md#first-push-baseline-preparation-2026-10-17). |
| M5-05 | pass | The review supports independent C-only host use, C++ consumption, shared C17 code, private native types and proportional complexity, with documented single-instance/session/range/serialization constraints. Host-only presentation and SNVS priority now belong to their composition/board layer. Basic hardware operation is user-reported; formal M4 timing/range/reset acceptance remains pending. |
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
The post-rename format script remains unverified because `clang-format-18` is missing from the
active container PATH; the exact failed command and error are recorded in the baseline. This is
an environment/tool availability limitation, not a format pass.
