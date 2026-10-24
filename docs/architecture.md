# Crux Pilot architecture

This document explains the architecture implemented in `crux-pilot` so that developers can understand the component boundaries and extend the code without access to a separate planning repository. It is an implementation overview, not a complete product architecture or a replacement for individual API contracts.

For setup and initial builds, start with the [README](../README.md). For contribution rules, use [CONTRIBUTING](../CONTRIBUTING.md). Current supported behaviour is defined by the public headers, adopted capability contracts, and recorded validation evidence. Historical proposals do not override implemented contracts.

## Scope and support boundaries

Crux is a reusable embedded software platform organised around small, component-owned capabilities, primarily using C17. A capability may instead qualify a standard API where that is the more appropriate boundary. It aims to let portable firmware and runtime-adapter code reuse the same selected interfaces across supported environments while native operating systems retain responsibility for their kernels, drivers, and board support.

The current development environments are a Linux Host build and Zephyr, including `native_sim` and the documented RT1060-EVKB Rev B QSPI configuration. Linux Host is a development and test environment, **not** a qualified Embedded Linux product variant. An EVKB cross-build is not proof of physical operation. A component is supported on a given environment only to the extent stated by its contract and validation record. Do not infer that every component has a Zephyr provider or has passed physical qualification.

The repository is an evolving pilot. Consult its [GitHub milestones](https://github.com/wonny-labs/crux-pilot/milestones), issues, and merged PRs for current delivery status. In particular, later board and independent-consumer qualification work must not be treated as complete merely because a component builds.

## Architecture model

```mermaid
flowchart TB
    subgraph Product["Product application"]
        P["Portable application logic / common runtime consumer"]
        X["Platform composition: board, Kconfig, providers, native initialization"]
    end
    A["Crux-owned public APIs"]
    C["Component implementations: portable code and optional native ports"]
    S["Qualified standard APIs: Sockets profile"]
    N["Selected OS / native services"]
    P -->|"Crux public headers and usage targets"| A
    A --> C
    C -->|"When native services are needed"| N
    P -->|"Approved standard headers and crux::sockets"| S
    S -->|"Direct standard API calls; no Crux socket wrapper"| N
    X -.->|"Selects implementations"| C
    X -.->|"Configures and initializes"| N
```

A product application can contain both portable logic and platform composition. These are responsibilities, not mandatory directories or an extra abstraction layer. The native-free boundary applies to **portable application logic and common runtime consumers**, not to all application code. Composition may use native OS and board APIs for initialization, configuration and provider selection.

The diagram distinguishes two supported consumer paths. Crux-owned APIs use component implementations, some of which need no native port. A qualified standard API instead calls the selected OS facilities directly: Sockets uses its bounded POSIX profile and `crux::sockets` usage target, without an intervening Crux socket implementation. Neither path authorizes arbitrary native APIs in portable logic.

| Role | Owns | Boundary |
| --- | --- | --- |
| Product application | Application policy and assembly of portable logic with platform composition | Declare which code is portable and which is native composition |
| Portable application logic / common runtime consumer | Policy, state, workflows and calls to supported public contracts | No Crux private headers, provider selection, board identifiers or unqualified native OS APIs |
| Public capability | Crux-owned declarations or an approved standard API profile, usage target, semantics and errors | No native Zephyr/NXP declarations in Crux-owned public headers; standard API use stays within its qualified profile |
| Component implementation | Shared mechanisms, private resource state and optional native ports | No application-specific policy or generic OSAL |
| Platform composition | Board/project configuration, provider selection, native initialization, OS/toolchain requirements and linkage | Keep unqualified native requirements and private implementation paths out of portable consumer usage |
| Native OS and board support | Kernel services, drivers and hardware resource bindings | Does not define portable application policy |

Consumers of Crux-owned APIs include `<crux/...>` headers and use their public CMake targets. Sockets consumers instead include the [qualified standard headers](networking-contract.md#socket-profile) and link `crux::sockets`. The [native-free consumer policy](native-interface-policy.md) defines the permitted standard APIs, native restrictions and enforcement limits. Native sample, fixture or product-composition code can use OS APIs where its declared role requires them; that permission does not extend to reusable portable logic.

## Component ownership and source layout

Each component owns its applicable public contract, common code, private helpers, build definitions and optional OS-specific implementations:

```text
components/<capability>/
  include/crux/          # Public C declarations, when needed
  src/                   # Common code and private interfaces
  ports/<os>/            # Optional, genuine OS-specific implementations
  CMakeLists.txt         # Component-owned Host build integration, where applicable
```

Public paths in the shared `crux/` include namespace must remain unique across components. Implementation headers and native requirements remain private, except for the narrowly approved usage-profile/toolchain requirements described by the native-free consumer policy.

Use actual OS names such as `linux` and `zephyr` for native ports. The term **Host** describes a development or test composition, not an operating system. An OS-independent memory implementation is not automatically a Linux port. Empty port directories, central backend registries, duplicated public headers and uniform test subdirectories are not required for consistency.

New platform-specific composition targets use `crux::<capability>_<os>` (for example,
`crux::queue_linux`); their implementation targets use `crux_<capability>_<os>`.
Reserve `host` for the development/test role. An API-family name such as `posix`
requires a separately verified support contract; POSIX calls alone do not establish
cross-OS support. These names do not imply production qualification.

For a concrete example, [Time](time-contract.md) owns a controlled memory implementation separately from its native Zephyr RTC port. The final composition must select an appropriate implementation rather than silently falling back to a test provider.

Thread and Sync are separate component-owned capabilities. `<crux/thread.h>` and `crux::thread` provide bounded joinable workers; `<crux/sync.h>` and `crux::sync` provide nonrecursive mutexes and counting semaphores. Sync uses native OS identity without depending on Crux Thread. Each owns its private metadata protection and deadline handling. The [Thread/Sync contract](thread-sync.md) defines current usage, Timeout, Ownership, Lifetime and provider limits, with links to historical decisions and validation evidence.

Tests remain under top-level `tests/`, grouped by capability or validation purpose. Samples and reference applications remain under `samples/`. A sample-owned helper is not automatically part of the Crux public API.

### Current capability integration

This is a source/integration inventory, not a qualification matrix. “Host” means the Linux development configuration; Zephyr support is limited to the selected native_sim/board profiles in the linked records. Presence of portable code does not imply an adopted production backend or physical qualification.

| Capability | Purpose | Current integration and limits | Reference |
| --- | --- | --- | --- |
| Base | Common status declarations | Header-only usage in Host and Zephyr; no OS provider | [Public header](../components/base/include/crux/status.h) |
| Time | Calendar/RTC time | Controlled memory implementation in Host; native Zephyr RTC port; no implicit production fallback | [Contract](time-contract.md) |
| Thread | Bounded joinable workers | Independent Linux pthread and Zephyr providers | [Current headers/targets](#build-and-native-composition), [contract](thread-sync.md) |
| Sync | Nonrecursive mutexes and bounded semaphores | Independent Linux and Zephyr providers; no Crux Thread dependency | [Current headers/targets](#build-and-native-composition), [contract](thread-sync.md) |
| Queue | Bounded copy messaging | Linux and Zephyr ports with composition-selected native providers | [Messaging contract](messaging-contract.md) |
| Monotonic | Elapsed time and absolute waits | Linux and Zephyr providers selected by composition | [Messaging contract](messaging-contract.md) |
| Netif | Read-only primary-interface status | Linux selected-interface and Zephyr native network integration | [Networking contract](networking-contract.md) |
| Sockets | Qualified IPv4 client standard-API profile | Linux libc and selected Zephyr POSIX facilities; no Crux wrapper or separate stack | [Qualified profile](networking-contract.md#socket-profile) |
| Configuration | Scalar validation, explicit save/load and encoding | Portable core with deterministic test persistence provider; no adopted Zephyr Settings/production persistence integration | [Software qualification and native gate](configuration-validation.md) |
| File | Bounded synchronous file operations | Linux Host provider and deterministic failure tests; Zephyr provider is a plan, not implemented support | [Contract](file-contract.md), [Zephyr handoff plan](file-validation.md#zephyr-provider-handoff-plan-only) |
| Serial | Bounded synchronous UART/TTY byte transfer | Linux PTY/TTY and selected Zephyr UART emulator/EVKB compositions; explicit binding and exclusive UART ownership; no physical qualification | [Contract](serial-contract.md), [validation](serial-validation.md) |

## Build and native composition

The root `CMakeLists.txt` supports the Linux Host development build. The `zephyr/` directory provides out-of-tree module integration, with Zephyr Kconfig and application/project configuration selecting supported native facilities. The `west.yml` manifest pins the relevant Zephyr dependency baseline.

Public targets such as `crux::time`, `crux::thread` and `crux::sync` are consumer-facing usage contracts. Internal native targets, their private include paths, and link dependencies belong to the selected composition. **A public target is not an installed SDK package, nor does every component use an identical provider-linkage pattern.** Follow the [supported source-tree consumer contract](../README.md#external-application-build-linux-host). Do not manually add private provider paths to an application to bypass a missing integration capability.

Host and Zephyr may compose providers differently. For example, a Host Time composition uses a controlled memory provider, while the Zephyr image registers its selected RTC implementation independently. This difference must not change the portable Time API.

Thread and Sync are independently selectable in Host and Zephyr builds. The [contract's build selection and provider limits](thread-sync.md#build-selection-and-provider-limits) document their component-owned switches and resource settings. Consumer code should depend only on the capabilities it uses.

The README documents the verified Linux Host `add_subdirectory()` entry point, component selection and final native composition. Dependency builds default to Base only, with Crux tests/samples disabled independently of parent testing. Queue/Monotonic expose documented composition-only Linux targets; portable source targets retain their public usage facades. Host Time memory selection is explicit for external applications and remains development/test only. Zephyr composition stays under module/Kconfig ownership. An installable package, `find_package(Crux)` and arbitrary OS/board/toolchain combinations remain unsupported; a comprehensive Build Guide is separate work.

## Engineering and validation boundaries

Crux uses complementary forms of evidence:

| Validation | What it demonstrates | What it does not prove |
| --- | --- | --- |
| Linux Host builds, tests and sanitizers | Portable behaviour and selected Host implementation paths | Zephyr or physical board behaviour |
| Independent C/C++ consumer builds and negative dependency checks | Selected public usage, source and build boundaries | Every possible API/ABI combination or product integration |
| Zephyr `native_sim` and Twister | Relevant native integration under simulated conditions | Target peripheral timing or physical persistence |
| EVKB firmware cross-builds | A selected target configuration compiles and links | Boot, Flash safety or real peripheral behaviour |
| Identified physical board tests | Only the documented hardware scenario and limits | Broad production readiness, other boards or endurance |

Run tests proportionate to the change. Preserve existing contract and boundary protections, including negative tests. Avoid repeating unrelated full test suites solely for a documentation change. Physical programming, reset and Flash operations require task-specific approval and verified target setup.

Evidence and historical qualification records must retain their original source, board and toolchain identity. Earlier Time Pilot M0–M5 records in [implementation status](implementation-status.md) are **not** the same milestones as the current platform roadmap.

## Extending Crux

Before adding or significantly changing a capability:

1. Identify the real consumer requirement and check whether an existing capability or qualified standard API is sufficient.
2. Define the minimum public contract and clarify ownership, errors, lifetime, concurrency and execution-context limits.
3. Keep consumer-facing declarations native-free. Add common implementation logic and optional OS ports only as justified by the contract.
4. Define the public usage target, explicit native composition, and supported environment selection without leaking private dependencies.
5. Add focused Host/Zephyr and independent consumer evidence for the actual claim. Record unsupported configurations rather than masking missing providers.

Use [CONTRIBUTING](../CONTRIBUTING.md) for code, PR and review requirements and [AGENTS.md](../AGENTS.md) for AI execution boundaries. Material API and architecture decisions require the applicable review or approval. This overview does not authorise a new capability or a hardware operation.

## Further reading

- [Time capability contract](time-contract.md)
- [Current Thread/Sync usage and selection](#build-and-native-composition)
- [Current Thread/Sync contract, design decisions and evidence](thread-sync.md)
- [Messaging and monotonic timing contract](messaging-contract.md)
- [Qualified networking profile](networking-contract.md)
- [File capability contract](file-contract.md)
- [Configuration capability contract](configuration-contract.md)
- [Native-free consumer policy](native-interface-policy.md)
- [Test organisation and commands](../tests/README.md)
- [Reference controller and supported scenarios](../samples/controller/README.md)

Capability-specific implementation and validation documents provide details beyond this overview. Treat any proposal or past validation record as historical where a later merged contract has superseded it.
