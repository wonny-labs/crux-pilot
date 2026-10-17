# Crux Initial Implementation Plan

| Item | Baseline |
| --- | --- |
| Version | 0.5 |
| Date | 17 October 2026 |
| Implementation repository | `crux-pilot` |
| Executable example | `samples/time` |
| Purpose | Independent architecture and engineering validation |
| Status | v0.4 baseline updated to match verified M0–M3 software implementation and the current M5 review; M4 hardware evidence remains pending |

Version 0.5 retains the v0.4 milestone and AC identifiers. It records implementation evidence that
the library-only host build must not require tests, samples belong outside the Zephyr module, CMake
must reject conflicting backend targets, and actual Shell parsing must preserve the shared command
error contract. The separate foundation smoke fixture was removed after the real C++ Time tests
covered the C17 consumer path.

## 1. Objective and completion states

Build the smallest useful Crux slice that connects a portable Time capability to an NXP RT1060 EVK's RT1062 internal SNVS RTC, with a C example, C++ API consumer verification and repeatable checks. Use existing Zephyr infrastructure and official examples before adding Crux-specific mechanisms.

The slice must demonstrate independent host execution, shared production source, explicit native integration, C and C++ interoperability, meaningful failure tests and identifiable target evidence. It does not establish production readiness or prove a future Embedded Linux implementation.

Report three separate states: **host foundation complete**, **Zephyr software integration complete**, and **physical-target validation complete**. Overall completion requires all three and the final review. Missing hardware is a pending milestone, never a passed test.

This file is a specification, not a record of completed builds or tests. Creating this file does not create a GitHub repository or authorise physical execution.

### Current execution boundary

The checkout is `/Users/jiwonkim/Projects/crux-workspace/crux-pilot`; the parent `crux-workspace`
is a west workspace, not a Git repository. The M0–M1 text in
`docs/plans/codex-start-instructions.md` is a historical handoff. `AGENTS.md` and
`docs/implementation-status.md` identify the active software scope and execution boundaries.

## 2. Scope and fixed choices

### Included

- One Git repository named `crux-pilot`; one coherent source tree for components, examples and tests.
- One Time capability, one build-selected backend per executable, and one RTC instance.
- C17 production and example code, with C++17 API consumers covered by compile/link tests.
- Native Linux host composition, Zephyr `native_sim` integration, EVK firmware build and controlled hardware testing.
- VS Code Dev Container, pinned dependencies, GoogleTest/CTest, Ztest/Twister, pytest, formatting, focused analysis and CI.
- A standard Zephyr application at `samples/time` and a minimal host composition of the same command behaviour.
- UTC calendar conversion and a bounded human-readable CLI.

### Excluded

No custom AI Harness, runner framework, agent orchestration, generic HAL/OSAL, service registry, dynamic backend discovery, C++ wrapper framework, multi-device abstraction, external RTC, Abingdon BSP, CODESYS, networking, SFTP, configuration service, secure boot, release system, alarm, wake-up, clock synchronisation, local time, DST, leap seconds, milliseconds, drift qualification or power-loss retention guarantee.

Narrow adapters or missing driver support may be proposed when evidence shows they are necessary. An upstream driver defect or missing hardware feature is a bounded investigation, not permission to implement a broad replacement.

### Language and runtime

- Compile owned `.c` production files as C17, including when linked into C++ tests.
- Compile owned C++ production and test files as C++17. Select a compatible fixed GoogleTest revision.
- Public C headers use C++ linkage guards for C functions; C++ API use is verified by consumers that
  compile and link against the C17 implementation.
- Disable exceptions and RTTI for the initial Zephyr production application. Apply options to owned production targets, not globally to GoogleTest or external code.
- The Time component and CLI require no dynamic allocation. Prefer automatic or explicitly owned storage and bounded buffers.
- Initialise services explicitly from the application entry point. Do not operate hardware from global C++ constructors or destructor assumptions.
- Any standard-library/runtime dependency is explicit and verified on target. Host tests do not prove target C++ runtime support.

Crux supports C and C++ as implementation languages. The first C17 Time component does not establish a permanent rule that all Core code must be C or that every service needs both C and C++ APIs.

## 3. Upstream reuse strategy

Use references from the selected Zephyr baseline rather than copying moving `main` versions. Record the source path, revision, licence and purpose of adapted code in `docs/upstream-reuse.md`; preserve required notices.

