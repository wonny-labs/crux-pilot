# Native-free consumer boundary

Scope: Issue #24. This policy and its enforcement are proposed for acceptance
through this PR. They preserve existing Time behavior and native provider ownership;
they introduce no general HAL, runtime service registry or new capability contract.

## Dependency policy

| Role | Permitted dependencies | Boundary |
| --- | --- | --- |
| Firmware application and common runtime-porting modules | Public Crux usage targets and explicitly qualified standard facilities | No Zephyr/NXP/board/vendor headers, types, symbols, resource identifiers or native link requirements. Native selection belongs to composition. |
| Portable Crux contracts and shared mechanisms | ISO C17, existing C++17 consumption, other approved public Crux contracts | Public headers have component ownership; private provider/test headers cannot become consumer requirements. |
| Crux-controlled native providers, BSP/drivers and project composition | Required Zephyr/NXP/board services and private implementation headers | Keep native requirements private to those targets; publish only the capability contract. Native provider code is expected, not forbidden. |
| Native qualification samples/diagnostics | Their explicitly selected native test composition | Not reusable firmware/runtime consumer examples; migration obligations remain visible below. |

Designated source roots are component public `include` and common `src` directories,
component `portable`/`runtime_porting`, `applications/`, `runtime/`, controller
`core`/`include`, and the portable Time command helper. New components and common
runtime consumers must join these roots or explicitly extend the inventory and
tests before acceptance. Moving a consumer into a provider directory does not
change its role or make a native dependency permissible.

Existing `samples/controller/zephyr` and `samples/controller/host` contain execution,
diagnostic and qualification composition code. They remain native fixtures for the
accepted controller scenarios, not evidence that the entire reference application
already satisfies the future portable consumer boundary. Native access there must
not become a template for new firmware application or common runtime modules.

## Standard APIs and owned interfaces

ISO C headers such as `stdint.h`, `stdbool.h`, `stddef.h`, `string.h`, `errno.h`
and `time.h` are permitted dependencies. This does not promise heap availability,
stdio routing, locale, thread safety, clock progression/epoch, ISR suitability,
bounded latency or equivalent provider behavior. Individual contracts must state
the guarantees they use. Standard error names are not aliases for Crux status
values. C++ consumption preserves the existing C linkage declarations; it does not
add a C++ platform API or a stable compiler-independent ABI.

