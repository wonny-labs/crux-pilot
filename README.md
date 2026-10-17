# Crux Pilot

A personal platform architecture experiment for a portable C17 Time API, C++17 consumption, and
Zephyr RTC integration. The repository contains the Crux module and sample; upstream Zephyr and
west dependencies stay in the parent workspace.

## Workspace and container

Open `crux-pilot/` as the primary VS Code/Codex folder. The Dev Container mounts its parent west
workspace at `/workspaces/crux` and opens `/workspaces/crux/crux-pilot`, so `west topdir` resolves to
the workspace while the Git root remains the Crux repository.

VS Code runs container commands as `vscode`. Use the same user for manual Docker execution;
Git's `safe.directory` settings belong to that user. The Dev Container post-create command trusts
only the exact workspace repositories. A manually started container needs that setup too;
`docker exec` as root does not inherit it. For example, in an initialized container:

```sh
docker exec --user vscode --workdir /workspaces/crux/crux-pilot <container> \
  bash -c 'export PATH=/opt/crux-cache/venv/bin:$PATH; scripts/test-zephyr-time.sh "mimxrt1060_evk@B//qspi"'
```

If west reports `cannot import contents of west.yml`, inspect the underlying Git error with
`git -C /workspaces/crux/zephyr show manifest-rev:west.yml` with the build's user and environment.
A dubious-ownership error requires the documented user/path setup; fetching dependencies again
does not address it.

### VS Code C/C++ support

The Dev Container installs clangd for C/C++ completion, navigation, diagnostics and formatting.
Microsoft C/C++ remains installed for GDB debugging; its IntelliSense engine is disabled so it does
not duplicate clangd's language analysis. CMake Tools remains the CMake workflow extension. The
clangd extension and clangd server are provided by the LLVM project; other installed extensions are
published by Microsoft, Red Hat or Arm.
After changing the extension list or Dockerfile, use **Dev Containers: Rebuild and Reopen in
Container** in VS Code to install the updates.

The CMake host presets and Crux Zephyr build scripts generate `compile_commands.json` files. The
standard host and Zephyr build scripts copy the active database to `build/clangd/`, which the root
`.clangd` config selects. The most recent standard build therefore controls clangd's target-specific
analysis. For a custom build directory, run `scripts/select-clangd-database.sh <build-directory>`.
If headers or defines look wrong, check the clangd Output channel and active database before adding
manual include paths.

`marus25.cortex-debug` is not installed. The Microsoft C/C++ debugger connects to a GDB server,
and Arm CMSIS Debugger is available for other J-Link workflows. Docker Desktop exposes the Mac
host to the Dev Container as `host.docker.internal`. Start the Mac-hosted J-Link GDB Server first
by double-clicking `scripts/macos/start-jlink-gdbserver.command`; it uses device
`MIMXRT1062xxx6A`, SWD at 400 kHz, and TCP port 2331. This speed has been verified for QSPI
download and a `main` breakpoint on the connected revision B board. Keep the Terminal window open
while debugging and press Ctrl-C when done.
The server listens beyond localhost, so run it only on a trusted network. Starting it may halt the
target.

