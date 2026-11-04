# Build and Integration Guide

This guide describes the supported ways to build Crux and consume its capabilities from another application. It covers the Linux development Host, the Zephyr module and Linux source-tree integration. For component behaviour and test-specific acceptance evidence, use the linked contracts and [test guide](../tests/README.md).

## Choose a build path

| Use case | Entry point | Scope |
| --- | --- | --- |
| Develop and test Crux on Linux | `scripts/bootstrap-host.sh` and CMake presets | Current Linux amd64 development environment |
| Build a Zephyr application | `west` with Crux's `zephyr/module.yml` | Pinned Zephyr workspace, selected `native_sim` and EVKB configurations |
| Consume Crux from a separate Linux CMake project | Root `add_subdirectory()` | Source-tree integration in the supported Linux environment |
| Start a product repository on Crux | [Product Composition Guide](product-composition.md) | Zephyr only, no Crux release tag yet |

Linux Host is a development and test composition, **not** a qualified production Embedded Linux variant. The Zephyr EVKB cross-build proves that a selected firmware image compiles and links, not that it boots or operates correctly on hardware. Installed packages, `find_package(Crux)`, a binary SDK and arbitrary toolchain or board combinations are not supported by this guide.

## Development environment

Use the repository's [Dev Container](../.devcontainer/devcontainer.json) with Docker and VS Code's Dev Containers extension. Its pinned Ubuntu 24.04 amd64 environment includes Clang 18 and installs CMake 3.28.4, Ninja 1.11.1, west 1.4.0 and the documented Python tools. The container is configured for 2 CPUs and 4 GiB of memory.

Open `crux-pilot/` in VS Code and choose **Dev Containers: Reopen in Container**. The container mounts the repository's parent directory, with this layout:

~~~text
/workspaces/crux/
  crux-pilot/    # This Git repository
  .west/         # Created by west init
  zephyr/        # Pinned upstream checkout
  modules/       # Selected Zephyr dependencies
~~~

Run the commands below from `/workspaces/crux/crux-pilot` inside the container unless a different working directory is specified. The container supplies `CRUX_WORKSPACE=/workspaces/crux` and uses an external cache. Keep generated builds under `build/` and the west workspace, SDK and dependencies outside the Crux Git source tree.

## Linux Host build

The source root uses [CMake presets](../CMakePresets.json) named `host-debug` and `host-sanitize`. For the initial build, dependency acquisition and Host CTest run:

~~~sh
scripts/bootstrap-host.sh
~~~

For repeatable subsequent builds and tests with network dependency resolution disabled:

~~~sh
scripts/check-host.sh
~~~

To run the sanitizer configuration (AddressSanitizer and UndefinedBehaviorSanitizer), after initial dependency setup:

~~~sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
~~~

The scripts configure, build and run CTest using the selected preset. Output is written under `build/linux/debug/` or `build/linux/sanitize/`, including `ctest-junit.xml`. To build or test an existing configured preset directly:

~~~sh
cmake --build --preset host-debug
ctest --preset host-debug
~~~

The root build enables Crux's development components and samples by default. `CRUX_BUILD_TESTS` initially follows the root project's `BUILD_TESTING` setting. Disabling internal tests also avoids the Crux GoogleTest/C++ test setup. The test and sample options are independent, so set `CRUX_BUILD_HOST_SAMPLE=OFF` and `CRUX_BUILD_REFERENCE_CONTROLLER=OFF` as well when the corresponding development targets are not wanted. Component-specific options are described below.

For the complete quality checks and component-specific runners, use the [test guide](../tests/README.md). `scripts/check-format.sh`, `scripts/analyze-host.sh` and `scripts/check-dependencies.sh` validate formatting, configured analysis and dependency boundaries. Analysis uses the configured compiler database and can report existing upstream or suppressed diagnostics, so a successful run does not imply zero warnings.

## Zephyr build and module integration

Zephyr is integrated through [zephyr/module.yml](../zephyr/module.yml), [zephyr/CMakeLists.txt](../zephyr/CMakeLists.txt) and [zephyr/Kconfig](../zephyr/Kconfig), **not** through the Linux Host root CMake entry point. The [west manifest](../west.yml) pins the Zephyr revision and selected CMSIS/NXP modules.

