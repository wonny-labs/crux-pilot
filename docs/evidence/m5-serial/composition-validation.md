# Early M5 static composition validation

Scope: Issue #27's updated composition criteria, before Logging #29. Starting
PR #58 revision: `27d955fd9f697ba120f62e5593cd086ea7536008`. Main remains
`98f05104ad2bc915e4da93ab9e8732e850c140c5`. Pinned Zephyr:
`684c9e8f32e4373a21098559f748f06915f950c9`.

## Finding and bounded correction

The earlier software evidence remains valid for the public Serial contract and
runtime behaviour. It did not establish the updated static ownership requirement:
Console, Shell and UART Logging conflicts built and returned BUSY only at open.
Both independent initial source assessments returned REVISE for that reason and
for insufficient supported-resource validation. Follow-up review also found an
obsolete emulator assertion expecting runtime Console BUSY; it was removed while
preserving the readiness, unsupported-API and stale-callback checks. Static
Console rejection now belongs to the actual build-negative suite.

The immutable Serial binding now rejects known conflicting native owners during
compilation. It follows actual pinned Zephyr selection: UART Console's chosen
node, serial Shell's chosen node (including polling mode), every chosen UART
Logging list member or its Console fallback, UART_MCUMGR including raw mode,
and legacy Bluetooth selections. Enabled child consumers are unsupported,
covering the parent-UART HCI ownership model. UART logging autostart disabled does
not release ownership. A chosen node without its enabled owner is not itself a
claim. Existing EVKB CSI pin-conflict rejection remains.

Only native_sim UART emulators and the RT1060-EVKB LPUART3 candidate are
admitted to this bounded profile. Other hardware selections, including LPUART8,
are rejected rather than implying unverified pin-conflict coverage. Hardware flow control, single-wire and NXP RS-485 mode are rejected.
This is not qualification of every device of those types. Native readiness and
per-call UART parameter support remain runtime contract checks.

`CRUX_SERIAL_PROVIDER` defaults to the existing composition's provider (`host`
or `zephyr`); empty, unsupported and multiple selections are configuration errors.
No public C API, resource registry or configuration framework is added.

All Serial UART sharing remains unsupported. A dedicated UART is sufficient for
M5. Future shared combinations require explicit selection and backend/target
validation before they may be admitted; no sharing implementation is required.

## Validation

Local execution uses the documented x86_64 Linux Dev Container, CMake 3.28.4,
West 1.4.0 and clang-format 18.1.8, with the existing SDK/cache. It uses a disposable
writable clone so deliberate source-leak boundary probes can execute.

| Affected check | Result |
| --- | --- |
| Host common + actual PTY Serial | 13/13 PASS |
| Host ASan/UBSan Serial | 13/13 PASS |
| Separate Host C17/C++17/controller/stale-ACK/policy consumers | 5/5 PASS |
| Zephyr native_sim | 7/7 cases, 3/3 executed configurations PASS |
| CMake consumer/build boundary | 22/22 PASS, including empty/unknown/multiple Serial providers |
| Native-source policy regression | 16/16 PASS |
| Dependency checks | PASS, 58 portable files plus execution/networking boundaries |
| Static matrix | 15/15 intended negative rejections and 1/1 positive disabled-owner control PASS |
| RT1060-EVKB full cross-build | PASS; 42,932B Flash / 8,064B RAM, no hardware operation |
| Pinned formatting and shell/Python syntax | PASS |

See [affected execution excerpts](composition-checks.txt). The matrix covers missing/disabled aliases, unsupported provider/device/resource,
Console, polling Shell, Logging fallback/list (including non-first entry and
autostart disabled), UART child consumers, raw management, CSI, LPUART8,
hardware flow control, single-wire and RS-485. The final runner refinement additionally reran the unsupported-provider case and
a stronger successful control with Console, Shell, management and Logging chosen
to the Serial UART but their native owners disabled. Both passed, followed by the
pinned formatter. Snapshot cleanup avoids retaining an earlier configuration when
provider selection fails before generation. The configuration timeout was raised
to accommodate the observed slow cross-toolchain discovery in this container.

The [source manifest](composition-source-sha256.txt) matches all 12 changed
source/test/build files to the tested clone. Existing older manifests and hosted
results describe their original revisions, not this follow-up. Raw negative build logs are generated under
`build/serial-composition/<case>/build.log`; each expected failure must contain
the intended Serial diagnostic, rather than an unrelated DTS/Kconfig/build error.
The build checks compile the actual Serial library against generated Zephyr
configuration, without running an executable. Existing Twister suites cover
successful full-image integration and observable native_sim behaviour.

## Acceptance and support limits

The existing [Serial validation](../../serial-validation.md) and historical raw
logs cover lifecycle, timeout/partial/error semantics, Host controlled failures,
reference controller and separate C17/C++17 consumer. This continuation does not
repeat unrelated M4 or M5 qualification.

