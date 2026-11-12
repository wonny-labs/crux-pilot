# Research: application boundary and API stability

Status: **research note**, input to [ADR-0001](../decisions/0001-application-boundary-and-api-stability.md). It decides nothing. Sources were retrieved on 12 November 2026. Repository facts are from `crux-pilot` main `54cc83e` and `crux-controller` main `d123620`.

## Why this note exists

A colleague reviewed Crux on 12 November 2026. A fact-check against main `54cc83e` found these claims largely true:

- Crux is focused on Zephyr, and Linux Host is a development and test environment only.
- The Crux I2C, SPI and GPIO APIs are narrow static bindings: one controller per bus, raw I2C addresses, one GPIO input line, and no device model.
- A product IC such as the ADS112C04 needs Zephyr work that the product owns.
- Zephyr composition is uneven. `zephyr/Kconfig` says Access and its event provider have no Zephyr composition.
- Secure management has never run on a board.
- The Settings integration takes sources from `tests/`.

The colleague's central point is that no general advantage over a well-structured Zephyr application has been shown yet. The owner decided on the same day that Crux will not be ported to an operating system other than Zephyr for the foreseeable future. This note collects what comparable platforms do, so the ADR can rest on practice rather than taste.

## Method and limits

- Three research passes ran in parallel, each limited to primary sources: Zephyr; Pigweed and NuttX; and general practice with the lessons of Mbed OS and vendor HALs. The pages were read through a fetch tool that summarises them, so the wording below is a paraphrase.
- Each section separates **facts**, each with its source, from **interpretation**, which is this note's reading. A fact that could not be confirmed on a fetched primary page is marked *secondhand* or *unverified*.
- Not done:
  - No code was built.
  - No Zephyr version other than the `latest` documentation and the `main` branch was checked, and `crux-pilot` pins v4.4.2.
  - The Vulkan specification, the primary Mbed OS blog posts, ST's UM1725 manual and Pigweed's own facades page could not be fetched. The items that depend on them are marked.

## Crux today

These facts are from this repository unless stated otherwise.