| Reference | Reuse or adapt | Do not import automatically |
| --- | --- | --- |
| Official `example-application` | Manifest repository pattern, out-of-tree module integration, standard application layout, minimal CI patterns | Custom board, driver, runner, west extension, documentation toolchain or unrelated sample library |
| `samples/drivers/rtc` | Device readiness, RTC alias, standard set/get API usage and display conventions | Its fixed startup timestamp or assumption that its tested-board list covers RT1060 |
| `samples/subsys/shell/shell_module` | Standard command registration, argument handling, help and UART shell transport | Another shell engine, dynamic command framework, login subsystem or unrelated commands |
| `samples/cpp/hello_world` | Selected-baseline C++ configuration and target smoke test | A broad C++ runtime configuration without a demonstrated requirement |
| Ztest and pytest examples | Small harness and test metadata patterns supported by the selected revision | A parallel custom test runner |
| Upstream RTC, Counter, SNVS driver and vendor modules | The existing hardware implementation and controlled module dependencies | Full NXP SDK import or duplicate register-level RTC implementation |

Important source findings: the v4.4.0 RTC sample writes a fixed date at startup, which conflicts with Crux's no-implicit-set requirement. Its `sample.yaml` does not list RT1060 EVK among allowed boards. This does not prove the board is unsupported; it means RT1060 readiness requires a separately configured build and hardware result. Do not modify upstream test metadata merely to claim upstream coverage.

The example-application root CMake file is a Zephyr module entry point. Crux instead reserves its root CMake entry point for independent host builds and uses `zephyr/CMakeLists.txt` as its Zephyr module entry point.

Reuse Zephyr Shell transport and registration, but retain a small Crux command adapter: directly enabling the native RTC shell would bypass the Crux API and cannot validate Crux's contract. Disable conflicting direct RTC write commands in the acceptance configuration.

## 4. Architecture and ownership

```text
Portable C17 Time command handler
             |
             v
Crux C17 Time API, range/validity rules and UTC conversion
             |
             v
Private Time port: exactly one link-selected implementation
       /                                  \
Host controlled clock               Zephyr RTC backend
                                           |
                                    Zephyr RTC/Counter
                                           |
                                    SNVS driver + NXP HAL

Host stdin/stdout adapter and Zephyr Shell adapter select the same
portable command handler. Native headers stay inside native adapters.
```

### Build boundaries

| Target responsibility | Dependencies and restrictions |
| --- | --- |
| Time common implementation | Standard C headers and its public/private Time headers; no Zephyr, SDK, board, GoogleTest or shell dependencies |
| UTC conversion | Portable C17 code owned by Time; deterministic conversion without local timezone or native OS types |
| Host backend | Private Time port and controlled clock state; no claim of production Linux behaviour |
| Zephyr backend | Private Time port, Zephyr RTC/device APIs and native conversion structures; no native types in exported headers |
| Portable commands | Crux public Time/calendar API and bounded C17 code; no Zephyr shell or host stream types |
| Host composition | Command handler, host backend and standard process I/O |
| Zephyr composition | Command handler, Zephyr backend, standard shell bridge, app Kconfig and board overlays |

Use target names such as `crux_time_common`, `crux_time_host` and `crux_time_zephyr`. Export a
consumer target `crux::time`. `components/time/sources.cmake` owns the small shared-source list used
by host, tests and Zephyr. The Zephyr module owns only base/Time integration; the sample owns the
portable command handler and Shell bridge. CMake compatible-interface metadata rejects a consumer
that combines different backend providers. Do not hide selection in weak symbols.

The private port has only initialise, read-instant and write-instant operations. It uses Crux-owned instant/status types. Select implementations through CMake for host and Crux module Kconfig for Zephyr. No public function table, backend registration API or runtime selection is needed.

Common code owns the initialised/valid state. The host backend owns controlled clock storage and test-only advance/failure controls. The Zephyr backend owns device acquisition and errno-to-Crux status mapping. Applications serialise access; concurrent calls, ISR use and re-entrancy are outside this slice. Do not add a global mutex abstraction to satisfy future scenarios.

### Common results and module-owned semantics

Use `crux_status_t` as the default result type for Crux operations, including future service modules where its meanings are sufficient. Define it in `<crux/status.h>`, owned by a minimal `components/base` component. The initial named enum has exactly these values:

| Constant | Value | Meaning |
| --- | --- | --- |
| `CRUX_OK` | 0 | Operation succeeded |
| `CRUX_ERR_INVALID_ARGUMENT` | -1 | Invalid pointer, syntax or field combination |
| `CRUX_ERR_OUT_OF_RANGE` | -2 | Well-formed value outside the supported interval |
| `CRUX_ERR_NOT_READY` | -3 | Required initialisation/readiness has not been established |
| `CRUX_ERR_NO_DATA` | -4 | No valid data is available under the API contract |
| `CRUX_ERR_IO` | -5 | Backend I/O operation failed |