After the Dev Container is ready, initialise or update the separate west workspace and install the pinned Zephyr SDK and Arm toolchain:

~~~sh
scripts/bootstrap-zephyr.sh --install-sdk
~~~

The bootstrap uses `west init -l` when needed, updates the pinned dependencies and installs SDK v1.0.1 with `arm-zephyr-eabi` support. Confirm available disk space before SDK installation.

Run a documented reference-controller scenario on Zephyr `native_sim` and cross-build its EVKB firmware:

~~~sh
scripts/test-controller.sh
~~~

For the Time capability's Twister tests, `native_sim` sample build and EVKB cross-build:

~~~sh
scripts/test-zephyr-time.sh 'mimxrt1060_evk@B//qspi'
~~~

Both commands use the module from this checkout. The current EVKB target is `mimxrt1060_evk@B//qspi`, limited to its documented configuration.

The Crux module compares the Zephyr release in use with the one Crux was verified on. On another release, every build that enables the module warns and continues without Crux evidence. `-DCRUX_REQUIRE_VERIFIED_ZEPHYR=ON` stops such a build instead. `west crux-pins` compares the workspace's project commits with the ones Crux pins and fails on an undeclared difference; see [its description](product-composition.md#west-crux-pins).

For another Zephyr application in the west workspace, prefer `EXTRA_ZEPHYR_MODULES` to add Crux while retaining the modules discovered through west. Existing Crux scripts also use the supported older spelling `ZEPHYR_EXTRA_MODULES`. By contrast, `ZEPHYR_MODULES` replaces automatic discovery and must list **all** required modules, not just Crux. For example, the following command builds the existing controller application on `native_sim` without hardware operations:

~~~sh
repo="$PWD"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
(cd "$workspace" && west build -p always -b native_sim/native/64 \
    -d "$repo/build/zephyr/controller-example" \
    "$repo/samples/controller/zephyr" -- -DEXTRA_ZEPHYR_MODULES="$repo")
~~~

Select the required `CONFIG_CRUX_*` Kconfig capabilities and supply board/project Devicetree and hardware-resource bindings where the component requires them. Follow the applicable capability contract and an existing tested Zephyr fixture instead of copying Linux provider settings.

Do not infer that every Linux Host capability has an equivalent Zephyr provider. Serial and Logging require explicitly supported UART resources and ownership settings, while Time's Zephyr system clock needs no RTC; optional RTC storage is separate from Linux system or development-memory realtime. The full [Zephyr test commands](../tests/README.md) and [architecture overview](architecture.md#build-and-platform-composition) describe the supported profiles and their limits. None of the build scripts above flashes, resets or operates the physical EVKB.


## RT1060-EVKB debugging

The existing EVKB QSPI debugging setup uses a SEGGER J-Link connected to a Mac. This is not a general firmware-programming procedure. Confirm the exact board, probe, matching firmware image and recovery plan, and obtain task-specific approval before connecting, resetting or downloading. Starting the GDB Server or attaching a debugger may halt the target.

On the approved macOS host, from the repository checkout (not the Dev Container), start the existing server:

~~~sh
bash scripts/macos/start-jlink-gdbserver.command
~~~

The launcher uses SWD at 400 kHz and serves GDB on TCP port 2331. It accepts remote connections rather than restricting access to localhost, so use it only on a trusted network. Stop it with Ctrl-C when debugging is finished.

The existing VS Code [debug configurations](../.vscode/launch.json) connect from the Dev Container to `host.docker.internal:2331`:

- **EVKB QSPI: Attach** uses an ELF matching the image already on the board and does not download firmware. The connection may still halt the target.
- **EVKB QSPI: Flash** builds the **Time sample**, then resets the board and downloads that image. It must not be used to flash arbitrary applications or unapproved storage candidates.