| Fact | Source |
| --- | --- |
| Crux already has a native-free consumer policy. Portable application logic may use public Crux targets and qualified standard APIs only. Native providers and platform composition may use Zephyr. | [Native-free consumer policy](../native-interface-policy.md), Issue #24, PR #39 |
| Two checks enforce it on Crux's own trees. `scripts/check-native-interfaces.py` is a lexical check of designated source roots, which already include `applications/`. `crux_assert_portable_consumer` in `cmake/CruxConsumerBoundary.cmake` checks public usage graphs. Both are bounded checks, not semantic proofs. | [Verification and limits](../native-interface-policy.md#verification-and-limits) |
| A composition interface exists: `crux::file_zephyr` with `<crux/file_zephyr.h>`. It names Zephyr on purpose and is for the code that mounts the file system. The consumer check rejects it by name and by path, and a negative test covers that. | [File root binding](../file-composition-interface.md), PR #346 |
| `<crux/i2c.h>`, `<crux/spi.h>` and `<crux/gpio.h>` each bind one static Devicetree alias. Their only callers are tests and test fixtures. No sample or application calls them. | [I2C](../i2c-contract.md), [SPI](../spi-contract.md) and [GPIO](../gpio-contract.md) contracts; `grep` for the function names at `54cc83e` |
| The reference controller's GPIO input calls Zephyr directly (`device_input.c`), not Crux GPIO. | [Controller migration map](../native-interface-policy.md#controller-migration-map) |
| Crux has no API stability tiers, no deprecation macro and no v1.0 criteria. Before `v1`, a `v0.y.z` release promises a recorded pin set, a stated Zephyr baseline and listed breaking changes, and makes no compatibility promise. | [Versions](../product-composition.md#versions); no `deprecated` in any public header |
| Breaking changes carry a `BREAKING CHANGE:` footer. Crux promises source compatibility to C17/C++17 consumers, not a binary ABI. | [Commit messages](../../CONTRIBUTING.md#commit-messages), [Public API consistency](../coding-guidelines.md#public-api-consistency) |
| A provisional compatibility table is generated from `west.yml`. It lists pins and promises nothing. | [Compatibility table](../compatibility-table.md) |
| `crux-controller`, the out-of-tree reference consumer, already splits its code into three trees, checked by its own `scripts/check_boundary.py` in CI. `workload/` uses ISO C only. `app/src` uses ISO C, public `<crux/NAME.h>` headers and the Sockets profile. The composition zone `app/compose` may use `<crux/file_zephyr.h>` and an allow-list of Zephyr headers. `app/src` reaches the zone only through a port header that it owns. | `crux-controller` `README.md` and `docs/composition-zone.md` at `d123620` |

## Zephyr

**Facts**

| Fact | Source |
| --- | --- |
| An application selects a node by label, alias, chosen node or path and gets its `struct device *` at build time with `DEVICE_DT_GET`. It must check `device_is_ready()` before use. | [Devicetree how-tos](https://docs.zephyrproject.org/latest/build/dts/howtos.html) |
| A `struct device` holds `config`, `data` and `api`, a pointer to a subsystem's table of functions. Public inline functions call through that table. | [Device driver model](https://docs.zephyrproject.org/latest/kernel/drivers/index.html) |
| `*_dt_spec` structures bundle the Devicetree data of one use, for example `gpio_dt_spec` with `gpio_pin_configure_dt`. `i2c_write_read_dt` and `spi_transceive_dt` take the I2C and SPI equivalents. | [GPIO](https://docs.zephyrproject.org/latest/hardware/peripherals/gpio.html), [I2C](https://docs.zephyrproject.org/latest/hardware/peripherals/i2c.html), [SPI](https://docs.zephyrproject.org/latest/hardware/peripherals/spi.html) |
| I2C offers message arrays with read, write, stop, restart and 10-bit flags, a target mode (described as experimental), RTIO submission and a per-bus transfer timeout. | [I2C interface](https://docs.zephyrproject.org/latest/doxygen/html/group__i2c__interface.html) |
| SPI offers `spi_config` with mode, bit order, held chip select, bus locking, dual, quad and octal lines, half duplex and TI frame format. It has synchronous, callback and RTIO forms. | [SPI interface](https://docs.zephyrproject.org/latest/doxygen/html/group__spi__interface.html) |
| GPIO offers input, output, pulls, drive modes, active levels and edge and level interrupts. | [GPIO](https://docs.zephyrproject.org/latest/hardware/peripherals/gpio.html) |
| The sensor API has a fetch-and-get model and a newer read-and-decode model on RTIO. The page says the newer model is still stabilising and the older one will likely be deprecated. | [Sensors](https://docs.zephyrproject.org/latest/hardware/peripherals/sensor/index.html) |
| API lifecycle: **experimental** (may change or go; needs docs, one implementation and one sample), **unstable** (may be removed without deprecation), **stable** (complete docs, test coverage, two development releases; a breaking change needs an RFC, a migration guide entry and maintainer approval, and raises the API's major version), **deprecated** (at least two full releases before removal, with a compiler attribute or Kconfig option, a named replacement and a migration guide entry). | [API lifecycle](https://docs.zephyrproject.org/latest/develop/api/api_lifecycle.html) |
| The API overview lists a version for each API, and a version of 1.0 or higher means stable. I2C is 1.0.0, SPI 1.1.0, GPIO 1.0.0, sensor 1.0.0 and ADC 1.0.0. The fetch read only part of the page. | [API overview](https://docs.zephyrproject.org/latest/develop/api/overview.html) |
| Zephyr releases about every six months. An LTS release comes every 2.5 to 3 years and is maintained for about five years. | [Release process](https://docs.zephyrproject.org/latest/project/release_process.html) |
| No ADS112C04 driver or binding was found upstream. `dts/bindings/adc` on `main` lists other TI ADCs (ADS1112 to ADS1115, ADS1118, ADS1119, ADS1220 and others) and no `ti,ads112c04.yaml`, and that file name returned 404. The `drivers/adc` listing was partial. No full-text search was run. | [dts/bindings/adc](https://github.com/zephyrproject-rtos/zephyr/tree/main/dts/bindings/adc), [drivers/adc](https://github.com/zephyrproject-rtos/zephyr/tree/main/drivers/adc) |
| Out-of-tree code joins a build as a module through `zephyr/module.yml`. The example-application repository shows an application with its own drivers, bindings and boards in the T2 west topology. That `dts_root` is the route for bindings is *unverified*. | [Modules](https://docs.zephyrproject.org/latest/develop/modules.html), [example-application](https://github.com/zephyrproject-rtos/example-application) |

**Interpretation**

- Zephyr already gives an application a device model, a Devicetree binding route and broad, stable driver APIs. A Crux bus API can only be a subset of them, and every feature it leaves out is one more reason to go around it.
- Zephyr's stable APIs change slowly and with notice: an RFC, a migration guide entry and two releases of deprecation. Code that calls them directly, such as a product's adapter, carries a bounded and announced upgrade cost.
- A product that needs the ADS112C04 writes either a Zephyr ADC driver in a module or direct I2C code in an adapter. Either way it is Zephyr code, whatever Crux offers.

## Pigweed

**Facts**

| Fact | Source |
| --- | --- |
| A facade is a swappable interface and a backend is its implementation, selected at build time (a Bazel label flag or platform constraint, a GN build argument). *Secondhand*: the facades page itself did not fetch. | [docs/facades.rst](https://pigweed.googlesource.com/pigweed/pigweed/+/HEAD/docs/facades.rst) |
| `pw_i2c` has an abstract `Initiator` whose backends implement a transfer of messages, and a `RegisterDevice` helper for register access with a timeout on each call. | [pw_i2c docs](https://raw.githubusercontent.com/google/pigweed/main/pw_i2c/docs.rst), [register_device.h](https://raw.githubusercontent.com/google/pigweed/main/pw_i2c/public/pw_i2c/register_device.h) |
| `pw_spi` has an `Initiator`, a `ChipSelector` and a `Device` with `WriteRead` and a transaction object that holds the bus. | [pw_spi docs](https://raw.githubusercontent.com/google/pigweed/main/pw_spi/docs.rst) |
| `pw_digital_io` has `DigitalIn`, `DigitalOut` and interrupt variants. Optional features are declared by flags, and calling an unsupported one crashes. | [pw_digital_io docs](https://raw.githubusercontent.com/google/pigweed/main/pw_digital_io/docs.rst) |
| Each module lists backends with their own status. The Zephyr backend of `pw_i2c` is Experimental and that of `pw_spi` Stable. The Zephyr backend of `pw_digital_io` is Experimental. | [pw_i2c backends](https://raw.githubusercontent.com/google/pigweed/main/pw_i2c/backends.rst), [pw_spi backends](https://raw.githubusercontent.com/google/pigweed/main/pw_spi/backends.rst), [pw_digital_io backends](https://raw.githubusercontent.com/google/pigweed/main/pw_digital_io/backends.rst) |
| Module metadata has a status of stable, unstable, experimental or deprecated. No definition of what each status guarantees, no versioning statement and no deprecation policy was found. *Secondhand* (search snippets). | [module_metadata.json](https://pigweed.googlesource.com/pigweed/pigweed/+/HEAD/docs/module_metadata.json) |

**Interpretation**

- Pigweed's hardware interfaces are small and capability-specific. They exist because Pigweed has several backends per interface, on several operating systems. With one operating system, Crux has no such second backend to justify the same layer.
- Even a project the size of Pigweed labels its Zephyr I2C and GPIO backends Experimental. A thin bus interface over Zephyr is not free to keep correct.
- Per-module status labels are common practice, but labels without stated guarantees leave consumers to pin revisions.

## NuttX

**Facts**

| Fact | Source |
| --- | --- |
| NuttX takes POSIX and ANSI as its primary standards. | [About NuttX](https://nuttx.apache.org/docs/latest/introduction/about.html) |
| Drivers are split into an upper half, which registers with the operating system and implements the file operations, and a lower half, which is hardware-specific and lives at architecture or board level. | [Drivers](https://nuttx.apache.org/docs/12.9.0/_sources/components/drivers/index.rst.txt) |
| An I2C lower half implements `struct i2c_ops_s`. The docs say master drivers are not normally accessed directly by user code. | [I2C drivers](https://nuttx.apache.org/docs/12.9.0/_sources/components/drivers/special/i2c.rst.txt) |
| An application can still reach a bus through the `/dev/i2cN` character driver and `I2CIOC_TRANSFER`, which a tool in `nuttx-apps` uses. | [i2c_devif.c](https://apache.googlesource.com/nuttx-apps/+/refs/heads/releases/9.1/system/i2c/i2c_devif.c) |
| NuttX prefers its newer sensor framework, with a common upper half and register-level lower halves, for new sensors. *Partly secondhand*: the framework page returned empty. | [Sensors](https://nuttx.apache.org/docs/12.9.0/_sources/components/drivers/special/sensors.rst.txt) |
| No NuttX API or ABI stability policy was found. Release notes have a compatibility section that covers build changes only. | [Release 10.1.0](https://nuttx.apache.org/releases/10.1.0/) |

**Interpretation**

- In NuttX the operating system owns the device model, and applications get devices as files. Crux is not an operating system and cannot take that role on top of Zephyr, which already has its own device model.
- NuttX itself steers applications away from raw bus access towards device-level drivers. That supports writing device code as a driver or an adapter rather than calling a generic bus API from application logic.

## General practice

**Facts**

| Topic | Fact | Source |
| --- | --- | --- |
| Ports and adapters | The application owns its ports. Each port is a boundary for one purposeful conversation. An adapter translates between a port and one technology, such as a real device or a test double. The aim is to run the application in isolation from its devices. | [Cockburn, Hexagonal architecture](https://alistair.cockburn.us/hexagonal-architecture/) |
| SemVer | MAJOR for incompatible changes, MINOR for compatible additions and for deprecations, PATCH for compatible fixes. Under `0.y.z` anything may change. The FAQ advises at least one minor release that carries a deprecation before the removal. | [Semantic Versioning 2.0.0](https://semver.org/) |
| Tiers | Kubernetes: alpha may be removed in any release without notice; beta is served for 9 months or 3 releases after deprecation, whichever is longer; GA is not removed within a major version. | [Kubernetes deprecation policy](https://kubernetes.io/docs/reference/using-api/deprecation-policy/) |
| Tiers | Rust keeps unstable features behind flags that only the nightly channel accepts. | [Rust Book, nightly Rust](https://doc.rust-lang.org/book/appendix-07-nightly-rust.html) |
| Contrast | The Linux kernel keeps no stable internal API. Only the user-space interface is kept stable, and in-tree callers are fixed together with an internal change. | [stable-api-nonsense](https://docs.kernel.org/process/stable-api-nonsense.html) |
| Add, don't change | libcurl does not break its ABI as a rule. It adds functions and options instead. | [libcurl ABI](https://curl.se/libcurl/abi.html) |
| Versioned structures | Win32 structures such as `WNDCLASSEX` carry a `cbSize` member that the caller sets. Vulkan's `sType`/`pNext` chains are *unverified* (the specification did not fetch). | [WNDCLASSEXW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-wndclassexw) |
| Deprecation | GCC's `deprecated` attribute applies to functions, variables, types and enumerators and warns at each use, with an optional message. | [GCC common attributes](https://gcc.gnu.org/onlinedocs/gcc/Common-Attributes.html) |
| Contract tests | In consumer-driven contracts each consumer states the parts of a provider's contract it depends on, and the provider checks them. | [Fowler, Consumer-Driven Contracts](https://martinfowler.com/articles/consumerDrivenContracts.html), [Pact](https://docs.pact.io/) |
| Mbed OS | Arm states that Mbed OS and the Mbed platform reach end of life in July 2026. The removal of about 265 deprecated APIs at once in Mbed OS 6 is *secondhand* (search summaries of `os.mbed.com` posts, which redirected). | [Arm, Mbed OS](https://www.arm.com/products/iot/mbed-os) |
| Vendor HALs | ST provides low-layer (LL) drivers next to its HAL, described as lighter, register-level and less portable. *Secondhand*: UM1725 did not fetch. | [ST UM1725](https://www.st.com/resource/en/user_manual/um1725-description-of-stm32f4-hal-and-lowlayer-drivers-stmicroelectronics.pdf) |
| Vendor HALs | CMSIS-Driver defines generic peripheral interfaces, independent of any RTOS, so middleware can be reused. | [CMSIS-Driver](https://arm-software.github.io/CMSIS_6/latest/Driver/index.html) |
| Vendor HALs | The Arduino core API is a platform-neutral header definition that each board core implements, and it carries a version macro. | [ArduinoCore-API](https://github.com/arduino/ArduinoCore-API) |

No named primary source criticising wrapper HALs was found, so this note attributes no such criticism.

**Interpretation**

- Ports and adapters fits the owner's concern directly. The product, not Crux, defines the narrow interfaces its logic needs in its own terms. Zephyr code lives in adapters, so it can use every Zephyr feature without Crux having to anticipate it.
- Stability practice converges on four ideas: label maturity, promise little before `1.0`, add rather than change, and deprecate with a stated window before removal. Zephyr's window is two releases, Kubernetes' is a time or release floor, and SemVer's is at least one minor release.
- A size or version field in a structure buys binary compatibility. Crux promises source compatibility only, so a rule that zero means default for every field gives the same evolution path with less ceremony.
- Mbed shows the cost of a wide API surface that must be deprecated in bulk. ST's LL drivers show that a wrapping HAL ends up offering a lower layer anyway. Both point to keeping Crux's surface small and leaving devices to Zephyr. This is interpretation: neither source says so about Crux.

## Implications for the ADR

1. Keep the existing native-free boundary and name the adapter explicitly. Product-owned ports with Zephyr adapters are the escape route that needs no Crux change.
2. Do not build a device or driver model on Zephyr's. With one operating system, a Crux bus API has no second backend to justify it and many Zephyr features to fall behind.
3. Use Zephyr's lifecycle names, so a Zephyr developer reads Crux labels the same way. Add a deprecation window and the rule that zero means default.
4. Measure Crux's value against a plain Zephyr application rather than assert it. A plain Zephyr application can also be structured as ports and adapters, so the comparison has to isolate what Crux adds: its services, the Host providers that let domain logic run without Zephyr, and the product composition tooling.
