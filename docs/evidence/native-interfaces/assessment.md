# Native-free interface enforcement verification

Issue #24; initial implementation baseline main `933e974` (merged PR #37).
The latest integration section below supersedes historical #28 integration gaps. Policy and
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

## Initial results and local review — `f761910`

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

## Historical independent review follow-up — `4d9adf0`

The subsequent [published technical review](https://github.com/wonny-labs/crux-pilot/pull/39#pullrequestreview-5621370662)
returned **REVISE before merge**, identifying a threading-link detection gap,
missing STATIC-provider composition coverage and the #28 integration gate.
The initial local PASS above is historical and does not supersede that review.

- Threading RED controls reproduced 14 consumer/public requirements configuring
  successfully before the fix. Two narrow CMake regex checks now reject
  `Threads::Threads`, `pthread`, `-pthread`, `-lpthread` and representative library/
  compile/link-option forms. Existing `LINK_ONLY` traversal is unchanged.
- Real STATIC-provider tests distinguish two graphs: public export of PRIVATE
  native dependencies through `LINK_ONLY` remains rejected; native composition
  independently links a provider and a portable consumer using only the public API.
  The latter compiles and executes a pthread-backed result of 42, verifying that
  native include paths, definitions and flags are present for the provider and
  absent from consumer compilation. Public headers have their own include root.
  This tests link/compile ownership, not native Zephyr scheduling semantics.
- The [policy](../../native-interface-policy.md#required-28-integration-gate) now
  acknowledges #28's approved in-flight IPv4 profile rather than describing its
  selection as undecided. The combined revision must register exact consumer paths,
  approved header/call scope and negative controls, reconcile native provider/close
  declaration requirements, and run both suites. No global POSIX exception or
  networking implementation is added here. #25 must preserve the same composition
  ownership on its actual execution-provider graph.

Follow-up evidence identifies the source revisions separately from the initial
target artifacts above. Existing source code, provider implementation and target
configuration inputs are unchanged; only the boundary checker, tests and policy
handoff are revised. No physical operation, merge or Issue closure is performed.

The final frozen source passed [199/199 Linux build-tool tests](review-build-tests.txt),
including 12 consumer/build methods with 18 threading requirement controls,
plus dependency/formatting checks. Host CTest **42/42**, the **20-cycle native_sim
scenario** and EVKB B/QSPI guard reconfigure/build passed again; the original four
ELF/configuration hashes remained identical. See the [regression excerpt](review-regression-excerpt.txt)
and [follow-up source identities](review-verification.json). The [14 threading REDs](review-threading-red.txt)
and [static graph RED/control](review-static-red.txt) retain the pre-fix outcomes.
One intermediate full-suite snapshot was explicitly stopped when it was found to
predate the last flag variants; it is retained locally and is not counted as a pass.
The final suite records both exact checker/test hashes before running.

Fresh independent Reviewer `/root/native_followup_review` returned **PASS** after
reading the actual GitHub findings, final code and policy, independently executing
all 12 focused methods and inspecting the RED/regression evidence. This focused
re-review grants no merge or Issue-closure authority. Final-head hosted checks and
actual #25/#28 combined integration retain their distinct gates.


## Networking integration with accepted main

Integration baseline: main `df52a62c0475fe87e67bde1fa50c49090a146936`
(merged PR #38), combined with the reviewed `4d9adf0` boundary changes.
The dependency-runner conflict is resolved by retaining both the common and
Networking checks. This section supersedes the historical #28 integration gap;
original evidence above remains attributed to its original source.

The common source inventory now attaches the approved networking profile only to
its six named consumer files and retains the existing stricter Networking gate.
Positive/negative controls cover approved headers/monotonic deadlines, the same
requirements outside the profile, pthread/native calls within it, unsupported
headers/server operations, and modified or misplaced close-declaration options.
Zephyr Netif's public facade is separated from native provider registration;
STATIC LINK_ONLY traversal remains strict. Direct guards cover Host portable
objects and independently configured C/C++ consumers. Native Zephyr application
startup and fixture servers remain native composition, not direct-link portability
proof. No approved Networking API or runtime behavior is changed.

Reproduction: `python3 -m unittest discover -s tests/build -v`,
`scripts/check-dependencies.sh`, `scripts/check-format.sh`, the Host CMake/CTest
commands above, and `scripts/test-networking.sh` with the existing pinned test
Picolibc module (`01254932e8e81085817ed61fd858648584ffe37c`). The normal
networking bootstrap is unnecessary when that clean external cache already exists.
The current combined source was copied into a disposable Git snapshot for the
full suite; real networking builds used read-only source/upstream/cache mounts
with a writable build directory. Neither container had device/network/home/socket
access. A first networking invocation could not resolve the host-absolute linked
worktree Git path; mounting the same workspace read-only at that path fixed
source discovery without changing source or upstream checkouts.

Final results on the combined source, identified by
[integration source/artifact hashes](integration-verification.json):

- [Full Linux build-tool suite](integration-build-tests.txt): **210/210 PASS**,
  including 13 common-source methods and 15 consumer/CMake methods; dependency
  and formatting gates passed. [Source RED](integration-source-red.txt) and
  [CMake RED](integration-cmake-red.txt) retain the pre-fix profile/forced-header
  failures; final controls also reject tampered declaration content.
- [Actual regression evidence](integration-regression.txt): independent Host
  C17/C++17 consumers built, linked and ran; **Host CTest 71/71 PASS**; actual
  Zephyr embedded-libc network suite **3/3 PASS**; native C++ declaration probe
  compiled; **EVKB B/QSPI cross-build PASS**. No physical target was operated.
- The final native `zephyr.exe` symbol/disassembly/source checks resolve `close`
  to Zephyr `device_io.c` and `zvfs_close`, and Netif functions to the intended
  common/native provider. This complements the intermediate Zephyr link map;
  the map alone contains relocatable addresses, not final runner addresses.

Separate-context independent Reviewer `/root/network_integration_review` returned
**PASS**, finding no actionable correctness/boundary defects. The Reviewer
inspected actual source, tests, contract/policy and pinned upstream registration;
independently checked final executable symbols/disassembly/source locations and
read the full-suite, Host, native and cross-build results. These suites were
executed by the author rather than independently rerun in full. The native C++
probe is compile-only; independent Host C/C++ performs actual linking/execution.
Hosted CI on the pushed final revision remains a separate publication check.
This is an author-maintained independent-review record, not GitHub Approve.

The #28 common-boundary software integration gap is addressed. Remaining limits
include representative lexical/graph enforcement, explicit registration for new
consumers, native mixed fixture composition, and #25's future execution-graph
integration gate. No installed SDK, complete semantic portability, physical
Networking/Flash safety, PR merge or Issue closure is established.
