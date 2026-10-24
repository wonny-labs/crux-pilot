# Crux Pilot

**Crux** is an independent, reusable embedded software platform intended to support products built on **Zephyr** and **Embedded Linux**. Its direction is to combine shared platform capabilities, selective portability and repeatable engineering and validation practices.

This repository is Crux's **initial development and architecture-validation workspace**. It contains platform code, reference applications, development tools and tests used to establish and validate the platform. Broader platform vision, architecture and development planning are maintained separately.

## Current implementation

The `main` branch currently includes:

| Area | Available implementation |
| --- | --- |
| Reusable components | `base`, `time`, `thread`, `sync`, `queue`, `monotonic`, `netif`, `sockets`, `configuration` and `file`, with [integration limits by capability](docs/architecture.md#current-capability-integration) |
| Reference application | A reference controller with shared portable Time and scheduling logic, a Linux host scenario and Zephyr device/network/health compositions |
| Device, networking and health integration | EVKB SW5 GPIO input, UART diagnostics, bounded IPv4 TCP/UDP exchanges, service readiness and workload-conditioned watchdog supervision |
| Zephyr integration | Out-of-tree module, RTC and native device/network services, Shell examples, `native_sim` tests and EVKB builds |
| Engineering foundation | CMake and west builds, Dev Container, automated tests, static checks, GitHub Actions and build-provenance evidence |

The initial hardware target is the **MIMXRT1060-EVKB revision B** using `mimxrt1060_evk@B//qspi`. Verified hardware behaviour and limitations are specific to the tested configurations.

The native Linux host runs portable code without Zephyr, while Zephyr `native_sim` exercises Zephyr integration on a host. The EVKB GPIO, networking and watchdog results apply to the documented Zephyr composition, not to the Linux host or other boards. They do not establish production readiness, long-duration reliability or concurrent-load performance. An **Embedded Linux production variant** is part of Crux's longer-term direction, not a currently implemented target. See [milestones](https://github.com/wonny-labs/crux-pilot/milestones) and [pull requests](https://github.com/wonny-labs/crux-pilot/pulls) for ongoing work.

### Capability status

The [qualified socket/netif contract](docs/networking-contract.md) supplies
IPv4 TCP-client/connected-UDP communication and read-only primary-interface
status. See the [networking software tests](tests/README.md#portable-networking-software-slice)
for supported configurations and commands. Physical Issue #28 acceptance is
pending; this slice does not complete the M5 milestone.

The portable [Configuration contract](docs/configuration-contract.md) provides
application-owned BOOL/U32 validation, explicit save/load and canonical encoding.
Its [software qualification](docs/configuration-validation.md) uses a deterministic
private provider; native Settings integration and physical persistence remain gated.

The software-qualified [portable File contract](docs/file-contract.md) exposes
bounded synchronous file operations through `crux::file`, with a Linux Host
provider and deterministic failure tests. Its [validation and Zephyr handoff
plan](docs/file-validation.md) document the separate native composition work;
no production Flash layout or writable QSPI backend is enabled.

## Architecture

Crux separates portable application behaviour, reusable platform capabilities and environment-specific integration. Shared contracts are introduced when they provide stable, useful semantics. Zephyr and Embedded Linux retain their native operating-system and device infrastructure rather than being hidden behind a universal HAL or OSAL.

Portable components, application logic and common runtime consumers avoid unqualified native dependencies. Product applications may also contain native platform-composition code for board configuration, initialization and provider selection. Host and Zephyr compositions explicitly select their implementations. Host, `native_sim` and physical-target tests provide complementary evidence rather than interchangeable guarantees.

For the component model, native-port ownership, build composition and dependency boundaries, see the [architecture overview](docs/architecture.md).

The [Thread and Sync contract](docs/thread-sync.md) defines independent joinable
workers (`<crux/thread.h>`, `crux::thread`) and nonrecursive mutexes/counting
semaphores (`<crux/sync.h>`, `crux::sync`). Each owns its Linux and Zephyr native
ports; Sync works independently of Crux Thread. See the contract for Timeout,
Ownership, Lifetime, provider selection, resource limits and historical evidence.

The implemented [Time contract](docs/time-contract.md) provides a concrete component example.

### External application build (Linux Host)

Crux supports source-tree consumption through `add_subdirectory()` in the documented
Linux development environment. For example, with the application owning `logic.c`
and `main.c`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_application LANGUAGES C)
set(CRUX_BUILD_TIME ON CACHE BOOL "Required capability")
set(CRUX_TIME_PROVIDER memory CACHE STRING "Development/test provider")
add_subdirectory("${CRUX_SOURCE_DIR}" crux)
add_library(application_logic STATIC logic.c)
target_link_libraries(application_logic PRIVATE crux::time)
crux_assert_portable_consumer(application_logic)
add_executable(my_application main.c)
target_link_libraries(my_application PRIVATE application_logic crux::time)
```

Configure with `-DCRUX_SOURCE_DIR=/absolute/path/to/crux-pilot`. Application source
includes public headers such as `<crux/time.h>`; no manual Crux include paths are
needed. Set the application's own C17/C++17 requirements on its portable targets.

As a dependency, Crux enables only Base by default. Select capabilities with
`CRUX_BUILD_TIME`, `CRUX_BUILD_THREAD`, `CRUX_BUILD_SYNC`, `CRUX_BUILD_QUEUE`,
`CRUX_BUILD_MONOTONIC`, `CRUX_BUILD_NETIF`, `CRUX_BUILD_SOCKETS`,
`CRUX_BUILD_CONFIGURATION` and `CRUX_BUILD_FILE`. Crux tests and samples default OFF,
without changing parent `BUILD_TESTING`. `CRUX_BUILD_TESTS` opts into Crux's internal
suite; it requires the suite's component dependencies and may enable C++/GoogleTest.
`CRUX_BUILD_HOST_SAMPLE` and `CRUX_BUILD_REFERENCE_CONTROLLER` select development
samples. Top-level development retains the existing components, samples and
`BUILD_TESTING`-based initial test default.

The final native executable declares its used public capabilities, as above, so
provider compatibility is checked at composition. Queue/Monotonic portable targets
link `crux::queue`/`crux::monotonic`; the final Host executable additionally links
`crux::queue_linux`/`crux::monotonic_linux`. These names identify the implementation
platform, not a production qualification.
The Queue port uses GNU pthread facilities; the Monotonic port uses POSIX monotonic
clock/sleep facilities in the supported Linux environment. These two native
composition targets must not be linked into portable source targets. Host
Thread/Sync/File implementations
remain linked through their public targets. Sockets remains a qualified standard
API profile. Host Time requires explicit `CRUX_TIME_PROVIDER=memory` in an external
project: this is a controlled development/test implementation, not a production RTC
fallback. Missing Queue/Monotonic providers fail at link; missing/unsupported Time
selection and conflicting Time providers fail during CMake configuration/generation.

The [external-consumer checks](tests/README.md#external-application-build-boundary)
exercise C17/C++17 Base/Time, Thread-only, Sync-only, Queue/Monotonic and parent-test
isolation from temporary projects outside Crux. Existing Linux Host and Zephyr
qualification limits remain unchanged. Zephyr still uses its module/Kconfig entry
point. Installation, `find_package(Crux)`, binary SDKs and arbitrary toolchains are
not supported by this source-tree contract.

## Repository structure

| Path | Purpose |
| --- | --- |
| `components/` | Reusable platform components and public interfaces |
| `samples/` | Capability examples and reference applications |
| `zephyr/` | Out-of-tree Zephyr module integration |
| `tests/` | Host, Zephyr and physical-target test assets |
| `scripts/` | Setup, build, analysis and validation commands |
| `.devcontainer/` | Linux development environment configuration |
| `.github/workflows/` | CI build, test and quality checks |
| `docs/` | Contracts, decisions and verification evidence |

## Getting started

### Development environment

Install Docker and VS Code with the **Dev Containers** extension. Place `crux-pilot/` in a dedicated parent directory for the west workspace, open the repository in VS Code and select **Dev Containers: Reopen in Container**.

The Dev Container uses Linux amd64 and mounts the parent workspace at `/workspaces/crux`. Zephyr, its modules and SDK dependencies are managed outside the `crux-pilot` Git tree. Run the commands below from the repository root **inside the container**, unless noted otherwise.

### Build and run on the Linux host

Set up the host build, compile the available targets and run their tests:

```sh
scripts/bootstrap-host.sh
```

Run the reference application without hardware:

```sh
build/linux/debug/samples/controller/host/crux-controller-host
```

The host executable runs a finite periodic scenario and reports its progress and Time status. See the [reference controller](samples/controller/README.md) for the scenario and its limitations.

For subsequent builds, tests and quality checks:

```sh
scripts/check-host.sh
scripts/check-format.sh
scripts/check-dependencies.sh
scripts/analyze-host.sh
```

Optional AddressSanitizer and UndefinedBehaviorSanitizer checks:

```sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
```

### Build and test Zephyr

When Zephyr development is needed, initialise the west workspace and install the pinned SDK. Ensure sufficient disk space first.

```sh
scripts/bootstrap-zephyr.sh --install-sdk
```

Run the current reference-controller `native_sim` scenarios and EVKB cross-build:

```sh
scripts/test-controller.sh
```

For device, networking, health and integrated-controller software checks, run `scripts/test-m3.sh`. These scripts **do not** program or reset the physical board. Additional component and target test instructions are in the [test guide](tests/README.md). Host and simulated execution do not replace physical-target verification.

### Flash and debug

Hardware flashing and debugging require a verified target configuration and a firmware image for the intended application. The existing VS Code **EVKB QSPI: Flash** profile is currently configured for the **Time sample**, not for arbitrary applications. The **EVKB QSPI: Attach** profile connects to an existing image using a matching ELF without downloading firmware. See the [debug configurations](.vscode/launch.json) and [associated build script](scripts/build-debug-target.sh) for the current behaviour.

For the existing macOS/J-Link setup, start `scripts/macos/start-jlink-gdbserver.command` on the Mac before connecting from VS Code. Confirm the board, Flash configuration and image before any programming or reset. Starting the server may halt the target, and its network listener is not limited to localhost, so use it only on a trusted network. See the [physical test instructions](tests/README.md#physical-uart-target) for the currently documented UART verification procedure.

## Documentation and project status

| Topic | Reference |
| --- | --- |
| Components and examples | [Time contract](docs/time-contract.md) and [reference controller](samples/controller/README.md) |
| Testing and evidence | [Test guide](tests/README.md), [Controller validation and support limits](docs/m3-validation.md), [execution profile](docs/execution-profile.md) and [verification records](docs/baseline.md) |
| Ongoing development | [GitHub milestones](https://github.com/wonny-labs/crux-pilot/milestones), [issues](https://github.com/wonny-labs/crux-pilot/issues) and [pull requests](https://github.com/wonny-labs/crux-pilot/pulls) |

The [original Time Pilot plan](docs/plans/initial-implementation-plan.md) and [implementation-status record](docs/implementation-status.md) retain historical results and earlier roadmap notes. Their M0–M5 identifiers differ from the current platform roadmap. Use the GitHub records above for current delivery status.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution, interface and review practices. AI agents must also follow [AGENTS.md](AGENTS.md).