See the [debug build script](../scripts/build-debug-target.sh) and [physical UART test instructions](../tests/README.md#physical-uart-target). Stop the debugger, GDB Server and serial monitors before a separately authorised physical test that requires exclusive access.

### Debug builds of other applications

`scripts/build-debug-target.sh` builds the Time sample unless told otherwise. Inside the Dev Container, `--app <directory>` builds another application, and `--debug-optimizations` adds [the fragment](../scripts/debug-optimizations.conf) that selects `-Og`:

~~~sh
scripts/build-debug-target.sh 'mimxrt1060_evk@B//qspi' --app samples/storage_sd --debug-optimizations
~~~

Without either option the build directory is the one that the VS Code profile and the clean task name. With `--app` the directory is `build/zephyr/<board>--<application path>`, even for `samples/time`, and `--debug-optimizations` appends `--og`. The script replaces each run of characters other than letters, digits, `_`, `.` and `-` with a single `_` in the board and in the path, so the example above builds in `build/zephyr/mimxrt1060_evk_B_qspi--samples_storage_sd--og`. A path outside the repository is keyed by its whole sanitised absolute path, and a relative path is resolved against the directory that the command runs in. The script prints the build directory before it builds and the ELF path when it finishes. A debug build records no provenance. A build with `--debug-optimizations` is never verification evidence: its stack use and timing may differ from the `-Os` images that the verification scripts build, and they have not been re-measured. `--app` builds an application's base configuration only: samples and tests that need extra configuration files, overlays or modules, such as the controller and networking compositions, are built by their own scripts. The **EVKB QSPI: Flash** profile still downloads only the Time sample. Downloading another application remains a separately authorised physical operation.

### What the platform provides for build and debug

| Provided by the platform | Owned by the project | Not provided |
|---|---|---|
| The build and debug scripts above for the RT1060-EVKB QSPI target. The J-Link GDB Server launcher and the VS Code profiles for that target. The `-Og` fragment. | The board, including its SDRAM timing and device configuration data (DCD). Any J-Link script that prepares a board other than the verified EVKB QSPI flow. Its own build, flash and debug scripts, which may reuse the pieces above. | A general flash or debug wrapper over `west flash` and `west debug`. Unattended programming. A J-Link script that initialises SEMC. |

The Zephyr J-Link runner needs a local J-Link Commander even when it is given a remote GDB host. The Dev Container has none, so the split between the Mac-hosted server and the container GDB stays.

## External Linux application using Crux source

The verified external-consumer contract uses the **root** `add_subdirectory()` entry point. When included as a dependency, Crux selects **Base only** by default. The parent project's `BUILD_TESTING`, enabled languages and language-standard settings remain its own. Crux's tests and samples default OFF in this mode.

Enable only the needed capabilities before inclusion with `CRUX_BUILD_<COMPONENT>`. The available choices are `TIME`, `DELAY`, `THREAD`, `SYNC`, `QUEUE`, `NETIF`, `SOCKETS`, `CONFIGURATION`, `FILE`, `SERIAL`, `LOGGING` and `SYSTEM`. Base is always available.

### Complete Queue, Time and Thread example

Create an application directory outside the Crux source tree. Add the following
three files; the example follows the [tested external fixture](../tests/external_application/CMakeLists.txt)
and selects Queue, Time and Thread. The portable logic sends and receives one
value, reads the monotonic clock and waits for an already reached deadline.

`logic.c`:

~~~c
#include <crux/time.h>
#include <crux/thread.h>
#include <crux/queue.h>

int controller_run(void) {
    crux_queue_t queue;
    if (crux_queue_create(1, sizeof(int), &queue) != CRUX_OK)
        return 1;
    int sent = 42;
    int received = 0;
    crux_monotonic_time_t now = {0};
    int failed = crux_queue_send(queue, &sent, sizeof(sent), 0) != CRUX_OK ||
                 crux_queue_receive(queue, &received, sizeof(received), 0) != CRUX_OK ||
                 received != sent || crux_monotonic_now(&now) != CRUX_OK ||
                 crux_thread_sleep_until(now) != CRUX_OK;
    crux_status_t cleanup = crux_queue_destroy(&queue);
    return failed || cleanup != CRUX_OK;
}
~~~

`main.c`:

~~~c
int controller_run(void);
int main(void) {
    return controller_run();
}
~~~

`CMakeLists.txt`:

~~~cmake
cmake_minimum_required(VERSION 3.28)
project(my_controller LANGUAGES C)

set(CRUX_BUILD_QUEUE ON CACHE BOOL "Use Crux Queue")
set(CRUX_BUILD_TIME ON CACHE BOOL "Use Crux typed clocks")
set(CRUX_TIME_PROVIDER memory CACHE STRING "Controlled development realtime")
set(CRUX_BUILD_THREAD ON CACHE BOOL "Use Crux absolute sleep")
add_subdirectory("${CRUX_SOURCE_DIR}" crux)

add_library(controller_logic STATIC logic.c)
set_target_properties(controller_logic PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(controller_logic PRIVATE crux::queue crux::time crux::thread)
crux_assert_portable_consumer(controller_logic)

add_executable(my_controller main.c)
target_link_libraries(my_controller PRIVATE
    controller_logic
    crux::queue crux::time crux::thread
    crux::queue_linux)
~~~

From the **external application's directory**, configure and build with an absolute Crux source path:

~~~sh
cmake -S . -B build -G Ninja -DCRUX_SOURCE_DIR=/absolute/path/to/crux-pilot
cmake --build build
./build/my_controller
~~~

The executable exits with status zero on success. Its portable logic is compiled
against the public facades; only the final executable links the Linux-specific
composition targets. No tests, samples or unrelated Crux capabilities are selected
by this example, and it does not access a physical device.

Portable source targets use public headers (for example, `<crux/queue.h>`), link the public `crux::` capability targets and can call `crux_assert_portable_consumer()` to reject native/private requirements. They must not include Crux's `src/` or `ports/` directories or link Linux-specific composition targets. The final executable explicitly links its used public capabilities and selects OS-specific implementations required by that composition. The consumer boundary checker validates the documented build graph, not arbitrary application architecture.

The external application tests also cover C++17, independently selected Thread/Sync, Serial/Logging/System consumers, parent-test isolation and invalid compositions. For a concise entry point to all build paths, see the [README](../README.md#getting-started).

## Capability and provider selection

| Capability or group | Linux source-tree composition rule |
| --- | --- |
| Base | `crux::base` always exists |
| Time | Set `CRUX_BUILD_TIME=ON` and explicitly select `CRUX_TIME_PROVIDER=memory` (controlled development) or `linux` (system realtime). Controlled Host samples require memory; Linux native writes need explicit application authority. Neither implies production qualification |
| Time CPU Busy-wait | Select `CRUX_TIME_BUSY_WAIT=ON` and Time with an explicit provider; link `crux::time`. Zephyr requires `CONFIG_CRUX_TIME=y` plus `CONFIG_CRUX_TIME_BUSY_WAIT=y`, without RTC. Disabled Busy-wait remains linkable and returns NOT_SUPPORTED for positive durations. See [limits and verified scope](time-busy-wait.md) |
| Thread, Sync | Select independently through `CRUX_BUILD_THREAD` and `CRUX_BUILD_SYNC`. Thread also requires `CRUX_BUILD_TIME=ON` and an explicit Time provider (`memory` for the current controlled Host composition). Their public targets connect the current Linux implementation |
| Access | Select `CRUX_BUILD_ACCESS=ON`, which requires `CRUX_BUILD_SYNC=ON`. The public target has no provider yet, so an image that calls it does not link. The [Access contract](access-contract.md) records the current state. |
| File service | Select `CRUX_BUILD_FILE_SERVICE=ON`, which requires `CRUX_BUILD_ACCESS=ON`, `CRUX_BUILD_FILE=ON` and `CRUX_BUILD_SYNC=ON`. It has no event provider yet, so an image that calls it does not link. The [File service contract](file-service-contract.md) holds its rules and limits |
| Credential store | Select `CRUX_BUILD_CREDENTIAL=ON`, which requires `CRUX_BUILD_SYNC=ON` and `CRUX_BUILD_FILE=ON`. The public target has no protection provider yet, so an image that calls it does not link, and a product build refuses the test-only ports at start. The [credential store](credential-store.md) records the current state. |
| Security events | Select `CRUX_BUILD_SECURITY_EVENT=ON`, which requires Access, Logging, Sync, Thread and Time. A final link adds the object target `crux_security_event_access_port`, or it does not link. A composition sets `CRUX_SECURITY_EVENT_RING`, `_RESERVE`, `_DRAIN_BURST` and `_DRAIN_PAUSE_MS` to suit its log buffer, and keeps the Logging threshold at INFO or more verbose. See the [Security event contract](security-event-contract.md). |
| Configuration | Select `CRUX_BUILD_CONFIGURATION=ON`. A composition with no provider links `crux_configuration_no_provider`, and a load or save then returns `NOT_SUPPORTED`. `CRUX_CONFIG_STRING_MAX` changes the longest string for every user of the header. See the [contract](configuration-contract.md) |
| Configuration service | Select `CRUX_BUILD_CONFIG_SERVICE=ON`, which requires `CRUX_BUILD_ACCESS=ON` and `CRUX_BUILD_CONFIGURATION=ON`. It has no event provider yet, so an image that calls it does not link. The [services contract](status-configuration-services.md) holds its rules and limits |
| Status service | Select `CRUX_BUILD_MGMT_STATUS=ON`, which requires Access, System, and Configuration for its library `crux_configuration_text`. A composition links `crux_mgmt_status_sources` and `crux_mgmt_status_no_security`, or its own providers. The [services contract](status-configuration-services.md) holds its rules and limits |
| Queue | Portable targets use `crux::queue`. The final Linux executable additionally links `crux::queue_linux` |
| Serial | Select `CRUX_BUILD_SERIAL=ON`. The existing Linux provider value is `CRUX_SERIAL_PROVIDER=host` and TTY binding belongs to platform composition |
| I2C | Select `CRUX_BUILD_I2C=ON`; the Host provider is a deterministic loopback fixture for portable consumer tests. Zephyr applications compile the native provider and bind `crux-i2c0` to a selected controller |
| SPI | Select `CRUX_BUILD_SPI=ON`; the Host provider is a deterministic loopback fixture. Zephyr applications compile the native provider and bind `crux-spi0` to a selected peripheral |
| Logging | Select `CRUX_BUILD_LOGGING=ON`. The existing Linux provider value is `CRUX_LOGGING_PROVIDER=host` |
| System | Select `CRUX_BUILD_SYSTEM=ON` and use `crux::system`; the Linux provider value is `CRUX_SYSTEM_PROVIDER=host`. Host watchdog observation is emulated; normal Host reset/reset-cause operations are unsupported. See the [System validation and limits](system-validation.md) |
| Netif, File, Configuration | Select the required `CRUX_BUILD_*` option and use the public capability target. Linux Netif's primary interface uses `CRUX_NETIF_HOST_INTERFACE` (default `lo`) |
| Sockets | Select `CRUX_BUILD_SOCKETS=ON` and link `crux::sockets` for the specifically qualified standard IPv4 socket profile. It is not a Crux socket-wrapper implementation |

Queue exposes the composition-only `crux::queue_linux` alias listed above. Other providers currently have different internal linkage patterns. Do not link undocumented concrete implementation targets or infer a common provider framework. Names ending in `_linux` identify an implementation environment, not product qualification.

The final application executable must directly declare the public capabilities it uses for the documented compatible-provider checks. Detection through arbitrary static-library private-only forwarding graphs is **not** part of the current contract. For Zephyr, Kconfig, the module entry point and platform composition select implementations instead of `CRUX_BUILD_*` Host options. See the [native consumer policy](native-interface-policy.md#external-source-tree-application-composition) for the exact supported enforcement boundary.

## Validation and troubleshooting

For the focused external-application integration suite, from the Crux source root on Linux:

~~~sh
python3 -m unittest discover -s tests/build -p test_external_application.py -v
~~~

For all existing Python build checks, use `python3 -m unittest discover -s tests/build -v`. Independent Host consumers and Zephyr composition runners are listed in the [test guide](../tests/README.md). The current CI definitions are [Host](../.github/workflows/host.yml) and [Zephyr](../.github/workflows/zephyr.yml). Run tests appropriate to the claimed change rather than assuming Host success qualifies Zephyr or hardware behaviour.

| Symptom | Check |
| --- | --- |
| Unknown `crux::...` CMake target | Enable the matching `CRUX_BUILD_*` option **before** `add_subdirectory()`. For Zephyr, use the correct Kconfig/module path |
| Unwanted GoogleTest, C++ or sample targets in an external project | Check cached `CRUX_BUILD_TESTS`, `CRUX_BUILD_HOST_SAMPLE` and `CRUX_BUILD_REFERENCE_CONTROLLER` settings. Use an isolated build directory |
| Missing or unsupported Time provider | An external Linux Time build requires explicit `CRUX_TIME_PROVIDER=memory` or `linux`. Zephyr system realtime uses Kconfig; optional RTC storage is separate |
| Serial/Logging provider conflict or unsupported selection | Review the existing `host`/`zephyr` provider contract and the selected build entry point. Do not mix Host and Zephyr providers |
| Undefined Queue symbols at final link | Add `crux::queue_linux` to the **final application executable**, not to portable libraries |
| `native consumer requirement` from CMake | Remove private/native targets, headers or flags from the portable consumer. Keep platform composition separate |
| Zephyr module or device selection failure | Verify west bootstrap, pinned SDK, selected module checkout, Kconfig prerequisites and board/Devicetree resource ownership |
| Offline Host check cannot resolve GoogleTest | Run `scripts/bootstrap-host.sh` once to populate the dependency cache before `scripts/check-host.sh` |

## Testing a consumer application

Applications own their tests. Crux adds no test target, Kconfig symbol or public header for them, and it ships no mock per API: write doubles at your application's own port. A passing level implies nothing about another, and `native_sim` is not hardware evidence.

| Level | Entry point | Example in the [out-of-tree consumer](../tests/consumer_poc/README.md) |
| --- | --- | --- |
| Unit | Your own CTest. Portable logic behind your own port needs no Crux target | `host/test_periodic.c` |
| Host integration | The [root `add_subdirectory()` entry point](#external-linux-application-using-crux-source), with `crux_assert_portable_consumer()` on portable targets | `host/test_integration.c` |
| `native_sim` | `west build` or `west twister` with `EXTRA_ZEPHYR_MODULES`, in a west workspace | `zephyr_test/` |
| Functional | Standalone `pytest-twister-harness` on a `native_sim` build | `zephyr/`, `functional/` |
| Hardware | No supported entry point. The internal [device-control tool](../tools/device_control/README.md) is untested on hardware, and any reset or flash needs authorisation for the current task | None |

~~~sh
west build -b native_sim/native/64 -d <build> <application> -- -DEXTRA_ZEPHYR_MODULES=<crux>
west twister -p native_sim/native/64 -T <ztest-application> --extra-args=EXTRA_ZEPHYR_MODULES=<crux>
PYTHONPATH="$ZEPHYR_BASE/scripts/pylib/pytest-twister-harness/src" python3 -m pytest \
  -p twister_harness.plugin --twister-harness --device-type=native --build-dir=<build> <tests>
~~~

> **Warning: `EXTRA_ZEPHYR_MODULES` is skipped when there is no west workspace.** With only `ZEPHYR_BASE` set, Zephyr clears `WEST` and processes no modules. The failure is a missing `kconfig_module_dirs.cmake`, then undefined `CRUX_*` symbols, and neither names the cause. Run from a west workspace, whose manifest does not need to list Crux. Without a workspace, `ZEPHYR_MODULES=<crux>` has been shown to configure; that is the only part of it that has been run.

- Do not depend on `ZEPHYR_<NAME>_*` variables for Crux. The module name follows the checkout directory.
- The harness is upstream code at Alpha status with no version promise, and `west.yml` fixes its revision. It needs `ZEPHYR_BASE` set, plus `pyyaml` and `pykwalify`, which it does not declare.
- Do not use the harness's `hardware` device type behind a safety claim: it flashes on every launch. The [proposal](test-foundation-proposal.md#pytest-twister-harness-findings) records this and the other gaps.

The proposal's [current state](test-foundation-proposal.md#current-state) lists what has run at each level and what has not.

## Support and safety boundaries

A successful Linux Host test proves behaviour only for the tested Host setup. Zephyr `native_sim` validates selected simulated integration. EVKB cross-builds do not prove Flash layout safety, UART electrical operation, timing, peripheral access or physical persistence. Firmware download, reset, Flash write/erase and physical validation require separate task-specific approval and verified hardware/provenance.

Treat unmerged branches, experimental storage targets and candidate writable images as separate gated work. Selecting System does not automatically arm a watchdog; Zephyr System requires explicit platform composition for watchdog, reset and hardware-information resources. Consult the relevant [capability contracts](architecture.md#current-capability-integration) and [validation records](../tests/README.md) before claiming support or running a physical operation.
