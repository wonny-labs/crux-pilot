# Crux Pilot

**Crux** is a reusable embedded software platform for controller and firmware applications. It provides portable C17 components and selected standard APIs, supported by OS-specific integration, build tools and tests. Portable application code remains separate from operating-system and board details.

This repository is Crux's development and architecture-validation Pilot. It currently provides Zephyr integration and a Linux development/test environment. Production Embedded Linux support is a longer-term goal. Crux is not yet a production-qualified or installable SDK.

## Capabilities

Applications select the components they need. Portable application and runtime-porting code use public interfaces, while board/project configuration selects the OS implementations and hardware resources.

| Area | Components | Purpose |
| --- | --- | --- |
| Foundation and timing | `base`, `time` | Shared status, typed clocks/authority/calendar, deadlines and bounded CPU busy-wait |
| Execution and synchronisation | `thread`, `sync`, `queue` | Threads, mutexes, semaphores and message queues |
| Communication and networking | `serial`, `sockets`, `netif` | Serial byte streams, IPv4 TCP clients/connected UDP and network-interface status |
| Storage and configuration | `file`, `configuration` | File access and application-supplied parameter validation and save/load |
| Diagnostics and system | `logging`, `system` | Diagnostic logging, service status, reset diagnostics and watchdog operations |
| Device I/O | `gpio`, `i2c`, `spi` | Synchronous GPIO input, I2C transfers and SPI exchange, each on one statically composed resource |
| Secure management | `access`, `credential`, `security_event`, `cli`, `file_service`, `config_service`, `mgmt_status`, `network_settings`, `reboot`, `tls_certificate`, `web`, `ssh` (private) | Accounts, roles and sessions shared by every management channel; status, configuration, file, network-settings and reboot services; the device TLS certificate and security events; HTTPS web management, the command line over serial and SSH, and SFTP. Tested on Linux Host, and most of it also on `native_sim` (not the reboot service or the serial console). Some parts are compiled for RT1060-EVKB; none has run on a board |

All listed components have implemented source, but available OS implementations and validation levels differ. The Sockets capability uses a documented, tested POSIX socket API subset rather than a Crux socket wrapper. For each component's contract, available implementations and limitations, see the [capability inventory](docs/architecture.md#current-capability-integration).

## Architecture

Crux separates reusable platform components and portable application code from **platform composition**: the board/project-specific configuration and initialisation that select OS implementations and bind hardware resources. Applications use Crux public APIs or documented standard API subsets. OS and device services remain within the selected platform integration.

See the [architecture overview](docs/architecture.md) for component ownership, portability boundaries and build dependencies.

### Development and target environments

