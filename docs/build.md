# Build and Integration Guide

This guide describes the supported ways to build Crux and consume its capabilities from another application. It covers the Linux development Host, the Zephyr module and Linux source-tree integration. For component behaviour and test-specific acceptance evidence, use the linked contracts and [test guide](../tests/README.md).

## Choose a build path

| Use case | Entry point | Scope |
| --- | --- | --- |
| Develop and test Crux on Linux | `scripts/bootstrap-host.sh` and CMake presets | Current Linux amd64 development environment |
| Build a Zephyr application | `west` with Crux's `zephyr/module.yml` | Pinned Zephyr workspace, selected `native_sim` and EVKB configurations |
| Consume Crux from a separate Linux CMake project | Root `add_subdirectory()` | Source-tree integration in the supported Linux environment |

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

For another Zephyr application in the west workspace, prefer `EXTRA_ZEPHYR_MODULES` to add Crux while retaining the modules discovered through west. Existing Crux scripts also use the supported older spelling `ZEPHYR_EXTRA_MODULES`. By contrast, `ZEPHYR_MODULES` replaces automatic discovery and must list **all** required modules, not just Crux. For example, the following command builds the existing controller application on `native_sim` without hardware operations:

~~~sh
repo="$PWD"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
(cd "$workspace" && west build -p always -b native_sim/native/64 \
    -d "$repo/build/zephyr/controller-example" \
    "$repo/samples/controller/zephyr" -- -DEXTRA_ZEPHYR_MODULES="$repo")
~~~

Select the required `CONFIG_CRUX_*` Kconfig capabilities and supply board/project Devicetree and native-resource bindings where the component requires them. Follow the applicable capability contract and an existing tested Zephyr fixture instead of copying Linux provider settings.

Do not infer that every Linux Host capability has an equivalent Zephyr provider. Serial and Logging require explicitly supported UART resources and ownership settings, while Time's Zephyr RTC implementation is independent of the Linux development-memory backend. The full [Zephyr test commands](../tests/README.md) and [architecture overview](architecture.md#build-and-native-composition) describe the supported profiles and their limits. None of the build scripts above flashes, resets or operates the physical EVKB.

## External Linux application using Crux source

The verified external-consumer contract uses the **root** `add_subdirectory()` entry point. When included as a dependency, Crux selects **Base only** by default. The parent project's `BUILD_TESTING`, enabled languages and language-standard settings remain its own. Crux's tests and samples default OFF in this mode.

Enable only the needed capabilities before inclusion with `CRUX_BUILD_<COMPONENT>`. The available choices are `TIME`, `THREAD`, `SYNC`, `QUEUE`, `MONOTONIC`, `NETIF`, `SOCKETS`, `CONFIGURATION`, `FILE`, `SERIAL` and `LOGGING`. Base is always available.

The following CMake example assumes the external project owns `logic.c` and `main.c` and consumes Queue and Monotonic. It follows the [tested external fixture](../tests/external_application/CMakeLists.txt):

~~~cmake
cmake_minimum_required(VERSION 3.28)
project(my_controller LANGUAGES C)

set(CRUX_BUILD_QUEUE ON CACHE BOOL "Use Crux Queue")
set(CRUX_BUILD_MONOTONIC ON CACHE BOOL "Use Crux Monotonic")
add_subdirectory("${CRUX_SOURCE_DIR}" crux)

