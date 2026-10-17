# Crux Pilot

Crux is a reusable embedded software platform. This repository currently provides a portable C17 Time capability with Linux host and Zephyr RTC backends, samples and automated tests. C++17 tests exercise the public C API.

## Workspace and container

Open `crux-pilot/` in VS Code and use **Dev Containers: Reopen in Container**. The Linux Dev Container mounts the parent west workspace at `/workspaces/crux` and opens `/workspaces/crux/crux-pilot`. The Git root is the Crux repository. Upstream Zephyr, west dependencies and SDKs remain outside this Git tree.

Run container commands as `vscode`, including manual Docker commands. The Dev Container configures Git trust for the workspace repositories for this user. For example, in an initialised container:

```sh
docker exec --user vscode --workdir /workspaces/crux/crux-pilot <container> \
  bash -c 'export PATH=/opt/crux-cache/venv/bin:$PATH; scripts/test-zephyr-time.sh "mimxrt1060_evk@B//qspi"'
```

### VS Code C/C++ support

The Dev Container uses clangd for code navigation and diagnostics, Microsoft C/C++ for GDB debugging, and CMake Tools for host builds. After changing the container configuration, use **Dev Containers: Rebuild and Reopen in Container**.

Build scripts select the active `compile_commands.json` in `build/clangd/`. The most recent build determines clangd's configuration. For a custom build directory, run `scripts/select-clangd-database.sh <build-directory>`.

### Flash and debug

Start `scripts/macos/start-jlink-gdbserver.command` on the Mac before debugging. It runs J-Link GDB Server for `MIMXRT1062xxx6A` over SWD at 400 kHz. The container connects through `host.docker.internal:2331`. Keep the server terminal open while debugging and press Ctrl-C when finished.

**Starting the server may halt the target. It listens beyond localhost, so run it only on a trusted network. Before programming, confirm the MIMXRT1060-EVKB revision B and its standard QSPI configuration, with no HyperFlash rework.**

In VS Code **Run and Debug**, select the profile for the intended operation:

| Profile | Behaviour |
| --- | --- |
| **EVKB QSPI: Flash** | F5 performs an incremental build, resets and programs the board, and stops at the startup entry. Set a breakpoint in `samples/time/src/main.c` and continue to step through the sample. |
| **EVKB QSPI: Attach** | Connects without building, resetting or programming. Supply the ELF matching the image already on the board. |

For a full source rebuild, use **Tasks: Run Clean Task → Clean firmware** before F5. After changing board configuration, use the pristine `scripts/build-target.sh 'mimxrt1060_evk@B//qspi'` path. CMake Tools' **CMake: Clean** applies to the host preset.

Detailed setup and troubleshooting records are in the [debugging evidence](docs/baseline.md#m4-vs-code-download-failure-and-fix-2026-10-17).

## Linux development

Run build and software-test commands from the repository root in the Dev Container. For initial host dependency setup, build and tests:

```sh
scripts/bootstrap-host.sh
```

For subsequent checks using the cached dependencies:

```sh
scripts/check-host.sh
scripts/check-format.sh
scripts/check-dependencies.sh
scripts/analyze-host.sh
```

Run the optional ASan/UBSan configuration with:

```sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
```

For initial Zephyr workspace and SDK setup, ensure sufficient disk space, then run:

```sh
scripts/bootstrap-zephyr.sh --install-sdk
```

Run the Time integration tests and target build with:

```sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

This command runs `native_sim` Ztests, builds the native simulation sample and cross-builds the EVKB QSPI image. It does not flash or reset hardware. See [tests/README.md](tests/README.md) for test coverage and the separate macOS physical UART workflow, which does reset the board.

### Time sample

The host sample uses a controlled clock rather than host wall time:

```text
build/linux/debug/samples/time/host/crux-time-host
time set 2026-10-16T14:30:00
time get
```

### Run GitHub Actions

Pushes and pull requests trigger [host-foundation](.github/workflows/host.yml) for host builds, tests and quality checks, and [zephyr-software](.github/workflows/zephyr.yml) for `native_sim` tests and the EVKB build. Either workflow can also be started from **Actions → Run workflow**. Test reports, firmware and build evidence are uploaded as configured in the workflows, with 14-day retention. These jobs do not operate physical hardware.

### Build and test provenance

The full Time verification command writes `build/zephyr/mimxrt1060_evk_B_qspi/provenance.json`, linking source and dependency revisions, toolchain, configuration, successful tests and firmware checksums. Keep inputs unchanged during verification and use a clean committed source tree for an accepted baseline. A dirty-tree fingerprint identifies inputs but is not a recoverable source archive.

`scripts/build-target.sh <board>` provides a standalone pristine build without attaching earlier test reports. F5 produces development firmware without a verification manifest. Use the full verification path for an accepted build and check recorded hashes before relying on its evidence. See [build evidence](docs/baseline.md) for provenance details and recorded results.

### Configure only the host libraries

To build libraries without samples, GoogleTest or C++ configuration:

```sh
cmake -S . -B build/linux/library-only -G Ninja \
  -DBUILD_TESTING=OFF -DCRUX_BUILD_HOST_SAMPLE=OFF
cmake --build build/linux/library-only --target crux_time_common crux_time_host
```

Host and Zephyr applications use the same public target:

```cmake
target_link_libraries(my_app PRIVATE crux::time)
```

For host integration, add this repository with `add_subdirectory()` after disabling `BUILD_TESTING` and `CRUX_BUILD_HOST_SAMPLE` when those targets are not needed. For Zephyr, discover the repository as a module and enable `CONFIG_CRUX_TIME=y`. The public target supplies headers and the selected backend. Consumers should not add private include paths or link implementation targets directly.

## Architecture and status

`components/base` owns common status codes. `components/time` owns the public Time API, shared implementation and private host/Zephyr backends. `samples/time` owns command handling and presentation. `zephyr/` provides Crux's native module integration.

See the [Time contract](docs/time-contract.md) for behaviour and ownership, [upstream reuse notes](docs/upstream-reuse.md) for source assessments, and [CONTRIBUTING.md](CONTRIBUTING.md) for contribution, public-header and review rules. The [initial implementation plan](docs/plans/initial-implementation-plan.md) records the historical Time Pilot milestones M0–M5.

For physical setup and test procedures, see [physical test instructions](tests/README.md#physical-uart-target)
and the retained evidence in the baseline. These checks establish bounded behaviour, not drift or
backup-power retention.
Those M0–M5 identifiers belong to the historical Time Pilot. The current platform
roadmap M1 assessment and later implementation constraints are recorded in
[platform M1 readiness](docs/platform-m1-readiness.md).
See `docs/implementation-status.md` for current acceptance status. The private GitHub repository uses push and pull-request workflows; recorded hosted-run
results are tracked in `docs/implementation-status.md`.