There is **no generally qualified consumer POSIX subset**. Linux host adapters
and execution probes may use POSIX internally without qualifying it for consumers.
Issue #28's approved bounded BSD/POSIX IPv4 client profile was merged in
[PR #38](https://github.com/wonny-labs/crux-pilot/pull/38). The
[networking contract](networking-contract.md) qualifies selected socket
headers/functions and `clock_gettime(CLOCK_MONOTONIC)` for networking deadlines.
It does not qualify pthreads, general POSIX use or a general clock service.
Architecture approval, software integration and physical qualification remain
separate. No additional socket wrapper is required by this boundary policy.

### Selected standard-library toolchain sysroot

Portable objects still require the selected CPU/ABI and standard C library.
The usage guard recognises a complete `--sysroot=<path>` compile option only
when its normalised absolute path exactly equals the selected toolchain's
`SYSROOT_DIR`. This is not an SDK-prefix or native-header exception. Different
roots, child paths, compound flag strings, include paths and link options remain
subject to the existing native checks. New Queue/Monotonic/Execution combined
consumer probes cover this distinction on Host, native_sim and EVKB builds.

### Scoped #28 integration

The common checker registers exactly `samples/networking/client.{c,h}`,
`tests/networking/runtime_port.{c,h}`, `tests/networking_consumer/consumer.h`
and `tests/networking_consumer/cpp_probe.cpp` under the named networking profile.
The existing networking checker supplies its stricter include/operation checks;
the common gate additionally retains native/threading rejection and permits only
literal `clock_gettime(CLOCK_MONOTONIC, ...)` among its native clock patterns.
The same POSIX headers/calls remain forbidden in unqualified consumer roots.
This is representative lexical enforcement, not exhaustive C/C++ API validation.
The contract still governs ownership, deadlines, supported operations and errors.

Public `crux::sockets`/`crux::netif` usage graphs, Host portable client/runtime
objects and independently configured C/C++ consumers use the CMake guard.
Zephyr registers the Netif implementation independently from its public facade,
so its native STATIC provider requirements do not propagate through `LINK_ONLY`.
The existing declaration-only `close` workaround is permitted solely for the
`crux_sockets` interface compile option, canonical header path and reviewed header
SHA-256. Different targets, extra flags, other private paths or modified content
remain rejected; this is not a provider-header exception. Revisit/remove this
pinned exception when the supported upstream declaration changes.

Actual Zephyr networking applications combine portable source with native fixture
startup/server code in `app`. The source gate and checked public facade apply;
that mixed native application's direct link graph is not claimed as a portable
consumer. Host independent C/C++ execution, the native C++ compilation probe,
real Zephyr stack tests and EVKB cross-build complement these bounded checks.
See [integration evidence](evidence/native-interfaces/assessment.md).

Current owned interfaces include `<crux/status.h>`, `<crux/time.h>`,
`<crux/time_calendar.h>` and `<crux/netif.h>`. Component private ports remain
implementation details. File/Configuration and subsequent execution/device
contracts retain their existing review gates.

## Controller migration map

This is a reviewed migration plan, not a claim that migration is implemented.
Historical controller source/output/evidence records remain intact.

| Current location and native dependency | Target consumer boundary / owner | Outstanding decision |
| --- | --- | --- |
| `core/controller.c`, `core/health.c`, portable headers; existing `crux_time_*` use | Keep portable policy and UTC contract unchanged | No new UTC abstraction; provider remains native internally. |
| `zephyr/main.c`, `m3_main.c`: `k_thread_*`, stack declarations, priorities, `k_sleep`, spinlocks/atomics | Execution/synchronisation contracts under #25; native thread/stack ownership stays in provider/composition | Lifecycle, waits, resource/priority guarantees need contract review; do not mechanically expose `k_*` types. |
| `m3_main.c`: `k_uptime_get`, absolute releases; host `clock_gettime`/sleep | Monotonic/timing/event services under #26, while schedule/health policy stays portable | Preserve monotonic versus UTC distinction, overrun policy and accepted freshness limits. |
| `device_input.c`: DT GPIO binding, `gpio_*`, native device readiness | GPIO contract #31; logical resource binding #32 | Board pin/active-level and startup ownership stay native; no GPIO public API created here. |
| `network_client.c`, `network_probe.c`: `zsock_*`, `net_if_*`/link state | Approved #28 Socket profile and separate Network Interface contract; resource/address binding #32 | Use the scoped common-checker profile above when migrating with short-I/O/deadline/recovery evidence; preserve current IPv4 limits. |
| `m3_main.c`: `wdt_*`, `hwinfo_*`, reset-cause and readiness | System health/reset/watchdog #30 | Preserve application-owned feed policy and timing/reset risks; no implied permission to change watchdog policy. |
| `native_logger.c`, `printk`, logging controls and diagnostic shell hooks | Logging #29; UART logical serial path #27 when needed | Keep bounded-output/loss semantics and test-only fault hooks separate from reusable consumer API. |
| `network_mdio_trace.c`, PHY diagnostics, MDIO completion source selection | Native driver diagnostics and controlled project composition | Remain native; do not migrate device registers into consumer code or count diagnostics as portable contracts. |
| Future File/Configuration uses | #23/#15 File and #16 Configuration, backed by approved providers | #14 storage decisions and retained Flash physical/production safety gates still apply. |

No source migration, second-board claim or scheduling/thread mechanism selection
is authorised by this map. Final integrated/independent-consumer qualification
remains #34 work, after the relevant contracts and providers exist.

## Verification and limits

The source checker, build-interface checks and independently configured C/C++
consumer provide complementary regression evidence. They must reject negative
probes while accepting controlled native provider implementations. The source
checker is bounded lexical policy checking, not a C/C++ semantic proof: token
pasting, arbitrary aliasing, unlisted vendor families and opaque generated code
need review. Compiler success alone cannot establish architectural independence.

The independent fixture consumes public usage targets without GoogleTest or the
reference application. Host and Zephyr compositions still select their respective
backend; the fixture does not claim OS/hardware equivalence or a supported install
package. A build guard is only effective where invoked; unsupported new target
forms must extend the guard/tests rather than silently acquire approval.

`cmake/CruxConsumerBoundary.cmake` checks public usage graphs separately from a
designated consumer's direct requirements. Providers' implementation-only PRIVATE
requirements remain private; propagated static `LINK_ONLY` requirements are still
part of the consumer link and are checked. Deferred checks cover the existing
base/Time/networking facades and designated portable consumers. New designated
consumer targets must call `crux_assert_portable_consumer` after declaration.
The guard supports documented source-tree generator-expression forms, checks all
conditional branches conservatively, and rejects opaque forms. Install-tree
requirements and arbitrary compiler/linker flag encodings are outside this bounded
gate; no installed SDK/package is claimed. Source review remains necessary for
transitive helper includes, aliases and third-party library semantics.

Static providers retain PRIVATE native requirements at the composition boundary.
Unqualified consumer/public requirements such as `Threads::Threads`, `pthread`
and `-pthread` are rejected; a controlled Host provider can still use them privately.
Because CMake can export their private link dependencies through `LINK_ONLY`, they
must not be inserted into a portable public facade if that would expose native
requirements. As in the Time Zephyr module, select/register the native implementation
independently in the native image composition. A designated portable consumer links
the public API; the final composition supplies the implementation. There is no
blanket provider exemption or stripping of `LINK_ONLY`. #25 integration must preserve
this separation and run the static-provider controls on its actual execution graph.

Reproduce inside the existing Linux development environment:

```sh
scripts/check-dependencies.sh
python3 -m unittest discover -s tests/build -v
cmake -S tests/consumer_boundary -B build/independent-consumer
cmake --build build/independent-consumer
build/independent-consumer/consumer_c
build/independent-consumer/consumer_cpp
```

The fixture disables repository samples and GoogleTest, links only public usage
targets, and checks actual C/C++ compile, link and runtime results. Existing Host CI
discovers the source/graph/fixture tests without a new workflow or runner.
No hardware operation,
Flash change, private planning edit or change to historical acceptance follows.

### External source-tree application composition

The [supported Linux Host entry point](../README.md#external-application-build-linux-host)
selects capabilities without modifying parent test policy. Portable source targets
remain subject to `crux_assert_portable_consumer`. Final native executables declare
the used public capabilities and may link the documented `crux::queue_linux` and
`crux::monotonic_linux` composition targets. These aliases are not portable usage
facades or exceptions to recursive `LINK_ONLY` checks. The guard rejects both
composition targets and their backing implementations, including application aliases
and transitive wrappers. This does not inspect all provider-private requirements
or restrict the supported public capability facades. Zephyr selection and the
Sockets declaration/selected-sysroot exceptions remain unchanged.