In **Run and Debug**, choose **EVKB QSPI: Flash** and press F5. VS Code runs
`scripts/build-debug-target.sh 'mimxrt1060_evk@B//qspi'`, which reuses the CMake/Ninja build
directory and rebuilds only changed inputs. The debugger then connects to the Mac-hosted GDB Server,
loads its ELF and symbols, resets and programs the image through SEGGER's QSPI flash loader, then
pauses at the startup entry point. Set a breakpoint at `samples/time/src/main.c:7` and press F5 to
run to the sample, then use F10 to step through it.
No ELF path prompt is needed. This profile assumes an MIMXRT1060-EVKB using its standard QSPI flash;
it programs the board and should only be used after confirming the physical board and that its flash
has not been reworked for HyperFlash. The Microsoft debug adapter download and source breakpoint
were verified on this board; see [debugging evidence](docs/baseline.md#m4-vs-code-download-failure-and-fix-2026-10-17).

To force a full source rebuild for the Zephyr target, run **Tasks: Run Clean Task** and choose
**Clean firmware**; the next F5 rebuilds all compiled outputs while keeping the
configured build directory. After adding new board configuration files, run the pristine
verification build once so Zephyr rediscovers them; existing CMake caches retain their earlier
`CONF_FILE` list. The root CMake Tools **CMake: Clean** command applies to the selected
host preset, because the Zephyr target is configured through west. `scripts/build-target.sh` remains
the pristine build path for verification and provenance reports; it is not used by F5.

The separate **EVKB QSPI: Attach** profile is for debugging an
image already on the board. It does not build, reset, or program; provide the ELF matching that
image when prompted. The Mac-hosted GDB Server must already be running for either profile.

The firmware profile uses `miDebuggerServerAddress` and `postRemoteConnectCommands` so the
Microsoft debugger retains its default ELF loading and architecture detection. The attach profile
explicitly loads symbols before connecting. Replacing the default launch sequence with
`customLaunchSetupCommands` without loading the ELF causes `-target-download` to fail with
`No executable file specified`; GDB then disconnects before any download. `GDB closed TCP/IP
connection` in the server log describes that client disconnect.

## Linux development

Run CMake, CTest, Twister, and Zephyr builds in the Dev Container. The usual host check configures
and builds the sample and tests, runs GoogleTest/CTest, and checks the sample's stdin/stdout flow:

```sh
scripts/bootstrap-host.sh
scripts/check-host.sh
scripts/check-format.sh
scripts/check-dependencies.sh
scripts/analyze-host.sh
```

This README documents the development environment and executable build/test commands. See
[CONTRIBUTING.md](CONTRIBUTING.md) for contribution, commit, pull request, review, and validation
practices.

`host-sanitize` is the optional ASan/UBSan preset. The command
`scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'` runs the native_sim Ztests, builds the Shell
sample for native_sim, and cross-builds the EVKB QSPI image. It does not flash or reset hardware.

The target build creates `build/zephyr/mimxrt1060_evk_B_qspi/provenance.json`. It binds a
content-addressed source snapshot and Git state to west project revisions, toolchain and image
identity, target configuration, Twister reports, size reports, and firmware checksums. GitHub
Actions uploads this manifest and the referenced evidence with 14-day retention. Both workflows
can be started manually with `workflow_dispatch` after the repository is connected to GitHub.

### Run GitHub Actions

After committing this repository to GitHub, a push or pull request starts both workflows:
`host-foundation` checks the host build, tests, formatting, dependencies, and analysis;
`zephyr-software` runs native_sim tests and builds the EVKB QSPI firmware. To rerun either one,
open **Actions**, select the workflow, and choose **Run workflow**. No repository secrets are
needed for these checks. The Zephyr job uploads the firmware, provenance, configuration, size
reports, Twister results, and raw logs for 14 days.

The host sample uses a controlled clock rather than host wall time:

```text
build/linux/debug/samples/time/host/crux-time-host
time set 2026-10-16T14:30:00
time get
```

### Build and test provenance

`scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'` records inputs before the
native_sim tests and passes that successful input record to the pristine target build.
The recorder rejects source/workspace changes during the run, dependency HEADs that differ from
pinned revisions, and dependency modifications (excluding macOS Finder `.DS_Store` metadata).
Only matching, successful tests are linked into `provenance.json`.

`scripts/build-target.sh <board>` can also build independently; it does not attach old test
reports. An optional second argument supplies the successful test-input record from the full
verification command. Both target build entry points and the VS Code clean task invalidate old provenance before
changing outputs, including on failure. F5 produces development firmware without a verification manifest; use the
pristine verification path when accepting an image. Verify recorded hashes before consuming a
bundle that may have been changed by manual commands.

The manifest records the source fingerprint, commit/dirty state, actual dependency commits,
configuration, tools, size reports and firmware hashes. `source.recoverable_from_git` is true
only for a clean committed source tree. A dirty fingerprint identifies development inputs but is
not a recoverable source archive. Commit reviewed source before generating an accepted baseline.

### Configure only the host libraries

When a consumer needs the library targets without the sample or test dependencies, configure C
only, without GoogleTest or network access:

```sh
cmake -S . -B build/linux/library-only -G Ninja \
  -DBUILD_TESTING=OFF -DCRUX_BUILD_HOST_SAMPLE=OFF
cmake --build build/linux/library-only --target crux_time_common crux_time_host
```

The test-enabled build uses the pinned GoogleTest dependency and enables C++17 for the C++ tests.
Production and sample code remain C17. Those tests exercise C++ calls through the public C API;
there is no duplicate compile-only or link-only smoke target.

## Architecture and status

`components/base` owns the common status header. `components/time` owns the portable API, shared
source list, and private host/Zephyr backends. `samples/time` owns command parsing and presentation;
the host process and Zephyr Shell adapter use the same C17 handler. The Zephyr module exports the
base target independently and does not compile sample sources.

See [the Time contract](docs/time-contract.md), [implementation plan](docs/plans/initial-implementation-plan.md),
[implementation status](docs/implementation-status.md), [build evidence](docs/baseline.md), and
[upstream reuse notes](docs/upstream-reuse.md). Software milestones M0–M3 and the software-only
checks in M5 are recorded there. Remaining M4 acceptance criteria and hosted GitHub Actions
execution are pending. EVKB/J-Link download and source debugging passed, and the user reports successful guided
UART operation. Formal M4 timing/range/reset evidence and UART automation remain outstanding.
See `docs/implementation-status.md` for current acceptance status. The private GitHub repository
exists and is empty; the first push will start the configured workflows.