These are Crux-defined values, not errno numbers. Document each API's supported subset and error precedence; do not pass arbitrary native errors through this type. Keep these assigned values stable when extending the common set. Add common codes only for demonstrated, genuinely shared semantics. Do not add speculative codes or a generic catch-all to avoid defining a meaningful contract.

The base component exports a header-only CMake INTERFACE target `crux::base` (backed by `crux_base`). Its public header has no Zephyr, SDK, logging, allocation, service-lifecycle or runtime-library dependency. Time exports this usage requirement to consumers. Common status definitions must not include module headers. Do not create a broader base framework merely to house this header.

Service-specific data, current states and recovery-relevant results belong to the corresponding module. A future network connection state or transfer result must not be added to the common enum merely because a service needs it. Introduce a module-owned error/result type or an explicit detail output only when the caller needs distinctions the common set cannot express. Do not cast unrelated module enums to `crux_status_t` or extend its numeric range through undocumented module codes. Do not duplicate the common codes in per-module aliases such as `crux_time_status_t`.

C APIs normally use the common result plus output arguments for returned data. C++ services may expose module-owned value/result types when useful; C++17 support does not justify implementing a generic Result/expected framework now. Module-specific return shapes must document success, failure and unchanged-output behaviour. No future service API or public Time-state query is required by this pilot.

Map native results at the backend boundary. For the initial Zephyr RTC path, `-ENODATA` maps to `CRUX_ERR_NO_DATA`, invalid native input to `CRUX_ERR_INVALID_ARGUMENT`, native range failure to `CRUX_ERR_OUT_OF_RANGE`, and other operational failures to `CRUX_ERR_IO` unless the documented API contract requires a more specific distinction. Device readiness failures map to `CRUX_ERR_NOT_READY`. Record the actual mapping for the selected baseline and test it; preserve available native error context in bounded diagnostics without exposing native types in the public API. A newly discovered recovery-relevant failure must trigger contract review rather than silent collapse into `IO_ERROR`. Do not add a global last-error store, numeric module-group registry or error-reporting framework.

### Public and private interfaces

`<crux/time.h>` exports an `int64_t` Unix-seconds instant and initialisation/get/set operations returning the common `crux_status_t` from `<crux/status.h>`. `<crux/time_calendar.h>` exports a small UTC calendar structure and conversion to/from the instant. Calendar fields are ordinary year, month 1–12, day 1–31, hour 0–23, minute 0–59 and second 0–59; they must not expose `struct tm` offsets or timezone state.

Use these initial operations, with namespaced types documented in the contract:

```c
crux_status_t crux_time_init(void);
crux_status_t crux_time_get(crux_time_instant_t *out);
crux_status_t crux_time_set(crux_time_instant_t value);
crux_status_t crux_time_from_utc(const crux_time_utc_t *in,
                                      crux_time_instant_t *out);
crux_status_t crux_time_to_utc(crux_time_instant_t in,
                                    crux_time_utc_t *out);
```

The conversion functions are included to support the agreed calendar CLI and avoid duplicated host/Zephyr conversion semantics. Implement a bounded, reviewed conversion or reuse suitable permissively licensed portable code. Do not introduce a general date/time dependency or timezone framework. The application owns string parsing/formatting and calls these public conversion functions; it must not include Time private headers.

All test-only controls use private include paths. Public declarations describe source compatibility within the selected toolchain baseline; cross-compiler binary ABI stability is not promised.

## 5. Time and CLI contracts

### Time semantics

- UTC only, one-second resolution, no leap seconds.
- Pilot range: 2000-01-01T00:00:00 inclusive to 2100-01-01T00:00:00 exclusive. Check post-2038 representation through the selected native path during readiness. If unsupported, report it before accepting target implementation; do not narrow the range silently.
- Public APIs use the six common status constants defined in Section 4. `CRUX_ERR_NO_DATA` means that no valid time is established for the current session. Native errno values remain private.
- Invalid pointer, malformed calendar and invalid field combinations return `INVALID_ARGUMENT`. A well-formed instant/calendar outside the pilot interval returns `OUT_OF_RANGE`.
- Validate supplied arguments before backend access. A valid get/set before successful init returns `NOT_READY`.
- First successful init checks backend readiness, enters ready-but-unset state and never writes a default time. Repeated successful init is a no-op preserving validity. Failed init remains not-ready and may be retried.
- A successful set establishes session validity. Before that, get returns `CRUX_ERR_NO_DATA`.
- A rejected argument does not write or invalidate an established time. A backend write failure conservatively invalidates session time and returns the mapped error, because hardware may have changed.
- Failed reads and conversions leave caller output unchanged. Backend `CRUX_ERR_NO_DATA` clears session validity; other read failures report their mapped status without fabricating an instant.
- A get reporting an instant outside the supported interval fails rather than clamping or normalising it.
- After reboot the Crux session starts unset, even if a hardware counter or upstream epoch offset is retained. Persistence is explicitly deferred, not declared impossible.

