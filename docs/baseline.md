# Crux implementation and readiness evidence

Recorded from 2026-10-16 (Europe/London). Primary repo: `/Users/jiwonkim/Projects/crux-workspace/crux-pilot`.

This log contains dated snapshots; historical failures remain as evidence. The latest dated section
at the end records current results and supersedes older status summaries.

## Repository and host state

| Check | Command / source | Actual result |
| --- | --- | --- |
| Repository location/state | `pwd && git status --short --branch` | Correct primary repo; initial branch `main`, no commits; no remote. At inspection, only starter files and `.DS_Store` were untracked. Existing files were read before edits. |
| Parent workspace | `ls -la ..` | `/Users/jiwonkim/Projects/crux-workspace` contains only `crux-pilot/`; no `.west/`, `zephyr/`, or module siblings exist. It is outside the Crux Git root. |
| OS/CPU | `uname -a; uname -m; sw_vers` | macOS 15.7.4, Apple M3, `arm64`. |
| Memory | `sysctl hw.memsize` and `system_profiler SPHardwareDataType` | 8 GiB RAM. |
| Free storage at initial inspection | `df -h .` | 11 GiB free (95% volume use), below the plan's 30–40 GiB working headroom; no files were deleted. |
| Tools | `command -v` / `--version` | Present: Git 2.39.5 (Apple Git), Apple Clang 17.0.0.13.0. Absent: CMake, Ninja, clang-format, clang-tidy, Python 3.12+, uv, Docker/Podman, VS Code CLI, west, Homebrew, MacPorts. System Python is 3.9.6. |
| Docker architecture/daemon | `docker version`; `docker info` | User installed and started Docker Desktop. Client 29.8.2, Desktop 4.95.0, Engine 29.8.2; client `darwin/arm64`, engine `linux/arm64`; 8 CPUs and 4,106,604,544 bytes reported. |
| Attached USB devices | `system_profiler SPUSBDataType` | Two USB buses listed; no device entries. Does not identify or prove the presence of an EVK/probe/UART. |
| Network/dependency access | `git ls-remote` for Zephyr, example-application and GoogleTest; HTTPS reads of official upstream raw files and GitHub release API | Succeeded. Small metadata/source requests and the later pinned west fetch work. Docker was installed and started after the initial inspection. |

The initial inspection commands were:

```sh
pwd
git status --short --branch
ls -la
ls -la ..
uname -a
uname -m
sw_vers
df -h .
sysctl hw.memsize
system_profiler SPHardwareDataType
system_profiler SPUSBDataType
docker version
```

Tool inventory used `command -v` and version commands for `git cmake ninja ctest clang clang-format clang-tidy python3 uv docker podman code west brew`. No authentication action was attempted. The pinned CMake/Ninja/clang-format versions were installed into `/tmp/crux-m1-tools`; no privileged/system package changes were made. Storage later measured 41 GiB free before west bootstrap and 40 GiB afterward. After the Docker image build and SDK download, `df -h .` reported 32 GiB free; it later measured 33 GiB after SDK setup and 31 GiB after Zephyr run-test dependencies and smoke. After the Linux sanitizer runtime/image rebuild, it measured 26 GiB free, below the plan's advisory 30–40 GiB headroom; no files or images were deleted.

## Selected pinned inputs

| Input | Selection and identity | Evidence / limit |
| --- | --- | --- |
| Zephyr | `v4.4.0`, commit `684c9e8f32e4373a21098559f748f06915f950c9` | `git ls-remote https://github.com/zephyrproject-rtos/zephyr.git refs/tags/v4.4.0 'refs/tags/v4.4.0^{}'`; manifest pins the peeled commit. The project release page lists v4.4.0 as latest stable on the record date. |
| CMSIS 6 | commit `30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74` | Imported from Zephyr v4.4.0's pinned `cmsis_6` project; required by its Cortex-M build (`cmsis_core.h`). Added to this repository's west import allowlist after the EVKB compile exposed the missing dependency. |
| Example-application reference | `v4.4.0`, commit `aaff110f0d5656e8346ca43eddd1c874bf8a2f93` | Tag resolved with `git ls-remote`; reference only, no code copied. |
| GoogleTest | `v1.15.2`, commit `b514bdc898e2951020cbdca1304b75f5950d1f59`; archive SHA-256 `9257316d65d6259f6596bc4fa5464092305ab3f3d9dc6d7e5780ab462d8baa64` | Tag resolved with `git ls-remote`; the archive was streamed and hashed, 873,745 bytes. Root CMake uses the immutable commit archive and hash. |
| Zephyr SDK | `v1.0.1`, `arm-zephyr-eabi` toolchain | Zephyr v4.4.0's `SDK_VERSION` file selects 1.0.1; the migration guide sets 1.0.0 as its minimum. SDK, ARM compiler and host-tools are installed in the Docker named volume. `ZEPHYR_SDK_INSTALL_DIR` points at that persistent path. |
| west | `1.4.0` | Pinned in the Dev Container's persistent-volume venv and later installed in `/Users/jiwonkim/Projects/crux-workspace/.crux-cache/zephyr-venv`; `west topdir`, `west list`, and pinned workspace bootstrap passed. |
| Host image | `mcr.microsoft.com/devcontainers/base:ubuntu-24.04@sha256:d94c97dd9cacf183d0a6fd12a8e87b526e9e928307674ae9c94139139c0c6eae`, `linux/amd64` | Built successfully. Latest local image ID `sha256:dbbf1293a2032554bd9174c44059859e3cf8bc4f1b05b30e2fb093858b8dbbd7`. The Dockerfile includes pinned Clang 18 sanitizer runtime `libclang-rt-18-dev=1:18.1.3-1ubuntu1` for Linux ASan/UBSan builds. |
| Host build tools | CMake `3.28.4`, Ninja `1.11.1`, clang-format `18.1.8`, Clang/clang-tidy `18.1.3`, C17/C++17 | Installed identities checked inside the amd64 container. Host CMake/Ninja/format versions were also used successfully on macOS. |
| Python | Zephyr 4.4 requires Python 3.12 minimum; Ubuntu 24.04 provides the selected platform baseline | Container reports Python 3.12.3. The macOS Python 3.9.6 was not used for Zephyr requirements. |

Zephyr's v4.4 migration guide records minimum Python 3.12 and SDK 1.0.0. Exact resolved source tags and commits are in this file and `west.yml`. Apt tool versions are exact-pinned because the base-image digest does not pin mutable apt repositories; capture the actual installed version strings after the first container build.

## Hardware and range assessment

