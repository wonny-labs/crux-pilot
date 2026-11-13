# Choosing an API for product code

**Status: developer rule, approved by the owner on 13 November 2026 (platform direction review, decisions P1 and P2).** This guide tells an application developer where each kind of code goes. It adds no check and no public interface. The reasoning, the alternatives and the evidence are in the [platform direction review](research/platform-direction-review.md). The three layers it builds on are in [Three layers for a product](architecture.md#three-layers-for-a-product-adr-0001).

## The rule: three questions

Ask them in order for each piece of code.

1. **Is it an OS or hardware facility?** Threads, priorities, locks, queues, timers, interrupts, devices, buses, pins, network stack set-up. Use **Zephyr** directly, in the product's Zephyr code: adapters, composition and the execution shell.
2. **Is it a platform service that Crux provides?** Management over web, command line, SSH and SFTP; accounts; configuration with validation; file storage with Crux's rules; time authority and calendar; health and watchdog; log records. Use **Crux**, from any zone, with its provider bound in composition.
3. **Is it the product's own behaviour?** Control laws, state machines, protocol logic, limits and alarms. Write **plain C in the domain**, behind ports that the product owns. The domain may call Crux public contracts and service APIs (Time, Logging, File, Configuration, System and the management services), never Zephyr. It owns no thread: the product's execution shell calls it. It is tested on the Linux Host with fakes of its ports.

## Examples

| Case | Question 1: Zephyr | Question 2: Crux | Question 3: domain |
| --- | --- | --- | --- |
| Cyclic task timers of a ported real-time runtime | One `k_thread` per timer at mapped priorities; `k_sem` or `k_timer` for a wakeable wait; recursive `k_mutex` | Monotonic timestamps, Logging, File for retained data, System watchdog | The runtime's scheduling logic, above its port layer |
| A controller workload (the `crux-controller` shape) | Producer, consumer and queue as `k_thread` and `k_msgq` | Logging; File for its configuration record | The workload stays plain C with Host tests |
| A temperature sensor | A Zephyr driver or `i2c_dt_spec` code behind a product port such as `temperature_read(&millikelvin)`; an emulator on `native_sim` | The status service for a remote reading; Configuration or File for limits | Filtering, limits and hysteresis, tested with a fake of the port |
| An SSH service | The product's priority map places Crux's SSH threads | The `CONFIG_CRUX_SSH`, `CONFIG_CRUX_SSH_CLI` and `CONFIG_CRUX_SSH_SFTP` options; Access; the credential store | Which commands exist and who may run them |

## Frozen for new product code: Thread, Sync, Queue and Serial

Do not use `<crux/thread.h>`, `<crux/sync.h>`, `<crux/queue.h>` or `<crux/serial.h>` in new product code. Question 1 covers what they offered: use Zephyr threads, locks, message queues and UART drivers in the product's Zephyr code.

- **Frozen means:** no new features and no real-time extension. It adds no promise and removes none. The contracts, tests and evidence stay as they are, and existing users keep working.
- **Why:** threads and locks decide scheduling, through priorities, inheritance and pools. Two kinds of them in one image is a design problem. Time, Logging, File, Configuration and System carry policy or serve both Host and Zephyr consumers, and none of them decides scheduling, so they stay public contracts.
- **Not decided yet:** whether these four are deprecated or become Crux-internal. That decision waits for the evidence of the platform direction review (its decision P9). Until then nothing is removed.
- **Planned, not implemented:** the product template will reject these four headers in a product's domain. Today the freeze is a documented rule only.

## Coexistence in one image

When Zephyr code and Crux run together:

1. **One priority map per product, declared once.** It lists the product's own real-time threads, Zephyr's own threads (network, logging, work queues, the HTTP server under the web adapter), Crux's pool workers, and SSH's native threads. Crux workers sit below real-time threads unless the product decides otherwise.
2. **The product sets Crux's resource budgets:** thread and mutex pools, and one stack size for every worker. Product callbacks run on those stacks.
3. **One lock kind per piece of state.** Do not guard the same state with both a Crux lock and a Zephyr lock. Do not hold a lock across a Crux call that can call back into code that takes it.
4. **No unbounded scheduler or interrupt lock in Crux.** Any such section must state a bound measured on the target.
5. **Real-time code calls only Crux APIs that take no lock,** such as the monotonic clock read. Realtime Time, File, System, Queue and Serial take global locks.

Rules 1 and 4 describe the direction the platform follows. Per-service priority settings and the scheduler-lock review are separate, later work and are not part of this guide.

## State shared with Crux service callbacks

Crux services call product code on their own threads, for example a command handler or a status read. The domain owns no lock, so use this pattern:

1. **One owner.** Each piece of domain state has one owning thread in the execution shell. Domain functions run only there.
2. **Reads by snapshot.** The owner publishes a snapshot after each step, with C11 atomics (`<stdatomic.h>`) for single values, or a double buffer whose swap an adapter protects with its own `k_mutex`. A status read or a busy check runs on the caller's thread and reads the snapshot only.
3. **Writes by message.** Command and web handlers post a request to the owner's `k_msgq` through an adapter. The owner applies it between steps. A handler that needs a result waits for a reply with a timeout.
4. **Locks live in adapters.** The domain's Host tests stay single-threaded and deterministic.

An example of this pattern in the product template is planned, not present.

## See also

- [Architecture overview](architecture.md), with the three layers and the capability inventory
- [Native-free consumer policy](native-interface-policy.md)
- [Component readiness](component-readiness.md)
- [Thread and Sync contract](thread-sync.md), [messaging contract](messaging-contract.md) and [serial contract](serial-contract.md) (frozen for new product code)
