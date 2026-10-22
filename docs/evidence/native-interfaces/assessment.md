# Native-free interface enforcement verification

Issue #24; implementation baseline main `933e974` (merged PR #37). Policy and
migration map: [native-free consumer boundary](../../native-interface-policy.md).
This records software checks, not hardware behavior or adoption of future APIs.

| Acceptance criterion | Result and evidence boundary |
| --- | --- |
| 1. Dependency policy | Policy defines firmware/common-runtime/shared-contract/native-provider roles and current/future source inventory. Proposed for human acceptance with this PR; no claim of prior adoption or migration of every native sample. |
| 2. Automated rejection and valid providers | Source gate checks representative native headers/types/calls/environment selection and relative provider/private header paths. CMake checks public graphs and designated consumer direct requirements. Negative native links/includes, injected direct links and unsupported expressions fail; controlled private native providers pass. |
| 3. Independent C/C++ consumption | Separate CMake project builds and runs C17/C++17 consumers against only `crux::base`/`crux::time`, including Time/calendar calls. It does not use GoogleTest, private include paths or reference-controller targets. Actual Zephyr module facade removes native exports while preserving image registration and backend conflict rejection. |
| 4. Owned versus standard APIs | ISO C facilities remain permitted with no new runtime/timing/threading guarantees. No consumer-wide POSIX subset is qualified. Existing native POSIX probes/providers do not confer consumer approval. |
| 5. Reviewed migration map | Policy maps existing controller thread/clock/GPIO/network/watchdog/log/diagnostic calls to the existing capability issues and native ownership. No controller source or historical physical acceptance record is migrated or rewritten here. |

## Reproduction

The public policy documents source, build-tool and independent consumer commands.
Tests are discovered by existing Host CI; no new CI job or dependency is added.
Use the existing Linux amd64 development image, CMake 3.28.4 and cached dependencies.

Host verification configured with `cmake -S "$REPO" -B "$OUTPUT/host" -G Ninja
-DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_FULLY_DISCONNECTED=ON
-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$CACHE/cmake/googletest-src"`, then
`cmake --build "$OUTPUT/host"` and
`ctest --test-dir "$OUTPUT/host" --output-on-failure`.

For Zephyr, use pinned Zephyr `684c9e8f32e4373a21098559f748f06915f950c9`,
HAL NXP `2c2f28ac333e995d7279777409817c4b4f92c1ec`, SDK 1.0.1/GNU Arm 14.3.0.
Build the existing `samples/controller/zephyr` twice with
`west build -b native_sim/native/64 -d "$OUTPUT/controller-native" "$REPO/samples/controller/zephyr" -- "-DZEPHYR_MODULES=$REPO;$WORKSPACE/modules/hal/nxp;$WORKSPACE/modules/hal/cmsis;$WORKSPACE/modules/hal/cmsis_6"`
and the same command with `-b 'mimxrt1060_evk@B//qspi'` and a separate
`controller-evkb` output. Explicit modules select this worktree instead of the
manifest's primary checkout. Run
`python3 scripts/check-controller.py "$OUTPUT/controller-native/zephyr/zephyr.exe" --native-sim --log "$OUTPUT/controller-native/scenario.log"`.
The EVKB command builds only, without device access or programming tools.

Containers had no network/device/home/socket access; upstream/SDK mounts were
read-only. Build-tool tests used a disposable writable source snapshot with a local
Git repository because dependency probes create/delete temporary test sources.
Original logs remain local. Committed selected logs identify their excerpt/path
normalisation where applicable.

## Failures, scope and review

TDD source RED/GREEN output is retained locally; the selected RED excerpt and
final source tests retain representative failures and successful controls.
The implementation agent observed the real Zephyr facade RED on
`crux_time_api -> zephyr_interface` before the fix. A separately retained reproduction
restores baseline `933e974`'s Zephyr CMake in a temporary source copy and confirms
the new facade test fails on that same native edge. This is labelled as a later
RED reproduction, not a fabricated original transcript. Direct-link-injection RED
was observed in the implementation transcript; final automated negatives exercise it.

Unsupported macOS full-suite attempts encountered existing provenance `/var` versus
`/private/var` path errors and Linux-only command guards; no unrelated portability
fix was made. A first read-only container west invocation did not find build
extensions because Git rejected Zephyr checkout ownership. Read-only diagnosis
identified `safe.directory`; only the disposable container's Git config was set
for the mounted Zephyr checkout, then the same build resumed. The pinned source
was not modified. The board DTS still reports its upstream USB unit-address warning.

The lexical gate is representative, not semantic C/C++ analysis. The CMake guard
checks supported source-tree expressions and known native/private requirement
families; arbitrary aliases, encoded flags, recursive source includes and installed
SDK exports still need review. Existing native controller fixtures remain native.
New consumer targets must register their role and checks; a directory rename is
not evidence of portability. No universal HAL, POSIX profile, new capability API,
physical guarantee or closed Issue is implied by these tests.

## Final results and independent review

- [Linux build-tool suite](build-tests.txt): **196/196 passed**, including 11 source
  policy methods and 9 consumer/build-interface methods; no skips. The independent
  C/C++ fixture was compiled, linked and executed by these tests.
- [Host configure/build/CTest](host.txt): **42/42 passed**. Dependency and formatting
  checks passed in the final source snapshot.
- [Zephyr excerpt](zephyr-excerpt.txt): **native_sim 20-cycle scenario passed**, with
  unset/valid UTC and backward UTC change; **EVKB B/QSPI cross-build passed**.
  No target was programmed or operated. [Verification identities](verification.json)
  record final source and ELF/configuration hashes; logs trim trailing whitespace
  and replace the absolute source workspace path with `<repo>` where present.

Separate-context Reviewer `/root/native_independent_review` returned **PASS** with
no remaining material findings. The Reviewer independently fetched Issue #24,
inspected actual source/policy/migration/evidence, reran 11 source tests, 9
consumer/build tests and the dependency gate, and verified all recorded source and
Zephyr artifact/configuration hashes. Full-suite/Host/native_sim/EVKB results were
inspected as retained evidence rather than independently re-executed in full.
This author-maintained summary is not a GitHub Approve event or new authority.

All five ACs are supported by the proposed implementation and reviewed policy/map,
subject to human acceptance of the PR. Issue #24 remains open. No hardware, merge,
Issue closure or dependent capability implementation is authorised by this PASS.