| Category | Environment | Current scope |
| --- | --- | --- |
| Development and test environment | Linux Host | Linux amd64 development, testing and selected Linux implementations, not production Embedded Linux support |
| Simulated Zephyr target | Zephyr `native_sim` | Selected Zephyr component and reference-controller tests without physical hardware |
| Physical Zephyr target | MIMXRT1060-EVKB Rev B (`mimxrt1060_evk@B//qspi`) | Initial physical target, with results limited to documented firmware and configurations |
| Physical Zephyr target | RT1170-EVK Rev A1 Cortex-M7 | Second board, software scope of M6 and M7 (cross-build, identification, review); hardware verification deferred to [#155](https://github.com/wonny-labs/crux-pilot/issues/155), not qualified |
| Future variant | Production Embedded Linux | Future platform variant, not currently implemented or qualified |

**What the Linux Host providers are used for today.** They run the Host tests (CTest with GoogleTest suites and standalone C/C++ tests, see the [test guide](tests/README.md)). The `host-sanitize` preset builds them with AddressSanitizer and UndefinedBehaviorSanitizer, through `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh` and several `scripts/test-*.sh` runners. `scripts/test-mgmt-services.sh` and `tests/ssh/cli/test-thread-sanitizer.sh` run selected suites under ThreadSanitizer. In this repository these sanitizer builds run on Linux Host, while Zephyr `native_sim` runs Ztest. The Host providers also serve source-tree consumers, as in [External Linux application using Crux source](docs/build.md#external-linux-application-using-crux-source), [`tests/consumer_poc`](tests/consumer_poc/README.md) and `tests/external_application`. This is not Embedded Linux support.

Host tests, `native_sim` runs and target cross-builds do not prove physical operation. The M4 Pilot has selected microSD with Zephyr VFS/FatFs for writable File storage instead of QSPI NOR. The Zephyr Settings File backend has been tested only on `native_sim`, with corrections staged in the test fixture; there is no Settings File target provider and the backend is not qualified. Physical microSD evidence (one board, one card) covers mount, reboot persistence, power cuts during plain File appends on FatFs and a repeat of the compaction's write, close and rename order (not the backend), and blank-card, wrong-layout, stack and card-removal runs; it is not a qualification of the board, the card or the Settings File backend. See the [current storage decision](https://github.com/wonny-labs/crux-pilot/issues/14).

For each component's Zephyr composition, strongest recorded evidence, gaps and provisional stability tier, see [Component readiness](docs/component-readiness.md).

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
| `components/` | Public capability interfaces, shared implementations and OS-specific ports |
| `samples/` | Capability examples and reference controller applications |
| `zephyr/` | Out-of-tree Zephyr module integration |
| `tests/` | Host, Zephyr, build-policy and target validation |
| `web/` | Management API specification, TypeScript client, mock server and browser tests for the web examples in `samples/web`. No C build needs it. See the [web guide](web/README.md). |
| `scripts/` | Setup, build, analysis and test commands |
| `tools/` | Test tooling for a machine that holds a board's probe and UART; outside the runtime build |
| `templates/` | Copy-and-start skeletons for a product repository: `templates/product`, with its own README |
| `.devcontainer/` | Linux development environment |
| `.github/workflows/` | CI workflows |
| `docs/` | Architecture, API contracts, decisions and evidence |

## Documentation

| Topic | Reference |
| --- | --- |
| Architecture, components and API contracts | [Architecture overview and capability inventory](docs/architecture.md) |
| Setup, builds, external applications and debugging | [Build and Integration Guide](docs/build.md) |
| Composing, versioning and releasing a product on Crux | [Product Composition Guide](docs/product-composition.md) |
| Test commands and qualification procedures | [Test guide](tests/README.md) |
| Testing your own application on Crux | [Consumer test guide](docs/build.md#testing-a-consumer-application) and [test foundation decisions](docs/test-foundation-proposal.md) |
| Reference scenarios and recorded hardware validation | [Reference controller](samples/controller/README.md) and [M3 validation](docs/m3-validation.md) |

### Secure management

The documents for accounts, the management services, HTTPS web management, the command line over serial and SSH, and SFTP. **A product team that consumes Crux as a west module starts with [Secure management in a product](docs/management-in-a-product.md)**: what to enable, what is not built, limits, tested clients and browsers, and the open decisions. Nothing here has run on a board. The [capability inventory](docs/architecture.md#current-capability-integration) gives each component's validation level.

| Topic | Documents |
| --- | --- |
| Terms | [Glossary](docs/management-glossary.md): session channel and access session, provider and adapter, refuse and deny, route, listener, profiles |
| Accounts, roles, sessions and security events | [Access contract](docs/access-contract.md), [credential store](docs/credential-store.md), [security events](docs/security-event-contract.md) |
| Management services | [Status and configuration services](docs/status-configuration-services.md), [file service](docs/file-service-contract.md), [network settings](docs/network-settings-service.md), [reboot](docs/reboot-service.md) |
| HTTPS web management | [HTTPS composition](docs/https-composition.md), [web contract](docs/web-contract.md), [sessions and login](docs/web-session.md), [management APIs](docs/web-management-apis.md), [packaged web assets](docs/web-assets.md), [TLS certificate](docs/tls-certificate.md), [API specification](web/api/openapi.yaml) |
| Browser examples | [Web guide](web/README.md) with the Preact and Angular examples, [configuration flow](docs/web-config-flow.md), and [end to end on `native_sim`](docs/e2e-native-220.md) |
| Command line over serial and SSH | [Command line contract](docs/cli-contract.md), [SSH provider contract](docs/ssh-provider-contract.md), [wolfSSH provider](docs/ssh-wolf-provider.md), [command line over SSH](docs/cli-over-ssh.md) |
| SFTP | [SFTP design](docs/sftp-design.md), including the recorded WinSCP runs |
| Management services together on `native_sim` | [Management composition](docs/native-management-composition.md) and [the services image](docs/native-composition-242.md) |

## Project status and contributing

Follow [GitHub milestones](https://github.com/wonny-labs/crux-pilot/milestones), [issues](https://github.com/wonny-labs/crux-pilot/issues) and [pull requests](https://github.com/wonny-labs/crux-pilot/pulls) for current delivery work.

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution and review rules and [AGENTS.md](AGENTS.md) for AI execution boundaries.

## License

Crux is licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE).