- Selected board: NXP MIMXRT1060-EVKB. [Zephyr v4.4.0's board guide](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/boards/nxp/mimxrt1060_evk/doc/index.rst) maps EVKB to revision B; its out-of-box target uses QSPI, while HyperFlash is described as a board rework. The explicit build target `mimxrt1060_evk@B//qspi` compiled successfully. Since the board is not attached, the physical PCB marking, any flash rework, probe firmware, macOS USB serial device, and reset behavior remain unverified. No hardware operation was attempted.
- User-provided NXP product-page specifications for MIMXRT1060-EVKB list the MIMXRT1062DVL6B processor, 64 Mbit QSPI flash, and 512 Mbit HyperFlash. This supports the EVKB product-level component inventory, but listing both memories does not establish which flash is connected/selected on the user's physical board or whether it was reworked. The actual board marking/revision, debug-probe identity/firmware, and macOS USB serial path remain unverified. Source: [NXP MIMXRT1060-EVKB product page](https://www.nxp.com/design/design-center/development-boards-and-designs/MIMXRT1060-EVKB?tab=Documentation_Tab).
- User plans to use a SEGGER J-Link debug probe and the J41 USB connection for the console. Zephyr v4.4.0 lists the J-Link runner as supported by NXP and documents EVKB's SWD isolation jumpers (J9/J10), 20-pin JTAG/SWD header (J2), and OpenSDA USB-to-serial console on J41 with J45/J46 connected and 115200 baud, 8-N-1. These record the intended setup and upstream guidance, not actual probe model/firmware, USB enumeration, UART device path, or host tool installation; none was checked or operated. No jumper changes were made. Source: [Zephyr v4.4.0 EVK programming and debug guide](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/boards/nxp/mimxrt1060_evk/doc/index.rst).
- Zephyr v4.4.0 documents the on-board OpenSDA interface as the USB-to-serial console via J41, with J45/J46 installed by default, and console settings 115200 baud, 8-N-1; SW9 is the documented reset button. These are upstream instructions, not observations of this user's board. Hardware acceptance still requires confirming the actual board/USB enumeration before any physical action.
- The v4.4 RT1060 EVK devicetree identifies `nxp,mimxrt1062`; its `rtc` alias points to an enabled `zephyr,rtc-counter` child nested under the SNVS RTC node (`nxp,imx-snvs-rtc`). The MCUX SNVS Counter driver consumes that RTC-compatible node, reports 1 Hz and `UINT32_MAX` top.
- Zephyr `struct rtc_time` stores each calendar member, including `tm_year`, as `int`; it is not a `time_t` API.
- The generic `rtc_counter` path uses `timeutil_timegm64()` for input but converts output through `time_t` and `gmtime_r()`. It rejects derived seconds above `UINT32_MAX`. Calendar seconds from 2000 through 2099 fit that unsigned 1 Hz counter range, but target `time_t`/libc behavior still needs a compile and test. The v4.4 DTS and counter driver identify the actual candidate SNVS path; a board build must confirm the selected Kconfig/devicetree configuration. Source inspection is not target acceptance.
- The selected baseline's RTC sample sets `2024-11-17 04:19:00` on startup and its test expects `04:19:01`; it cannot be used as an unchanged Crux behavior or test oracle.

## M0–M1 work sequence

1. Build the pinned amd64 Dev Container; record `docker image inspect` identity, resource allocation, and `cmake`, compiler, Ninja, Python, LLVM, west versions. (Done; see latest execution log.)
2. Run `scripts/bootstrap-host.sh`, then `scripts/check-host.sh` with GoogleTest already cached and CMake FetchContent disconnected.
3. Run `scripts/check-format.sh`, `scripts/check-dependencies.sh`, and `scripts/analyze-host.sh`.
4. Run `scripts/bootstrap-zephyr.sh` from the repo with the pinned Python 3.12 environment active (or set `CRUX_PYTHON`); it fetches west projects and installs Zephyr base, build/test and run/test requirements plus pinned west 1.4.0. (Done.) Install SDK v1.0.1 with `scripts/bootstrap-zephyr.sh --install-sdk`. (Done; see execution log.)
5. Run `scripts/check-zephyr-smoke.sh`; verify `west topdir`, module listing, generated host/native artifacts and Twister report. (Done: native_sim Hello World and C++ Hello World built; Twister passed 1/1; Crux module discovery was reported for native and EVKB configurations; `mimxrt1060_evk@B//qspi` Hello World cross-compiled. Do not flash.)
6. Update AC status with command outputs and actual results. Stop before M2.

## Latest resumed M0–M1 execution (2026-10-16)

| Command / check | Actual result |
| --- | --- |
| `df -h .` | 41 GiB available at start of resumed work; 40 GiB after west bootstrap. The 30–40 GiB plan headroom is met. |
| `docker version`; `docker info` | Pass after user installed and started Docker Desktop: Client 29.8.2 / `darwin/arm64`, Desktop 4.95.0, Engine 29.8.2 / `linux/arm64`, 8 CPUs and 4,106,604,544 bytes memory. |
| `python3 -m venv ../.crux-cache/zephyr-venv && ../.crux-cache/zephyr-venv/bin/python -m pip install --disable-pip-version-check --no-cache-dir west==1.4.0 && ../.crux-cache/zephyr-venv/bin/west --version` | Pass. Installed pinned west v1.4.0 in the external workspace cache; no system or repo installation. |
| `PATH="/Users/jiwonkim/Projects/crux-workspace/.crux-cache/zephyr-venv/bin:/tmp/crux-m1-tools/bin:$PATH" CRUX_CACHE_DIR="/Users/jiwonkim/Projects/crux-workspace/.crux-cache" scripts/bootstrap-zephyr.sh` (host) | Pass for west workspace initialization/fetch. This host-side run predates the container requirements installation; it is not evidence that host Python 3.9 can install Zephyr requirements. |
| `/Users/jiwonkim/Projects/crux-workspace/.crux-cache/zephyr-venv/bin/west topdir && /Users/jiwonkim/Projects/crux-workspace/.crux-cache/zephyr-venv/bin/west list` (from parent workspace) | Pass. `west topdir` is `/Users/jiwonkim/Projects/crux-workspace`; manifest, Zephyr, CMSIS, and NXP HAL resolve to expected siblings and pinned revisions. |
| `PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/check-host.sh` | Pass on resumed run with CMake FetchContent disconnected; CTest 1/1 passed. |
| `PATH=/tmp/crux-m1-tools/bin:$PATH CLANG_FORMAT=/tmp/crux-m1-tools/bin/clang-format scripts/check-format.sh`; `scripts/check-dependencies.sh`; `bash -n scripts/*.sh` | Pass. Formatting, host/Zephyr build boundary, and shell syntax pass after the bootstrap update. |
| `python3 -m json.tool .devcontainer/devcontainer.json`; `python3 -m json.tool CMakePresets.json`; Ruby YAML parse for `west.yml`, host workflow, and `zephyr/module.yml` | Pass; all JSON and YAML parsed. |
| Hardware command scan `rg -n 'west[[:space:]]+flash|nrfjprog|JLinkExe|pyocd|openocd' scripts/check-zephyr-smoke.sh scripts/bootstrap-zephyr.sh` | No matches; bootstrap and smoke scripts do not invoke flash/debug probe commands. |
| `west sdk install --help` (after west update) | Blocked loading Zephyr's SDK extension: `ModuleNotFoundError: No module named 'patoolib'`. |
| `docker build --platform linux/amd64 -f .devcontainer/Dockerfile -t crux-pilot-dev .` (re-run after image lookup error) | Pass. Docker reported `naming to docker.io/library/crux-pilot-dev:latest`; image inspect reports the ID above and `linux/amd64`. Dockerfile includes Zephyr v4.4 Linux development prerequisites. |
| User's `docker run ... crux-pilot-dev ...` diagnostic | First attempt failed before container start: `Unable to find image 'crux-pilot-dev:latest' locally` / `pull access denied for crux-pilot-dev`. `docker context show` returned `desktop-linux`; `docker image inspect crux-pilot-dev` confirmed the image was absent in that engine. After rebuilding, the same diagnostic passed: `/opt/crux-cache/venv/bin/cmake` version 3.28.4, `/opt/crux-cache/venv/bin/west` version 1.4.0, and executable `/opt/crux-cache/sdk/zephyr-sdk-1.0.1/setup.sh` present. The existing named volume retained its SDK files. |
| Read-only SDK host-tools inspection (Docker volume mounted `:ro`) | User's pasted diagnostic output matches. `setup.sh` checks CMake and then invokes `hosttools/zephyr-sdk-x86_64-hosttools-standalone-0.10.sh -y -d .`; its output is redirected to `/dev/null`, and a nonzero exit becomes the generic `Host tools installation failed` message (script lines 297–304). The 68 MiB installer and partial host-tools files existed; this inspection alone did not prove successful installation. The next row records the explicit successful invocation. |
| `docker run --rm --platform linux/amd64 --memory=4g --cpus=2 -v /Users/jiwonkim/Projects/crux-workspace:/workspaces/crux -v crux-host-cache:/opt/crux-cache -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev <check-command>` | Pass after fixing `/opt/crux-cache` ownership. Replaced `<check-command>` with the in-container identity/mount inspection, host-check, Zephyr bootstrap and smoke commands below. `arch=x86_64`; Git root `/workspaces/crux/crux-pilot`; `west topdir=/workspaces/crux`; `.west`, Zephyr, CMSIS, NXP HAL siblings visible. CMake 3.28.4, clang/clang++ 18.1.3, Ninja 1.11.1, Python 3.12.3, west 1.4.0, clang-format 18.1.8, clang-tidy 18.1.3. Only the documented parent bind and named cache volume were mounted; no home, credentials or Docker socket. |
| Container `scripts/bootstrap-zephyr.sh` | Pass. Installed Zephyr `requirements-base.txt`, `requirements-build-test.txt`, `requirements-run-test.txt`, and pinned west==1.4.0 into the persistent cache venv. |
| Initial container `scripts/bootstrap-zephyr.sh --install-sdk` | Failed with `ERROR: Host tools installation failed` / `FATAL ERROR: command "/opt/crux-cache/sdk/zephyr-sdk-1.0.1/setup.sh -t arm-zephyr-eabi -h" failed`. A direct `setup.sh -t arm-zephyr-eabi -h` diagnostic outside the venv PATH exited 90 with `cmake` not found; that diagnostic did not reproduce the wrapper's environment. The first generic error's underlying child failure was hidden by the SDK script's output redirection. Relevant repo files: `scripts/bootstrap-zephyr.sh`, `.devcontainer/devcontainer.json`; installer state is in the external Docker named volume. |
| SDK host-tools recovery and bootstrap | The user authorized continuing after the earlier report. Direct command `cd /opt/crux-cache/sdk/zephyr-sdk-1.0.1/hosttools && ./zephyr-sdk-x86_64-hosttools-standalone-0.10.sh -y -d .` completed with exit 0 and `SDK has been successfully set up and is ready to be used.` Then `scripts/bootstrap-zephyr.sh --install-sdk` completed with exit 0, installed SDK v1.0.1 and ARM toolchain, and printed `All done.` Initial generic output was suppressed by `setup.sh`; its source shows the failing child installer and redirection at lines 297–304. The underlying first failure cause was not recovered from that suppressed run. |
| First `scripts/bootstrap-zephyr.sh --install-sdk` rerun | Failed before SDK work with `fatal: detected dubious ownership in repository at '/workspaces/crux/crux-pilot'`. Retried with per-process Git `safe.directory` entries for the four exact checked-out repos; no host/global Git settings were changed. Wrapper then completed successfully. `.devcontainer/devcontainer.json` post-create now adds those exact safe-directory entries inside the container. |
| Initial post-install `scripts/check-zephyr-smoke.sh` in a fresh `docker run --rm` container | Failed before native_sim configure because CMake's SDK package registration was written under ephemeral `/home/vscode/.cmake/packages/Zephyr-sdk`; that container's home disappears after exit. Zephyr v4.4 `FindZephyr-sdk.cmake` supports `ZEPHYR_SDK_INSTALL_DIR`, so the Dev Container now sets it to `/opt/crux-cache/sdk/zephyr-sdk-1.0.1`. |
| Smoke after setting `ZEPHYR_SDK_INSTALL_DIR` | native_sim configured and built; CMake printed `Crux Pilot Zephyr module discovered`. Twister first reported missing `natsort`, then after run-test requirements were added reported missing `junitparser`. These were missing Zephyr `requirements-run-test.txt` and `requirements-build-test.txt` dependencies, not SDK errors. `scripts/bootstrap-zephyr.sh` was amended to install both sets in addition to `requirements-base.txt`. |
| Initial final `scripts/check-zephyr-smoke.sh` before EVKB selection | Pass, exit 0. native_sim Hello World configured and built with host GCC; Crux module discovery confirmed. Twister ran 1 test configuration / 1 test case and passed (no warnings). C++ Hello World configured and built; Crux module discovery confirmed again. EVK compile was skipped at that time because `CRUX_BOARD_TARGET` was unset and the board/revision had not yet been identified. No flash or hardware action occurred. |
| Generated artifacts | Verified from the host after container exit: `../build/zephyr/zephyr.elf` (579 KiB) and `../build/zephyr/zephyr.dts` exist; `../twister-out/twister.json`, `twister.xml`, and `twister_report.xml` exist. These outputs are outside the `crux-pilot` Git root in the parent workspace. |
| Final config/shell validation | `python3 -m json.tool .devcontainer/devcontainer.json` and `bash -n scripts/*.sh` passed after the SDK path, Git safe directories, and Zephyr build/test dependency updates. Container `git rev-parse` passed for Crux and all three west repositories after the exact in-container safe-directory config. |
| Container host foundation (historical preset name): `CRUX_HOST_PRESET=host-debug-container scripts/bootstrap-host.sh`; then `scripts/check-host.sh && scripts/check-format.sh && scripts/check-dependencies.sh && scripts/analyze-host.sh` | Pass. This was the original separate container preset, before the Linux-only preset consolidation recorded below. CTest 1/1 passed; offline host check, format, dependency boundary and clang-tidy passed. |
| `df -h .` after image and SDK work | 31 GiB available after Zephyr run-test dependency installation and final smoke. This remains within the planned 30–40 GiB headroom range. |

Container invocation and check commands (run from the primary repo; Docker CLI was used to reproduce the Dev Container settings):

```sh
docker build --platform linux/amd64 -f .devcontainer/Dockerfile -t crux-pilot-dev .
docker volume create crux-host-cache
docker run --rm --user root -v crux-host-cache:/opt/crux-cache crux-pilot-dev \
  chown -R vscode:vscode /opt/crux-cache
docker run --rm --platform linux/amd64 --memory=4g --cpus=2 \
  -v "$PWD/..:/workspaces/crux" \
  -v crux-host-cache:/opt/crux-cache \
  -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev \
  bash -lc 'uname -m; git rev-parse --show-toplevel; west topdir; west list; cmake --version; clang --version; ninja --version; python3 --version; west --version; clang-format --version; clang-tidy --version'
docker run --rm --platform linux/amd64 --memory=4g --cpus=2 \
  -v "$PWD/..:/workspaces/crux" \
  -v crux-host-cache:/opt/crux-cache \
  -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev \
  bash -lc 'export PATH=/opt/crux-cache/venv/bin:$PATH; CRUX_HOST_PRESET=host-debug scripts/bootstrap-host.sh; scripts/check-host.sh; scripts/check-format.sh; scripts/check-dependencies.sh; scripts/analyze-host.sh'
docker run --rm --platform linux/amd64 --memory=4g --cpus=2 \
  -v "$PWD/..:/workspaces/crux" \
  -v crux-host-cache:/opt/crux-cache \
  -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev \
  bash -lc 'git config --global --add safe.directory /workspaces/crux/crux-pilot; git config --global --add safe.directory /workspaces/crux/zephyr; git config --global --add safe.directory /workspaces/crux/modules/hal/cmsis; git config --global --add safe.directory /workspaces/crux/modules/hal/nxp; export PATH=/opt/crux-cache/venv/bin:$PATH; export CRUX_PYTHON=/opt/crux-cache/venv/bin/python; export CRUX_WORKSPACE=/workspaces/crux; export CRUX_CACHE_DIR=/opt/crux-cache; export ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1; scripts/bootstrap-zephyr.sh'
docker run --rm --platform linux/amd64 --memory=4g --cpus=2 \
  -v "$PWD/..:/workspaces/crux" \
  -v crux-host-cache:/opt/crux-cache \
  -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev \
  bash -lc 'git config --global --add safe.directory /workspaces/crux/crux-pilot; git config --global --add safe.directory /workspaces/crux/zephyr; git config --global --add safe.directory /workspaces/crux/modules/hal/cmsis; git config --global --add safe.directory /workspaces/crux/modules/hal/cmsis_6; git config --global --add safe.directory /workspaces/crux/modules/hal/nxp; export PATH=/opt/crux-cache/venv/bin:$PATH; export CRUX_WORKSPACE=/workspaces/crux; export CRUX_CACHE_DIR=/opt/crux-cache; export ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1; export CRUX_BOARD_TARGET="mimxrt1060_evk@B//qspi"; scripts/check-zephyr-smoke.sh'
```

For the checks above, the persistent cache venv had the Dev Container `postCreateCommand` packages and Zephyr requirements installed. SDK host-tools setup and full SDK bootstrap completed after the user explicitly asked to continue. CMake package registration under a disposable Docker CLI container was not persistent, so `ZEPHYR_SDK_INSTALL_DIR` now points directly to the named-volume SDK path. No hardware was attached or operated.

The first resumed bootstrap invocation used a relative PATH and exited 127 with `west: command not found`; the absolute-path invocation succeeded. Host Python 3.9 could not satisfy Zephyr v4.4 requirements, while container Python 3.12.3 installed base, build/test and run/test requirements. The initial SDK host-tools call failed with suppressed child output; a direct host-tools invocation then succeeded, followed by successful SDK bootstrap. The first fresh-container smoke missed the ephemeral CMake package registration; setting the persistent SDK path fixed that. Twister exposed two missing requirement sets, which were added to bootstrap; the final smoke passed. Docker CLI reproduced the documented resource limits, parent bind, primary folder and cache volume, but the VS Code Dev Containers extension UI itself was not launched. No hardware operation or flash command was run.

## Current build policy (2026-10-16)

Per the user's direction, all CMake host builds and Zephyr smoke builds now run on Linux in the Dev Container. The M1-prescribed CMake presets are named `host-debug` and optional `host-sanitize`; their conditions expose them only when CMake runs on Linux. This removes the Mac/container duplicate while preserving the plan's preset names. `scripts/bootstrap-host.sh`, `scripts/check-host.sh`, `scripts/analyze-host.sh`, and `scripts/check-zephyr-smoke.sh` also stop with exit 2 on non-Linux hosts. Historical macOS build results above remain evidence of the earlier M1 verification; they are not the current supported build path.

Current commands, from `/workspaces/crux/crux-pilot` inside the Dev Container:

```sh
# Standard C17/C++17 smoke configure, build, and test
scripts/bootstrap-host.sh

# Offline repeat of configure, build, and test
scripts/check-host.sh

# Optional Linux ASan/UBSan configure, build, and test
cmake --preset host-sanitize -S . -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build --preset host-sanitize
ctest --preset host-sanitize

# Zephyr native_sim, Twister, C++ smoke; no flash
scripts/check-zephyr-smoke.sh
```

The CMake Tools extension should select `host-debug` for both configure and build presets while attached to the Dev Container. The preset conditions and script guards prevent the documented build flow from running on macOS.

| Current Linux-only check | Command | Actual result |
| --- | --- | --- |
| Host preset discovery | `/tmp/crux-m1-tools/bin/cmake --list-presets` on macOS | No Linux presets were listed. `scripts/bootstrap-host.sh` exited 2 with `Host CMake builds are configured for Linux only; reopen this repository in the Dev Container.` |
| Linux preset discovery | Docker CLI with the documented `linux/amd64` image and cache volume: `/opt/crux-cache/venv/bin/cmake --list-presets` | Listed only `host-debug` and `host-sanitize`. |
| Linux debug CMake/CTest | Docker CLI, then `scripts/check-host.sh` | Pass. CMake 3.28.4/Clang 18.1.3 configured and built in `build/linux/debug`; CTest 1/1 passed. |
| Initial Linux sanitizer configure | `cmake --preset linux-sanitize -S . -DFETCHCONTENT_FULLY_DISCONNECTED=ON` inside Docker, before preset names were aligned to the M1 plan | Failed because `/usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.asan_static-x86_64.a` and `libclang_rt.asan-x86_64.a` were absent. |
| Sanitizer runtime fix | Added `libclang-rt-18-dev=1:18.1.3-1ubuntu1` to `.devcontainer/Dockerfile` and rebuilt `crux-pilot-dev` | Image build passed; latest image is `sha256:dbbf1293a2032554bd9174c44059859e3cf8bc4f1b05b30e2fb093858b8dbbd7`, `linux/amd64`. |
| Initial Linux sanitizer configure/build/test (temporary preset name) | `cmake --preset linux-sanitize -S . -DFETCHCONTENT_FULLY_DISCONNECTED=ON && cmake --build --preset linux-sanitize && ctest --preset linux-sanitize` inside the rebuilt Docker image | Pass after adding the Clang runtime; CTest 1/1 passed in `build/linux/sanitize`. It was rerun below with the M1-prescribed `host-sanitize` name. |
| Linux format, dependency, analysis, and initial Zephyr smoke before EVKB selection | Container command with `CLANG_FORMAT=clang-format`: `scripts/check-format.sh; scripts/check-dependencies.sh; scripts/analyze-host.sh; scripts/check-zephyr-smoke.sh` | Pass, exit 0. Clang-tidy emitted and suppressed third-party warnings; Zephyr native_sim C and C++ smoke builds and Twister completed. EVK compile was skipped at that time because no target had been selected. |
| Docker storage after image rebuild | `df -h .` | 26 GiB free; no cleanup was performed. |

The CLI format check initially used the script's default `clang-format-18` name, which is not provided by the Python formatter package in the cache venv. Re-running with the Dev Container's configured `CLANG_FORMAT=clang-format` passed. The Dev Container now declares the CMake Tools extension so it is installed in the Linux VS Code environment.

| Final M1 preset-name verification | Command | Actual result |
| --- | --- | --- |
| Final Linux preset discovery | Docker CLI: `/opt/crux-cache/venv/bin/cmake --list-presets` | Pass; only `host-debug` and `host-sanitize` are available. |
| Final Linux debug build/check | Docker CLI: `scripts/check-host.sh` | Pass; CMake/CTest succeeded, 1/1 test passed in `build/linux/debug`. |
| Final Linux sanitizer build/check | Docker CLI: `cmake --preset host-sanitize -S . -DFETCHCONTENT_FULLY_DISCONNECTED=ON && cmake --build --preset host-sanitize && ctest --preset host-sanitize` | Pass; CTest 1/1 passed in `build/linux/sanitize`. |
| Initial EVKB cross-build without Dev Container SDK environment | `west build -p always -b 'mimxrt1060_evk@B//qspi' /workspaces/crux/zephyr/samples/hello_world -- -DZEPHYR_EXTRA_MODULES=/workspaces/crux/crux-pilot` | Failed during CMake configure because `Zephyr-sdkConfig.cmake` was not found. The Docker CLI invocation did not inherit Dev Container `containerEnv`; rerun with `ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1`. |
| EVKB cross-build with SDK path | Same `west build` command with `ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1` | Board resolved as revision B, qualifiers `mimxrt1062/qspi`, then failed compiling `offsets.c`: `cmsis_core.h: No such file or directory`. `west.yml` imported `cmsis` but omitted Zephyr v4.4.0's separate pinned `cmsis_6` project. |
| CMSIS 6 manifest and container fix | Added `cmsis_6` to `west.yml`'s Zephyr import allowlist and `/workspaces/crux/modules/hal/cmsis_6` to `.devcontainer/devcontainer.json`'s `safe.directory` setup; reran `scripts/bootstrap-zephyr.sh` from the documented container mount | Pass after adding the new path to the one-off Git safe-directory list. Imported CMSIS 6 revision `30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74`; Zephyr requirement packages were already satisfied. The first update attempt stopped because the new checkout was not yet a Git safe directory; its safe-directory entry is now part of Dev Container setup. |
| Full Zephyr smoke with EVKB target | Docker CLI using the documented parent bind and cache volume; `CRUX_BOARD_TARGET=mimxrt1060_evk@B//qspi scripts/check-zephyr-smoke.sh` | Pass, exit 0. native_sim Hello World compiled; Twister passed 1/1 in 80.43 s; C++ native_sim Hello World compiled; EVKB Hello World compiled and linked all 157 steps. Flash/reset was not invoked. Zephyr DTS emitted one `unique_unit_address_if_enabled` warning for upstream `usbd@402e0000`/`usbh@402e0000`; build succeeded. |

## M2 Time host implementation verification (2026-10-16)

All M2 work stayed in the Linux Dev Container and used the documented parent workspace bind, named cache volume and `crux-pilot` as the working directory. No board, J-Link, UART or flash operation was used. The reproducible debug and sanitizer runs were:

```sh
docker run --rm --platform linux/amd64 --memory=4g --cpus=2 \
  -v "$PWD/..:/workspaces/crux" -v crux-host-cache:/opt/crux-cache \
  -w /workspaces/crux/crux-pilot --user vscode crux-pilot-dev \
  bash -lc 'set -e
    git config --global --add safe.directory /workspaces/crux/crux-pilot
    export PATH=/opt/crux-cache/venv/bin:$PATH
    export CRUX_WORKSPACE=/workspaces/crux
    export CRUX_CACHE_DIR=/opt/crux-cache
    export ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1
    scripts/check-host.sh
    CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh'
```

Both configurations completed CMake configure, Ninja build and CTest with 28/28 tests passing. Each also passed `scripts/check-host-sample.sh` with a deterministic stdin/stdout scenario. The test set includes the M1 C17/C++17 smoke and M2 Time API, calendar, command and production-link tests.

Additional container checks, after setting `CLANG_FORMAT=clang-format`, passed `scripts/check-format.sh`, `scripts/check-dependencies.sh` and `scripts/analyze-host.sh`. Clang-Tidy suppressed third-party header warnings; no owned-code diagnostic remained. The deliberate M2-07 boundary test temporarily added `#include <zephyr/kernel.h>` to `components/time/src/time.c`; `scripts/check-dependencies.sh` failed as expected with exit 1 and the native/host dependency diagnostic. The source was restored and the ordinary dependency check then passed.

| Initial result | Resolution / final status |
| --- | --- |
| First M2 build reported missing public include paths and undeclared `NULL` in common C sources. | Added target include requirements and standard `<stddef.h>` includes. A subsequent build found the same missing include in the host backend; fixed it. Final debug and sanitizer builds passed. |
| First CTest run showed `crux_time_tests_NOT_BUILT` placeholders although binaries linked. | `include(GoogleTest)` existed only in the sibling smoke-test directory, leaving the M2 discovery script path empty. Added it to `tests/time/CMakeLists.txt`; final discovery ran all 28 tests. |
| Initial formatter invocation used its default `clang-format-18`, which was not found in the venv. The first check with the correct executable found formatting differences. | Set `CLANG_FORMAT=clang-format`, formatted owned C/C++ files and reran successfully. |
| Clang-Tidy flagged duplicate adjacent branches in the CLI status mapping. | Removed the duplicate `CRUX_ERR_IO` branch; the default branch preserves `ERR IO_ERROR`. Analysis then passed. |
| First deliberate dependency mutation check was blocked by Git's dubious-ownership guard in the temporary container. | Added the exact `safe.directory` entry inside that container, repeated the negative test successfully, restored the source and confirmed the normal dependency check passed. |

At the time of this 2026-10-16 snapshot, M2-08 was failing focused analysis after the C command-handler refactor; see the later 2026-10-17 section for the revised analyzer result. M0-05 and M4 physical evidence remain pending because no board was operated. M1-06 passes from local failure-propagation evidence and the non-flashing build path; the GitHub Actions workflows have not been executed, so M5-03 remains pending external CI validation because no remote or push was authorized.

## M3 Zephyr integration and M5 software-only evidence (2026-10-16)

All work used the running Linux Dev Container (`linux/amd64`, image ID `sha256:dbbf1293a2032554bd9174c44059859e3cf8bc4f1b05b30e2fb093858b8dbbd7`), mounted parent workspace `/workspaces/crux`, and `crux-pilot` Git root. Pinned worktree revisions were Zephyr `v4.4.0` commit `684c9e8f32e4373a21098559f748f06915f950c9`, CMSIS 6 commit `30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74`, and NXP HAL commit `2c2f28ac333e995d7279777409817c4b4f92c1ec`. Tools: west `1.4.0`, Zephyr SDK `1.0.1` (`arm-zephyr-eabi-gcc` 14.3.0), CMake `3.28.4`, Ninja `1.11.1`, and Clang `18.1.3`. Build output and raw reports remain in ignored `build/` paths; no upstream files were copied into the repository.

The initial M3 implementation shared the C17 Time sources and C++17 command handler between host and Zephyr. The C17 command-handler refactor and its replacement build evidence are recorded later in this section. The Zephyr backend binds the standard `rtc` alias, uses `rtc_get_time()`/`rtc_set_time()`, maps native errors to Crux status values, and keeps Zephyr types private. The Zephyr sample explicitly initializes Time before printing `CRUX_TIME_READY`; Zephyr RTC Shell commands are disabled. `CONFIG_RTC_INIT_PRIORITY=61` is set because the pinned upstream RTC-counter wrapper asserts that its initialization priority is greater than the SNVS counter driver's priority 60.

The native_sim Ztest commands run from `/workspaces/crux` inside the container:

```sh
west twister -p native_sim/native/64 \
  -T /workspaces/crux/crux-pilot/tests/zephyr_time \
  -O /workspaces/crux/crux-pilot/build/zephyr/m5-final-twister --inline-logs

west build -p always -b native_sim/native/64 \
  -d /workspaces/crux/crux-pilot/build/zephyr/m5-final-native-sample \
  /workspaces/crux/crux-pilot/samples/time -- \
  -DZEPHYR_EXTRA_MODULES=/workspaces/crux/crux-pilot \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

/workspaces/crux/crux-pilot/scripts/build-target.sh 'mimxrt1060_evk@B//qspi'
```

The final clean Twister run completed in 234.33 seconds with no warnings: 2/2 configurations and 4/4 Ztest cases passed. In addition to the real native_sim RTC no-data/set/get path and RTC-disabled NOT_READY case, tests cover Crux mapping of `-ENODATA`, `-EINVAL`, `-ERANGE`, `-ENODEV`, `-ENXIO`, and fallback `-EIO`. This is simulator/backend evidence, not silicon behavior. Twister reports and raw logs are in `build/zephyr/m5-final-twister/`. The final native_sim Shell application build passed all 140 steps and generated `build/zephyr/m5-final-native-sample/zephyr/zephyr.elf`.

The pristine EVKB target build `scripts/build-target.sh 'mimxrt1060_evk@B//qspi'` completed successfully (204/204 compile/link steps; ROM and RAM reports exited 0). Generated `.config` says revision B and `mimxrt1062/qspi`. Generated `zephyr.dts` maps `rtc = &counter_rtc` to `nxp,imx-snvs-rtc` and its `zephyr,rtc-counter` child. At that time the sample enabled C++17 for its command handler; the later C17 sample rebuild below supersedes that configuration. `CONFIG_RTC_INIT_PRIORITY=61` is set, and `CONFIG_RTC_SHELL` is disabled; upstream counter priority is 60. Zephyr's linker report gives 65,772 B FLASH / 8 MiB and 8,448 B RAM / 32 MiB. `rom.json` totals 65,764 bytes and `ram.json` totals 8,426 bytes; these are static build reports, not measured board memory use.

The final `zephyr.elf` SHA-256 is `3b395af12960b006b5815ce216bafa15549a1b951644b38dc4ee95035f274342`; `zephyr.bin` is `bf5b7d8d85e066b83a2dbf3af1c6dcaaa63e0bc2bc3b4b5e97e50b91e5aebce6`. The BIN checksum matches the preserved pre-mapping image; the ELF changed after making the RTC error mapper directly testable, and its final map includes `crux_time_zephyr_status_from_rtc_error`. Pre-mapping artifacts remain in `build/evidence/m5-pre-mapping-target/zephyr/` and `build/evidence/m5-prior-target/zephyr/`. Current `.config`, generated DTS, `.map`, `.stat`, `rom.json`, and `ram.json` remain under `build/zephyr/mimxrt1060_evk_B_qspi/`; full final clean-build output is `build/evidence/m5-evkb-build.log`.

Clean host reproduction used separate output directories so earlier evidence remained available:

```sh
cmake --preset host-debug -S . -B build/linux/m5-repro-debug \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build/linux/m5-repro-debug
ctest --test-dir build/linux/m5-repro-debug --output-on-failure
ctest --test-dir build/linux/m5-repro-debug --output-junit \
  build/linux/m5-repro-debug/ctest-junit.xml
scripts/check-host-sample.sh host-debug build/linux/m5-repro-debug

cmake --preset host-sanitize -S . -B build/linux/m5-repro-sanitize \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build/linux/m5-repro-sanitize
ctest --test-dir build/linux/m5-repro-sanitize --output-on-failure
ctest --test-dir build/linux/m5-repro-sanitize --output-junit \
  build/linux/m5-repro-sanitize/ctest-junit.xml
```

Both clean host presets built from the pinned cached GoogleTest dependency and passed 28/28 CTest cases. The fresh debug sample stdin/stdout scenario passed. `LastTest.log` and JUnit XML for both clean runs are retained in their build directories. The updated `scripts/check-host.sh` was run in both `host-debug` and `host-sanitize`; each regenerated CTest JUnit output and passed all 28 tests plus the host sample scenario. `scripts/check-format.sh`, `scripts/check-dependencies.sh`, and `scripts/analyze-host.sh` passed; Clang-Tidy suppressed third-party diagnostics and emitted no owned-code diagnostic. A first clean-format check exposed formatting in `time_port_zephyr.c`; the file was formatted and the check passed. A first sample invocation found its old output path; `check-host-sample.sh` and README now use `samples/time/host/crux-time-host`, and the scenario passed from the clean output.

M5-02 negative check: wrote the temporary string `int main(){return 0;}` to `tests/time/src/m5_format_failure_probe.cpp` and ran `scripts/check-format.sh`. It failed as expected with exit 1 and line diagnostics; the source was removed and the normal formatter check passed. The raw negative output is `build/evidence/m5-format-negative.log`.

`.github/workflows/host.yml` covers format, dependency boundaries, host configure/build/tests and analysis; the host check scripts emit CTest JUnit XML and raw logs. `.github/workflows/zephyr.yml` installs pinned Python tools and SDK, fetches the pinned west manifest, runs `scripts/test-zephyr-time.sh`, and uploads Twister raw/JUnit-compatible reports, target configuration/DTS/size reports and firmware. Both workflows use 14-day artifact retention, and the Zephyr upload explicitly includes hidden `.config`. Ruby's standard YAML parser accepted both workflow files. GitHub Actions itself was not run: there is no remote, and the user prohibited remote creation and push. CI status therefore remains pending. Repo state is still the initial `main` branch with no commits and all project files untracked; there is no remote.

### C17 sample and shared command refactor (2026-10-16)

The Time sample and shared command handler now use C17. `samples/time/src/main.c`, `time_shell.c`, `time_command.c`, and `samples/time/host/main.c` are compiled as C. The command header has C++ linkage guards. Host GoogleTests and the Zephyr Ztest application were C++17 consumers at this checkpoint, including `PublicApiLink.CppConsumerCallsC17ProductionLibrary`; the later test-language simplification below converts the Zephyr Ztest source to C17. The Zephyr sample configuration sets `CONFIG_STD_C17=y` and leaves `CONFIG_CPP` disabled.

Commands ran in the existing Linux/amd64 Dev Container at `/workspaces/crux/crux-pilot` with the pinned `/opt/crux-cache` tools:

```sh
scripts/check-format.sh
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
scripts/check-dependencies.sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

Format and dependency checks passed. Debug and ASan/UBSan host builds each passed all 28 CTest cases and the host sample stdin/stdout scenario. `PublicApiLink.CppConsumerCallsC17ProductionLibrary` passed in both runs. Twister selected 2/2 `native_sim/native/64` scenarios and passed 4/4 Ztest cases in 143.05 seconds. The native_sim Shell sample built in 136/136 steps, and the EVKB QSPI firmware compiled and linked in 199/199 steps. No hardware command ran.

The final sample `.config` selects `CONFIG_STD_C17=y`, leaves `CONFIG_CPP` disabled, uses serial Shell and RTC init priority 61, and disables RTC Shell. The target's static size report is 65,972 B FLASH / 8 MiB and 8,448 B RAM / 32 MiB. Final ELF SHA-256: `c122247ea634c6bda8ffcf6570f54c4cab5dad9f069dbbe97c47db1b6c250cce`; BIN SHA-256: `0cea307a02e772c2e60227042fe65e71653a4b985b300ba3deaf0e22ccb5e120`. Current configuration, DTS, map, size reports and firmware are in ignored `build/zephyr/mimxrt1060_evk_B_qspi/`; Twister reports are in `build/zephyr/twister-time/`.

Clang-Tidy remains a recorded failure after two focused attempts. In the first attempt, the command sequence `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh; scripts/check-format.sh; scripts/check-dependencies.sh; scripts/analyze-host.sh` stopped in the final command with exit 1. It reported warnings-as-errors at `samples/time/src/time_command.c:86` and `:130`: `snprintf` triggered `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling`. The fix replaced those calls with a length-checked copy and manual date formatting. The second command, `clang-tidy-18 -p build/linux/debug samples/time/src/time_command.c`, also returned exit 1 because the same rule flagged the length-checked `memcpy` calls at lines 90 and 150. Further retries are paused per instruction; no suppression or workaround was added.

### M5-05 architecture exit review

| Area | Classification | Evidence and constraint |
| --- | --- | --- |
| Independent host use | supported | Root CMake configures host-only libraries/tests/sample without loading Zephyr; Linux presets remain separate from west builds. |
| C/C++ interoperability | supported with constraints | C++17 GoogleTests include public headers and call the C17 Time implementation in the host test composition. The production `crux::time` target is not independently linked from C++ after removal of its dedicated smoke test; Zephyr production and integration tests use C17. |
| Source reuse | supported with constraints | Shared C17 Time and command sources are referenced by each composition. Host controlled-clock and Zephyr RTC backends remain separate. |
| Visible composition | supported | CMake links explicit common, backend and command targets. Zephyr Shell delegates to the portable handler. No backend registry or weak-symbol discovery exists. |
| Native-type isolation | supported | `struct rtc_time`, `struct device`, errno and RTC APIs remain in the Zephyr backend. Public and common code use Crux types. |
| Proportional complexity | supported with constraints | One Time module, one private port per platform, two Ztest scenarios/four test cases and thin scripts/CI workflows; no general HAL/service framework was introduced. Init priority and RTC semantics are Zephyr/board-specific. |

### M5-06 unresolved material issues and next actions

1. M4 physical acceptance is still pending. Identify the actual EVK revision/flash state, J-Link model/firmware, J41 serial path and reset method; only then perform separately authorized board verification. No flashing, reset, J-Link, UART or other hardware operation occurred here.
2. SNVS RTC read/write, no-data on silicon, progression, range behavior, reset semantics and serial Shell response have only compile/simulator evidence. Keep all hardware ACs pending until measured on the identified board.
3. M5-03 still needs an authorized GitHub Actions run for external CI validation. Both workflows parse and define the intended coverage; no remote or push exists. M1-06's failure-propagation and no-hardware-operation criteria are met locally.
4. At this 2026-10-16 checkpoint, M2-08 focused host analysis failed on Clang-Tidy's `DeprecatedOrUnsafeBufferHandling` rule for checked standard C calls. The 2026-10-17 analyzer configuration and result are recorded below.
5. Free host storage is now 20 GiB, below the plan's advisory 30–40 GiB headroom. No files were deleted. Avoid more large downloads/builds until space is reviewed.
6. The user-provided product data lists both QSPI and HyperFlash, but the build target selects QSPI. Confirm the physical board's flash/rework state before any future programming.

| Initial result | Resolution / current evidence |
| --- | --- |
| First Zephyr application configure failed because public Crux include directories were not propagated to the sample target. | Added a small public-header INTERFACE target in the Zephyr module; native_sim sample then built. |
| The first Zephyr C++ build could not find `<cstring>` in Zephyr's minimal C++ library. | Replaced standard C++ wrappers with equivalent C headers/functions supported by the configured runtime; native builds passed. |
| A C++ Ztest could not link `crux_time_port_read()` because the private port header lacked C linkage guards. | Added `extern "C"` guards; both native_sim scenarios and all four Ztest cases passed. |
| The first EVKB target compile hit the upstream assertion that RTC priority must be greater than SNVS Counter priority 60. | Set `CONFIG_RTC_INIT_PRIORITY=61`; EVKB QSPI application build passed. |
| Zephyr printed an upstream `unique_unit_address_if_enabled` DTS warning for `usbd@402e0000` and `usbh@402e0000`. | Build completed; no Crux DTS change was made because the conflict is in pinned upstream board description. |
| Initial C++ formatter check rejected `time_port_zephyr.c`. | Applied the repository's clang-format and reran the full owned-code format check successfully. |
| The first host sample verification used its pre-M3 path. | Moved the host target beneath `samples/time/host/`, updated the helper and README, and verified the sample in the clean debug directory. |

No physical board, J-Link, UART, flash or reset operation was performed. Thus the build verifies target compilation and DTS integration, not SNVS hardware operation or persistence. The free-space check after the M5 builds reported 20 GiB. No files or images were deleted.

## Current blockers

1. M0-05 and M4-01..08 remain pending for physical configuration and behavior: the actual PCB marking/rework and active flash selection, probe firmware, macOS USB serial device and reset remain unobserved because the board was not attached or operated.
2. M5-03's GitHub Actions execution remains pending because no remote or push was created as instructed. Local workflow parsing and M1-06 failure-propagation verification are recorded above.
3. Storage is 20 GiB free after the clean M5 build outputs. This is below the plan's advisory 30–40 GiB working-headroom target, not an official minimum. No files or images were deleted.
4. Dev Container image, mount layout, workspace identity, SDK path, tool identities, host checks and clang-tidy were verified via Docker CLI. The CMake Tools extension is declared in the Dev Container; its preset selection and build actions in the VS Code UI were not exercised here.

## Scaffold verification performed

The commands and results in this section record the initial M0–M1 validation, including macOS runs before the Linux-only build decision. The active preset names are still `host-debug` and `host-sanitize`, but their current conditions restrict them to Linux; use the current commands in [Current build policy](#current-build-policy-2026-10-16).

| Command | Actual result |
| --- | --- |
| `bash -n scripts/*.sh` | Pass, including after changing `check-format.sh` from Bash 4 `mapfile` to a Bash 3-compatible array loop. |
| `python3 -m json.tool .devcontainer/devcontainer.json` and `python3 -m json.tool CMakePresets.json` | Both parsed successfully. |
| Ruby standard-library YAML parse | `west.yml`, `.github/workflows/host.yml`, and `zephyr/module.yml` parsed successfully. |
| `scripts/check-dependencies.sh` | Pass: root CMake remains host-only and the module CMake entry is `zephyr/CMakeLists.txt`. |
| Apple Clang C17 compile of `tests/build_smoke/src/smoke_value.c`, followed by Apple Clang++ C++17 compile/link/run of a temporary consumer | Pass: C object linked into and called from a C++17 executable. Temporary files are under `/tmp/crux-m1-smoke`, outside the repo. This does not substitute for CMake/GoogleTest acceptance. |
| `python3 -m venv /tmp/crux-m1-tools && /tmp/crux-m1-tools/bin/python -m pip install --disable-pip-version-check --no-cache-dir cmake==3.28.4 ninja==1.11.1` | Pass; CMake/Ninja installed in a task-local virtual environment, outside the repo. |
| `/tmp/crux-m1-tools/bin/pip install --disable-pip-version-check --no-cache-dir clang-format==18.1.8` | Pass; pinned formatter installed in the same task-local virtual environment. No system package changes. |
| `PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/bootstrap-host.sh` | Pass: CMake configure, Ninja build, and CTest; `BuildSmoke.C17LibraryIsCallableFromCpp17` passed (1/1). GoogleTest fetched to external cache with pinned SHA-256. |
| `PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/check-host.sh` | Pass: CMake configured with `FETCHCONTENT_FULLY_DISCONNECTED=ON` and CTest passed (1/1) in both the initial offline run and final rerun after formatting. The final run rebuilt the changed host targets. Zephyr, west and SDK were absent from this workspace/run. |
| ASan/UBSan CMake preset configure/build/CTest, with `FETCHCONTENT_FULLY_DISCONNECTED=ON` | Pass on macOS AppleClang 17; 1/1 CTest passed. |
| `CLANG_FORMAT=/tmp/crux-m1-tools/bin/clang-format scripts/check-format.sh` after installing `clang-format==18.1.8` | Pass; reported `clang-format version 18.1.8`. The first formatter run exposed style differences; `.clang-format` was tightened and the smoke fixture formatted before the passing run. |
| Apple Clang `--analyze` for the C17 library and C++17 GoogleTest consumer | Pass with `-Wall -Wextra -Wpedantic -Werror`; no diagnostics. `clang-tidy` was absent on macOS but passed in the container. |
| Initial `CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/bootstrap-host.sh` | Blocked, exit 127 at `cmake: command not found`; resolved by installing pinned CMake/Ninja into the task-local venv above. |
| Initial `scripts/check-format.sh` | First attempt exposed Bash 3's lack of `mapfile`; after fixing the script it stopped with exit 127 (`clang-format-18: command not found`). The pinned formatter was then installed in the task-local venv and the check passed. |
| Initial `scripts/bootstrap-zephyr.sh` attempt before west was installed | Blocked, exit 2 with `west is required. Install the pinned version with: python3 -m pip install west==1.4.0`; resolved during resumed setup above. |
| Initial `scripts/check-zephyr-smoke.sh` attempt before west bootstrap | Blocked, exit 2 with `Run scripts/bootstrap-zephyr.sh before the Zephyr smoke.`; workspace bootstrap later completed. No build or hardware action occurred in that initial attempt. |
| Failure propagation probe | With `/tmp/crux-m1-fake-bin/cmake` returning 37, `CRUX_CACHE_DIR=/tmp/crux-m1-fake-cache PATH=/tmp/crux-m1-fake-bin:/usr/bin:/bin scripts/bootstrap-host.sh` returned 37. This checks shell failure propagation only; the fake tool is outside the repo. |
| Hardware command scan | `rg -n 'west[[:space:]]+flash|nrfjprog|JLinkExe|pyocd|openocd' scripts/check-zephyr-smoke.sh scripts/bootstrap-zephyr.sh` had no matches. |
| `git diff --check` | Pass for tracked diff; at this starter-repo state all implementation files are untracked, so whitespace was additionally scanned with `rg -n '[\\t ]+$' ...` and no matches were found. |

YAML parser command used:

```sh
ruby -e 'require "yaml"; %w[west.yml .github/workflows/host.yml zephyr/module.yml].each { |f| YAML.load_file(f); puts "parsed #{f}" }'
```

Historical host tool installation and verification commands (these ran before the Linux-only build decision; use the current Linux commands in [Current build policy](#current-build-policy-2026-10-16)):

```sh
python3 -m venv /tmp/crux-m1-tools
/tmp/crux-m1-tools/bin/python -m pip install --disable-pip-version-check --no-cache-dir cmake==3.28.4 ninja==1.11.1
/tmp/crux-m1-tools/bin/pip install --disable-pip-version-check --no-cache-dir clang-format==18.1.8
PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/bootstrap-host.sh
PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache scripts/check-host.sh
PATH=/tmp/crux-m1-tools/bin:$PATH CRUX_CACHE_DIR=/tmp/crux-m1-cache cmake --preset host-sanitize -S "$PWD" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
PATH=/tmp/crux-m1-tools/bin:$PATH cmake --build --preset host-sanitize
PATH=/tmp/crux-m1-tools/bin:$PATH ctest --preset host-sanitize
CLANG_FORMAT=/tmp/crux-m1-tools/bin/clang-format scripts/check-format.sh
clang --analyze -std=c17 -Wall -Wextra -Wpedantic -Werror -I tests/build_smoke/include tests/build_smoke/src/smoke_value.c
clang++ --analyze -std=c++17 -Wall -Wextra -Wpedantic -Werror -I tests/build_smoke/include -isystem /tmp/crux-m1-cache/cmake/googletest-src/googletest/include tests/build_smoke/src/smoke_test.cpp
```

Historical macOS host CMake/CTest, sanitizer and clang-format checks passed before the Linux-only decision. Current Linux container CMake/CTest, ASan/UBSan, clang-format, dependency checks, clang-tidy and Zephyr smoke passed. The final Zephyr smoke built native_sim and C++ Hello World, Twister passed 1/1, and Crux module discovery was reported twice. Only the board-specific compile is pending because the exact EVK target is unknown; CI remains unrun.

### Public-header compile fixture simplification (2026-10-17)

Removed the six `tests/time/public_headers/{status,time,time_calendar}.{c,cpp}` compile-only fixtures and their two CMake object-library targets. Zephyr documents C++ applications consuming its C API and organizes Ztest coverage around APIs/features ([C++ synchronization sample](https://docs.zephyrproject.org/latest/samples/cpp/cpp_synchronization/README.html), [Ztest guide](https://docs.zephyrproject.org/latest/develop/test/ztest.html)); its guidance does not require a C/C++ compile-only fixture for every public header. At this checkpoint, Crux retained `PublicApiLink.CppConsumerCallsC17ProductionLibrary` alongside the C++ `TimeCalendar.*` tests. The later decision below removes that separate production-target smoke; public API C++ guards and C++ GoogleTests remain.

Commands ran in the existing Linux/amd64 Dev Container at `/workspaces/crux/crux-pilot`:

```sh
PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-host.sh
GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/workspaces/crux/crux-pilot PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-format.sh
GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/workspaces/crux/crux-pilot PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-dependencies.sh
```

Results: host Debug configured and built successfully; all 28/28 CTest cases passed, including `PublicApiLink.CppConsumerCallsC17ProductionLibrary` and all seven `TimeCalendar.*` cases. The host sample stdin/stdout scenario passed. Formatting and dependency-boundary checks passed. The initial direct `check-host.sh` invocation lacked the cached tool directory in `PATH` and exited 127 (`cmake: command not found`); rerunning with the pinned cache path passed. The first format/dependency invocations stopped on Git's dubious-ownership check; setting `safe.directory` in each command's temporary Git configuration resolved it without changing persistent Git configuration.

### Test-language simplification (2026-10-17)

The test directories have distinct jobs: `tests/time/` runs fast host behavior/API tests with GoogleTest and a deterministic backend; `tests/zephyr_time/` runs native Zephyr backend/integration scenarios with Twister and Ztest. Host GoogleTest sources remain C++ because GoogleTest is a C++ framework; those tests exercise public C API calls against the C17 host test composition. The then-present `PublicApiLink.CppConsumerCallsC17ProductionLibrary` additionally linked the production `crux::time` target and is removed in the later decision below. Zephyr Ztest does not need to be C++ for API-consumption coverage: its sources are now C17. This is a judgment about test purpose and maintenance cost, not a rule that Ztest cannot be used from C++.

Changes: renamed `tests/zephyr_time/src/main.cpp` to `src/main.c`; removed the test application's C++ language options from its CMake file and `CONFIG_CPP`/`CONFIG_STD_CPP17` settings from `prj.conf`; removed `extern "C"` wrappers from the private Zephyr port headers because no C++ consumer includes them. Public API C++ guards remain. `tests/README.md`, the README, readiness review and implementation status describe the split and C++ API coverage through host GoogleTests. The earlier per-public-header compile-only fixtures remain removed.

Commands ran in the existing Linux/amd64 Dev Container at `/workspaces/crux/crux-pilot`:

```sh
PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-host.sh
GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/workspaces/crux/crux-pilot PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-format.sh
GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/workspaces/crux/crux-pilot PATH=/opt/crux-cache/venv/bin:$PATH scripts/check-dependencies.sh
ZEPHYR_BASE=/workspaces/crux/zephyr GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/workspaces/crux/crux-pilot PATH=/opt/crux-cache/venv/bin:$PATH scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

Results: host Debug build and all 28/28 CTest cases passed; the deterministic host sample scenario passed; formatting and dependency-boundary checks passed. The first Twister invocation lacked `ZEPHYR_BASE` and west could not discover the Twister extension; setting `ZEPHYR_BASE=/workspaces/crux/zephyr` fixed that setup issue. With the variable set, Twister started but both native_sim configurations stopped during CMake configuration, before source compilation or test execution:

```text
ERROR: project zephyr (zephyr): cannot import contents of west.yml; do you
need to run "west update"?
```

This occurred in `zephyr/cmake/modules/zephyr_module.cmake` while running `zephyr/scripts/zephyr_module.py`, at its `Manifest.from_file()` workspace-manifest load. Twister reported 0/2 configurations built, 2 errors, 0 tests executed. The recorded build log is `build/zephyr/twister-time/native_sim_native_64/host_gnu/crux-pilot/tests/zephyr_time/crux.time.zephyr/build.log` (the no-RTC scenario has its corresponding log). `west -vv list` and direct west manifest parsing from the workspace succeeded, so the cause is not yet isolated. No `west update` was run and no speculative retry was made. The previous C++ Ztest revision had passed 4/4 cases in 2/2 scenarios; that historical result does not verify the new C17 test source. M3-05 is therefore pending until the current C17 suite builds and passes.

### Removal of the dedicated production-target C++ smoke (2026-10-17)

Removed `tests/time/src/public_api_link_test.cpp` and the `crux_time_api_link_tests` CMake target. The existing host GoogleTests are themselves C++ consumers: `TimeApiTest.*` and `TimeCalendar.*` include public C headers and call the C17 common implementation, compiled into the deterministic test composition. They already exercise C/C++ linkage for the APIs they call. The dedicated smoke's only distinct coverage was linking those calls through the production `crux::time` target rather than the test composition. The user chose to drop that additional target-specific check; this narrows M2-02 accordingly and does not remove the public headers' C++ linkage guards or C++ support from the API.

The last host run before removal passed 28/28 CTest cases and the host sample scenario; it included the now-removed smoke case. The remaining GoogleTests in that run passed, but no build or test command was run after deleting the separate target. Therefore this record does not claim a post-removal CTest run or a C++ link against the production `crux::time` target. The status table describes the revised M2-02 contract and points to the last run as historical evidence.

## Architecture-boundary cleanup and current verification (2026-10-17)

The platform review resulted in these bounded changes:

- Root CMake starts as a C-only project. `BUILD_TESTING=OFF` disables C++ and GoogleTest; setting `CRUX_BUILD_HOST_SAMPLE=OFF` also omits sample code. This provides a C-only library configure/build path.
- Removed the generic `tests/build_smoke/` fixture because the real C++ Time GoogleTests already compile and call the public C17 API. The per-header compile fixtures and the separate production-target Time link smoke remain removed.
- Added `components/time/sources.cmake` so the Time component owns its short common-source list. Host, host tests and Zephyr include it.
- Kept `components/base` independent. The Zephyr module's `crux_base` exports the status include path only; Zephyr usage requirements enter at the Time target.
- Moved command-handler ownership out of `zephyr/CMakeLists.txt`. The host and Zephyr applications compile the same sample-owned C17 command source.
- Added CMake compatible-interface provider identity to host and Zephyr backend targets. A temporary consumer linking `crux::time` and a second provider configured with a conflicting identity failed CMake generation: `INTERFACE_CRUX_TIME_BACKEND ... does not agree with ... CRUX_TIME_BACKEND`.
- Expanded public Time API comments to state UTC units/range, session initialization, output behavior, status conditions and serialization/ISR limits. The Shell adapter lets argument counts reach the shared handler; Ztest now invokes the registered Shell command through Zephyr's dummy backend in both RTC and no-RTC configurations.
- Standardized the CLI spelling to `ERR NOT_SET`. Readiness output now says `composition=host-simulation` or `composition=zephyr`; these are composition labels, not unique build identities. A source/configuration identity and firmware checksum manifest remain unimplemented, so M5-04 is pending.
- Removed the stale deleted-source reference from `scripts/analyze-host.sh`. `.clang-tidy` now analyzes owned component/sample/test headers. It disables the checker that flags bounded `memcpy`/`snprintf` use and the enum-size check that cannot be applied to a C17 enum without changing its representation; other selected checks remain errors.

The following checks ran inside the existing Linux/amd64 Dev Container at
`/workspaces/crux/crux-pilot`, using CMake 3.28.4, Ninja 1.11.1, Clang 18.1.3, west 1.4.0,
Zephyr v4.4.0 and the pinned cache at `/opt/crux-cache`.

| Command | Actual result |
| --- | --- |
| `scripts/check-host.sh` | Pass. Debug configured/rebuilt, CTest passed 26/26, and the host stdin/stdout scenario passed with `ERR NOT_SET` and the composition marker. |
| `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh` | Pass. ASan/UBSan configured/rebuilt, CTest passed 26/26, and the host sample scenario passed. |
| `cmake -S . -B build/linux/library-only-final -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF -DCRUX_BUILD_HOST_SAMPLE=OFF` | Pass. Configure identified only Clang C; it did not configure C++, GoogleTest or the sample. |
| `cmake --build build/linux/library-only-final --target crux_time_common crux_time_host` | Pass; both C17 static libraries built. |
| `scripts/check-format.sh` with `CLANG_FORMAT=clang-format` | Pass. |
| `scripts/check-dependencies.sh` | Pass. It checks base independence, native-header isolation, private test controls, backend identity declarations, shared source-list use and sample/module ownership. |
| `scripts/analyze-host.sh` | Pass. Clang-Tidy processed host implementation, command code and host C++ tests; no enabled owned-code diagnostic remained. It reported 83,066 generated warnings, mostly suppressed dependency/header diagnostics. |
| `ctest --show-only=json-v1` in `build/linux/debug` | Pass. All 26 discovered host tests have `TIMEOUT=30` seconds; this is execution timeout, separate from GoogleTest discovery timeout. |
| CMake negative consumer probe for backend selection | Pass. CMake generation rejected two different `CRUX_TIME_BACKEND` interface values before linking. The probe lived under `/tmp` and is not a repository test target. |

### Zephyr verification blocker

Historical failure: the root cause was subsequently isolated to Git trust configuration under the
manual build user. See [the resolution below](#zephyr-manifest-blocker-resolution-2026-10-17).

The updated Twister tests and Shell application were not compiled because west manifest loading failed
before Crux module loading. The exact command was:

```sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

It ran with `ZEPHYR_BASE=/workspaces/crux/zephyr`, `CRUX_WORKSPACE=/workspaces/crux`,
`CRUX_CACHE_DIR=/opt/crux-cache`, and
`ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1`. The first run used Twister's default
eight jobs. To match the container's configured two CPUs, the script was changed to run
`west twister -j 2` and executed again. Both attempts failed before source compilation:

```text
CMake Error at /workspaces/crux/zephyr/cmake/modules/zephyr_module.cmake:73 (message):
  ERROR: project zephyr (zephyr): cannot import contents of west.yml; do you
  need to run "west update"?
```

Twister reported 0/2 configurations built and 0 tests executed; six test-case records were marked
errored. Logs are under `build/zephyr/twister-time/native_sim_native_64/host_gnu/crux-pilot/tests/zephyr_time/`
in `crux.time.zephyr/` and `crux.time.zephyr_missing_rtc/`.

The Zephyr source involved is `zephyr/cmake/modules/zephyr_module.cmake:73`, which invokes
`zephyr/scripts/zephyr_module.py:735` and `Manifest.from_file()`. Workspace inputs are
`../.west/config` and this repository's `west.yml`; the Crux caller is
`tests/zephyr_time/CMakeLists.txt`. Read-only diagnostics passed: `west topdir` returned
`/workspaces/crux`; `west list` showed Zephyr and the three pinned modules; `west manifest --resolve`
loaded the same manifest; and direct `zephyr_module.py` execution with
`--zephyr-base=/workspaces/crux/zephyr` and `--extra-modules=/workspaces/crux/crux-pilot` returned
0. Reducing Twister from eight jobs to two did not resolve the CMake failure. No `west update` or
further speculative retry was run. The cause remains unresolved, so M3-01, M3-03, M3-04, M3-05,
M3-07 and M5-01 remain pending for current Zephyr source. Earlier EVKB and native_sim binaries are
historical and stale after these source changes.

The checkout still has no commit and all project files are untracked. The readiness marker identifies
only composition, and no content-addressed source/configuration manifest is tied to a firmware
checksum. M5-04 remains pending. No board, probe, UART, flash or reset was used; no remote was
created and nothing was pushed.

## Zephyr manifest blocker resolution (2026-10-17)

The apparent CMake-only manifest error was an execution-user/Git trust problem. The earlier manual
commands used root with a per-process `safe.directory` entry for `/workspaces/crux/crux-pilot` only.
The Python child process reading the imported Zephyr manifest failed Git's ownership check for
`/workspaces/crux/zephyr`. West's `Manifest.from_file()` surfaced that as the generic
`cannot import contents of west.yml` error; the manifest and pinned revision were present.

A controlled diagnostic ran `subprocess.run(['git', 'show', 'manifest-rev:west.yml'],
capture_output=True, text=True)` and then `Manifest.from_file()` from `/workspaces/crux/zephyr`.
With the earlier one-entry Git environment, Git returned 128 with:

```text
fatal: detected dubious ownership in repository at '/workspaces/crux/zephyr'
```

Adding the exact Zephyr path as a second per-process `safe.directory` entry made the same Git call
return 0 and the same manifest load succeed. The before/after transcripts are retained in ignored
`build/evidence/zephyr-manifest-before.log` and `build/evidence/zephyr-manifest-after.log`.
The previous successful CLI/direct probes were insufficient to establish that the failing Python
child had the same effective Git trust setup. Reducing Twister concurrency was unrelated.

The supported Dev Container already sets `remoteUser` to `vscode` and its post-create command
configures the exact Crux/Zephyr/CMSIS/CMSIS 6/NXP Git paths for that user. No wildcard trust,
upstream source patch, manifest revision change, dependency fetch, or new installation was needed.
The resolution was to execute verification as that configured user. README now documents this
requirement for manual Docker execution. The current validation command is:

```sh
docker exec --user vscode --workdir /workspaces/crux/crux-pilot \
  --env PATH=/opt/crux-cache/venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  --env ZEPHYR_BASE=/workspaces/crux/zephyr \
  --env CRUX_WORKSPACE=/workspaces/crux --env CRUX_CACHE_DIR=/opt/crux-cache \
  1d7f83b19856 scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

The inherited SDK setting is `/opt/crux-cache/sdk/zephyr-sdk-1.0.1`. Both Twister configurations
passed manifest loading and CMake generation under `vscode`, demonstrating that the blocker is
resolved. Twister then passed 2/2 configurations and 4/4 executed test cases, with zero failures,
errors or runtime filters. Reports are `build/zephyr/twister-time/twister_report.xml` and
`twister.json`; each scenario's `handler.log` records the executed Ztests, including actual Shell
argument rejection in the RTC and no-RTC cases. The native_sim Shell sample also built successfully.

An additional native sample execution used:

```sh
build/zephyr/native_sim_time/zephyr/zephyr.exe -uart_stdinout -rt
```

It received `time get`, `time set 2024-01-01T00:00:00 extra`,
`time set 2038-01-19T03:14:08`, then `time get` over stdin. The transcript in
`build/evidence/zephyr-native-shell.log` shows the readiness marker, `ERR NOT_SET`,
`ERR INVALID_ARGUMENT`, `OK`, then `2038-01-19T03:14:09 UTC`. The simulator stopped at 1.210 s;
the one-second RTC advancement is expected. An initial ad hoc check incorrectly expected the
read-back timestamp to equal the set value exactly and exited 1. Rechecking the same retained log
with elapsed time allowed passed; the sample was not rerun and production code was not changed.
An earlier guessed argument `-uart0_stdinout` was rejected; `--help` identified the actual
`-uart_stdinout` option used above. These were diagnostic invocation/oracle errors, not Zephyr
manifest or Crux runtime failures.

The full `scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'` invocation completed with exit 0.
Its Twister output directory was new (the prior output was retained as `twister-time.9`); the
sample and target builds used `west build -p always`.

| Check | Actual result |
| --- | --- |
| C17 native_sim Ztests, RTC and missing RTC | Pass: 2/2 configurations, 4/4 executed cases; no failures, errors or skipped cases. |
| native_sim Shell sample build | Pass; current sample/module composition compiled and linked. |
| native_sim sample runtime | Pass from the retained transcript: ready, initial unset, syntax rejection, set and progressing read-back. |
| EVKB QSPI build | Pass: `mimxrt1060_evk@B//qspi`, revision B, `mimxrt1062/qspi`, current sample compiled and linked. |
| C17 compilation policy | Both sample compilation databases contain six owned `.c` translation units, all with `-std=c17`. |
| Target configuration | `CONFIG_STD_C17=y`, `CONFIG_RTC_INIT_PRIORITY=61`, `CONFIG_RTC_SHELL` disabled. |
| Target size | Linker usage: FLASH 65,976 B; RAM 8,448 B. Symbol-based `rom_report` and `ram_report` totals are 65,964 B and 8,416 B respectively; these are distinct accounting views. |
| Dependency boundary check | `scripts/check-dependencies.sh` passed under `vscode`. |

The existing target script generated firmware/configuration/DTS/map/stat files and checksums under
`build/zephyr/mimxrt1060_evk_B_qspi/zephyr/`. Current SHA-256 values are:

```text
zephyr.bin  904f5a3480cc8a689d4710ef99787f0c03dcb178ae66f1581ce424e19a61dda6
zephyr.elf  20f6c93d2d0e30ff11fa310d127b2afda5a7e8e598b3ef9aa21dbe7b6e40a4fc
```

For complete report retention, the report-only targets were repeated after the successful build
as `vscode`, without source changes:

```sh
cmake --build build/zephyr/mimxrt1060_evk_B_qspi --target rom_report > build/evidence/zephyr-rom-report.log 2>&1
cmake --build build/zephyr/mimxrt1060_evk_B_qspi --target ram_report > build/evidence/zephyr-ram-report.log 2>&1
```

`build/evidence/zephyr-build-output-tail.log` retains partial captured build stdout; the two report
logs above retain the complete report output. Twister retains its full build/runtime logs.
At the time this Zephyr blocker resolution was recorded, external CI (M5-03) and build provenance
(M5-04) were deferred. The current state and subsequent evidence for those items are recorded in
[CI and build-artifact provenance](#ci-and-build-artifact-provenance-2026-10-17). M4 physical work
remains pending. No remote was created or pushed, and no board, debugger, UART device, flash or reset
operation was used.

## CI and build-artifact provenance (2026-10-17)

### CI workflows and limits

`.github/workflows/host.yml` (`host-foundation`) runs on push, pull request, or manual dispatch. It
installs pinned host tools and runs host configure/build/CTest, format, dependency-boundary, and
Clang-Tidy checks. Host jobs do not set up Zephyr or its SDK. Its CTest JUnit XML and raw test log
are uploaded for 14 days, including after a failed check when those files exist.

`.github/workflows/zephyr.yml` (`zephyr-software`) has the same triggers and separately installs
the pinned west/Python tools, Zephyr workspace inputs and SDK. It runs the two native_sim Twister
scenarios, builds the native_sim Shell sample, then cross-builds `mimxrt1060_evk@B//qspi`. It does
not flash or reset a board. The workflow uploads the Twister reports and per-scenario raw logs, the
EVKB `.config`, DTS, map/stat and ROM/RAM reports, firmware ELF/BIN, build logs and
`provenance.json`, with 14-day retention. Hidden files are enabled for the generated `.config`.
Both workflows request only `contents: read`; no repository secrets are used by these jobs.

Both workflows pass `actionlint` v1.7.12, as well as JSON, shell-syntax and Python-syntax checks.
Actionlint initially rejected `runner.temp` in job-level `env` blocks. Those paths now use the
workspace parent, outside the checked-out repository; the Zephyr Python tool venv uses the same
location. The official macOS ARM64 actionlint release archive was SHA-256 verified before use.
The workflow lint command was `actionlint .github/workflows/host.yml .github/workflows/zephyr.yml`;
it exited 0 after that correction.
Local equivalents for host debug/sanitizer builds and tests, format,
dependency checks, analysis, native_sim/Twister and EVKB builds passed as recorded in the preceding
sections. GitHub Actions itself has **not** run: this checkout has no commit and no remote. The
original task boundary prohibits creating a remote or pushing, so M5-03 remains pending until the
user publishes the reviewed tree and a workflow run completes. M1-06's local failure-propagation
and no-implicit-hardware criteria remain independently verified.

To trigger the workflows, the user needs to create or select a GitHub repository, commit this tree,
configure an authenticated `origin`, and push `main`. A push starts both workflows; later runs can
be started from **Actions → host-foundation/zephyr-software → Run workflow**. GitHub CLI
authentication (`gh auth login`) or the user's configured SSH/Git credential flow must be completed
on the Mac. Credentials should remain local; no token needs to be added as a workflow secret. If
Actions are disabled by repository policy, enable them in the repository's Actions settings.

### Target build provenance

`scripts/build-target.sh 'mimxrt1060_evk@B//qspi'` now captures the full EVKB build, ROM report and
RAM report logs under ignored `build/evidence/`, then writes
`build/zephyr/mimxrt1060_evk_B_qspi/provenance.json`. It records:

- a SHA-256 snapshot over repository file paths, modes and contents, plus Git commit/branch/dirty
  state and a hash of `west.yml`;
- all resolved west project revisions and `.west/config` hash;
- CMake, Ninja, west, compiler and Python identity, Dev Container recipe/package inventory, the
  runtime image ID when available, and GitHub runner/run URL metadata when running in Actions;
- the selected target, generated `.config` and DTS hashes, ROM/RAM reports, ELF/BIN and all
  required configuration/report files with sizes and SHA-256 values;
- Twister test totals and hashes/sizes for the reports and per-test build/handler logs.

The deterministic `build_id` is derived from the source/build identity and firmware/configuration
outputs; timestamp, CI run number and raw test-log contents are retained as metadata but do not
change that identity. The manifest itself is a CI artifact rather than a repository file, so its
own changing digest does not create a recursive source fingerprint.

The fresh local validation used the Linux Dev Container as user `vscode`, with the documented
parent workspace mounted at `/workspaces/crux` and repository at
`/workspaces/crux/crux-pilot`:

```sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
```

That run completed the fresh EVKB target build at 198/198 steps, emitted ROM/RAM reports, and
generated the provenance manifest. The existing clean Twister run for the same production/test
sources had 2/2 configurations and 4/4 test cases, with zero failures, errors, or skips. The final
manifest reports 11 target/build evidence files and 8 Twister report/log files; independent local
verification recomputed every recorded file size and SHA-256 with no mismatch. It records the
container image ID `sha256:c42936535e1b5a253afbeba80f1708208f7014aec63d76ff7a29e834fc785654`, all
west revisions, and no Git commit because the working tree has not been committed. Exact ELF/BIN
hashes and sizes are in the generated manifest. No remote, push, hardware operation or flash was
performed.

| Acceptance criterion | Status | Evidence / remaining action |
| --- | --- | --- |
| M1-06: local commands and CI fail on actual check failures; target build does not invoke hardware | pass locally; GitHub CI pending | `set -euo pipefail`, a synthetic CMake exit-37 propagation probe, non-flashing target script, local workflow parsing and the local checks above. External CI execution is tracked separately as M5-03. |
| M5-03: CI covers format, forbidden dependencies, host build/tests/analysis, native_sim tests and target build | pending | Workflow definitions and local equivalents are verified. A GitHub Actions run requires a user-created/selected GitHub repo, commit, authenticated push, then review of both workflow results/artifacts. |
| M5-04: record source/module/tool/image/config/size/checksum/report/log provenance | pass | `build/zephyr/mimxrt1060_evk_B_qspi/provenance.json`; all 19 referenced artifact and test-report file hashes/sizes independently matched. A future clean GitHub run will add its commit SHA and runner/run metadata. |
