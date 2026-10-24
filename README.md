# Crux Pilot

**Crux** is an independent, reusable embedded software platform intended to support products built on **Zephyr** and **Embedded Linux**. Its direction is to combine shared platform capabilities, selective portability and repeatable engineering and validation practices.

This repository is Crux's **initial development and architecture-validation workspace**. It contains platform code, reference applications, development tools and tests used to establish and validate the platform. Broader platform vision, architecture and development planning are maintained separately.

## Current implementation

The `main` branch currently includes:

| Area | Available implementation |
| --- | --- |
| Reusable components | Portable C17 `base` and `time` components, with Linux host and Zephyr Time implementations |
| Reference application | A reference controller with shared portable Time and scheduling logic, a Linux host scenario and Zephyr device/network/health compositions |
| Device, networking and health integration | EVKB SW5 GPIO input, UART diagnostics, bounded IPv4 TCP/UDP exchanges, service readiness and workload-conditioned watchdog supervision |
| Zephyr integration | Out-of-tree module, RTC and native device/network services, Shell examples, `native_sim` tests and EVKB builds |
| Engineering foundation | CMake and west builds, Dev Container, automated tests, static checks, GitHub Actions and build-provenance evidence |

The initial hardware target is the **MIMXRT1060-EVKB revision B** using `mimxrt1060_evk@B//qspi`. Verified hardware behaviour and limitations are specific to the tested configurations.

The native Linux host runs portable code without Zephyr, while Zephyr `native_sim` exercises Zephyr integration on a host. The EVKB GPIO, networking and watchdog results apply to the documented Zephyr composition, not to the Linux host or other boards. They do not establish production readiness, long-duration reliability or concurrent-load performance. An **Embedded Linux production variant** is part of Crux's longer-term direction, not a currently implemented target. See [milestones](https://github.com/wonny-labs/crux-pilot/milestones) and [pull requests](https://github.com/wonny-labs/crux-pilot/pulls) for ongoing work.

## Architecture

Crux separates portable application behaviour, reusable platform capabilities and environment-specific integration. Shared contracts are introduced when they provide stable, useful semantics. Zephyr and Embedded Linux retain their native operating-system and device infrastructure rather than being hidden behind a universal HAL or OSAL.

Portable components and application logic avoid Zephyr, board and vendor dependencies. Host and Zephyr compositions explicitly select their implementations. Host, `native_sim` and physical-target tests provide complementary evidence rather than interchangeable guarantees.

The bounded [portable execution contract](docs/execution-contract-proposal.md)
provides joinable workers, nonrecursive mutexes and counting semaphores through
`crux::execution`. The private native providers live in
`components/execution/ports/linux/` and `components/execution/ports/zephyr/`; the
Zephyr port owns its native build definitions, selected by the top-level module.
See [validation and limits](docs/execution-validation.md) for
Host/Zephyr profiles, independent consumers and unresolved target evidence.

The implemented [Time contract](docs/time-contract.md) provides a concrete component example.

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

### Portable networking client slice

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