The physical RT1060 UART path, measured electrical/timing/stack behaviour and
physical coexistence remain pending. A cross-build does not satisfy those parts
of Issue #27. The Linux Host port retains the approved private bind-before-use
PTY/TTY test bootstrap; a dynamically created PTY's path/readiness is checked at
runtime. Static Zephyr resource-map evidence is not a claim of compile-time Host
TTY existence or OS-wide ownership enforcement.

## Logging #29 handoff

Reuse explicit final composition, component-owned native linkage, native-free
portable usage targets and the existing CMake boundary guard. Reuse Devicetree
chosen/alias inputs and Kconfig owner activation when validating selected native
backends. Validate the fully resolved configuration, not just a requested flag.
Keep missing/unsupported/conflicting selections as hard build errors, with
negative cases in the existing runners and a successful dedicated-resource
control. Do not integrate the Serial runtime lifecycle into Logging.

Before Logging implementation, select its first qualified backend and logical
role; define initialisation/readiness, format/data lifetime, overflow/drop,
concurrency, ISR and blocking semantics. Determine whether UART logging uses an
explicit chosen list or Console fallback and account separately for Console and
Shell ownership. Reuse the existing native diagnostic route where appropriate,
keeping Serial on its separate UART. Native Console/Shell/Logging coexistence
needs its own declared backend semantics; it does not grant Serial sharing. Sharing,
file/network routing and additional unqualified backends remain optional and
unsupported until separately justified and validated.

PR #57 remains separate: its source-tree dependency defaults, public targets,
parent test isolation and explicit final native composition are relevant patterns,
not changes to import wholesale into this Serial correction. Its documented
link-time provider omissions do not substitute for Serial's static binding checks.
General board/project binding and two-board acceptance remain M6 #32.

## Updated Issue #27 acceptance mapping

| Criterion | Evidence boundary |
| --- | --- |
| Public lifecycle, configuration, ownership, timeout, partial/error contract | Existing normative contract and Host/common tests; public API unchanged |
| Host invalid configuration, short I/O, timeout, unready/recovery | Existing controlled/common and actual PTY suites, rerun for affected validation |
| Logical Zephyr binding and RT1060 path | Static alias/provider/resource checks, native_sim and EVKB cross-build; physical UART verification still pending |
| Reference and separate native-free consumer | Existing controller and C17/C++17 runtime tests and source/build guards |
| Missing required binding | Zephyr missing and disabled alias builds must fail at the Serial binding diagnostic |
| Unsupported provider/resource | Host empty/unknown/multiple provider selections and Zephyr unknown provider/device/LPUART8 builds must fail |
| Exclusive resource conflicts | Console, polling Shell, chosen-list/fallback UART logger, raw management, enabled UART child and EVKB CSI negative builds |
| Optional sharing | No shared profile admitted; same-UART native owners fail builds, while dedicated coexistence succeeds |
| Unsupported electrical/RS-485 modes | Hardware flow-control, single-wire and NXP RS-485 selection negative builds, plus unchanged runtime parameter contract |

The static rows qualify the affected Zephyr compositions. They do not supersede
the approved Host runtime binding contract or imply acceptance of all of #27.
There is no general automatic discovery of arbitrary direct native users; those
users violate the declared composition boundary and require review.

## Retained failed attempts

- Baseline RED: the chosen-list Logging collision built successfully, so its
  expected-build-failure test failed. Empty, unknown and multiple Host provider
  selections also configured successfully and failed the new assertions. See
  [raw RED evidence](composition-red.txt).
- Initial local Host configure used an unset cache variable, resolving the
  offline FetchContent directory to `/cmake`; GoogleTest targets were missing.
  The supported cache path is `/opt/crux-cache`; the corrected invocation is
  recorded separately and does not turn the initial failure into a pass.
- An initial matrix harness used `west build -c`, which forces CMake but still
  builds the complete application. The missing-alias case failed in the app
  before the intended binding diagnostic and was correctly reported FAIL. That
  run was stopped. The runner now uses `--cmake-only` followed by the actual
  Serial-library build, which isolates the production validation boundary.
- The formatter's default `clang-format-18` executable is absent in this
  container. The configured pinned `clang-format` executable is selected with
  `CLANG_FORMAT=clang-format`; no formatter version/toolchain upgrade is made.


## Independent final assessments

Separate Reviewer and Challenger agents independently inspected the complete
source diff, actual container execution logs and support boundaries. Both returned
**PASS** for the bounded software composition gate, with no remaining material
source finding. They confirmed the matrix, Host/sanitizer/consumer, native_sim,
boundary and EVKB results rather than relying only on an author summary. Both independently rechecked the final refined provider/disabled-owner log and
all 12 manifest hashes and confirmed final PASS with no pending evidence condition. These are independent AI assessments, not
human GitHub approvals or physical/merge authority.