### CLI

The sample registers a `time` group through Zephyr Shell. Its host composition accepts the same lines through stdin until EOF. Both call the same portable command handler.

```text
time set 2026-10-16T14:30:00
OK
time get
2026-10-16T14:30:00 UTC
```

Accept exactly `YYYY-MM-DDTHH:MM:SS`; each field has the shown width. Require the separators and seconds. Do not accept milliseconds, a trailing `Z`, timezone offsets, date-only/time-only input or Unix-seconds CLI input in this slice. Help states that input is UTC. These are bounded interface choices, not universal RTC requirements.

Normalise transport line endings and allow shell-style whitespace between command tokens. Do not accept extra arguments or whitespace embedded in the timestamp. Invalid input must not reach a backend write.

Use one stable result line: `OK`, a UTC timestamp, or `ERR <STATUS>`. Preserve the existing Time-specific CLI vocabulary: map `CRUX_ERR_NO_DATA` to `ERR NOT_SET`, `CRUX_ERR_IO` to `ERR IO_ERROR`, and the remaining errors to `ERR INVALID_ARGUMENT`, `ERR OUT_OF_RANGE` or `ERR NOT_READY`. This presentation mapping is owned by the Time command handler; it does not add a public Time error type or rename the common codes. Normal shell prompts and transport echo are separate from that result. Help may be multiline. The portable handler returns the corresponding Crux status to its adapter. Map malformed/unknown commands within the `time` group to `ERR INVALID_ARGUMENT`; Zephyr Shell's rejection of an unknown root command is transport behaviour, not part of the Crux result grammar. Do not add JSON or a second machine protocol.

After successful explicit initialisation, emit `CRUX_TIME_READY build=<build-id>` once. The artefact manifest associates that build ID with source identity, dirty-state content where applicable and firmware checksum. On initialisation failure, report its status and do not emit the ready marker. pytest must recognise the expected marker for the flashed artefact before executing cases.

The host backend is a deterministic simulation: set stores an instant, read returns it, and test-only controls advance it or inject failures. The host sample identifies itself as a simulation at startup. It does not silently use host wall time and does not claim autonomous clock progression. The native RTC must progress on the EVK.

## 6. Repository and environment

```text
crux-workspace/                  # west workspace, not one Git repository
├── .west/
├── crux-pilot/                  # Crux manifest/implementation repository
├── zephyr/                      # external, west-managed
└── ...                         # required west projects

crux-pilot/
├── components/base/            # common status header and usage target
├── components/time/            # public API, common source list, host/Zephyr providers
├── samples/time/               # Zephyr app, host process, shared commands and Shell adapter
├── tests/time/                 # GoogleTest behavior and C++ API consumption
├── tests/zephyr_time/           # native_sim Ztest and Shell adapter integration
├── zephyr/                     # module metadata, independent CMake and Kconfig
├── scripts/                    # narrow build/check entry points
└── docs/                       # contracts, AC status, and execution evidence
```

Create only directories with actual content. No empty `platform/`, `hal/`, `drivers/` or future service catalogue. `components/time/include` is the exported header root so consumers use `<crux/time.h>` independent of checkout location.

The host root must not call `find_package(Zephyr)`. In `zephyr/module.yml`, point module CMake/Kconfig to `zephyr/`, not the host root. Ensure the manifest repository is discovered as a module by the selected west workflow or pass an explicit module path from the build entry point. Test discovery; do not assume a file's presence is sufficient. No custom board/DTS roots are required for an upstream EVK plus sample overlays.

### Bootstrap and tool identity

Use a VS Code Dev Container, initially Ubuntu 24.04 `linux/amd64` with a 64-bit native_sim target, two build jobs and approximately 4 GiB Docker memory as a starting configuration. Measure resource pressure on the current M3/8 GB Mac. Verify storage before installation; approximately 30–40 GiB free is working headroom, not an official minimum. Do not delete user files to make space.

Host bootstrap installs C/C++ compilers, CMake, Ninja, selected LLVM formatting/analysis tools,
GoogleTest and Python test requirements without Zephyr or its SDK. The host library-only configure
uses `BUILD_TESTING=OFF` and `CRUX_BUILD_HOST_SAMPLE=OFF`, enables only C, and does not fetch
GoogleTest. Zephyr bootstrap is a separate explicit operation. Pin an existing reusable Zephyr
baseline if authorised and available; otherwise assess v4.4.0 first and record the resolved commit,
compatible SDK, required module set and example-application reference revision. Do not fetch moving
`main` for accepted builds.

