# Platform direction review: Zephyr, Linux and the application API

Status: **review and proposal**, revised after an independent review (REVISE) and an adversarial review. It is for the owner's decision. It decides nothing. It changes no code, check, contract, decision record or protected path. Written on 13 November 2026 against main `828021cf`.

**Sources.** Repository facts are cited as `path:line` at `828021cf`. `crux-controller` facts are from its public `main` at `405cce47ed`. One further consumer is external and private: it ports a third-party real-time runtime onto Crux, and is called the **runtime port** below. Only its generic needs are stated. External facts come from two research passes, one on other platforms and one on drivers and host execution. They are given as those passes report them, with their URLs, and their *unverified* marks are kept.

**Labels.** **Fact**: checked in a cited file or source. **Inference**: this review's reading. **Recommendation**: a proposal for the owner.

## 1. Summary

**Three decisions, not one.** The question "Zephyr only, or Crux APIs?" splits into three decisions:

1. **What product code uses.**
2. **What Crux's own services use inside.**
3. **Whether Crux keeps Host providers so that domain code runs without Zephyr.** That is hypothesis H2 of ADR-0001.

The evidence settles the first decision now. It does not settle the other two.

**Recommendation: a staged path.**

- **Now: one rule for product code, in three questions** ([section 5.3](#53-the-developer-rule)).
  - OS and hardware facilities go to Zephyr, called directly from the product's Zephyr code.
  - Platform services go to Crux.
  - Product behaviour is plain C in the domain, behind ports that the product owns, and is tested on the host.
  - The rule is the same under O2 ("Zephyr only") and O4 ("Zephyr for execution, Crux for services"). They differ only inside Crux.
- **Now: freeze Thread, Sync, Queue and Serial for new product code.**
  - No new features and no real-time extension. Existing users keep working.
  - The product template's include check rejects these four headers in a product's domain.
  - A pattern for domain state shared with Crux service callbacks goes into the template ([section 5.4](#54-domain-state-shared-with-service-callbacks)).
  - Crux's worker priorities get one Kconfig setting per service.
- **D10 phase 1 runs Build B as approved, plus a Build B'** with the shape of this rule: a domain without threads behind a Zephyr shell.
- **Later: decide the deprecations, and O2 against O4 for the services' internals.** That decision waits for D10, a `native_sim` sanitizer probe and a spike of a slimmed internal Sync. Until then, the public contracts that carry policy or serve both consumers stay as they are: Time, Logging, File, Configuration and System. So do the adopted Netif and Sockets profile. None of these decides scheduling. The management services and composition tooling also stay.
- **Draft PR #174 should not be merged as it is.** The priority defect should be fixed by slimming internal Sync, without a scheduler lock ([section 7](#7-coexistence-in-one-image-and-what-to-do-with-174)).

**This is not ADR-0001 option c.** On 12 November the owner chose to measure instead of narrowing Crux at once. Option c was declined because it gives up H2 and H3 untested (`docs/decisions/0001-application-boundary-and-api-stability.md:355-357`). The staged path freezes, measures, then decides. It amends D10 by adding Build B', not by replacing Build B. The Decision 3 row "Time, Thread, Sync, Queue, Logging, File, Configuration with Host providers" stays until the later decision. The freeze narrows what new product code should use under D1. That is listed as an owner decision.

**Scores.** Corrected, O2 and O4 both score 385 of 500, and defensible one-point changes do not separate them. Both lead O4a by 20 to 45 points, and O1 and O3 by 100 or more ([section 4.9](#49-scores)).

**Would Crux be unnecessary with Zephyr only?** Not because of this choice. ADR-0001 justifies the thin kernel contracts by Host execution alone, and calls them thin over the Zephyr kernel (`:181`). Crux's case rests on its services (H1), which have Host and `native_sim` evidence only and no board run (`:344`), and on its product composition tooling (H4), which has one recorded check outside CI (`:347`). nRF Connect SDK is the precedent for a Zephyr-only platform that adds services.

**Owner decisions needed:** P1 to P10 ([section 9](#9-owner-decisions)).

## 2. Evaluation criteria

Weights add up to 100. Each option gets 1 (poor) to 5 (good) on each criterion.

| # | Criterion | Weight | Why this weight |
| --- | --- | --- | --- |
| C1 | **Developer clarity**: one rule an application developer can apply without asking | 20 | The owner's first concern: mixing Crux and Zephyr without a rule (`docs/decisions/0001-application-boundary-and-api-stability.md:39`) |
| C2 | **Evidence from real consumers**: what `crux-controller` and the runtime port use and bypass | 15 | The engineering approach asks for real consumers (`AGENTS.md:23`, `docs/coding-guidelines.md:29`) |
| C3 | **Real-time fitness**: priorities, run-time changes, lock counts, priority inheritance, wait resolution | 15 | The runtime port uses native threads and locks for real time. Its timer wait still uses Crux Sync and Thread sleep, at millisecond steps (F10) |
| C4 | **Tests: what runs where, how fast, and what it proves** | 10 | Services have Host tests under ASan and UBSan, and three suites under ThreadSanitizer, in CI only (F13). Host tests prove logic, not timing |
| C5 | **Maintenance cost**: two providers for each contract, CI time | 10 | 13 components have Linux or Host ports of about the size of their Zephyr ports (F3) |
| C6 | **Future embedded Linux** | 5 | Low: the owner decided on no non-Zephyr port for the time being (`:43`). Not zero: `README.md:5` names it a longer-term goal |
| C7 | **Zephyr upgrade churn**: who absorbs a baseline move | 10 | ADR-0001 rule 6 puts it in adapters (`:269`) |
| C8 | **Risk of a least-common-denominator API** | 10 | Extending a thin layer to real-time needs grows a second kernel API, the owner's churn concern (`:40-41`) |
| C9 | **Cost of change from today** | 5 | Nothing is tagged, and the affected interfaces are Experimental, which may change in any release (`:228`) |

## 3. Lessons from other platforms

Condensed from the two research passes. In "Outcome", a statement is fact where a source states it, and inference where marked. "Lesson for Crux" is inference throughout.

| Project | Abstracts | Leaves native | Backends | Outcome | Lesson for Crux |
| --- | --- | --- | --- | --- | --- |
| Pigweed ([pw_thread backends](https://pigweed.dev/pw_thread/backends.html), [pw_sync_zephyr](https://pigweed.googlesource.com/pigweed/pigweed/+/HEAD/pw_sync_zephyr/docs.rst), [pw_thread_zephyr](https://raw.githubusercontent.com/google/pigweed/main/pw_thread_zephyr/docs.rst)) | Thread, sync, chrono, log, assert and interrupt facades; small I2C, SPI and digital I/O interfaces | No OS-level filesystem or socket facade (*inferred, unverified*) | Five thread backends, including a first-class host STL backend and Zephyr | An older snapshot called Zephyr support preliminary. Current `pw_thread_zephyr` docs say it supports thread creation. *Inference*: it pays where many consumers share modules across OSes | Facades pay with several real backends. Counting a host backend, Crux has two. Whether the second earns its cost is what C4 and the probes must show |
| Arm Mbed OS ([Arm](https://www.arm.com/products/iot/mbed-os), [Memfault 1.40.0](https://components.espressif.com/components/memfault/memfault-firmware-sdk/versions/1.40.0/changelog)) | One C++ API over HAL, RTOS, connectivity and security | Little | Many Cortex-M targets, one RTOS | End of life in July 2026, per Arm. Memfault deleted its Mbed port. Arduino moved to Zephyr and kept its own thin API above it | Do not build on a layer you do not control. A thin own API above the OS protected Arduino's large body of existing code: the best external case for O1, and weak here with two consumers |
| CMSIS-RTOS2 on Zephyr ([Zephyr docs](https://docs.zephyrproject.org/latest/services/portability/cmsis_rtos_v2.html)) | Threads, mutexes, semaphores, flags, queues, timers | Everything else | RTX, FreeRTOS, Zephyr | Zephyr documents unsupported calls, and fixes inheritance and recursion as always on. It sits under "portability" | A common RTOS API over Zephyr leaves semantic gaps. It is a porting aid, not a base for new code |
| nRF Connect SDK ([test framework](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/test_and_optimize/test_framework/index.html)) | No OS layer found (*absence unverified*) | Zephyr kernel, Devicetree, logging and settings, called directly by its services | Zephyr only | A large product base. Tests use Ztest, Unity with CMock, `native_sim` and `unit_testing` | The precedent for O2, including inside services: they call `k_*` directly and test on Zephyr's own host targets |
| Matter SDK ([Zephyr platform](https://pigweed.googlesource.com/third_party/github/project-chip/connectedhomeip/+/HEAD/src/platform/Zephyr)) | Platform managers for lifecycle, configuration, connectivity, BLE and Thread, plus a System Layer | Each OS inside its port | More than ten (*list unverified*) | *Inference*: it works because vendors fund their own ports of one large service codebase. A reviewer recalls a small internal system mutex with Zephyr and POSIX backends (*unverified*) | If Linux becomes real, port at the service boundary. Internal locking can be a thin shim |
| OpenThread ([PAL guide](https://openthread.io/guides/porting/implement-platform-abstraction-layer-apis)) | Alarm, radio, entropy, settings, optional UART and logging, reset | Threads, mutexes, files: a single-threaded core with a process loop | Zephyr, FreeRTOS, a POSIX simulator (*counts unverified*) | *Inference*: a PAL shaped by the library's needs stays small | Shape a port by the service. One executor in place of many workers is an option worth a spike |
| Memfault and Golioth ([Memfault](https://docs.memfault.com/docs/mcu/faq), [Golioth](https://docs.golioth.io/reference/firmware/golioth-firmware-sdk/)) | Small port hooks for a service library | The OS integration | Several RTOSes; Golioth's Linux port is experimental | Linux arrived late and stayed experimental. A reviewer recalls Memfault needing only lock and unlock hooks (*unverified*) | Linux comes as a lower-priority service port. A lock shim can be two functions |
| ESP-IDF Linux target, NuttX sim ([ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/host-apps.html), [NuttX](https://nuttx.apache.org/docs/12.8.0/implementation/simulation.html)) | The RTOS API in a host process | Real timing | One RTOS each | Kept as preview or development tools; ESP-IDF warns the simulation does not represent the device | A host target is a test aid, never target evidence |
| embedded-hal 1.0 ([announcement](https://blog.rust-embedded.org/embedded-hal-v1/), [changelog](https://github.com/rust-embedded/embedded-hal/blob/master/embedded-hal/CHANGELOG.md)) | Narrow driver traits | Configuration, timers, serial, ADC, CAN: moved or dropped | Many HALs; Linux through linux-embedded-hal | Succeeded after cutting scope; about 1,800 dependent crates reported (approximate) | A device interface pays only with many cross-platform drivers. In C on Zephyr, Zephyr's driver API is that standard |
| Zephyr device model, emulators, `native_sim` ([emulators](https://docs.zephyrproject.org/latest/hardware/emulator/bus_emulators.html), [native_sim](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html), [POSIX arch](https://docs.zephyrproject.org/latest/boards/native/doc/arch_soc.html)) | Devices through Devicetree and subsystem APIs; emulators run unchanged drivers | Crossing OS boundaries is not a goal | Every Zephyr board | `native_sim` runs the real kernel deterministically, one thread at a time, with documented ASan and UBSan options. Zephyr aims it at application code, not low-level driver or timing work | Device code belongs in Zephyr adapters, tested on `native_sim` with emulators |
| AUTOSAR ([Adaptive Platform](https://www.autosar.org/standards/adaptive-platform)) | Classic: basic software on an OSEK-style OS, with an RTE. Adaptive: POSIX-based | Each platform's internals | Two platforms | Split into an MCU platform and a POSIX platform. In Classic, components own no tasks: the RTE maps their runnables to tasks (*general practice, not fetched here*) | No thin layer spans MCU and Linux products. Precedent for a domain that owns no threads |
| Eclipse MRAA and UPM ([MRAA](https://github.com/eclipse/mraa)) | Pin and bus access across Linux boards | Kernel drivers | Many boards | Appears dormant; last release mentioned January 2020 (*unverified*) | A board-zoo device abstraction ages badly |

**What the lessons say for Crux** (inference):

1. **Count backends and consumers.** One production backend does not pay for a portable kernel layer facing products. A test backend may pay, if its value is shown.
2. **Abstract at the service boundary.** Ports are shaped by the service (OpenThread, Matter, Memfault), and internal locking can stay a thin shim.
3. **Use Zephyr's host story** for code that touches Zephyr, and keep pure logic OS-free. nRF Connect SDK does this all the way into its services.
4. **Do not grow a common RTOS API to real time** (CMSIS-RTOS2).
5. **Let the runtime own the tasks.** AUTOSAR runnables and cyclic task runtimes keep domain code free of threads.
6. **A thin own API above the OS protects existing code** (Arduino). That argues for not breaking `crux-controller` before the evidence is in.

## 4. Options

### 4.1 Facts the options rest on

| # | Fact | Source |
| --- | --- | --- |
| F1 | Of the twelve management components, ten include `<crux/sync.h>` (all but `cli` and `ssh`) and five include `<crux/thread.h>`. Five worker threads are created: one each in `security_event`, `network_settings` and `reboot`, two in `web`. The services hold 14 mutex and 7 semaphore creation sites | Include scan; `components/security_event/src/security_event_drain.c:164`, `components/network_settings/src/network_settings.c:639`, `components/reboot/src/reboot.c:432`, `components/web/management/web_management_save.c:186`, `components/web/session/web_session_attempt.c:371` |
| F2 | No Crux component includes `<crux/queue.h>` | Include scan of `components/` |
| F3 | Linux or Host ports exist for 13 components. The ten `ports/linux` trees hold 1,612 raw lines; their Zephyr ports hold 1,913. Thread, Sync and Interrupt's Linux ports hold 373 | `ls components/*/ports`; `wc -l` |
| F4 | Crux Thread maps three classes to Zephyr priorities 4, 5 and 6, fixed in code. One stack size serves every worker. No run-time change is promised | `components/thread/ports/zephyr/thread_native.c:5,99`; `zephyr/Kconfig:56-59`; `docs/thread-sync.md:109-113` |
| F5 | Pools: threads, mutexes and semaphores 1..64 each, by Kconfig. Test compositions already set 8 threads and 24 or 16 mutexes. Queue is fixed at four queues of sixteen 64-byte records | `zephyr/Kconfig:51-54,72-80`; `tests/native/management_services/prj.conf:43,47`; `tests/native/management_composition/prj.conf:37`; `components/queue/src/queue.c:7-9` |
| F6 | Thread and Sync promise no real-time scheduling, and Host runs every class at normal priority. Sync mutexes are non-recursive | `docs/thread-sync.md:10-13,109-113` |
| F7 | Monotonic time reads `k_uptime_ticks` with no lock. Realtime Time, File, System, Queue and Serial take a global `k_mutex` in their Zephyr ports. SSH creates native threads at the lowest application priority | `components/time/src/time_clock.c:6-24`; `components/time/ports/zephyr/time_port_zephyr.c:9`; `components/file/ports/zephyr/file_native.c:88`; `components/system/ports/zephyr/system_zephyr.c:10`; `components/queue/ports/zephyr/queue_port.c:6`; `components/serial/ports/zephyr/serial_zephyr.c:21`; `components/ssh/ports/zephyr/crux_ssh_wolf_provider.c:61-62,1004-1006,1243-1245` |
| F8 | Crux Logging on Zephyr calls Zephyr's `LOG_*` macros after its own checks | `components/logging/ports/zephyr/logging_zephyr.c:13-28` |
| F9 | `crux-controller` builds its execution on Crux: two `crux_thread_create` calls at NORMAL priority and a Crux Queue, with no Zephyr include (`app/src/workload_thread.c:10-14,137,150-159`). Its S1 step exists to run the workload on Crux Thread, Queue and Time (`app/conf/s1.conf:1`). Its Host tests link only `workload` and `net_client`, and no Crux provider | `crux-controller` `tests/host/CMakeLists.txt:20-32`, `README.md:46-52`, at `405cce47ed` |
| F10 | The runtime port uses Crux File, Logging, Time, System and the Sockets profile. Its cyclic timers run on native threads because they need distinct priorities set while they run. Its main thread and writer run native because their priorities fall outside 4 to 6. Its timer wait uses a Crux semaphore and `crux_thread_sleep_until`: a Crux sleep cannot be woken and semaphore waits have millisecond steps, so the wait takes two steps. It uses native recursive locks by default, because it needs more than 64 at once and because of the priority defect below | The runtime port's records, generic facts only |
| F11 | Draft PR #174 (open since 3 November 2026) fixes a defect: a thread kept an inherited priority after `crux_mutex_unlock`. It also raises the mutex maximum to 256. The defect comes from the global `sync_metadata` `k_mutex` held around the native unlock. The fix runs every Sync metadata section with the scheduler locked: one CPU, no meta-IRQ threads, one call can delay every other thread, and `create` scans the pool inside that section. Nothing was measured on hardware | PR #174 description; `components/sync/ports/zephyr/sync_native.c:6`; `components/sync/src/sync.c:139-153` |
| F12 | ADR-0001 cites `crux-controller`'s Host tests as evidence for H2. Those tests use no Crux Host provider (F9). No product consumer uses a Crux Host provider | `:345`; F9 |
| F13 | ThreadSanitizer covers three suites (`config_service_concurrency`, `mgmt_status_snapshot`, the SSH command line) and cannot run under local emulation. No `native_sim` image enables `CONFIG_ASAN` or `CONFIG_UBSAN` | `scripts/test-mgmt-services.sh:56-67`; `tests/ssh/cli/test-thread-sanitizer.sh:2-4`; `docs/cli-over-ssh.md:119`; `grep` |
| F14 | Host CI job, 12 November: 27.5 minutes, including 574 s of build provenance safeguards, 151 s of clang-tidy and 268 s of host sanitizers (run 43321893524). Zephyr shards: 12.6 to 28.6 minutes (run 43321893624) | `gh run view` |
| F15 | Provisional tiers: Thread, Sync, Queue, Serial, Logging, System Experimental; Time, File, Netif, Sockets Unstable | `docs/component-readiness.md:43-56` |
| F16 | Linux Host is a development and test environment. Embedded Linux is a longer-term goal and a future variant | `README.md:5,33,37,39`; `docs/architecture.md:11` |
| F17 | Services call product code on their own threads: command and web handlers, status reads, reboot policy and busy checks | `components/cli/include/crux/cli.h:61`; `components/web/include/crux/web.h:66`; `components/mgmt_status/include/crux/management_status.h:69`; `components/reboot/include/crux/reboot.h:98,106` |
| F18 | A POSIX subset was evaluated. `PTHREAD_PRIO_INHERIT` returned `ENOTSUP` at the pin. Timed waits convert realtime deadlines. Message queues allocate from the heap. Native and POSIX images used 52,328/17,412 and 73,928/82,516 bytes of flash/RAM, not all of it API overhead. POSIX stays an option for another evidenced consumer | `docs/execution-profile.md:64-67,131-134,143`; `docs/thread-sync.md:218-224` |

### 4.2 What consumers asked for

- **A larger mutex pool** (#174's 256).
  - Answer: Crux mutexes are non-recursive by contract (F6), so a recursive lock on top of them is an extra layer. More than 64 recursive locks belong in native `k_mutex` under any option.
  - The runtime port recorded a plan to return to Crux locks once the defect is fixed in Crux. This review departs from that plan: it keeps those locks native. `AGENTS.md:47` allows reopening a recorded decision on new evidence. The evidence is the non-recursive contract and the scheduler lock that the proposed fix adds (F11). That consumer needs to be told and to agree.
- **A priority model, after a board measurement.**
  - Answer: no Crux priority API. Real-time threads are native. Crux's own workers get per-service priority settings (P3).
- **A wakeable wait finer than milliseconds** (F10).
  - Answer: `k_sem`, `k_timer` or `k_sleep` in Zephyr code, at tick resolution.
- **A monotonic timestamp finer than the kernel tick.**
  - Open: a Time addition for a named consumer, or the cycle counter in Zephyr code.
- `crux-controller` asked for none of these. It uses Thread and Queue as they are (F9).

### 4.3 O1. ADR-0001 as it is

The domain uses Crux Time, Thread, Sync, Queue, Logging, File and Configuration, with Host providers. Adapters use Zephyr.

- *For*: no change. `crux-controller` is built this way (F9).
- *Against*:
  - Two thread and lock APIs in one image, chosen by zone rather than by job.
  - Real-time threads cannot be made in the domain (F4), so the runtime port went native (F10).
  - Each real-time gap closed grows a second kernel API (C8).
  - H2 has no product evidence (F12).

### 4.4 O2. Zephyr only

Products follow the three-question rule. The services call `k_thread`, `k_mutex` and `k_sem` directly (ADR-0001 option c, `:357`). Time, Logging, File, Configuration and System survive as public contracts with Zephyr providers only. Their Linux providers go, so domain code that calls them is tested on the host with fakes or on `native_sim`.

- *For*:
  - The rule for products is the same as O4's.
  - One mechanism inside Crux.
  - The #174 defect disappears with the metadata lock.
  - Each service sizes its own static objects.
  - SSH already works this way (F7).
- *Against*:
  - About 96 call sites change: 69 mutex, 22 semaphore and 5 thread-creation calls.
  - The larger cost is the services' Host tests, which drive concurrency through the Linux providers. They move to `native_sim`, which has no ThreadSanitizer equivalent. Its ASan and UBSan options are documented but untried here (F13).
  - Removing the providers touches `CMakePresets.json`, `scripts/check-host.sh` and `.github/workflows/host.yml`, all protected.
  - Local `native_sim` runs are emulated on an arm64 Mac.

### 4.5 O3. A full portable layer, extended for real time and Linux production

Numeric priorities, run-time changes, recursive mutexes, larger pools, a qualified Linux provider.

- *Against*:
  - It reverses the owner's decision of no non-Zephyr port (`:43`) and the rule against a universal OSAL (`AGENTS.md:23`).
  - Linux real-time scheduling usually needs privileges (*general Linux behaviour, not checked here*).
  - It is the CMSIS-RTOS2 pattern.
  - Of what consumers asked (section 4.2), only the pool and the priority model would land in Crux. The runtime port wants a board measurement before any priority model.

### 4.6 O4a. ADR-0001 with a real-time carve-out

As O1, plus: priority-sensitive work always uses Zephyr.

- *For*: little change. It fits both consumers most closely: `crux-controller`'s non-real-time workload stays on Crux Thread, and the runtime port's real-time threads stay native. Only the runtime port's timer wait would move to Zephyr.
- *Against*: a developer still picks between two thread APIs for non-real-time work.

### 4.7 O4. Zephyr for execution, Crux for services

Products follow the three-question rule. Thread and Sync stay as Crux's internal mechanism, with Linux test providers, so the services keep their Host tests. Queue and Serial leave the public surface.

- *For*:
  - The same product rule as O2, keeping the services' Host suites.
  - O4 to O2 stays possible later, because no product calls Thread or Sync.
- *Against*:
  - Two providers per contract stay (F3).
  - "Internal" is a label until enforced (section 5.7).
  - Products still set Crux's pools, stack size and priorities (F4, F5).

### 4.8 O5. A documented POSIX subset

`AGENTS.md:23` asks for this to be evaluated, and it was (F18). It lost on priority inheritance, realtime-deadline waits, heap-allocated queues and footprint, at the recorded pin. It is not scored. It stays an option for a consumer whose evidence differs (`docs/execution-profile.md:143`).

### 4.9 Scores

| Criterion (weight) | O1 | O2 | O3 | O4a | O4 |
| --- | --- | --- | --- | --- | --- |
| C1 clarity (20) | 2 | 5 | 4 | 3 | 5 |
| C2 consumer evidence (15) | 3 | 3 | 1 | 4 | 3 |
| C3 real time (15) | 2 | 5 | 3 | 4 | 5 |
| C4 tests (10) | 4 | 3 | 4 | 4 | 4 |
| C5 maintenance (10) | 2 | 4 | 1 | 2 | 3 |
| C6 embedded Linux (5) | 3 | 2 | 5 | 3 | 2 |
| C7 Zephyr churn (10) | 3 | 3 | 2 | 3 | 3 |
| C8 common-denominator risk (10) | 2 | 5 | 1 | 4 | 4 |
| C9 cost of change (5) | 5 | 1 | 1 | 4 | 3 |
| **Weighted total (of 500)** | **265** | **385** | **250** | **345** | **385** |

What changed from the first version, which gave O4 395 and O2 370:

- **C1.** It scored a difference that exists only inside Crux.
- **C2.** It counted the Host tests a second time, after C4. Corrected:
  - O2 and O4 match the runtime port's native threads and locks.
  - Neither matches `crux-controller`, which builds on Crux Thread and Queue and must migrate under both.
  - No product uses a Crux Host provider (F12).
  - O4a fits both consumers most closely, so it scores highest here.
- **C6.** It contradicted section 6. Under the service-boundary rule, internal providers give a Linux product nothing.
- **C7.** It claimed that kernel changes reach only two internal mechanisms. Six Zephyr ports call the kernel directly (F7), and `k_mutex`, `k_sem` and `k_thread` are among Zephyr's most stable APIs.

**Range.** Defensible one-point changes move each of O2 and O4 between about 365 and 390: O2's C4 at 2, C1 at 4 for both, or O4's C6 at 3 for the public contracts' Linux providers. **The matrix does not separate O2 from O4.** Both stay ahead of O4a by at least 20 points, and of O1 and O3 by at least 100. The choice between O2 and O4 is an engineering choice inside Crux, left to the evidence (section 5.1).

## 5. Recommendation and final API design

### 5.1 Stages

| Stage | What | Needs |
| --- | --- | --- |
| **Now** | Publish the developer rule. Freeze Thread, Sync, Queue and Serial for new product code: no new features, no real-time extension, documented as "not for new product code". Add the template's domain include check rejecting `crux/thread.h`, `crux/sync.h`, `crux/queue.h` and `crux/serial.h`, and the shared-state pattern (5.4). Add per-service priority Kconfig. Do not merge #174 as it is | Documentation; `templates/product` and its tests; one Kconfig change by test-driven development. `CONTRIBUTING.md:45` (protected) names `crux::queue` and `crux::thread` as example usage targets and must change |
| **D10 phase 1** | Build A and Build B as approved. Add Build B': Build B's domain without Crux Thread, behind a Zephyr shell. B against B' measures O1 against O4; A against B' measures O4 against plain Zephyr. The GPLv2 limit of Configuration's only provider applies to B' too (`:383-387`). Add the `native_sim` sanitizer probe and the slim-Sync spike | The approved phase 1, plus one more build; CI only |
| **Later** | Decide, by a rule fixed before the runs (P9): (a) the Internal tier and product deprecation of Thread, Sync, Queue and Serial; (b) O2 or O4 for the services' internals; (c) whether the Linux providers stay | D10, the probe, the spike; protected paths for removals (section 8) |

### 5.2 Component roles

Today's tiers are the provisional ones in `docs/component-readiness.md:42-69`. "Later" is a candidate, decided under P9.

| Component | Role | Tier today | Now | Later (candidate) |
| --- | --- | --- | --- | --- |
| `base` | Public contract | Unassigned | None | Unstable at tier assignment |
| `time` | Public contract | Unstable | None. Real-time code may read monotonic time (no lock); realtime takes a lock (F7) | A finer timestamp only for a named consumer |
| `thread` | Mechanism of Crux services | Experimental | Frozen for new product code | Internal tier, or Zephyr directly in services (O2) |
| `sync` | Mechanism of Crux services | Experimental | Frozen; defect fixed by slimming (P6) | As `thread` |
| `queue` | None inside Crux (F2) | Experimental | Frozen for new product code | Deprecated, then removed; migration note in 5.6 |
| `logging` | Public contract | Experimental | None | None |
| `file` | Public contract; composition interface `crux::file_zephyr` | Unstable | None | None |
| `configuration` | Public contract | Experimental | None; provider decision (Issue #16) open | None |
| `system` | Public contract | Experimental | None | None |
| `interrupt` | Crux-internal | Internal | None | None |
| `netif` | Public contract, read-only | Unstable | No extension; more data from Zephyr `net_if` | None |
| `sockets` | Qualified standard profile | Unstable | None | None |
| `serial` | Transport of the command line's console | Experimental | Frozen for new product code | Internal tier |
| `gpio`, `i2c`, `spi` | Retire (D4) | Experimental, frozen | As D4 | As D4 |
| `security_event`, `network_settings`, `reboot`, `web` | Crux services that own workers (F1) | Experimental | A priority Kconfig per service (P3) | Internals per P9 |
| `ssh` (private) | Crux service with native threads (F7) | Experimental | Its priority define becomes a Kconfig setting (P3) | None |
| `access`, `credential`, `config_service`, `mgmt_status`, `tls_certificate`, `file_service`, `cli` | Crux services without workers of their own | Experimental | None | Internals per P9 |

### 5.3 The developer rule

1. **Is it an OS or hardware facility?** Threads, priorities, locks, queues, timers, interrupts, devices, buses, pins, network stack set-up. → **Zephyr**, in the product's Zephyr code: adapters, composition and the execution shell.
2. **Is it a platform service that Crux provides?** Management over web, command line, SSH and SFTP; accounts; configuration with validation; file storage with Crux's rules; time authority and calendar; health and watchdog; log records. → **Crux**, from any zone, with its provider bound in composition.
3. **Is it the product's own behaviour?** Control laws, state machines, protocol logic, limits and alarms. → **Plain C in the domain**, behind product ports. It may call Crux public contracts and service APIs, never Zephyr. It owns no thread: the execution shell calls it.

| Case | Q1: Zephyr | Q2: Crux | Q3: domain |
| --- | --- | --- | --- |
| Cyclic task timers of a ported real-time runtime | `k_thread` per timer at mapped priorities; `k_sem` or `k_timer` for a wakeable wait; recursive `k_mutex` | Monotonic timestamps, Logging, File for retained data, System watchdog | The runtime's scheduling logic, above its port layer |
| `crux-controller` workload | Producer, consumer and queue as `k_thread` and `k_msgq`, once its migration is decided (today Crux Thread and Queue, F9) | Logging; File for its configuration record | `workload/` stays plain C with Host tests |
| Temperature sensor (ADS112C04) | A Zephyr driver or `i2c_dt_spec` code behind `temperature_read(&millikelvin)`; emulator on `native_sim` | Status service for a remote reading; Configuration or File for limits | Filtering, limits and hysteresis, tested with a port fake |
| SSH service | The priority map places Crux's SSH threads | `CONFIG_CRUX_SSH`, `_SSH_CLI`, `_SSH_SFTP`; Access; the credential store | Which commands exist and who may run them |

*Why these contracts stay public and Thread does not* (inference): Time, Logging, File, Configuration and System carry policy, or serve both consumers, and none of them decides scheduling. They are not free of interaction. Realtime Time and File take global locks (F7), which coexistence rule 5 handles. Threads and locks decide scheduling through priorities, inheritance and pools. Two kinds of them in one image is a design problem.

### 5.4 Domain state shared with service callbacks

Crux services call product code on their own threads (F17). The domain owns no lock, so the pattern goes into `templates/product` before any product migrates (P4):

1. **One owner.** Each piece of domain state has one owning thread in the shell. Domain functions run only there.
2. **Reads by snapshot.** The owner publishes a snapshot after each step. It uses C11 atomics (`<stdatomic.h>`, ISO C) for single values, or a double buffer whose swap an adapter protects with its own `k_mutex`. Status reads and reboot busy checks read the snapshot.
3. **Writes by message.** Command and web handlers post requests to the owner's `k_msgq`, through an adapter. The owner applies them between steps. A handler that needs a result waits for a reply with a timeout.
4. **Locks live in adapters.** Domain Host tests stay single-threaded and deterministic.

The alternative is an exclusive-area port per product, with a `k_mutex` adapter and a Host fake. That is a small OSAL in every product, and it is not recommended.

### 5.5 Zones of a product

The three layers of [ADR-0001](../architecture.md#three-layers-for-a-product-adr-0001) stay.

| Zone | May call | Must not call | Tested on |
| --- | --- | --- | --- |
| Domain | ISO C; product ports; Crux public contracts and service APIs | Zephyr; Thread, Sync, Queue, Serial (frozen); composition interfaces; Devicetree identifiers | Linux Host with port fakes, and Crux Host providers where it calls Crux |
| Adapters, execution shell, composition | Any Zephyr API; Crux public contracts, service APIs, composition interfaces | Crux private headers and `ports/` include paths | `native_sim` with emulators, then the board |

### 5.6 What it asks of each consumer

| Consumer | Change | Effort (inference) |
| --- | --- | --- |
| `crux-controller` | None now; frozen headers keep working. If deprecation is decided: threads and queue move into Zephyr code | More than one file: `app/src/workload_thread.c` (195 lines, with its stop and join ordering). The S1 definition `app/conf/s1.conf:1` and its `native_sim` scenario. The two-header allow-list `scripts/check_boundary.py:55-58`, its test and its zone document. Its daily build against Crux main (ADR D8) breaks if Queue goes before the migration lands |
| Runtime port | Timer wait to `k_sem` or `k_timer`; recursive locks stay native, a departure from its recorded plan (section 4.2) | Small, in its own repository, after its agreement |
| Crux services | Per-service priority Kconfig; later, the internal mechanism per P9 | One Kconfig change now |
| A new product | The three questions | None extra |

**Migration note for Queue to `k_msgq`.** Crux Queue is not a thin copy of `k_msgq`.
- It is a ring buffer in common code (`components/queue/src/queue.c:15`), under one metadata `k_mutex` with a `k_condvar` per queue on Zephyr (`components/queue/ports/zephyr/queue_port.c:6-7`).
- Handles carry slot and generation, so stale handles are rejected.
- Destroy returns BUSY while calls are active or waiting.
- Waits are finite: zero polls, with no forever, and one deadline covers lock admission and the wait.
- It is thread context only (`components/queue/include/crux/queue.h:3-7`). The default is a cooperative stop, and a purge is not an abort (`docs/messaging-contract.md:60-62`).
- The pool is fixed at 4 × 16 × 64 bytes.

A product keeps these properties after the move only if its adapter provides them.

### 5.7 What "Internal" would mean, if chosen later

ADR-0001 defines Internal for private headers, ports and test controls (`:224`). Thread and Sync differ:
- They are public `crux::` targets with headers in `include/crux/`.
- Compositions set their pools in `prj.conf` (F5), and those symbols are interface under rule 5.

Internal for them would therefore extend `:224`. The pool and stack symbols stay product-facing as resource budgets.

A plain `deprecated` attribute would warn inside Crux's own services, which include these headers. Three ways out:
- (a) A deprecation macro that expands only outside Crux targets, by a compile definition set on Crux component targets.
- (b) An include root that only Crux targets get. This changes CMake and possibly `cmake/CruxConsumerBoundary.cmake`, which is protected.
- (c) A label plus the template include check.

Recommended then: (a) with (c).

## 6. Execution targets

| Target | What runs | What it proves | What it cannot prove | Cost |
| --- | --- | --- | --- | --- |
| RT1060-EVKB | The product image with Crux services and native real-time threads | E4 in narrow slices: timing, interrupts, peripherals, persistence, priority interplay, Sync latency | Other boards, endurance, production readiness (`docs/component-readiness.md:16`) | Highest: board time and owner approval per task; E3 builds are cheap in CI |
| `native_sim` (Dev Container, CI) | Real Zephyr kernel, Crux's Zephyr providers, adapters with emulators, service images | E2: integration, kernel API use, deterministic ordering; ASan and UBSan documented, untried here (F13) | Timing, preemption, data races (one thread at a time), MMIO (`docs/component-readiness.md:14`) | Moderate: west workspace; shards 12.6 to 28.6 minutes; emulated on an arm64 Mac |
| Linux Host unit and sanitizer builds | Crux portable code, services on Linux test providers, product domains with port fakes | E1: logic, error paths, ASan, UBSan, ThreadSanitizer for three suites, fuzzing, mutants | Zephyr behaviour, priorities (F6), timing | Low per run. Of the 27.5-minute job, 574 s is provenance and 151 s clang-tidy, not provider cost (F14). About 1,600 lines of providers to keep in step |
| Future embedded Linux product | Not designed | — | — | Only on the trigger below |

**Linux providers** (P7): keep them as test providers. Retire the GPIO, I2C and SPI Host fixtures with D4. Add none without a Crux test need. Decide on the rest with the O2-or-O4 decision. They stay "not production Embedded Linux" (`docs/thread-sync.md:204-205`).

**Embedded Linux** (P8): design nothing now.
- The trigger is a named product that needs Linux, such as for process isolation, an MMU or a rich interface, and the owner's decision.
- The boundary is then each service the product needs, ported to Linux services as in Matter and OpenThread. Some services would give way to Linux-native ones, such as SSH to OpenSSH. This holds under O2 and O4 alike.
- `README.md:5` should say that Embedded Linux is not planned for the foreseeable future, and would be designed at the service boundary if a product needs it. "For the foreseeable future" follows the owner's words in `:43`.

## 7. Coexistence in one image, and what to do with #174

**Rules for an image where Zephyr code and Crux run together** (P3, P4):

1. **One priority map per product, declared once.** It lists:
   - native real-time threads;
   - Zephyr's own threads (network, logging, work queues, the HTTP server that the web adapter sits on, `docs/architecture.md:287`);
   - Crux's pool workers, at 4, 5 and 6 today (F4);
   - SSH's native threads at the lowest application priority (F7).

   **Per-service priority settings** (P3) let the map place Crux's threads. Per-service settings survive O2 and O4. Per-class settings would become public interface tied to the internal shim.
2. **The product sets Crux's resource budgets**: thread and mutex pools, and one stack size for every worker (F4, F5). Product callbacks run on those stacks (F17).
3. **One lock, one kind.** Product state follows section 5.4. A product never holds a lock across a Crux call that can call back into code that takes it.
4. **No unbounded scheduler or interrupt lock in Crux.** Any such section states a bound measured on the target. Crux workers sit below real-time threads unless the product decides otherwise.
5. **Real-time code calls only Crux APIs that take no lock**, such as the monotonic read. Realtime Time, File, System, Queue and Serial take global locks (F7).

**#174: do not merge it as it is** (P6).

- Its scheduler lock breaks rule 4, unmeasured.
- `create` scans the pool inside that lock, so the raise to 256 lengthens the worst case. That is reason enough to drop 256 under any option. 256 slots would not give recursion either (F6).
- Fix the defect by slimming internal Sync:
  - Composition-lifetime objects that embed a `k_mutex`, with no global metadata lock on Zephyr.
  - `docs/thread-sync.md:213-217` already names composition-lifetime resources as the smaller alternative.
  - Removing the metadata lock removes the defect's cause (F11), the scheduler lock and the pool limit, and keeps the Linux test provider.
  - The services' 14 mutex and 7 semaphore creation sites (F1) bound the change. Whether all run at initialisation is part of the spike.
- #174's third alternative, the native unlock outside the metadata section, is a smaller step in the same direction.
- Keep #174's regression test, which reads a holder's priority before, during and after.
- A slimmed Sync also eases a later move to O2.

## 8. Changes from ADR-0001, migration and missing evidence

| ADR-0001 item | Now | Later (P9) |
| --- | --- | --- |
| D1 three layers | Kept. New product code does not use the four frozen headers (P2) | The domain owns no threads, if the deprecations are decided |
| D2, D3, D6 to D8 | Kept. D6 rule 5 applies to the new priority Kconfig | — |
| D4 device model | Kept | Queue and Serial may join the retirement |
| D5 tiers | Kept | Internal for Thread and Sync (section 5.7) |
| D9 | Kept | — |
| D10 phase 1 | Build B kept; Build B', the probe and the spike added (P5) | Results feed P9 |
| Decision 3 row "... with Host providers" (`:181`) | Unchanged | Split, if the deprecations are decided |
| Decision 5, option c (`:355-357`) | Not adopted | Reconsidered with D10's result, as the ADR says |
| Decision 5, H2 evidence (`:345`) | Corrected by F12 (P10) | — |

**Migration steps, in order:**

1. The owner's decisions P1 to P10.
2. ADR-0002 records them and amends ADR-0001 by reference (P10).
3. Documentation updates. These are not protected:
   - `docs/architecture.md`, `docs/native-interface-policy.md` and `docs/thread-sync.md`;
   - `docs/messaging-contract.md` and `docs/serial-contract.md` (the freeze);
   - `docs/component-readiness.md`;
   - `README.md:5`, the Embedded Linux wording;
   - `README.md:9,14-15`, which list Thread, Sync, Queue and Serial for applications.
   - `CONTRIBUTING.md:45` must change too, and it is protected.
4. The template's include check and shared-state example, and per-service priority Kconfig, by test-driven development.
5. Replace #174 with the slim-Sync fix.
6. D10 phase 1 with B', the `native_sim` sanitizer probe, and the slim-Sync spike.
7. P9's decision. If the deprecations are decided:
   - `crux-controller` migrates first, coordinated with its session;
   - tier table and `Status:` lines (ADR-0001 follow-up 5);
   - `CRUX_DEPRECATED` at the first tag, removal in the next;
   - `docs/composition-interfaces.md:7,35`, which name `crux::queue_linux`, is updated.

**Protected paths.** Each needs the owner's approval of that change when it is made.

| Path | When | Why |
| --- | --- | --- |
| `CONTRIBUTING.md:45` | Now (P2) | Names `crux::queue` and `crux::thread` as example application targets |
| `CONTRIBUTING.md:40` | Later, if Internal (P9) | "Publish only interfaces required by consumers" against headers that stay public but internal |
| `scripts/test-messaging.sh`, `scripts/measure-messaging.sh:36,58`, `scripts/analyze-host.sh:83-84`, `scripts/test-m5-integrated-smoke.sh:32`, `scripts/measure-tidy-coverage.py:56`, `cmake/CruxConsumerBoundary.cmake:90` | Later, if Queue is removed | Queue runners, measurements, clang-tidy file list, a required Kconfig option, a tidy-coverage rule, the `crux_queue_linux` guard |
| `.github/workflows/host.yml:63,105`, `.github/workflows/zephyr.yml:167-168,259,340-345` | Later, with the above | Queue steps and artefacts, and the integrated smoke step that requires `CONFIG_CRUX_QUEUE` |
| `scripts/test-serial.sh`, `.github/workflows/host.yml:71`, `.github/workflows/zephyr.yml:173` | Later, if Serial's public tests shrink | Serial runners |
| `scripts/test-gpio.sh`, `scripts/test-i2c.sh`, `scripts/test-spi.sh`, `.github/workflows/zephyr.yml:188,193,198` | D4 removal, already decided | Retired APIs' runners |
| `cmake/CruxConsumerBoundary.cmake`, `scripts/check-native-interfaces.py` | Optional, later | Enforcing Internal for products |
| `CMakePresets.json`, `scripts/check-host.sh` | Only under O2 | Removing the Linux providers |
| `AGENTS.md`, `docs/evidence/` | None | `AGENTS.md:22` already prefers Zephyr-native kernel facilities; evidence stays unchanged |

**Evidence still missing, and the cheapest probe for each:**

| Gap | Cheapest probe | Needs |
| --- | --- | --- |
| `native_sim` ASan and UBSan for a services image, and turnaround against `host-sanitize` | One CI run of an existing services image with `CONFIG_ASAN=y` and `CONFIG_UBSAN=y`, timed | CI only |
| H2, and O1 against O4 for domain code | D10 phase 1 with B and B' | Phase 1, approved |
| Whether services can use a slimmed Sync | A spike: composition-lifetime objects embedding `k_mutex`, no metadata lock, existing service tests on both providers | CI only |
| Latency of any scheduler lock in Sync | A cycle-counter measurement on the EVKB, avoided entirely by the slim fix | A board run needs approval |
| Jitter of a native real-time thread with Crux services running | A board run with the services composed | Board approval and an image that does not exist yet |
| Whether services hold an internal lock while calling product code (rule 3) | Code review of the F17 callback paths | Reading only |

## 9. Owner decisions

| # | Decision | Recommendation | Reason | Alternative |
| --- | --- | --- | --- | --- |
| P1 | Adopt the three-question rule for product code now, in the documentation and the product template | Approve | It fits the real-time evidence, is the same under O2 and O4, and is reversible | O4a: keep Crux Thread open for non-real-time product work |
| P2 | Freeze Thread, Sync, Queue and Serial for new product code: no new features, no real-time extension; template include check; change `CONTRIBUTING.md:45` | Approve | Stops growth of a second kernel API without breaking `crux-controller` before D10 | Leave them open until D10 (O1 as is) |
| P3 | Per-service priority Kconfig for Crux's workers and SSH threads, defaulting to today's priorities | Approve | Products must place Crux threads; per-service settings survive O2 and O4 | Three per-class settings: simpler, but public interface tied to the shim |
| P4 | Put the shared-state pattern (section 5.4) in `templates/product` before any product migrates | Approve | Service callbacks run on Crux threads (F17), and the domain owns no lock | An exclusive-area lock port per product |
| P5 | D10 phase 1: Build B as approved, plus Build B'; add the `native_sim` sanitizer probe and the slim-Sync spike | Approve | Measures O1 against O4 directly, without turning the test into a confirmation | B only, with no direct comparison; or replace B with B', which tests only the chosen direction |
| P6 | #174: do not merge as is. Fix the defect by slimming internal Sync, or by #174's third alternative; no scheduler lock; drop 256; keep its test; tell the runtime port that its locks stay native | Approve | Removes the defect's cause and an unmeasured system-wide delay | Merge as is and document one CPU and the unmeasured delay |
| P7 | Keep the Linux providers as test providers; add none without a Crux test need; decide the rest with P9 | Approve | They carry the services' Host suites; removing them now preempts the probe | Remove them now (O2) |
| P8 | Reword `README.md:5`: Embedded Linux not planned for the foreseeable future; if needed, designed at the service boundary | Approve | Uses the owner's own wording in ADR-0001 | Keep it, which reads against ADR-0001, or delete it |
| P9 | Defer to after D10, the probe and the spike: the Internal tier and deprecations, O2 or O4 for internals, the Linux providers. Fix the rule before the runs. Proposed: deprecate the four for products if B' meets the ADR's limits against B (within 1.3 times the effort, domain tests on Host). Choose O2 if `native_sim` sanitizers work within twice the `host-sanitize` time and the services' concurrency tests can move; otherwise keep the slim shim (O4) | Approve | Decides with evidence, as on 12 November | Decide the deprecations now (the first version's O4), which takes up option c untested |
| P10 | ADR-0002 records P1 to P9 and the corrected H2 evidence, amending ADR-0001 by reference | Approve | Keeps the accepted record intact and the change traceable | A dated amendment section in ADR-0001 |

## 10. Limits of this review

- No code was built or run. The CI times are from two recorded runs (F14).
- External facts are paraphrased by a fetch tool, and several are marked *unverified*. The Matter and Memfault lock shims come from a reviewer's memory. AUTOSAR runnables are general practice, not fetched.
- Weights and scores are judgement, and section 4.9 states their range.
- The runtime port is private. Only its generic needs are used, and a public reader cannot check them.
- `crux-controller` was read at `405cce47ed` through the GitHub API.
- Line counts are raw `wc -l` and include comments.
- Not examined:
  - the services' callback paths and lock use (rule 3);
  - whether all Sync creations happen at initialisation;
  - a single Crux service executor, such as a `k_work_q`, in place of five workers;
  - whether `native_sim` builds natively on arm64 Linux.
- Planning documents maintained outside this repository were not used. On Embedded Linux, only the README and ADR-0001 are cited.
