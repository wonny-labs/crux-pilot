# Platform direction review: Zephyr, Linux and the application API

Status: **review and proposal**, for an independent review, an adversarial review and then the owner's decision. It decides nothing. It changes no code, check, contract, decision record or protected path. Written on 13 November 2026 against main `828021cf`.

**Sources.** Repository facts are cited as `path:line` at `828021cf`. `crux-controller` facts are from its public `main` at `405cce47ed`. One further consumer is private: it ports a third-party real-time runtime onto Crux and is called the **runtime port** below. Only generic facts about it are used, and the ones already public are in Draft PR #174. External facts come from two research passes on other platforms and on drivers and host execution. They are given as those passes report them, with their URLs, and their *unverified* marks are kept.

**Labels.** **Fact**: checked in a cited file or source. **Inference**: this review's reading. **Recommendation**: a proposal for the owner.

## 1. Summary

**Recommendation: option O4, "Zephyr for execution and devices, Crux for services".**

- **One rule for application code, in three questions** ([section 5.2](#52-the-developer-rule)). OS and hardware facilities go to Zephyr, called directly from the product's Zephyr code. Platform services go to Crux. Product behaviour is plain C in the domain, behind ports that the product owns, and is tested on the host.
- **Public Crux contracts stay only where they carry policy or are adopted with consumers**: Time, Logging, File with its composition interface, Configuration, System, and Netif and the Sockets profile as they are. The twelve management services and the product composition tooling stay as they are.
- **Thread and Sync become Crux-internal.** They are the mechanism that the services use, with a Zephyr provider and a Linux test provider. Products use Zephyr threads and locks. Crux Thread and Sync get no real-time extension: no numeric priorities, no run-time priority change, no recursive mutex.
- **Queue and Serial leave the public surface** by the same process as I2C, SPI and GPIO (ADR-0001 D4). Products use `k_msgq` and the Zephyr UART API.
- **Linux Host stays a test environment.** Its providers stay as test providers for Crux's own tests and for domain code that uses Crux's public contracts. Embedded Linux is not designed now. If a named product needs it, the port is made at the service boundary, not at the kernel layer.
- **Draft PR #174 should not be merged as it is** ([section 7](#7-coexistence-in-one-image-and-what-to-do-with-174)).

**What changes from [ADR-0001](../decisions/0001-application-boundary-and-api-stability.md).** No owner decision of ADR-0001 is reversed. D1 would be amended: the domain owns no thread, lock or queue. D4 would extend to Queue and Serial, and D5's Internal tier would apply to Thread and Sync. The row of Decision 3 that lists "Time, Thread, Sync, Queue, Logging, File, Configuration with Host providers" would be split into public contracts and internal mechanisms. Build B of the D10 experiment would change. The owner's statement that Crux gets no non-Zephyr port stays. The README's "longer-term goal" sentence about Embedded Linux would be reworded. Every one of these is an owner decision in [section 9](#9-owner-decisions). None is made by this document.

**Would Crux be unnecessary with Zephyr only?** Not because of this choice. Crux's thin kernel wrappers were never a strong reason for it to exist: the justification ADR-0001 gives them is Host execution, which it calls thin over the Zephyr kernel (`docs/decisions/0001-application-boundary-and-api-stability.md:181`). Crux's case rests on its services (H1) and its product composition tooling (H4). Both are still unproven on a board (`docs/decisions/0001-application-boundary-and-api-stability.md:344-347`). nRF Connect SDK is the precedent for a Zephyr-only platform that adds services. *Inference*: if H1 fails on hardware, Crux loses its main reason to exist, whatever happens to Thread and Sync.

**Owner decisions needed:** ten, P1 to P10, each with a recommendation ([section 9](#9-owner-decisions)).

## 2. Evaluation criteria

Weights add up to 100. Each option gets 1 (poor) to 5 (good) on each criterion.

| # | Criterion | Weight | Why this weight |
| --- | --- | --- | --- |
| C1 | **Developer clarity**: one rule that an application developer can apply without asking | 20 | This is the owner's first concern: mixing Crux and Zephyr without a rule (`docs/decisions/0001-application-boundary-and-api-stability.md:39`) |
| C2 | **Evidence from real consumers**: what `crux-controller` and the runtime port actually use and bypass | 15 | The engineering approach asks for real consumers (`AGENTS.md:23`, `docs/coding-guidelines.md:29`) |
| C3 | **Real-time fitness**: priorities, run-time changes, lock counts and priority inheritance where a real-time consumer needs them | 15 | The one demanding consumer bypasses Crux Thread and Sync on its real-time path |
| C4 | **Test speed, and what host tests prove** | 10 | The services have Host tests, built with ASan and UBSan, and selected suites run under ThreadSanitizer (`README.md:39`). Host tests prove logic, not timing |
| C5 | **Maintenance cost**: two providers for each contract, CI time | 10 | 13 components have Linux or Host ports of about the size of their Zephyr ports (F3) |
| C6 | **Future embedded Linux** | 5 | Low: the owner decided on no non-Zephyr port for the foreseeable future (`docs/decisions/0001-application-boundary-and-api-stability.md:43`). Not zero: `README.md:5` still names it a longer-term goal |
| C7 | **Zephyr upgrade churn**: who absorbs a baseline move | 10 | ADR-0001 rule 6 puts it in adapters (`docs/decisions/0001-application-boundary-and-api-stability.md:269`) |
| C8 | **Risk of a least-common-denominator API** | 10 | Extending a thin layer to real-time needs grows a second kernel API, the owner's churn concern (`docs/decisions/0001-application-boundary-and-api-stability.md:40-41`) |
| C9 | **Cost of change from today**: code, consumers, deprecations, protected paths | 5 | Low weight because nothing is tagged and most affected interfaces are Experimental, which may change in any release (`docs/decisions/0001-application-boundary-and-api-stability.md:228`) |

## 3. Lessons from other platforms

Condensed from the two research passes. "Outcome" is a fact where a source states it. "Lesson for Crux" is inference.

| Project | Abstracts | Leaves native | Backends | Outcome | Lesson for Crux |
| --- | --- | --- | --- | --- | --- |
| Pigweed ([pw_thread backends](https://pigweed.dev/pw_thread/backends.html), [pw_sync_zephyr](https://pigweed.googlesource.com/pigweed/pigweed/+/HEAD/pw_sync_zephyr/docs.rst), [os/zephyr.rst snapshot](https://pigweed.googlesource.com/pigweed/pigweed/+/a833a8f03bed891b5012d7889dbfcc09a8b951bd/docs/os/zephyr.rst)) | Thread, sync, chrono, log, assert, interrupt facades; small I2C, SPI and digital I/O interfaces | No filesystem or socket facade at OS level (*inferred, unverified*) | Five thread backends: embOS, FreeRTOS, host STL, ThreadX, Zephyr. Host STL is a first-class backend | Works where many consumers share modules across OSes. The Zephyr backend is the youngest; a snapshot called it preliminary, verified on `native_posix` only | A facade layer pays when there are several real backends. Crux has one production backend |
| Arm Mbed OS ([Arm](https://www.arm.com/products/iot/mbed-os), [Memfault 1.40.0 changelog](https://components.espressif.com/components/memfault/memfault-firmware-sdk/versions/1.40.0/changelog)) | One C++ API over HAL, RTOS, connectivity and security | Little: the platform owned the drivers | Many Cortex-M targets, one OS (RTX) | End of life in July 2026, per Arm. Memfault deleted its Mbed port. Arduino moved to Zephyr and kept its own thin API above it | Do not build on an abstraction layer you do not control. Plan for deleting a port |
| CMSIS-RTOS2 on Zephyr ([Zephyr CMSIS-RTOS v2](https://docs.zephyrproject.org/latest/services/portability/cmsis_rtos_v2.html)) | Threads, mutexes, semaphores, flags, queues, timers | Everything else | RTX, FreeRTOS, Zephyr | Zephyr documents unsupported calls and fixed semantics: priority inheritance and recursion always on. It sits under "portability" | A common RTOS API over Zephyr leaves semantic gaps and serves as a porting aid, not a base for new code |
| nRF Connect SDK ([test framework](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/test_and_optimize/test_framework/index.html)) | No OS layer found (*absence unverified*) | Zephyr APIs, Devicetree, Kconfig, logging and settings used directly | Zephyr only | A large product base on Zephyr plus vendor services. Host tests use Ztest, Unity with CMock, `native_sim` and `unit_testing` | The closest analogue of "Zephyr-only plus services". It tests on Zephyr's own host targets, not on a second OS port |
| Matter SDK ([Zephyr platform tree](https://pigweed.googlesource.com/third_party/github/project-chip/connectedhomeip/+/HEAD/src/platform/Zephyr)) | Platform managers: lifecycle, configuration, connectivity, BLE, Thread stack, plus a System Layer | Each platform's OS inside its port | More than ten, Zephyr and Linux among them (*current list unverified*) | Works because silicon vendors fund their own ports of one large service codebase | If Linux becomes real, abstract at the service boundary, not with a Thread or Mutex wrapper |
| OpenThread ([PAL guide](https://openthread.io/guides/porting/implement-platform-abstraction-layer-apis)) | Alarm, radio, entropy, settings or flash, optional UART and logging, reset | Threads, mutexes and files: the core is single-threaded with a process loop | Zephyr, FreeRTOS, a POSIX simulator, many vendors (*counts unverified*) | A narrow PAL shaped by what the library needs stays small | Shape a port by what the service needs |
| Memfault and Golioth SDKs ([Memfault docs](https://docs.memfault.com/docs/mcu/faq), [Golioth SDK](https://docs.golioth.io/reference/firmware/golioth-firmware-sdk/)) | Small port hooks for a service library | The OS integration, larger on Zephyr | Memfault: FreeRTOS, Zephyr, ESP-IDF and others. Golioth: Zephyr, ESP-IDF, ModusToolbox, Linux (experimental) | Services with thin ports. Linux was added late and kept experimental | Linux comes as a lower-priority service port, not as a co-equal kernel layer |
| ESP-IDF Linux target, NuttX sim ([ESP-IDF host apps](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/host-apps.html), [NuttX simulation](https://nuttx.apache.org/docs/12.8.0/implementation/simulation.html)) | The RTOS API on a host process | Real timing | One RTOS each | Kept as preview or development tools; ESP-IDF warns the simulation does not represent the device | A host target is a test aid. It never replaces target evidence |
| embedded-hal 1.0 ([announcement](https://blog.rust-embedded.org/embedded-hal-v1/), [changelog](https://github.com/rust-embedded/embedded-hal/blob/master/embedded-hal/CHANGELOG.md), [linux-embedded-hal](https://github.com/rust-embedded/linux-embedded-hal)) | Narrow driver traits: digital, I2C, SPI, delay, PWM duty | Configuration, initialisation, timers, serial, ADC, CAN: moved or dropped | Many HALs, plus Linux userspace through linux-embedded-hal | Succeeded after cutting scope to what generic drivers need; about 1,800 dependent crates reported (approximate) | A device interface pays only with many cross-platform drivers. In C on Zephyr, Zephyr's driver API already is that standard |
| Zephyr device model, emulators and `native_sim` ([bus emulators](https://docs.zephyrproject.org/latest/hardware/emulator/bus_emulators.html), [native_sim](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html), [POSIX arch](https://docs.zephyrproject.org/latest/boards/native/doc/arch_soc.html)) | Devices through Devicetree and subsystem APIs; I2C and SPI emulators run the unchanged driver | Nothing to cross OS boundaries: that is not a goal | Every Zephyr board; emulators work on `native_sim` and on targets with enough memory | `native_sim` runs the real kernel deterministically, one thread at a time, with ASan and UBSan options. Zephyr says it is for application code, not low-level driver or timing work | Device code belongs in Zephyr adapters, tested on `native_sim` with emulators. Host evidence never shows timing |
| AUTOSAR ([Adaptive Platform](https://www.autosar.org/standards/adaptive-platform)) | Classic: layered basic software on an OSEK-style OS. Adaptive: POSIX-based | Each platform's internals | Two platforms | The strongest standardisation effort split into an MCU platform and a POSIX platform | Do not expect one thin layer to span MCU and Linux products |
| Eclipse MRAA and UPM ([MRAA](https://github.com/eclipse/mraa)) | Pin and bus access across Linux boards, sensors above it | The kernel drivers | Many boards | Appears dormant; last release mentioned January 2020 (*unverified*) | A board-zoo device abstraction ages badly, and so does what sits on it |

**What the lessons say for Crux** (inference):

1. **Count backends and consumers.** With one production backend, a portable kernel layer is cost without payoff. The platforms that paid for one had several real backends (Pigweed, Matter).
2. **Abstract at the service boundary.** Where a service library needs a port, it is shaped by the service (OpenThread, Matter, Memfault), not by the OS.
3. **Use Zephyr's own host story** (`native_sim`, emulators, Twister) for code that touches Zephyr, and keep pure logic OS-free so that a plain host compiler can test it.
4. **Do not grow a common RTOS API to real time.** CMSIS-RTOS2 on Zephyr shows the gaps that result.
5. **Device interfaces belong to the OS.** Zephyr's device model plays the role that embedded-hal plays for Rust.

## 4. Options

### 4.1 Facts the options rest on

| # | Fact | Source |
| --- | --- | --- |
| F1 | Of the twelve management components, ten include `<crux/sync.h>` (all but `cli` and `ssh`) and five include `<crux/thread.h>`. Four create worker threads: `security_event`, `network_settings`, `reboot`, `web` | Include scan of `components/`; `components/security_event/src/security_event_drain.c:164`, `components/network_settings/src/network_settings.c:639`, `components/reboot/src/reboot.c:432`, `components/web/management/web_management_save.c:186`, `components/web/session/web_session_attempt.c:371` |
| F2 | No Crux component includes `<crux/queue.h>` | Include scan of `components/` |
| F3 | Linux or Host ports exist for 13 components: ten under `ports/linux` (time, thread, sync, queue, logging, file, system, serial, netif, interrupt), GPIO under `ports/host`, I2C and SPI under `host/`. The ten `ports/linux` trees hold 1,612 raw lines of C and headers; their Zephyr ports hold 1,913 | `ls components/*/ports`; `wc -l` at `828021cf` |
| F4 | Crux Thread maps three classes to Zephyr priorities `6 - class`, that is 4, 5 and 6, fixed in code. No run-time change is promised | `components/thread/ports/zephyr/thread_native.c:5,99`; `docs/thread-sync.md:109-113` |
| F5 | Pools: threads 1..64, mutexes 1..64, semaphores 1..64 by Kconfig; Queue fixed at four queues of sixteen 64-byte records | `zephyr/Kconfig:51-54,72-80`; `components/queue/src/queue.c:7-9` |
| F6 | The Thread and Sync contract does not promise real-time scheduling, and Host runs every class at normal priority | `docs/thread-sync.md:10-13,109-113` |
| F7 | Crux monotonic time reads `k_uptime_ticks` with no lock; realtime takes a `k_mutex` | `components/time/src/time_clock.c:6-24`, `components/time/ports/zephyr/time_clock_zephyr.c:7`, `components/time/ports/zephyr/time_port_zephyr.c:18` |
| F8 | Crux Logging on Zephyr calls Zephyr's `LOG_*` macros after its own checks | `components/logging/ports/zephyr/logging_zephyr.c:13-28` |
| F9 | `crux-controller` calls `crux_thread_create` twice at NORMAL priority and a Crux Queue in `app/src/workload_thread.c` (lines 10-14, 137, 150-159). Its Host tests link only `workload` and `net_client`, not any Crux provider. So its Crux code runs only on `native_sim` and the board | `crux-controller` `tests/host/CMakeLists.txt:20-32`, `README.md:46-52`, at `405cce47ed` |
| F10 | The runtime port uses Crux File, Logging, Time, System and the Sockets profile. It runs its cyclic task timers, its main thread and its writer on native threads, because it needs distinct priorities set at run time. By default it uses a native recursive mutex, because it needs more than 64 locks at once and because of the priority defect that #174 fixes. Its timer wait uses a Crux semaphore and `crux_thread_sleep_until` | Draft PR #174 (public part); the private records (generic facts only) |
| F11 | #174 has been a Draft since 3 November 2026. It fixes the defect where a thread keeps an inherited priority after `crux_mutex_unlock`, and raises the mutex maximum to 256. It runs Sync metadata sections with the scheduler locked: it needs one CPU and no meta-IRQ threads, one Sync call can delay every other thread, and nothing was measured on hardware | PR #174 description |
| F12 | ADR-0001 cites `crux-controller`'s Host tests as evidence for H2 (Host providers). By F9, those tests use no Crux Host provider. No consumer evidence for H2 exists | `docs/decisions/0001-application-boundary-and-api-stability.md:345`; F9 |
| F13 | ThreadSanitizer runs on Linux Host for selected suites. No `native_sim` image with `CONFIG_ASAN` or `CONFIG_UBSAN` is configured in `tests/`, `scripts/` or `.github/` | `scripts/test-mgmt-services.sh:58-59`; `tests/ssh/cli/test-thread-sanitizer.sh:2-4`; `grep` at `828021cf` |
| F14 | CI on 12 November 2026: the Host job took 27.5 minutes (run 43321893524); the Zephyr shards took 12.6 to 28.6 minutes (run 43321893624) | `gh run view` of both runs |
| F15 | Thread, Sync, Queue, Serial and Logging are provisionally Experimental; Time, File, Netif and Sockets provisionally Unstable | `docs/component-readiness.md:43-56` |
| F16 | Linux Host is a development and test environment, not Embedded Linux support. Embedded Linux is called a longer-term goal and a future variant | `README.md:5,33,37,39`; `docs/architecture.md:11` |

### 4.2 O1. ADR-0001 as it is

The domain uses Crux Time, Thread, Sync, Queue, Logging, File and Configuration through their Host providers. Adapters use Zephyr. D10 measures whether that pays.

- *For*: nothing changes. `crux-controller`'s `app/src` already works this way (F9).
- *Against*: two thread and lock APIs in one image, and the rule for which to use depends on the zone, not on the job. A real-time thread cannot be created in the domain (F4), so it moves to an adapter anyway, and the runtime port did exactly that (F10). Every real-time gap closed in Crux Thread or Sync grows a second kernel API (C8). H2 has no consumer evidence (F12).

### 4.3 O2. Zephyr only

The services are rewritten on `k_thread`, `k_mutex` and `k_sem`. The Linux providers are removed. Tests run on `native_sim`. Crux keeps services and tooling (ADR-0001 option c, `docs/decisions/0001-application-boundary-and-api-stability.md:357`).

- *For*: the clearest rule; one API for everything that touches the OS; no second provider; real-time is native.
- *Against*: about one hundred Sync and Thread call sites in ten services change (counted: 69 mutex and 22 semaphore calls, 6 thread creations). The services lose their Host tests, the Host sanitizer runs and ThreadSanitizer. *Inference*: `native_sim` runs one thread at a time, so it cannot stand in for ThreadSanitizer. Its ASan and UBSan support was never tried here (F13). Domain code can no longer link the real Crux services on a host. Removing the Linux providers touches `CMakePresets.json`, `scripts/check-host.sh` and `.github/workflows/host.yml`, all protected. Any later Linux product would rewrite every service.

### 4.4 O3. A full portable layer, extended for real time and Linux production

Numeric priorities, run-time priority change, recursive mutexes, larger or caller-provided pools, and a qualified Linux production provider.

- *For*: one Crux API across Zephyr and Linux.
- *Against*: it reverses the owner's decision of no non-Zephyr port (`docs/decisions/0001-application-boundary-and-api-stability.md:43`) and the rule against a universal OSAL (`AGENTS.md:23`, `docs/coding-guidelines.md:29`). Linux real-time semantics differ: a real-time Linux scheduling class usually needs privileges (general Linux behaviour, not checked here), while Host runs every class at normal priority today (F6). It is the CMSIS-RTOS2 pattern (section 3). No consumer asked for it. The runtime port explicitly waits for a hardware measurement before proposing a priority model (its own record).

### 4.5 O4a. ADR-0001 with a real-time carve-out

As O1, plus one rule: priority-sensitive work always uses Zephyr. Crux Thread and Sync stay public but are documented as non-real-time.

- *For*: little change; stops the growth of Crux Thread.
- *Against*: the developer still chooses between two thread APIs for non-real-time work. That choice is the confusion the owner named.

### 4.6 O4. Zephyr for execution and devices, Crux for services (recommended)

- Products use Zephyr for threads, priorities, locks, queues, timers, interrupts, devices and network stack set-up.
- Domain code is plain C behind product ports, plus Crux's public contracts and service APIs. It owns no threads: a Zephyr "execution shell" in the product's adapter zone runs it. `crux-controller`'s `workload/` already has this shape (F9).
- Thread and Sync stay as Crux-internal mechanisms of the services, with Zephyr and Linux test providers, so the services keep their Host tests. Queue and Serial leave the public surface.
- *For*: one rule ([section 5.2](#52-the-developer-rule)), matching what both consumers already do on their critical paths. Real time is native. Host tests of services and of Crux-using domain code stay.
- *Against*: Linux providers stay, so each change to Thread, Sync or a public contract is still made and tested twice: the ten `ports/linux` trees hold 1,612 lines (F3), and the Host CI job takes 27.5 minutes (F14). `crux-controller` and the runtime port make small changes. Crux service threads still share the scheduler with product threads, so products must place them in their priority map ([section 7](#7-coexistence-in-one-image-and-what-to-do-with-174)).

### 4.7 Scores

| Criterion (weight) | O1 | O2 | O3 | O4a | O4 |
| --- | --- | --- | --- | --- | --- |
| C1 clarity (20) | 2 | 5 | 4 | 3 | 4 |
| C2 consumer evidence (15) | 3 | 3 | 1 | 3 | 4 |
| C3 real time (15) | 2 | 5 | 3 | 4 | 5 |
| C4 tests (10) | 4 | 2 | 4 | 4 | 4 |
| C5 maintenance (10) | 2 | 4 | 1 | 2 | 3 |
| C6 embedded Linux (5) | 3 | 1 | 5 | 3 | 3 |
| C7 Zephyr churn (10) | 3 | 3 | 2 | 3 | 4 |
| C8 common-denominator risk (10) | 2 | 5 | 1 | 4 | 4 |
| C9 cost of change (5) | 5 | 1 | 1 | 4 | 3 |
| **Weighted total (of 500)** | **265** | **370** | **250** | **330** | **395** |

Reasons for the scores that decide the ranking:

- **C1.** O2 gets 5 because "the OS is Zephyr" is one sentence. O4 gets 4 because the domain may also call Crux's public contracts, and Crux's own service threads appear in the product's priority map. O1 gets 2 because the zone, not the job, picks the thread API.
- **C2.** O4 matches the evidence. The runtime port goes native exactly where O4 says to go native (F10). `crux-controller`'s tested code is plain C (F9). Both use Time, Logging and File. O2 also matches the real-time evidence, but discards the services' Host tests and their ThreadSanitizer runs (F13).
- **C4.** Host providers keep the services' Host runs under ASan, UBSan and, for selected suites, ThreadSanitizer (F13). O2 replaces them with `native_sim`, which proves integration but has no ThreadSanitizer equivalent and untried ASan and UBSan. Pure modules can be fuzzed and mutated on the host without any provider under every option, as the SSH wire checks are (`docs/ssh-wolf-provider.md:220,399`).
- **C7.** Under O4, Zephyr kernel changes reach Crux only in the providers of two internal mechanisms. Products absorb the rest in adapters, as ADR-0001 rule 6 already states. O2 puts the services directly on the kernel API.

**Sensitivity.** O4 leads O2 by 25 points. Raising C1 to 30, at the expense of C6 and C9, makes O2 lead by 5. *Inference*: the choice between O4 and O2 depends on how much the owner values one OS API against keeping Host tests of the services. Two probes can settle it with evidence: the `native_sim` sanitizer probe and D10 phase 1 ([section 8](#8-changes-from-adr-0001-migration-and-missing-evidence)). If `native_sim` gives the same evidence at a similar turnaround, the case for Linux providers falls. Moving from O4 to O2 then touches only Crux's own code, because no product calls Thread or Sync under O4.

## 5. Recommendation and final API design

### 5.1 Component roles

Roles: **Public contract**, a portable contract for domain and Zephyr code. **Composition interface**, see [the rules](../composition-interfaces.md). **Crux service**: a service that products compose. **Crux-internal**: used by Crux components only, no promise to products. **Retire**: products use Zephyr in adapters. Tiers follow [ADR-0001 Decision 4](../decisions/0001-application-boundary-and-api-stability.md#decision-4-api-stability-and-change-policy). Today's tier is the provisional one in `docs/component-readiness.md:42-69`.

| Component | Target role | Tier: today, then target | Change from today |
| --- | --- | --- | --- |
| `base` | Public contract (status codes) | Unassigned, then Unstable at tier assignment | None |
| `time` | Public contract: typed clocks, authority, calendar | Unstable | None. Real-time code may read monotonic time, which takes no lock (F7). Its resolution on Zephyr is the kernel tick (F7); a finer timestamp only for a named consumer |
| `thread` | Crux-internal: service worker threads | Experimental, then Internal | Products use `k_thread`. No real-time extension. The header stays in place for Crux components; product use is deprecated at the first tag (P2) |
| `sync` | Crux-internal: service locks and semaphores | Experimental, then Internal | Products use `k_mutex` and `k_sem`. #174 rescoped (P7) |
| `queue` | Retire | Experimental, then Deprecated at the first tag, removed in the next | Products use `k_msgq`. No Crux component uses it (F2); `crux-controller` migrates (P3) |
| `logging` | Public contract | Experimental | None. Zephyr code may use `LOG_*` directly; both reach the same Zephyr log (F8) |
| `file` | Public contract, plus composition interface `crux::file_zephyr` | Unstable | None |
| `configuration` | Public contract: per-parameter policy | Experimental | None. Provider decision (Issue #16) stays open |
| `system` | Public contract: single-owner watchdog, reset cause | Experimental | None |
| `interrupt` | Crux-internal | Internal | None |
| `netif` | Public contract, read-only status; source of the status service | Unstable | None, and no extension. Further interface data comes from Zephyr `net_if` in product Zephyr code |
| `sockets` | Qualified standard profile | Unstable | None |
| `serial` | Crux-internal: transport of the command line's serial console | Experimental, then Internal; product use deprecated at the first tag | Products use the Zephyr UART API in adapters (P4) |
| `gpio`, `i2c`, `spi` | Retire | Experimental, frozen (D4) | As decided in D4: deprecated at the first tag, removed in the next |
| `access` | Crux service | Experimental | None in role. Its threads follow the product priority map (P8) |
| `credential` | Crux service | Experimental | As `access` |
| `security_event` | Crux service | Experimental | As `access`; owns a drain worker (F1) |
| `config_service` | Crux service | Experimental | As `access` |
| `mgmt_status` | Crux service | Experimental | As `access` |
| `network_settings` | Crux service | Experimental | As `access`; owns a worker (F1) |
| `reboot` | Crux service | Experimental | As `access`; owns a worker (F1) |
| `tls_certificate` | Crux service | Experimental | As `access` |
| `file_service` | Crux service | Experimental | As `access` |
| `cli` | Crux service | Experimental | As `access`; its serial console uses internal Serial |
| `web` | Crux service | Experimental | As `access`; owns two workers (F1) |
| `ssh` (private) | Crux service, private provider interface | Experimental, for its Kconfig symbols | As `access` |

**How "Crux-internal" is made visible.** The cheapest way is the tier table and a `Status:` line in each header, both already follow-up 5 of ADR-0001. A check that rejects `crux::thread` and `crux::sync` in a product's domain would change `cmake/CruxConsumerBoundary.cmake` and `scripts/check-native-interfaces.py`, both protected. It is optional and comes later.

### 5.2 The developer rule

An application developer asks three questions, in this order:

1. **Is it an OS or hardware facility?** Threads, priorities, locks, queues, timers, interrupts, devices, buses, pins, network stack set-up. → **Zephyr**, called directly in the product's Zephyr code: adapters, composition and the execution shell. Never through Crux.
2. **Is it a platform service that Crux provides?** Management over web, command line, SSH and SFTP; accounts and access; configuration with validation; file storage with Crux's rules; time authority and calendar; health and watchdog; log records. → **Crux**, from any zone. Composition binds its provider, through a composition interface where one exists.
3. **Is it the product's own behaviour?** Control laws, state machines, protocol logic, limits and alarms. → **Plain C in the domain**, behind ports that the product owns. It may call Crux's public contracts and service APIs, never Zephyr. It owns no thread: the execution shell calls it. It is tested on the host.

| Case | Q1: Zephyr | Q2: Crux | Q3: domain |
| --- | --- | --- | --- |
| Cyclic task timers of a ported real-time runtime | One `k_thread` per timer at priorities from the product's priority map; `k_sem` or `k_timer` for the wake; a recursive `k_mutex` for the runtime's locks | Monotonic timestamps (no lock, F7), Logging, File for retained data, System watchdog | The runtime's own scheduling logic, above its port layer |
| `crux-controller` workload | Producer and consumer threads and their queue: `k_thread` and `k_msgq` in its Zephyr zone. Today they use Crux Thread and Queue in `app/src` (F9) | Logging. File for its configuration record, bound through `crux::file_zephyr` | `workload/` stays plain C, with Host tests and sanitizers, as today |
| Temperature sensor (ADS112C04) | A Zephyr ADC or sensor driver, or `i2c_dt_spec` code, in an adapter behind the product port `temperature_read(&millikelvin)`; a Zephyr emulator on `native_sim` | The status service for a remote reading. Configuration, or File, for the limits | Filtering, alarm limits and hysteresis, tested on the host with a fake of the port |
| SSH service | The product's priority map places Crux's worker threads (Kconfig, P8); the network stack is set up in Zephyr | `CONFIG_CRUX_SSH`, `CONFIG_CRUX_SSH_CLI`, `CONFIG_CRUX_SSH_SFTP`; Access accounts; the credential store | Which commands exist and who may run them, registered with the command line and Access |

*Why Logging and Time stay public while Thread does not* (inference): log records and clock readings merge without interaction, whatever the zone. Threads and locks interact through priorities, inheritance and pools, so two kinds in one image is a design problem, not a style choice. The consumer evidence matches: both consumers use Logging and Time on their main paths, and the real-time consumer bypasses Thread and Sync (F10).

### 5.3 Zones of a product under O4

The three layers of [ADR-0001](../architecture.md#three-layers-for-a-product-adr-0001) stay. Only what each zone may call changes.

| Zone | May call | Must not call | Tested on |
| --- | --- | --- | --- |
| Domain | ISO C; the product's own ports; Crux public contracts (Time, Logging, File, Configuration, System, Netif, the Sockets profile) and service APIs | Zephyr; Crux-internal targets (Thread, Sync, Serial); composition interfaces; Devicetree identifiers | Linux Host: plain C with port fakes, plus Crux Host providers where it calls Crux |
| Adapters, execution shell and composition | Any Zephyr API, Devicetree, driver or subsystem; Crux public contracts and service APIs; composition interfaces | Crux private headers, `ports/` include paths and Crux-internal targets | `native_sim` with emulators, then the board |
| Crux | Its internal mechanisms; Zephyr inside providers | Product knowledge | Linux Host with test providers, `native_sim`, the board |

### 5.4 What O4 asks of each consumer

Effort figures are estimates (inference), not measured.

| Consumer | Today | Under O4 | Effort |
| --- | --- | --- | --- |
| `crux-controller` | Crux Thread and Queue in `app/src`; Time, Logging and File; Host tests of plain C `workload/` (F9) | Its two threads and their queue move to `k_thread` and `k_msgq` in its Zephyr zone; `app/src` keeps Time, Logging and File | One file of 195 lines and its boundary allow-list; its own repository and session |
| Runtime port | Native threads and native recursive locks already; a Crux semaphore and `crux_thread_sleep_until` in its timer wait (F10) | The timer wait moves to `k_sem` and `k_sleep` | Small; a private repository |
| Crux services | Thread and Sync through public headers; Host tests on Linux providers (F1) | The same code; the headers' tier becomes Internal; worker priorities come from Kconfig (P8) | The Kconfig settings and a template example |
| A new product | Three layers; its domain may use Crux Thread, Sync and Queue | The three questions; a plain C domain with Crux public contracts; execution in Zephyr | None extra, and fewer choices |

## 6. Execution targets

| Target | What runs | What it proves | What it cannot prove | Cost |
| --- | --- | --- | --- | --- |
| RT1060-EVKB (`mimxrt1060_evk@B//qspi`) | The product image: Zephyr, drivers, Crux services, native real-time threads | E4, in narrow slices: timing, interrupts, peripherals, persistence, priority interplay, the latency #174 adds | Other boards, endurance, production readiness; only what a recorded run covers (`docs/component-readiness.md:16`) | Highest: board time, owner approval per task, custody coordination. An E3 build alone is cheap and runs in CI |
| `native_sim` (Dev Container and CI) | The real Zephyr kernel and subsystems, Crux's Zephyr providers, product adapters with Zephyr emulators, service images | E2: integration, kernel API use, deterministic ordering, emulated devices. ASan and UBSan exist as options (*untried here*, F13) | Timing, interrupt preemption, data races (one thread at a time), memory-mapped I/O, target memory (`docs/component-readiness.md:14`) | Moderate: a west workspace; CI shards 12.6 to 28.6 minutes (F14); Linux only, emulated amd64 on an arm64 Mac |
| Linux Host unit and sanitizer builds | Crux portable code; services on Linux test providers; product domain code with port fakes | E1: logic, error paths, ASan, UBSan, ThreadSanitizer, fuzzing, mutants | Zephyr behaviour, priorities (all classes run at normal priority, F6), target ABI, anything about timing | Low per run; Host CI job 27.5 minutes (F14); about 1,600 lines of Linux providers to keep in step (F3) |
| Future embedded Linux product | Nothing: not designed | — | — | Only on the trigger below |

**The existing Linux providers under O4** (recommendation, P5):

- **Keep as test providers**: Time (memory and Linux), Thread, Sync, Interrupt, Logging, File, System, Netif, and Serial (PTY, for the command line's console tests). They serve Crux's own service tests and product domain code that uses Crux's public contracts. They stay "not production Embedded Linux", as `docs/thread-sync.md:204-205` already says.
- **Retire with their APIs**: the GPIO, I2C and SPI Host fixtures (D4) and the Queue Linux provider (P3).
- **No new Linux provider** unless a Crux test needs one.
- **Revisit** after the `native_sim` sanitizer probe and D10 phase 1. If they show `native_sim` gives the same evidence at a similar turnaround, remove the providers and move to O2.

**Embedded Linux.** *Recommendation*: design nothing now. The trigger is a named product with a requirement that needs Linux, such as process isolation, an MMU or a rich user interface, and the owner's decision to support it. The boundary is then each Crux service that the product needs, ported to Linux services in the way Matter and OpenThread ports work. Several services would probably give way to Linux-native ones: SSH to OpenSSH, for example. The kernel layer is not the boundary. `README.md:5` should then say that Embedded Linux is not planned, and would be designed at the service boundary if a product needs it, so that it no longer reads against `docs/decisions/0001-application-boundary-and-api-stability.md:43` (P6).

## 7. Coexistence in one image, and what to do with #174

**Rules for an image where native Zephyr code and Crux run together** (recommendation, P8):

1. **One priority map per product, declared once**, in composition. It lists every thread: native real-time threads, Zephyr's own threads (network, logging, work queues), and Crux's internal workers. Crux's classes are fixed at 4, 5 and 6 today (F4). A new Kconfig setting per class, defaulting to today's values, would let the map place them. These are new public Kconfig symbols under ADR-0001 rule 5, added without changing defaults.
2. **One lock, one kind.** Product state is protected by product locks, `k_mutex` or `k_sem`. Crux's locks are inside Crux. A product never holds one of its locks while calling a Crux API that can call back into product code that takes the same lock.
3. **No unbounded scheduler or interrupt lock in Crux.** A Crux section that locks the scheduler or interrupts states its bound, measured on the target. Crux makes no real-time promise (F6), so Crux workers belong below the product's real-time threads unless the product decides otherwise.
4. **Real-time code calls only Crux APIs that take no lock.** The monotonic read is one (F7). Other calls belong on lower-priority threads until their locking is checked and stated.
5. **Crux pools are resource budgets.** The product sets them by Kconfig and tests them in its own image.

**What #174's constraints mean under these rules.** One CPU and no meta-IRQ threads fit the RT1060 today. The scheduler lock in every Sync call breaks rule 3 until it is measured. A Crux service thread taking a lock would delay a product's real-time thread for the longest metadata section, and nobody knows how long that is (F11). The raise from 64 to 256 mutexes served a consumer that, under rule 2, keeps its own native locks.

**Recommendation for #174 (P7):** do not merge it as it is.

- Keep its regression test, which reads a holder's priority before, during and after the wait.
- Choose the fix among the alternatives it lists, judged by rule 3. If the scheduler-lock design is still the best, measure its longest section on the EVKB before merging. That needs the owner's approval of a board run.
- Drop the 64-to-256 change unless a Crux service needs it.
- The defect itself must be fixed, because Sync stays as the services' lock: a service thread can stay boosted today.

## 8. Changes from ADR-0001, migration and missing evidence

| ADR-0001 item | Under O4 | Owner decision |
| --- | --- | --- |
| D1 three layers | Kept. Amended: the domain owns no thread, lock or queue; the execution shell is adapter code | P1, P2 |
| D2 Zephyr only in declared places | Kept | — |
| D3 composition interface rules | Kept | — |
| D4 no device model; I2C, SPI, GPIO retired | Kept. Extended to Queue and Serial | P3, P4 |
| D5 tiers | Kept. Internal applied to Thread and Sync | P2 |
| D6 change rules | Kept. The new priority Kconfig is an addition under rule 5 | P8 |
| D7, D8, D9 | Kept. D8's contract tests still cover Thread and Sync, now for Crux's own use | — |
| D10 experiment, phase 1 | Kept. Build B amended: plain C domain with Crux Time, Logging, and Configuration or File; threads in a Zephyr shell, not Crux Thread (`docs/decisions/0001-application-boundary-and-api-stability.md:374`) | P9 |
| Decision 3, row "Time, Thread, Sync, Queue, Logging, File, Configuration with Host providers" (`:181`) | Split: Time, Logging, File, Configuration and System public with Host providers; Thread and Sync internal with Host providers for Crux's tests; Queue retired | P2, P3 |
| Decision 5, H2 evidence cell (`:345`) | Corrected: the cited Host tests use no Crux provider (F12) | P10 |
| Owner's "no non-Zephyr port" (`:43`) | Kept | — |

*No owner decision of ADR-0001 is reversed.* Statements in Decision 3 and the D10 specification would change, and each change is listed above as an owner decision.

**Migration steps, in order** (recommendation):

1. The independent and adversarial reviews of this note, then the owner's decisions P1 to P10.
2. A new ADR-0002 records the accepted decisions and amends ADR-0001 by reference. Documentation updates follow: `docs/architecture.md`, `docs/native-interface-policy.md`, `docs/thread-sync.md`, `docs/messaging-contract.md`, `docs/serial-contract.md`, `docs/component-readiness.md`, and `README.md` lines 5, 33 and 37. None of these is protected.
3. Freeze Thread, Sync, Queue and Serial against new features, by documentation.
4. Rescope #174 (P7).
5. The priority Kconfig and an example priority map in `templates/product`, by test-driven development.
6. D10 phase 1 with Build B amended, and the `native_sim` sanitizer probe.
7. `crux-controller` moves its workload threads and queue to its Zephyr zone (195 lines in `app/src/workload_thread.c`). That is that repository's change, coordinated with its session.
8. Tier table and `Status:` lines (ADR-0001 follow-up 5), with Internal for Thread, Sync and Interrupt. `CRUX_DEPRECATED` on Queue, Serial, GPIO, I2C and SPI at the first tag.
9. Removal in the next tagged release.
10. Optional: a check that rejects internal targets in a product's domain.

**Protected paths that would be touched.** Each needs the owner's approval of that change when it is made.

| Path | Step | Why |
| --- | --- | --- |
| `scripts/test-messaging.sh`, `scripts/test-serial.sh`, `scripts/test-gpio.sh`, `scripts/test-i2c.sh`, `scripts/test-spi.sh` | 9 | Removal of the retired public APIs' runners, or their reduction to internal tests |
| `.github/workflows/host.yml:63,71`, `.github/workflows/zephyr.yml:167,173,188,193,198` | 9 | The workflow steps that call those runners |
| `cmake/CruxConsumerBoundary.cmake`, `scripts/check-native-interfaces.py` | 10, optional | Enforcing Internal for products |
| `CONTRIBUTING.md` | 2, optional | Naming the developer rule next to the public-header rules |
| `AGENTS.md` | None needed | `AGENTS.md:22` already says to use Zephyr-native kernel facilities when suitable |
| `CMakePresets.json`, `scripts/check-host.sh` | None under O4 | O2 would change them |
| `docs/evidence/` | None | Retained evidence stays unchanged |

**Evidence still missing, and the cheapest probe for each:**

| Gap | Cheapest probe | Needs |
| --- | --- | --- |
| Does `native_sim` give ASan and UBSan results for a services image, and at what turnaround compared with `host-sanitize`? | One CI run of an existing `native_sim` services image with `CONFIG_ASAN=y` and `CONFIG_UBSAN=y`, timed | CI only |
| H2: is Host execution of Crux-using domain code worth its cost against `native_sim`? | D10 phase 1, with Build B amended | Approved for phase 1 |
| The latency that #174's scheduler lock adds | A cycle-counter measurement of the longest Sync section on the EVKB, or a fix that takes no scheduler lock | A board run needs the owner's approval |
| Jitter of a native real-time thread while Crux services run | A board run of a high-priority native thread with the services image composed | Board approval, and a composed image that does not exist yet |
| Edit-to-result turnaround of Host and `native_sim` tests | Timestamps in CI logs, or D10's turnaround measure | CI only |
| Whether Crux services call product callbacks while holding an internal lock (rule 2) | Code review of the services' callback paths | Reading only |

## 9. Owner decisions

| # | Decision | Recommendation | Reason | Alternative |
| --- | --- | --- | --- | --- |
| P1 | Adopt O4 as the platform direction, with the three-question developer rule | Approve | It scores highest, follows what both consumers do on their critical paths, and gives one rule | O2, if one OS API matters more than Host tests of the services. Or O4a, for the smallest change |
| P2 | Make Thread and Sync Crux-internal: tier Internal, no real-time extension, product use deprecated at the first tag | Approve | Real-time code bypasses them already; the services still need them; a second thread API in products is the confusion named | Keep them public as Experimental with a non-real-time rule (O4a) |
| P3 | Retire Queue: freeze now, deprecate at the first tag, remove in the next; `crux-controller` moves to `k_msgq` | Approve | No Crux component uses it; it is a thin copy of `k_msgq` | Make it internal instead, or keep it public until D10 reports |
| P4 | Make Serial internal to the command line's console, and deprecate product use at the first tag | Approve | It is the last device API after D4; its only Crux consumer is the console | Keep it public as Experimental until the console has a Zephyr composition |
| P5 | Keep the Linux providers as test providers; retire the GPIO, I2C, SPI and Queue Host providers with their APIs; add none without a Crux test need; revisit after the probes | Approve | They hold the services' Host and ThreadSanitizer evidence | Remove them now (O2) |
| P6 | Embedded Linux: no design now; a named product and your decision trigger it; the boundary is the service; reword `README.md:5` | Approve | It matches your decision of no non-Zephyr port and the precedents | Delete the sentence (Zephyr only, fully), or keep it, which leaves it reading against ADR-0001 |
| P7 | #174: do not merge as it is. Keep its test; pick a fix that meets rule 3, or measure the scheduler lock on the board; drop the 256 change | Approve | The scheduler lock can delay native real-time threads by an unmeasured amount | Merge as it is and document the one-CPU limit and the unmeasured delay |
| P8 | Adopt the coexistence rules, with a new Kconfig setting per Crux thread class, defaulting to today's values | Approve | Products need to place Crux's workers in their priority map | Document the rules without the Kconfig settings, leaving Crux's priorities fixed at 4, 5 and 6 |
| P9 | Amend D10 phase 1 Build B: a plain C domain with Crux Time, Logging, and Configuration or File; threads in a Zephyr shell. Add the `native_sim` sanitizer probe | Approve | The experiment should measure the direction chosen, not the one left | Run Build B as approved, which measures O1 |
| P10 | Record the result as a new ADR-0002 that amends ADR-0001 by reference, including the corrected H2 evidence | Approve | It keeps the accepted record intact and the change traceable | Add a dated amendment section to ADR-0001 itself |

## 10. Limits of this review

- **Not built or run.** No code was compiled and no test was run for this review. The CI times are from two recorded runs (F14).
- **External facts.** The research passes read pages through a fetch tool that summarises them, so their wording is paraphrase. Several Pigweed, Matter, OpenThread and nRF Connect SDK details are marked *unverified* in section 3.
- **Scores are judgement.** The weights and scores are this review's reading of the evidence. Their sensitivity is stated in section 4.7.
- **The runtime port is private.** Its records were read only for generic facts (F10). A reviewer without access to it can check only what #174 states.
- **`crux-controller`** was read at `405cce47ed` through the GitHub API, not from a local clone.
- **Line counts** are raw `wc -l` of `.c` and `.h` files and include comments and blank lines.
- **Not examined.** The services' callback paths and lock use (rule 2), the effort to migrate the runtime port's timer wait, and whether a fix for #174 exists that needs neither a scheduler lock nor an unbounded inversion.
- **Planning documents** that are maintained outside this repository were not used. Where the README and ADR-0001 differ on Embedded Linux, only those two are cited.
