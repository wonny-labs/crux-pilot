# Crux Pilot

**Crux** is an independent, reusable embedded software platform for controller and firmware applications. It provides small, capability-owned C17 interfaces and qualified standard-API profiles, with native integration for supported operating systems and boards. Its longer-term direction includes Zephyr and Embedded Linux.

This repository is the initial Crux development and architecture-validation Pilot. It contains reusable components, reference applications, tools and tests used to establish the platform. It is not a production-qualified or installable SDK.

## Capabilities

Applications select the capabilities they need. Portable application and common runtime code use public interfaces, while board/project composition selects native implementations and resources.

| Area | Components | Purpose |
| --- | --- | --- |
| Foundation and timing | `base`, `time`, `monotonic` | Common status, calendar/RTC time, elapsed time and deadlines |
| Execution and synchronisation | `thread`, `sync`, `queue` | Workers, mutexes, semaphores and bounded message passing |
| Communication and networking | `serial`, `sockets`, `netif` | UART/TTY byte streams, qualified TCP/UDP and interface status |
| Storage and configuration | `file`, `configuration` | Bounded file operations and validated application parameters |
| Diagnostics and system | `logging`, `system` | Diagnostic output, service observations, reset and watchdog services |

These components are present in `main`, but their provider support and qualification differ by environment. `sockets` uses a qualified POSIX/BSD API subset rather than a Crux-owned socket stack. See the [capability inventory](docs/architecture.md#current-capability-integration) for API contracts, provider availability, validation evidence and limitations.

## Architecture

Crux separates portable application behaviour, reusable platform capabilities and native OS/board integration. Portable consumers do not depend on unqualified native APIs or private component headers. Platform composition owns provider selection, device resources and native initialisation. Crux reuses suitable OS facilities instead of introducing a universal HAL or OSAL.

For the component model, source ownership and dependency rules, see the [architecture overview](docs/architecture.md).

### Development and target environments

| Environment | Current scope |
| --- | --- |
| Linux Host | Portable development, testing and selected Linux implementations. Not a qualified production Embedded Linux variant |
| Zephyr `native_sim` | Selected native-component and reference-controller tests without physical hardware |
| MIMXRT1060-EVKB Rev B (`mimxrt1060_evk@B//qspi`) | Initial physical target. Validated behaviour applies only to the recorded images and configurations |
| RT1170-EVK Rev A1 Cortex-M7 | Planned second physical target under M6, not yet qualified |
| Embedded Linux products | Longer-term direction, not currently implemented or qualified |

A successful Host test, `native_sim` run or EVKB cross-build does not establish physical-target qualification. In particular, writable RT1060 QSPI File/Configuration persistence remains gated by M4 storage-safety decisions and is not supported by the merged physical configuration.

## Getting started

The supported development workflow uses Docker and VS Code Dev Containers with a Linux amd64 workspace. Keep the separate Zephyr west workspace and SDK dependencies outside this repository. See the [Build and Integration Guide](docs/build.md) for prerequisites, setup and supported configurations.

### Linux Host

From the repository root inside the Dev Container, bootstrap the Host build and run the existing reference-controller example:

```sh
scripts/bootstrap-host.sh
build/linux/debug/samples/controller/host/crux-controller-host
```

For subsequent build and test runs:

```sh
scripts/check-host.sh
```

### Zephyr

Initialise the pinned Zephyr workspace, then run the controller `native_sim` scenarios and RT1060-EVKB cross-build:

```sh
scripts/bootstrap-zephyr.sh --install-sdk
scripts/test-controller.sh
```

These commands do not program or reset the physical board. See the [test guide](tests/README.md) for component-specific checks and qualification procedures.

### Use Crux from another application

External Linux source-tree projects can consume Crux with CMake `add_subdirectory()`. Base is available by default, and the application explicitly enables the required `CRUX_BUILD_<COMPONENT>` options. Public targets and native composition are selected according to the chosen capabilities.

Follow the [complete external application example](docs/build.md#external-linux-application-using-crux-source). Installed packages, `find_package(Crux)` and binary SDK distribution are not currently supported.

**Physical hardware:** Programming, resets and debug attachment need an approved target-specific procedure. The existing VS Code **EVKB QSPI: Flash** profile is configured for the Time sample, not arbitrary firmware. See the [debug configuration](.vscode/launch.json) and [physical test instructions](tests/README.md#physical-uart-target) before connecting or downloading an image.

## Repository structure

| Path | Purpose |
| --- | --- |
| `components/` | Public capability contracts, shared code and private native ports |
| `samples/` | Component examples and reference controller applications |
| `zephyr/` | Out-of-tree Zephyr module integration |
| `tests/` | Host, Zephyr, build-policy and physical-target tests |
| `scripts/` | Setup, build, analysis and validation commands |
| `.devcontainer/` | Reproducible Linux development environment |
| `.github/workflows/` | CI validation workflows |
| `docs/` | Architecture, contracts, technical decisions and evidence |

## Documentation

| Topic | Reference |
| --- | --- |
| Platform model and supported capabilities | [Architecture overview](docs/architecture.md) and [capability inventory](docs/architecture.md#current-capability-integration) |
| Setup, builds and application integration | [Build and Integration Guide](docs/build.md) |
| API contracts and environment-specific limits | [Capability contracts and validation references](docs/architecture.md#current-capability-integration) |
| Tests and qualification procedures | [Test guide](tests/README.md) |
| Reference scenarios and verified hardware behaviour | [Reference controller](samples/controller/README.md) and [controller validation](docs/m3-validation.md) |
| Contribution and review practices | [CONTRIBUTING.md](CONTRIBUTING.md) |

## Project status and contributing

Crux is under active Pilot development. Consult the [current milestones](https://github.com/wonny-labs/crux-pilot/milestones), [issues](https://github.com/wonny-labs/crux-pilot/issues) and [pull requests](https://github.com/wonny-labs/crux-pilot/pulls) for delivery status. Component availability alone does not imply physical qualification or production readiness.

The [original Time Pilot plan](docs/plans/initial-implementation-plan.md) and [implementation-status record](docs/implementation-status.md) remain historical evidence. Their M0–M5 identifiers are separate from the current platform milestones.

See [CONTRIBUTING.md](CONTRIBUTING.md) for development and review practices and [AGENTS.md](AGENTS.md) for AI execution boundaries.