Store SDKs, west modules and caches outside the image layer or in named volumes; document persistence and regeneration. Do not place the complete west workspace inside the Crux Git tree. Use only required SDK architectures/modules. Do not mount the Docker socket, user home, company source or credentials into the development container by default.

After bootstrap, host configuration/build/tests must work with Zephyr unavailable and network disconnected, using already resolved test dependencies. Record package/tool versions and the accepted image digest; distinguish digest reuse from a bit-for-bit reproducible Dockerfile rebuild.

J-Link/LinkServer and UART remain on macOS initially. Export firmware into a host-visible artefact folder with checksum. Linux CMake caches and runners contain Linux paths: do not execute container-generated `west flash` metadata directly on macOS. Document a board-correct native flash procedure rather than assuming an ELF alone configures every probe. Do not add USB/IP, a network debug service or a custom flashing runner unless a separate need is demonstrated.

## 7. Verification and quality policy

| Tool | Responsibility |
| --- | --- |
| GoogleTest | Common C17/C++ units, Time conformance, controlled host backend and portable CLI logic |
| CTest | Discover and execute host tests, enforce timeouts and collect results |
| Ztest | Zephyr-specific units and RTC integration, with suitable emulator/device fixtures |
| Twister | Build/select/run/report Zephyr test configurations |
| pytest | Execute sample commands externally, parse result lines, collect UART logs and verify regression cases |

Do not add Unity, Ceedling or a custom runner. Keep conformance inputs/expected results framework-neutral where useful, with thin framework-specific assertions. Do not build a generic test framework merely to remove a few repeated assertions.

Common tests must verify the status mapping and CLI mapping separately: API no-data is `CRUX_ERR_NO_DATA`, while the Time command result is `ERR NOT_SET`. Verify the base target's exported include requirements and independence from native dependencies. Common tests must cover valid dates, month boundaries, leap-year acceptance/rejection, endpoints and post-2038 values, all defined states, no-write on invalid input and unchanged output on failure. Use independent reference values and round-trip tests; round trips alone can hide paired conversion defects. Host GoogleTest must call the actual C17 library, not a C++ recompilation of its source.

For Zephyr tests, separate a device emulator's results from real SNVS evidence. A native_sim path must use a suitable upstream RTC emulation/standard device fixture where available; using only the Crux host fake inside native_sim does not satisfy native provider integration. Missing emulated fault coverage can be reported and covered at the appropriate isolated boundary instead of forcing a new driver framework.

Start analysis with correctness-focused, language-appropriate checks on owned, host-buildable code using actual compilation databases. Exclude external/generated source and justify suppressions. Enable agreed owned-code warnings as errors. Run clang-format with a version pinned to the accepted configuration, and run host ASan/UBSan where supported. Record Zephyr-only sources not analysed; compiler success and host analysis are not equivalent to target static-analysis coverage. Full MISRA compliance and SonarQube deployment are excluded.

## 8. Milestones and acceptance criteria

AC identifiers are stable evidence references. Record each as pass, fail, pending or not applicable with rationale. No calendar duration or company staffing commitment is implied.

### M0 Readiness and upstream assessment

**Goal:** identify assumptions before they affect code and preserve usable existing evidence.

**Deliverables:** `docs/baseline.md`, `docs/upstream-reuse.md`, input/blocker register and a short implementation sequence.

- **M0-01:** Inspect the authorised working directory and existing repo; do not overwrite it or create a remote without an explicit implementation instruction.
- **M0-02:** Record available toolchain, Docker architecture, memory/storage and network/dependency access. Report missing prerequisites without silently installing unrelated software.
- **M0-03:** Resolve and record a Zephyr/example reference, compatible SDK and GoogleTest/tool versions. Separate host bootstrap from Zephyr dependencies.
- **M0-04:** Inspect RTC sample behaviour/metadata, shell sample, C++ sample, module entry points and the RT1062 SNVS RTC/Counter path. Record upstream reuse and omissions.
- **M0-05:** Record actual EVK model, revision, QSPI/HyperFlash configuration, debug probe and UART details when available. If unknown, leave physical-target work pending and continue host work.
- **M0-06:** Assess the 2000–2099 range through native types/configuration, including post-2038 behaviour. Do not claim target acceptance from source inspection alone.

**Exit:** sufficient inputs for host work; target-specific blockers remain local to affected tasks. No HAL or repository-wide redesign is authorised by a readiness failure.

### M1 Repository and development foundation

**Goal:** establish independent host and upstream Zephyr smoke paths before Crux functionality.

**Deliverables:** repo scaffold, Dev Container, root host CMake/presets, manifest/module integration, thin bootstrap commands and minimum CI.

