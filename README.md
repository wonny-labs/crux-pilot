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
checks in M5 are recorded there. Physical EVK evidence M4 and hosted GitHub Actions execution are
pending. No board operation, remote creation, or push is authorized.
