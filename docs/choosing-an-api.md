# Choosing an API for product code

**Status: developer rule, approved by the owner on 13 November 2026 (decisions P1 and P2 of [ADR-0002](decisions/0002-platform-direction-zephyr-execution-crux-services.md); the shared-state pattern and coexistence rules follow P3 and P4).** This guide tells an application developer where each kind of code goes. It adds no public interface. The product template carries the domain include check and the shared-state example. The per-service priority settings of P3 are in [Priority map of Crux threads](thread-sync.md#priority-map-of-crux-threads). The reasoning, the alternatives and the evidence are in the [platform direction review](research/platform-direction-review.md). The three layers it builds on are in [Three layers for a product](architecture.md#three-layers-for-a-product-adr-0001).

## The rule: three questions

Ask them in order for each piece of code.

1. **Is it an OS or hardware facility?** Threads, priorities, locks, queues, timers, interrupts, devices, buses, pins, network stack set-up. Use **Zephyr** directly, in the product's Zephyr code: adapters, composition and the execution shell.
2. **Is it a platform service that Crux provides?** Management over web, command line, SSH and SFTP; accounts; configuration with validation; file storage with Crux's rules; time authority and calendar; health and watchdog; log records. Use **Crux**, from any zone, with its provider bound in composition.
3. **Is it the product's own behaviour?** Control laws, state machines, protocol logic, limits and alarms. Write **plain C in the domain**, behind ports that the product owns. The domain may call Crux public contracts and service APIs (for example Time, Logging, File, Configuration, System, the Sockets profile and the management services), never Zephyr. It owns no thread: the product's execution shell calls it. It is tested on the Linux Host with fakes of its ports.

## Examples

| Case | Question 1: Zephyr | Question 2: Crux | Question 3: domain |
| --- | --- | --- | --- |
| Cyclic task timers of a ported real-time runtime | One `k_thread` per timer at mapped priorities; `k_sem` or `k_timer` for a wakeable wait; recursive `k_mutex` | Monotonic timestamps, Logging, File for retained data, System watchdog | The runtime's scheduling logic, above its port layer |
| A controller workload (the `crux-controller` shape) | Producer, consumer and queue as `k_thread` and `k_msgq` | Logging; File for its configuration record | The workload stays plain C with Host tests. The existing `crux-controller` keeps its Crux Thread and Queue code until the P9 decision |
| A temperature sensor | A Zephyr driver or `i2c_dt_spec` code behind a product port such as `temperature_read(&millikelvin)`; an emulator on `native_sim` | The status service for a remote reading; Configuration or File for limits | Filtering, limits and hysteresis, tested with a fake of the port |
| An SSH service | The product's priority map places Crux's SSH listener and workers with `CONFIG_CRUX_SSH_LISTENER_PRIORITY` and `CONFIG_CRUX_SSH_WORKER_PRIORITY` (P3) | The `CONFIG_CRUX_SSH`, `CONFIG_CRUX_SSH_CLI` and `CONFIG_CRUX_SSH_SFTP` options; Access; the credential store | Which commands exist and who may run them |

## Frozen for new product code: Thread, Sync, Queue and Serial

Do not use `<crux/thread.h>`, `<crux/sync.h>`, `<crux/queue.h>` or `<crux/serial.h>` in new product code. Question 1 covers what they offered: use Zephyr threads, locks, message queues and UART drivers in the product's Zephyr code.

- **Frozen means:** no new features and no real-time extension. It adds no promise and removes none. The contracts, tests and evidence stay as they are, and existing users keep working.
- **Why:** threads and locks decide scheduling, through priorities, inheritance and pools. Two kinds of them in one image is a design problem. Time, Logging, File, Configuration and System carry policy or serve both of today's product consumers, and none of them decides scheduling, so they stay public contracts.
- **Not decided yet:** whether these four are deprecated or become Crux-internal. That decision (P9 of ADR-0002) waits for D10 phase 1, a `native_sim` sanitizer probe and a slim-Sync spike. Until then nothing is removed.
- **Checked by a script:** `templates/product/check_domain_includes.py` rejects these four headers, every `<zephyr/...>` header and the Crux composition interfaces in a product's domain, and the template's tests run it. With `--product ROOT` it also rejects the four headers in every source file of a product repository, adapters included, and `--exclude PATH` lists legacy files that have not moved yet. See [the template README](../templates/product/README.md#check-the-whole-product-repository).
- **Checked by the compiler, if the product asks:** define `CRUX_WARN_FROZEN_HEADERS` for the product's own targets and each of the four headers emits a `#warning` that names it and points here. The default is off, so no existing build changes, including a `-Werror` build. The targets that Crux's own CMake creates define `CRUX_INTERNAL_BUILD` as a private directory definition, so the warning stays silent in them and the definition reaches no consumer. Crux sources that a composition compiles into the product's own target, such as the service sources that a Zephyr image adds to `app`, are not covered and do warn, so define the macro only for targets that compile no Crux source, or accept the warnings there. A product that adds `-Werror` turns the warning into an error, and GCC with `-Wpedantic` in C17 mode adds its own note that `#warning` is an extension.
- **Not checked:** Crux's own trees and D10's Build B are exempt, and a product that does not run either check is not stopped.

## Coexistence in one image

When Zephyr code and Crux run together:

1. **One priority map per product, declared once.** It lists the product's own real-time threads, Zephyr's own threads (network, logging, work queues, the HTTP server under the web adapter), Crux's service workers, and SSH's native threads. Crux workers sit below real-time threads unless the product decides otherwise. Each Crux thread has its own priority setting, listed with its default in [Priority map of Crux threads](thread-sync.md#priority-map-of-crux-threads).
2. **The product sets Crux's resource budgets:** thread and mutex pools, and one stack size for every worker. Product callbacks that run on a Crux worker use those stacks.
3. **One lock kind per piece of state.** Do not guard the same state with both a Crux lock and a Zephyr lock. Do not hold a lock across a Crux call that can call back into code that takes it.
4. **No unbounded scheduler or interrupt lock in Crux.** Any such section must state a bound measured on the target.
5. **Real-time code calls only Crux APIs that take no lock,** such as the monotonic clock read. Realtime Time, File, System, Queue and Serial take global locks.

Rules 1 and 4 describe the direction the platform follows. The per-service priority settings of rule 1 exist (P3). The scheduler-lock review is separate, later work and is not part of this guide.

## State shared with Crux service callbacks

Crux services call product code on other threads: a command handler on the thread of its console or SSH session, a web handler on the single Zephyr HTTP server thread, the reboot busy check on the reboot worker (a Crux thread), and a status read on the caller's thread, which need not be a Crux thread and may run concurrently. The domain owns no lock, so use this pattern:

1. **One owner.** Each piece of domain state has one owning thread in the execution shell. Domain functions run only there.
2. **Reads by snapshot.** The owner publishes a snapshot after each step, with C11 atomics (`<stdatomic.h>`) for single values, or a double buffer whose swap an adapter protects with its own `k_mutex`. Status reads, on the caller's thread, and the reboot busy check, on the reboot worker, read the snapshot only.
3. **Writes by message.** Command and web handlers post a request to the owner's `k_msgq` through an adapter. The owner applies it between steps. A handler that needs a result waits for a reply with a timeout.
4. **Locks live in adapters.** The domain's Host tests stay single-threaded and deterministic.

The product template shows the pattern in `templates/product/app/src/main.c`: one owner thread, a status read by a C11 atomic, and a command by a `k_msgq` message. See its [README](../templates/product/README.md#shared-state-example).

## See also

- [Architecture overview](architecture.md), with the three layers and the capability inventory
- [Native-free consumer policy](native-interface-policy.md)
- [Component readiness](component-readiness.md)
- [Thread and Sync contract](thread-sync.md), [messaging contract](messaging-contract.md) and [serial contract](serial-contract.md) (frozen for new product code)