- **M1-01:** A fresh checkout plus documented dependencies configures/builds a host C17 library and C++17 GoogleTest suite through ordinary CMake. C++ consumption is covered by the component API tests; a separate generic smoke fixture is unnecessary.
- **M1-02:** Host configure/build/CTest succeeds with Zephyr, west and SDK absent; resolved host dependencies need no network during that run.
- **M1-03:** The selected-baseline native_sim Hello World and a small upstream Ztest suite build/run; C++ Hello World and EVK Hello World at least compile when the target baseline is known.
- **M1-04:** Demonstrate Crux module discovery through `zephyr/module.yml` without routing native module inclusion into host root CMake.
- **M1-05:** Record image/tool identity, volume/cache persistence and safe resource defaults. Keep `crux-pilot/` as the primary editor folder while mounting the parent west workspace (`${localWorkspaceFolder}/..` to `/workspaces/crux`, `workspaceFolder: /workspaces/crux/crux-pilot`) so `.west/`, `zephyr/` and selected module siblings are visible. Verify `west topdir` and Git root inside the container. No private keys, company files, user-home or Docker-socket mounts are included.
- **M1-06:** CI and local commands return failure status on real failure. Hardware access is not triggered by bootstrap or a target build.

**Exit:** independent host foundation and applicable upstream software smoke evidence. Missing hardware does not block this milestone.

### M2 Time contract and host reference behaviour

**Goal:** implement a small, reviewable capability with an explicit native boundary.

**Deliverables:** common status header/INTERFACE target, public Time headers, common C17 implementation and calendar conversion, private port, controlled host backend, tests and minimal host sample composition.

- **M2-01:** Document the common result contract, module-owned state/result policy, public Time API, private port, link selection, initialisation/validity states and error precedence from Sections 4–5. No separate Time error type duplicates the common status set.
- **M2-02:** Existing C++17 GoogleTests include public C headers and call the APIs under test while linking the C17 common implementation in the host test composition. This verifies C++ use and C/C++ linkage for exercised APIs; it does not require a separate executable linked to the production `crux::time` target. Production implementation remains compiled as C17; no per-header compile-only fixture set is required.
- **M2-03:** GoogleTest covers all listed semantic cases, including 2038/2099 boundaries, independent conversion anchors, null arguments and unchanged output after failure.
- **M2-04:** Repeated init preserves valid state; rejected writes do not call the backend; failed backend writes invalidate session validity.
- **M2-05:** Test-only advance by five seconds produces exactly five seconds of change. Private controls are absent from public headers.
- **M2-06:** The same command source runs in a host process using the controlled clock; banner/help identifies simulation behaviour.
- **M2-07:** A forbidden dependency deliberately inserted into owned common source is detected and then removed. No copied production implementation or weak-symbol backend discovery exists.
- **M2-08:** Owned-code warnings, formatting and focused host analysis pass; supported host sanitizers execute.
- **M2-09:** The common status header/INTERFACE target is independent of Time and native/service runtime dependencies. Host tests verify API `CRUX_ERR_NO_DATA` and CLI `ERR NOT_SET` as distinct contracts. Module-specific results remain module-owned by policy; no speculative service is implemented to demonstrate it.

**Review gate:** review the contract, composition and host evidence before implementing that contract's native backend. The executing agent may prepare this review; a missing reviewer does not imply acceptance. Upstream/native readiness work can continue independently.

### M3 Zephyr provider and Time sample

**Goal:** reuse Zephyr infrastructure while testing the Crux path.

**Deliverables:** Zephyr backend, standard `samples/time` app, shell bridge, native_sim Ztest fixtures, EVK firmware build.

- **M3-01:** Common C17 production sources and public headers are shared without copying; native dependencies stay inside the backend/bridge.
- **M3-02:** Bind the selected upstream RTC alias/device; use standard RTC API and existing SNVS/Counter integration before proposing driver additions.
- **M3-03:** `samples/time` uses ordinary Zephyr application CMake/Kconfig and minimal application overlays, without copied EVK BSP or vendor SDK.
- **M3-04:** The sample-owned portable C17 handler is consumed by Zephyr Shell through a C adapter; init is explicit. No static date is written at boot and native RTC commands cannot bypass the acceptance path. Shell argument-count handling must route malformed commands within the `time` group to the shared handler's stable error result.
- **M3-05:** Native_sim Ztest exercises the actual Crux Zephyr backend and registered Shell adapter; native-to-common status mappings (including no-data) and missing-device behavior are covered at an appropriate boundary.
- **M3-06:** Shared CLI cases cover strict syntax, invalid calendar dates, overflow/trailing input, stable errors and no backend write on rejected input.
- **M3-07:** The actual EVK target/revision firmware builds with the selected C17 application policy. Host C++ GoogleTests continue to exercise C++ calls to the C17 API in the test composition; a separate production-target C++ smoke executable is not required. Record native configuration and size reports.
- **M3-08:** Reports distinguish simulated RTC integration, upstream smoke tests, target compilation and unexecuted SNVS tests.