add_library(controller_logic STATIC logic.c)
set_target_properties(controller_logic PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(controller_logic PRIVATE crux::queue crux::monotonic)
crux_assert_portable_consumer(controller_logic)

add_executable(my_controller main.c)
target_link_libraries(my_controller PRIVATE
    controller_logic
    crux::queue crux::monotonic
    crux::queue_linux crux::monotonic_linux)
~~~

From the **external application's directory**, configure and build with an absolute Crux source path:

~~~sh
cmake -S . -B build -G Ninja -DCRUX_SOURCE_DIR=/absolute/path/to/crux-pilot
cmake --build build
~~~

Portable source targets use public headers (for example, `<crux/queue.h>`), link the public `crux::` capability targets and can call `crux_assert_portable_consumer()` to reject native/private requirements. They must not include Crux's `src/` or `ports/` directories or link Linux native composition targets. The final executable explicitly links its used public capabilities and selects native implementations required by that composition. The consumer boundary checker validates the documented build graph, not arbitrary application architecture.

The [README Time example](../README.md#external-application-build-linux-host) demonstrates a smaller tested C17 application with explicit development-memory selection. The external tests also cover C++17, independently selected Thread/Sync, Serial/Logging-only consumers, parent testing isolation and invalid compositions.

## Capability and provider selection

| Capability or group | Linux source-tree composition rule |
| --- | --- |
| Base | `crux::base` always exists |
| Time | Set `CRUX_BUILD_TIME=ON` and explicitly select `CRUX_TIME_PROVIDER=memory` for development/testing. Memory is **not** a production RTC fallback |
| Thread, Sync | Select independently through `CRUX_BUILD_THREAD` and `CRUX_BUILD_SYNC`. Their public targets connect the current Linux implementation |
| Queue, Monotonic | Portable targets use `crux::queue` / `crux::monotonic`. The final Linux executable additionally links `crux::queue_linux` / `crux::monotonic_linux` |
| Serial | Select `CRUX_BUILD_SERIAL=ON`. The existing Linux provider value is `CRUX_SERIAL_PROVIDER=host` and native TTY binding belongs to trusted composition |
| Logging | Select `CRUX_BUILD_LOGGING=ON`. The existing Linux provider value is `CRUX_LOGGING_PROVIDER=host` |
| Netif, File, Configuration | Select the required `CRUX_BUILD_*` option and use the public capability target. Linux Netif's primary interface uses `CRUX_NETIF_HOST_INTERFACE` (default `lo`) |
| Sockets | Select `CRUX_BUILD_SOCKETS=ON` and link `crux::sockets` for the specifically qualified standard IPv4 socket profile. It is not a Crux socket-wrapper implementation |

Only Queue and Monotonic expose the two composition-only Linux aliases listed above. Other providers currently have different internal linkage patterns. Do not link undocumented concrete implementation targets or infer a common provider framework. Names ending in `_linux` identify an implementation environment, not product qualification.

The final native executable must directly declare the public capabilities it uses for the documented compatible-provider checks. Detection through arbitrary static-library private-only forwarding graphs is **not** part of the current contract. For Zephyr, Kconfig, the module entry point and native project composition select implementations instead of `CRUX_BUILD_*` Host options. See the [native consumer policy](native-interface-policy.md#external-source-tree-application-composition) for the exact supported enforcement boundary.

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
| Missing or unsupported Time provider | An external Linux Time build requires explicit `CRUX_TIME_PROVIDER=memory`. Zephyr RTC uses its own Kconfig/native selection |
| Serial/Logging provider conflict or unsupported selection | Review the existing `host`/`zephyr` provider contract and the selected build entry point. Do not mix Host and Zephyr providers |
| Undefined Queue/Monotonic symbols at final link | Add the required `crux::queue_linux` and/or `crux::monotonic_linux` to the **native final executable**, not to portable libraries |
| `native consumer requirement` from CMake | Remove private/native targets, headers or flags from the portable consumer. Keep platform composition separate |
| Zephyr module or device selection failure | Verify west bootstrap, pinned SDK, selected module checkout, Kconfig prerequisites and board/Devicetree resource ownership |
| Offline Host check cannot resolve GoogleTest | Run `scripts/bootstrap-host.sh` once to populate the dependency cache before `scripts/check-host.sh` |

## Support and safety boundaries

A successful Linux Host test proves behaviour only for the tested Host setup. Zephyr `native_sim` validates selected simulated integration. EVKB cross-builds do not prove Flash layout safety, UART electrical operation, timing, peripheral access or physical persistence. Firmware download, reset, Flash write/erase and physical validation require separate task-specific approval and verified hardware/provenance.

The currently open storage and System integration work is **not** included in this merged-source build contract. Treat unmerged branches, experimental storage targets and candidate writable images as separate gated work. Consult the relevant [capability contracts](architecture.md#current-capability-integration) and [validation records](../tests/README.md) before claiming support or running a physical operation.
