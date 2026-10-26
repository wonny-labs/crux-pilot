# Crux Pilot

**Crux** is a reusable embedded software platform for controller and firmware applications. It provides portable C17 components and selected standard APIs, supported by OS-specific integration, build tools and tests. Portable application code remains separate from operating-system and board details.

This repository is Crux's development and architecture-validation Pilot. It currently provides Zephyr integration and a Linux development/test environment. Production Embedded Linux support is a longer-term goal. Crux is not yet a production-qualified or installable SDK.

## Capabilities

Applications select the components they need. Portable application and runtime-porting code use public interfaces, while board/project configuration selects the OS implementations and hardware resources.

| Area | Components | Purpose |
| --- | --- | --- |
| Foundation and timing | `base`, `time`, `monotonic`, `delay` | Shared status, typed clocks/authority/calendar, deadlines and bounded CPU busy-wait |
| Execution and synchronisation | `thread`, `sync`, `queue` | Threads, mutexes, semaphores and message queues |
| Communication and networking | `serial`, `sockets`, `netif` | Serial byte streams, IPv4 TCP clients/connected UDP and network-interface status |
| Storage and configuration | `file`, `configuration` | File access and application-supplied parameter validation and save/load |
| Diagnostics and system | `logging`, `system` | Diagnostic logging, service status, reset diagnostics and watchdog operations |

All listed components have implemented source, but available OS implementations and validation levels differ. The Sockets capability uses a documented, tested POSIX socket API subset rather than a Crux socket wrapper. For each component's contract, available implementations and limitations, see the [capability inventory](docs/architecture.md#current-capability-integration).

## Architecture

Crux separates reusable platform components and portable application code from **platform composition**: the board/project-specific configuration and initialisation that select OS implementations and bind hardware resources. Applications use Crux public APIs or documented standard API subsets. Native operating-system and device services remain within the selected platform integration.

See the [architecture overview](docs/architecture.md) for component ownership, portable/native boundaries and build dependencies.

### Development and target environments

| Environment | Current scope |
| --- | --- |
| Linux Host | Linux amd64 development, testing and selected Linux implementations, not production Embedded Linux support |
| Zephyr `native_sim` | Selected Zephyr component and reference-controller tests without physical hardware |
| MIMXRT1060-EVKB Rev B (`mimxrt1060_evk@B//qspi`) | Initial physical target, with results limited to documented firmware and configurations |
| RT1170-EVK Rev A1 Cortex-M7 | Planned second physical target under M6, not yet qualified |
| Production Embedded Linux | Future platform variant, not currently implemented or qualified |

Host tests, `native_sim` runs and target cross-builds do not prove physical operation. Writable File/Configuration persistence on the RT1060's QSPI Flash is not yet enabled or physically qualified.

## Getting started

Use Docker and VS Code Dev Containers with the documented Linux amd64 environment. Run the commands below from the Crux repository root **inside the Dev Container**. See the [Build and Integration Guide](docs/build.md) for workspace setup, prerequisites and configuration options.

### Linux Host

Build Crux and run its Host tests, then run the existing finite reference-controller scenario:

```sh
scripts/bootstrap-host.sh
build/linux/debug/samples/controller/host/crux-controller-host
```

The bootstrap configures, builds and runs CTest. The reference controller reports cycle progress and Time status. For subsequent builds and tests using the prepared environment:

```sh
scripts/check-host.sh
```

### Zephyr

Prepare the pinned west workspace and SDK, then run the controller simulations and RT1060-EVKB cross-build:

```sh
scripts/bootstrap-zephyr.sh --install-sdk
scripts/test-controller.sh
```

The script checks normal and RTC-absent system realtime `native_sim` scenarios and builds a target firmware image. It does not program or reset the physical board. See the [test guide](tests/README.md) for other components and test procedures.

### Use Crux from another application

A separate Linux CMake project can consume Crux from source with `add_subdirectory()`. Enable only the required `CRUX_BUILD_<COMPONENT>` options and link the documented public `crux::` targets. See the [complete external application example](docs/build.md#external-linux-application-using-crux-source). Installed packages, `find_package(Crux)` and binary SDKs are not supported.

**Physical hardware:** Debugging, resets and firmware programming require a separately approved procedure. The current VS Code Flash profile builds and downloads the **Time sample**, not arbitrary firmware. The Attach profile does not download an image but the debugger connection may halt the target. See the [RT1060 debugging instructions](docs/build.md#rt1060-evkb-debugging) before using either profile.

## Repository structure

| Path | Purpose |
| --- | --- |
| `components/` | Public capability interfaces, shared implementations and native ports |
| `samples/` | Capability examples and reference controller applications |
| `zephyr/` | Out-of-tree Zephyr module integration |
| `tests/` | Host, Zephyr, build-policy and target validation |
| `scripts/` | Setup, build, analysis and test commands |
| `.devcontainer/` | Linux development environment |
| `.github/workflows/` | CI workflows |
| `docs/` | Architecture, API contracts, decisions and evidence |

## Documentation

| Topic | Reference |
| --- | --- |
| Architecture, components and API contracts | [Architecture overview and capability inventory](docs/architecture.md) |
| Setup, builds, external applications and debugging | [Build and Integration Guide](docs/build.md) |
| Test commands and qualification procedures | [Test guide](tests/README.md) |
| Reference scenarios and recorded hardware validation | [Reference controller](samples/controller/README.md) and [M3 validation](docs/m3-validation.md) |

## Project status and contributing

Follow [GitHub milestones](https://github.com/wonny-labs/crux-pilot/milestones), [issues](https://github.com/wonny-labs/crux-pilot/issues) and [pull requests](https://github.com/wonny-labs/crux-pilot/pulls) for current delivery work.

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution and review rules and [AGENTS.md](AGENTS.md) for AI execution boundaries.