**Exit:** software integration accepted or failures identified. No simulated result satisfies M4.

### M4 Physical EVK verification

**Goal:** obtain evidence for the real RTC and external command path.

**Entry:** actual board/revision, target configuration, native flash procedure, firmware checksum, UART and reset method are known; physical execution is authorised. No fuse/lock changes, unsupported erase, power-control automation or retention experiments are included.

**Deliverables:** native flash procedure, pytest UART tests, raw logs, target evidence and limitations.

- **M4-01:** Confirm board identity and flash the identified image using the documented host-native tool; record checksum and result.
- **M4-02:** Observe the expected build/readiness marker and `ERR NOT_SET` before the first set. Cover repeated-init preservation through a target Ztest scenario where needed; do not add a public init command solely for this test.
- **M4-03:** Set a known date, read it back and observe progression. For progression, sample at approximately one-second intervals for at most five monotonic host seconds; require non-decreasing values and at least one increment. This is a liveness check, not a drift/accuracy measurement.
- **M4-04:** Read-after-set comparison uses the recorded monotonic transaction interval with a documented allowance of one RTC tick and bounded command timeout. Diagnose timing failures rather than increasing allowances until tests pass.
- **M4-05:** Reject malformed/invalid/out-of-range inputs, then confirm the last valid time continues normally. Verify representative values after 2038 and near the upper supported year, with a safe set value restored afterwards.
- **M4-06:** Reset using the recorded method and verify session-unset behaviour; do not infer backup-power retention from a reset.
- **M4-07:** pytest exclusively owns the UART during each run, uses explicit transaction/readiness timeouts, and retains full raw logs including failing runs. A disconnect, timeout or mismatch fails the test.
- **M4-08:** Record actual timing bounds, board/configuration, native tools and remaining native limitations. No default-only or hand-edited success report is accepted.

**Exit:** actual hardware AC pass. Hardware absence or failure remains pending/fail, not an accepted exception to overall completion.

### M5 Reproduction and architecture exit review

**Goal:** determine whether the slice supports the architecture and can be repeated.

- **M5-01:** Repeat required software verification from clean build directories with the pinned inputs; preserve target evidence from the accepted image.
- **M5-02:** A temporary representative failing assertion or owned-code format violation fails the relevant local/CI check, then is removed. Do not add tests that merely mirror implementation.
- **M5-03:** CI covers format, forbidden dependencies, host build/tests, host analysis, applicable native_sim tests and target build. Controlled hardware tests remain separate.
- **M5-04:** Record source/manifest/module revisions, dirty state, tool/image identity, target `.config`, generated devicetree, size report, checksums, reports and raw logs.
- **M5-05:** Review independent host use, C/C++ interoperability, source reuse, visible composition, native-type isolation and proportional complexity. Classify each as supported, supported with constraints, requiring more evidence or rejected.
- **M5-06:** Record unresolved material issues and next actions. Do not generalise the session-validity policy, directory layout or Time-specific private port to all future Crux modules.

## 9. Commands, CI and evidence

Supply thin documented entry points, not another orchestration framework:

```text
scripts/bootstrap-host.sh       # host tools/test dependency preparation
scripts/bootstrap-zephyr.sh     # explicit west/SDK preparation
scripts/check-format.sh         # non-mutating owned-code check
scripts/check-dependencies.sh   # native-header/dependency enforcement
scripts/analyze-host.sh         # selected clang-tidy checks
scripts/test-zephyr.sh          # selected Twister scenarios
scripts/build-target.sh         # requires an explicit validated board target
scripts/test-target.sh          # explicit image/UART/board inputs; no implicit flash
```

Use CMake configure/build/test presets named `host-debug` and `host-sanitize`; keep binary directories separate. `test-target.sh` invokes pytest against an already identified/flashed image. Flashing follows the explicit native procedure and is not hidden in a test import or configure step. Scripts validate arguments, print selected configurations and propagate subprocess status.

Host CI must not require Zephyr or SDK. Zephyr CI separately prepares west dependencies and SDK. Publish useful JUnit-compatible reports plus raw logs; choose one supported results path and document retention. Do not upload private or company data to personal CI artefacts.

Write progress to one `docs/implementation-status.md` table linking AC IDs to evidence. Preserve failures and pending work. Generated binaries, caches and bulk logs stay ignored in Git and are retained in an identified output/CI location. A source change after target testing requires affected evidence to be regenerated or clearly marked stale.

## 10. Execution inputs, boundaries and escalation

| Input or decision | Handling |
| --- | --- |
| Local checkout and GitHub owner/visibility | Required before creating/publishing a remote. Use an authorised local checkout for independent work; do not assume a remote exists. Keep personal experiment private unless instructed otherwise. |
| EVK part number, revision, flash and probe | Required for exact target configuration and flash. No silent default to revision A or another MCU. |
| Actual UART device, baud and reset method | Required for M4; discover where possible, otherwise ask one focused question. |
| Tool baseline and sample adaptation | Codex resolves compatible pinned versions in M0 and documents evidence; this is an engineering task, not a reason to ask about every package. |
| Contract review | Required at the M2 gate, consistent with the existing first-slice host-review rationale. |
| Physical execution approval | Required for identified board/image/actions; approval does not cover fuses, production equipment or company targets. |
| SNVS gap or native date-range failure | Investigate narrowly, report evidence and alternatives. Do not silently replace the Time semantics, add an external RTC or patch upstream extensively. |

Continue independent host work when only hardware inputs are missing. Required review or execution approval is not inferred from silence. When blocked, report the exact affected milestone and the work that can continue.

Existing planning documents remain unchanged. `components/time`, `samples/time` and the root module metadata are bounded pilot naming/composition choices, preserving the architecture's logical responsibilities without reproducing its entire provisional directory tree. No automatic rename of `embedded-platform` is included.

Company RTC implementations, BSPs, proprietary runtime sources, internal Harness assets, credentials and signing material must not enter the personal repo, image, AI context or CI without the applicable authorisation. This prototype does not complete the separate company Extended Pilot or validate its Harness.

## 11. Minimal Codex guidance and review rules

Create a concise implementation-repo `AGENTS.md` covering scope, dependency direction, owned-code quality, standard commands, approvals and completion reporting. Do not copy the entire specification into it. Keep this plan as the single milestone/AC source and record material deviations next to their rationale.

Implement in reviewable increments. Prefer a failed behaviour test before a change and preserve useful failure evidence. Review expected results independently of the implementation. Never weaken assertions, widen timeouts without diagnosis, hide skipped scenarios or label an unexecuted test as passed.

The agent may choose routine file/function names and small build details inside these boundaries. Material changes to contracts, ownership, dependencies, runtime models or authorised access require review. Do not introduce a custom AI Harness until observed failures justify a specific reusable automation.

## 12. References and provenance

Source review date: 16 October 2026. Use reference tags as inspected starting points, then pin selected resolved commits. Examples are references, not proof that Crux or the user's EVK has passed testing.

- [Zephyr example-application v4.4.0](https://github.com/zephyrproject-rtos/example-application/tree/v4.4.0): manifest/module/application reference; adapt the relevant parts only.
- [Example module metadata](https://github.com/zephyrproject-rtos/example-application/blob/v4.4.0/zephyr/module.yml) and [module CMake entry](https://github.com/zephyrproject-rtos/example-application/blob/v4.4.0/CMakeLists.txt).
- [RTC sample](https://github.com/zephyrproject-rtos/zephyr/tree/v4.4.0/samples/drivers/rtc), including [source](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/samples/drivers/rtc/src/main.c) and [test metadata](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/samples/drivers/rtc/sample.yaml).
- [Shell sample](https://github.com/zephyrproject-rtos/zephyr/tree/v4.4.0/samples/subsys/shell/shell_module) and [C++ sample](https://github.com/zephyrproject-rtos/zephyr/tree/v4.4.0/samples/cpp/hello_world).
- [RT1060 EVK configuration](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/boards/nxp/mimxrt1060_evk/mimxrt1060_evk.dtsi) and [RTC Counter implementation](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/drivers/rtc/rtc_counter.c).
- [GoogleTest](https://google.github.io/googletest/primer.html), [CTest](https://cmake.org/cmake/help/latest/manual/ctest.1.html), [Ztest](https://docs.zephyrproject.org/latest/develop/test/ztest.html), [Zephyr C++ support](https://docs.zephyrproject.org/latest/develop/languages/cpp/index.html).
- Governing personal planning context reviewed: [Architecture Concept v0.17](https://github.com/wonny-labs/embedded-platform/blob/main/docs/platform-architecture-concept.md), blob `9d48148e2277dd4fc414e1221111622a8bf666b7`; [Architecture Design v0.10](https://github.com/wonny-labs/embedded-platform/blob/main/docs/platform-architecture-design.md), blob `4f687983d872ae0bbac83019b1068c17f8ac3243`. The company-scope development plan is context, not authorisation to transfer its assets.
