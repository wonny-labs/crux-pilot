# Reboot pilot, Stage B: the reboot service on Zephyr kernel objects

Part of the P9 evidence of [ADR-0002](../decisions/0002-platform-direction-zephyr-execution-crux-services.md) (keep a slim Sync/Thread shim, O4, or move services to Zephyr-direct, O2). [Stage A](reboot-native-sim-stage-a.md) ran the 54 existing `reboot` cases on `native_sim/native/64` over the unchanged Crux Thread and Sync providers. Stage B swaps the service to `k_mutex`, `k_sem`, `k_thread` and `k_msleep` and runs the same suites again. **This is a Draft PR that is not merged before the owner decides P9.**

**Labels.** *Measured (local)* = measured by the author in one native arm64 `ubuntu:24.04` container (Docker VM with 8 CPUs, `ZEPHYR_TOOLCHAIN_VARIANT=host`, gcc 13.3, clang-18 for the Host build, Zephyr **4.4.0**; the repository pins 4.4.2, so `CruxZephyrBaseline` warns that its evidence does not apply to this build). *Inference* = my reading, labelled each time. *CI* = the GitHub Actions run of this PR, which is **not in this file** and must be added from that run. Nothing here is a physical-board result and nothing ran on RT1060.

**Load.** Another session ran a Docker container on the same VM during my first M2 batches. Each batch below states the load at its start and end (`/proc/loadavg` in the container, and `docker ps` on the host). A batch that says "idle" had no other container at its start and a load average below 1.5 at both ends; I checked `docker ps` before and after each such batch.

## What changed

| Change | Where |
| --- | --- |
| `crux_mutex_t state_lock` is `static K_MUTEX_DEFINE`; `work_signal` is `static K_SEM_DEFINE(…, 0, 1)`; the worker is `K_THREAD_STACK_DEFINE` + `struct k_thread` + `k_thread_create` with priority `CONFIG_CRUX_REBOOT_WORKER_PRIORITY` (the #395 name, default 6) and a stack of `CONFIG_CRUX_REBOOT_WORKER_STACK_SIZE` (new, default 4096) | `components/reboot/ports/zephyr/reboot_zephyr.c` (`git mv` of `src/reboot.c`) |
| The sleep port is `k_msleep()`, and a sleep that `k_wakeup()` ended early returns false. The private seam `crux_reboot_port_sleep` stays, so the fake port still gives virtual time | `components/reboot/ports/zephyr/reboot_sleep_zephyr.c` (replaces `src/reboot_sleep.c`) |
| The priority symbol moves out of `if CRUX_THREAD`, because the service needs no Crux Thread; the stack symbol is new | `zephyr/Kconfig` |
| The Host core and the Crux sleep provider targets go; `CRUX_REBOOT_SOURCES` and `CRUX_REBOOT_SLEEP_SOURCE` name the Zephyr files | `components/reboot/CMakeLists.txt` |
| `--wrap=crux_mutex_lock` no longer reaches the lock: `fault_bridge.c` wraps `z_impl_k_mutex_lock`, tells the service's lock **by address**, and asks the suite's own `__wrap_crux_mutex_lock` what to do | `tests/reboot/fault_bridge.c` |
| The Host builds the same core over a stub of the kernel calls, so `Reboot.*` and `Web.reboot_*` still run on the Host | `tests/reboot/host_kernel/` |
| The placement check knows a Zephyr-direct worker | `tests/service_priority/check_placement.py` and its test |
| The image named `sleep_provider` runs three cases of the new port and worker | `tests/reboot/zephyr/main_sleep_provider.c` |

`components/reboot/include/crux/reboot.h` is unchanged (`git diff origin/main -- components/reboot/include` is empty). `scripts/check-native-interfaces.py` is unchanged and passes with the core in `ports/zephyr` (185 portable files): D-A needed no protected edit. `scripts/test-reboot-native.sh` is unchanged and runs Stage B as it ran Stage A, because the image names and the suite names are the same.

## Assumptions the owner has not approved

- **D-A:** the Zephyr-direct code lives under `components/reboot/ports/zephyr/` and the service is Zephyr only.
- **D-C, not applied as written.** A fake `crux_reboot_*` for `tests/web/reboot` would have to re-implement the state machine to keep 65 `CHECK`s unchanged: the route suites call `crux_reboot_work_once()`, read the order of steps, the policy calls, the reset calls and the events, and fail the service's own lock for `503 busy`. I did not write it. The real core builds on the Host over a 181-line kernel stub (`tests/reboot/host_kernel`) and the route suites run on it with their sources unchanged. The stub is a test-only pthread stand-in, not a Crux interface. Whether to keep it, or to write the fake, or to move the route suites to `native_sim` is the owner's choice (D-C stays open).
- The worker's own stack is a dedicated one (`CONFIG_CRUX_REBOOT_WORKER_STACK_SIZE`, default 4096, the default of `CRUX_THREAD_STACK_BYTES`), not a slot of the Crux Thread pool.

## M1 effort (measured, agent wall clock; no human hours)

From `git reflog`, UTC, 13 November: branch created 22:06; merge of main (#396) 22:11; container and Zephyr tools ready, Stage A re-run as a baseline 22:14; source, Kconfig and CMake 22:16 to 22:20; placement check and its test 22:19 to 22:21; Host stub, bridge, native project 22:21 to 22:23; first green native run (54 cases) about 22:23; code and tests committed 22:24. **Source, tests and a green run: about 0.3 h.** Measurement batches, mutants, the Host whole-preset run, the record and the PR: about 1.2 h more. The report's estimate for Stage B was 22 to 36 h (a person's hours); the agent's time is not comparable to it (see the extrapolation).

| Phase | Agent time |
| --- | --- |
| Reading the contract, the pilot report, the sources and tests | 0.1 h before the branch was made |
| Environment (container, tools, Stage A re-run) | 0.05 h |
| Source swap, Kconfig, CMake | 0.1 h |
| Test side (bridge, Host stub, native project, placement check) | 0.1 h |
| Docs | 0.1 h |
| Measurement (M2, M5, M7, M8, Host whole preset) | about 0.8 h, most of it waiting for builds |
| Record and PR | 0.3 h |

## M2 turnaround (measured, local)

`scripts/test-reboot-native.sh` as a whole (four image builds and 54 case runs, ASan + UBSan), 5 runs each, Stage A (`cb0fda6f`) and Stage B alternating in the same container.

| Build | Cold, median (min to max) | Warm, median (min to max) |
| --- | --- | --- |
| native_sim ASan + UBSan, **Stage A** | 99 s (87 to 101) | 62 s (54 to 72) |
| native_sim ASan + UBSan, **Stage B** | 86 s (80 to 100) | 50 s (46 to 63) |
| Host `host-sanitize` replica (clang-18), the reboot targets and 13 or 14 `ctest` entries, before the swap (`f98fe2e0`) | 4.0 s (3.7 to 4.6) | 2.1 s (2.0 to 2.3) |
| the same, after the swap | 4.5 s (4.4 to 5.1) | 2.6 s (2.5 to 2.8) |
| Host `host-sanitize` replica, the whole preset after the swap: configure, build and `ctest -j2` of 317 tests | 137 s (one run; 15 s build, 122 s `ctest`, bound by `NetworkSettings.validation` at 112 s) | |

Load. Cold batch (Stage A and B alternating): load average 2.87 at the start and 4.49 at the end, with another session's container running (`crux-reboot-native`, `docker ps` at the start); **measured under concurrent load**. Warm batch: 4.49 at the start, 1.27 at the end; the other container was running at the start and gone by 22:50; the later warm runs of both stages are faster, which is the load falling. **The Stage A and Stage B figures were taken in the same batches, so their difference is the better number; the absolute seconds are not idle numbers.** The Host batches ran at load 0.5 to 0.9 with no other container (checked): **idle**. The whole-preset run: load 0.8 at the start and 1.3 at the end, idle.

**Stage B against Stage A:** cold 13 % faster (86 against 99 s), warm 19 % faster (50 against 62 s). The spread of each batch (13 to 20 s) is about as large as the difference, so I read this as "no slower, probably a little faster". The cause is that the Crux Thread and Sync providers are no longer linked into the `core` image, not anything in the tests.

**The ratio, two readings (D-D names the preset as `host-sanitize` and does not say which denominator):**

| Reading | Stage B cold | Stage B warm |
| --- | --- | --- |
| Like-for-like: the same suites, native over Host (86 s over 4.5 s, 50 s over 2.6 s) | **about 19** | **about 19** |
| The whole preset (137 s measured here) | 0.63 | 0.36 |
| The whole preset, as Stage A recorded it (178 s local, 268 s CI run 43321893524) | 0.48 and 0.32 | 0.28 and 0.19 |

The like-for-like ratio measures the number of Zephyr images (four configurations of Kconfig and a kernel build, about 20 s each), not the tests: a case runs in about 0.1 s.

## M3 lines by zone

`git diff --numstat -M f98fe2e0 HEAD` (Stage A plus main, then this change), then the record and README.

| Zone | Added / removed | Note |
| --- | --- | --- |
| Portable (`src`, `include`) | 0 / 23 | `src/reboot_sleep.c` is replaced; `src/reboot.c` leaves `src` (444 lines) |
| `ports/zephyr` | 474 new files | `reboot_zephyr.c` is a rename of `src/reboot.c`: **47 added and 31 removed lines** inside it (460 lines now, 444 before); `reboot_sleep_zephyr.c` 14 |
| Tests, new | 49 + 113 + 68 | `fault_bridge.c`, `kernel_stub.c`, the stub's `kernel.h` |
| Tests, changed | 27/12 + 25/15 + 88/5 + 1/0 + 8/5 + 31/6 + 11/7 | `tests/reboot/CMakeLists.txt`, `tests/reboot/zephyr/CMakeLists.txt`, `main_sleep_provider.c`, `prj.conf`, `tests/web/reboot/CMakeLists.txt`, `check_placement.py`, `test_check_placement.py`; **no assertion source of a suite changed** |
| CMake and Kconfig (non-test) | 11/20 + 28/8 | `components/reboot/CMakeLists.txt`, `zephyr/Kconfig` |
| Protected | **0 new** | `scripts/test-reboot-native.sh` and one `zephyr.yml` step are Stage A's two, unchanged |
| Docs | 17/5 + 1/1 + 8 + 192 (this file) | `docs/reboot-service.md`, `docs/thread-sync.md`, `tests/README.md` |

Lines no longer needed in Crux Sync and Thread for this service: **13 lexical Crux Sync or Thread references** leave the service (10 in `reboot.c`, 3 in `reboot_sleep.c`), against 9 `k_*` references in the new files. **Provider lines freed: 0.** The Linux Sync and Thread providers, the Zephyr ones (241 lines) and the common ones (546) are all still used by 10 other services, so none can go until the last of them moves.

**Test-only kernel shims, for the 302-line cap of D-D:**

| Shim | Lines | Used for |
| --- | --- | --- |
| `tests/reboot/zephyr/compat/pthread.h` and `compat_pthread.c` (Stage A) | 204 | the pthread calls of `Reboot.concurrency` on `native_sim` |
| `tests/reboot/zephyr/zephyr_case.c` (Stage A, one case for each process) | 103 | what `fork()` did on the Host |
| `tests/reboot/fault_bridge.c` | 49 | `crux_mutex_lock` wraps to `z_impl_k_mutex_lock` |
| `tests/reboot/host_kernel/` | 181 | the Host build of the core |

## M4 behaviour and tests identical

- `git diff origin/main -- components/reboot/include`: empty. `docs/reboot-service.md`: the behaviour tables and the error rows are unchanged; a note at the top says what became impossible, and four rows that named Sync, the sleep provider or the low class were reworded.
- No existing assertion source changed: the diff of `tests/reboot/*.c`, `tests/web/reboot/*.c` and `tests/access` against Stage A plus main is empty. `CHECK(` counts of the sources: service 108, steps 135, concurrency 37, reason 8, lock_faults 25, reboot_test.c 5, pending 84, web 65.
- `native_sim`: 54 of 54 cases pass, as at Stage A. The 54th is not the same case: the image `sleep_provider` runs 3 new ztest cases (below), not the 9 checks of `sleep_provider.c`.
- Host, after the swap: `Reboot.service`, `steps`, `concurrency`, `reason`, `lock_faults`, `pending`, `nodiscard`, `Web.reboot_routes`, `no_service`, `openapi` and `ManagementProviders.events` all pass (ASan + UBSan, clang-18); the whole preset passes, 317 of 317. `Reboot.sleep_provider` no longer exists as a ctest entry.
- **What became impossible, the one place that needs an owner decision.** `tests/reboot/sleep_provider.c` has 9 `CHECK`s in 1 case. It wraps `crux_semaphore_take()` to give a take's answers (timeout, `ERR_IO`, `ERR_INVALID_ARGUMENT`, `ERR_INVALID_STATE`, `OK`) to the old provider and expects false for all but the timeout. The new port is `k_msleep()` and has no take, so none of the nine can run. I did not delete or edit the file; it is built nowhere. The new image holds three cases in their place (it sleeps the time asked in simulated time; a sleep that `k_wakeup()` ended early says false; the worker runs at the Kconfig priority, which is 6). The pilot report said that no test asserts a create-failure path, and that is true of the service: no `crux_*_create` wrap exists in `tests/reboot` or `tests/web/reboot`. It did not foresee this suite, which asserts the provider's reading of the take's failures. **Decision for the owner:** delete the file, or keep it as a record of the Crux provider's contract, or implement the port on a never-given `k_sem` so that the nine checks survive behind a second bridge for `z_impl_k_sem_take` (about 20 lines; I did not do it because the brief names `k_msleep`).

## M5 flakiness (measured, local, idle)

`Reboot.concurrency` (its three cases) on the `native_sim` ASan + UBSan `core` image of Stage B: **100 runs in a row, 100 passed (7 s); 200 runs, 8 at a time, 200 passed (3 s).** Load 1.0 at the start and 1.6 at the end, no other container. Stage A recorded the same two bars as passing. `native_sim` runs one Zephyr thread at a time and switches only when a thread blocks or sleeps, so this bar is weaker evidence than on the Host (Stage A limits); the real worker thread is now a `k_thread` and the concurrency suite's own threads are Zephyr threads through the shim.

## M6 fault-injection parity

| Wrapped call | Stage A | Stage B |
| --- | --- | --- |
| `crux_mutex_lock`, the service's lock (`lock_faults`, 4 cases; waits of 20 ms and 1000 ms and the Nth wait) | 4 of 4 | **4 of 4**, through `z_impl_k_mutex_lock` told by the lock's address (`CRUX_REBOOT_TESTING` exposes it) |
| `crux_mutex_lock`, the other services' locks (`pending`, 9 cases, wait of 20 ms) | 9 of 9 | **9 of 9**; the bridge is linked too, so the service's own 20 ms waits are hit as at Stage A |
| `crux_semaphore_take`, the sleep provider's take (`sleep_provider`, 9 checks) | passes | **cannot run** (M4) |
| Fake-sleep failures in `steps` (the port seam) | pass | pass |

Cases that needed the pointer-discriminated wrap: all of `lock_faults` and the 20 ms waits of `pending` that reach the service's lock. **Negative control:** with the `--wrap=z_impl_k_mutex_lock` link option removed from the `lock_faults` image, all 4 cases fail, so the bridge is what holds the cases. The bridge wraps one kernel function and creates one proxy Crux mutex so that the suite's wrapper can answer for a handle; passed-through waits take the proxy and release it at once. It exists because the suites could not be changed, and a suite written for Zephyr would wrap `z_impl_k_mutex_lock` directly.

## M7 mutation probe (measured, local; `reboot_zephyr.c` or `reboot_sleep_zephyr.c` edited, run, restored)

Host = `host-sanitize` replica (clang-18) over the kernel stub, `ctest` of 9 reboot and web suites and `Reboot.concurrency` repeated until failure up to 100 times. native = `scripts/test-reboot-native.sh` and 100 runs of `Reboot.concurrency`.

| Mutant | Host | native_sim ASan + UBSan |
| --- | --- | --- |
| R1: the worker's `end_job` writes `svc.state` without the lock | `lock_faults` fails; `concurrency` 0 of 100 failed (until-fail ran all 100) | `lock_faults` case 3 fails; `concurrency` 0 of 100 failed |
| R2: no state lock anywhere | `lock_faults`, `Web.reboot_routes` and `Web.reboot_openapi` fail; **`Reboot.concurrency` fails** | `lock_faults` cases 0 to 3 fail; `concurrency` 0 of 100 failed |
| L1: a second worker may take a running job (`svc.running` test removed) | `concurrency` fails | `concurrency` case 2 fails, 100 of 100 |
| Z1 (new): the worker created at priority 5, not the Kconfig value | `ServicePriority` checks fail | 1 case fails, which can only be the `sleep_provider` image (the script prints no name for it); `check_placement.py` fails |
| Z2 (new): a woken sleep counts as a sleep (`== 0` to `>= 0`) | not seen (the Host uses the fake port) | 1 case fails, the `sleep_provider` image |

R2 on the Host is caught by `Reboot.concurrency` now, which Stage A (Host over the real Crux Sync) recorded as 0 of 100; the cause is the stub's raw pthreads, over which the unlocked service really runs concurrently. I did not investigate whether it is a data-race effect or a crash. On `native_sim` a data race is still not seen, as at Stage A: one Zephyr thread runs at a time and there is no ThreadSanitizer. So nothing the Host caught is lost for `reboot`, race coverage is missing on both, and the Host over the stub is in fact the stronger one for races.

## M8 footprint (measured: `size` on native_sim objects; RT1060 not measured)

| | Stage A | Stage B |
| --- | --- | --- |
| `core` image, `size` (text / data / bss), sanitized | 536270 / 807744 / 3042944 | 527486 / 807640 / 2928224 |
| the service's object (text / data / bss) | 7663 / 8048 / 672 | 7551 / 7656 / 17024 |
| the sleep port's object | one Crux semaphore, created at the first sleep | 156 / 8 / 0 |

The image figures are mostly not the service: the Crux Thread provider (a test-configured pool of 8 stacks of 16 KiB) is no longer linked into the `core` image, because nothing in it creates a Crux thread any more, which a product image with other Crux workers would not see. The service's own bss is 17024 bytes in this test configuration, which sets the worker stack to 16384; at the default of 4096 it is about 4.7 KiB. On a product image that still has Crux Thread users, reboot gives back one Thread pool slot (`CRUX_THREAD_STACK_BYTES`, default 4096) and one Sync mutex slot and two semaphore slots, and takes a dedicated 4096-byte stack and a `k_thread`, `k_mutex` and `k_sem`: the RAM is about the same, as the pilot report predicted (inference). **RT1060 flash and RAM need the ARM cross-build in CI; the container has no ARM Zephyr SDK.**

## Extrapolation to the other 11 services (inference, labelled)

Method: the call sites of the pilot report (section 1.2): reboot 9; the other ten Crux Sync/Thread services 6 (access), 1 (cli), 4 (config_service), 9 (credential), 3 (file_service), 3 (mgmt_status), 16 (network_settings), 21 (security_event), 7 (tls_certificate) and 20 (web), 90 in all; `ssh` is already native, 0. Weights as the report's: ×1.5 for the TSan- or wrap-heavy services (security_event, access, credential, config_service, mgmt_status: 43 sites), ×1 for the rest (47 sites): 47 + 64.5 = 111.5 sites, which is **12.4 reboots**.

What Stage B shows about cost, which the straight proportion does not:

1. **The service diff is small and mechanical.** +47 and −31 lines in the core, and the same few lines for each of the four near copies of the worker recipe (`network_settings`, `web_management_save`, `web_session_attempt`, and the drain of `security_event` in a different shape).
2. **The cost is in the test closure, and part of it is paid once.** Once: the fault bridge (49 lines; each service's `--wrap=crux_mutex_lock` suites need it too), the placement check and its test (31/6 and 11/7 lines), the Kconfig pattern, and a Host stub that grows only with the kernel calls used. Per service: the CMake of its suites and, for each, a decision on every assertion that cannot run.
3. **Assertions that cannot run, per service.** This pilot found one suite (9 checks). The services that wrap `crux_mutex_create` on purpose (`config_service`, `credential`, `mgmt_status`) assert a create failure that static objects cannot have: those are the pilot report's B3 case, an owner decision for each, and they are not in the pilot.
4. **What the pilot did not exercise:** ThreadSanitizer suites (B4: `config_service_concurrency`, `mgmt_status_snapshot`, the SSH CLI), test hooks that hand out Crux handles (B5), context rules for ISRs (B6), `k_condvar` or recursive-lock needs, and a mixed-kind image beyond `pending`'s reboot next to Crux-Sync network settings (which passed).

Range. **(a) From the measured agent time:** the first green run took 0.3 h for 12.4 reboots-worth, so 3.7 h; with the whole 1.5 h of this pilot as the unit, 19 h. **That is 4 to 19 agent hours, about 0.1 to 0.5 weeks if a week is 40 hours of the same work**, and it excludes owner decisions, independent review rounds, CI runs and the per-service blockers of point 3 and 4. **(b) From the report's pre-registered person estimate** (22 to 36 h for Stage B): 12.4 × 22 to 36 = 273 to 446 h, **6.8 to 11 person-weeks.** The pre-registered numbers were already 10 to 100 times above what Stage A (0.8 h against 8 to 14 h) and Stage B (0.3 to 1.5 h against 22 to 36 h) cost an agent, so (b) is an upper bound that a person doing this work by hand might still approach. Subtracting the once-only parts for the later services lowers both. I do not have a measured human-to-agent ratio, and the cap's unit (person-weeks) cannot be compared with agent hours without one. **The range I would defend: 1 to 4 person-weeks of person-led work for the 11 services plus their owner decisions, with the TSan and create-wrap services as the uncertain part.** That sentence is a judgement and not a measurement.

## Exit criteria of the report section 3.5 (D-D, approved 2026-11-13 21:02:50 UTC)

**Choose O2 only if all of these hold:**

| Criterion | Verdict | Evidence |
| --- | --- | --- |
| M4 identical: no public change, no deleted assertion | **FAIL, narrowly** | Public header diff empty, no assertion source changed or deleted. But the 9 checks of `sleep_provider.c` are no longer run, which is a deleted assertion in effect. If the owner takes the third option under M4 (a `k_sem_take`-based port and a second bridge) this row passes |
| M2 ratio of 2 or less to `host-sanitize` | **FAIL like-for-like (about 19); PASS against the whole preset (0.36 to 0.63, and 0.19 to 0.48 against Stage A's figures)** | M2. Which denominator D-D means is the owner's to say |
| M5 passes | **PASS** | 100 of 100, 200 of 200 |
| M6: every wrapped fault case reproducible on `native_sim` | **FAIL, narrowly** | 13 of 13 mutex-wrapped cases pass; the 9 checks of the semaphore-take wrap cannot exist. Same item as M4 |
| Effort extrapolates within 4 person-weeks for the other 11 services | **Not decidable here; PASS on agent hours (4 to 19 h), FAIL on the report's person estimate (6.8 to 11 person-weeks); my judgement 1 to 4 person-weeks** | Extrapolation |
| Blockers B1 to B4 have an owner answer | **FAIL, no answer exists** | B1: D-A works without a protected edit, but is an unapproved assumption. B2: a 181-line Host stub or `native_sim` keeps the suites, an owner choice (D-C). B3: the `crux_mutex_lock` wraps work through the bridge; the `crux_mutex_create` wraps of three services are not in the pilot. B4 (TSan): untouched by this pilot |

**Keep the slim shim (O4) if any of these:**

| Criterion | Verdict | Evidence |
| --- | --- | --- |
| Stage A fails its gate | no | Stage A passed its gate (its independent review asked for a corrected record, which the Stage A session is writing) |
| M2 ratio above 2 | **yes, like-for-like (about 19); no, whole preset** | M2 |
| A test-only kernel shim larger than the Linux Sync and Thread providers (302 lines) | **no on one environment, yes counting everything** | `native_sim`: 204 (pthread) + 49 (bridge) = 253. Host: 181 + 49 = 230. Both environments with the per-case harness: 204 + 103 + 49 + 181 = 537 (Stage A's reading, 307 with the harness, is already above 302). The shim of Stage A is not a Zephyr provider; the stub and the bridge are |
| M5 flakes | no | |
| The owner refuses B1's exemption and wants Host coverage of services | not asked | the Host stub keeps coverage at the stated cost |

**Inconclusive** (Stage A passes, Stage B passes, but B4 is open): applies to the whole-preset reading of M2.

**Conclusion by D-D as written (inference from the table).** O2 is **not** chosen under either reading: M4 and M6 each fail on one suite, and the blockers have no owner answer. O4 is chosen under the like-for-like reading of M2, and the result is "inconclusive, run a second pilot on `config_service` or `mgmt_status` for B4" under the whole-preset reading. The swap itself is not what decides it: the service changed by 47 and 31 lines, no suite but one lost a case, and the `native_sim` suites were as quick and as stable as on Stage A. What decides it is that **the Host and native_sim runs need three pieces of test machinery (a 49-line fault bridge, a 181-line Host stub, a placement check) that O4 does not, one suite has no equivalent, and the measured ratio of the like-for-like reading is about 19**.

## Limits

- One arm64 container, Zephyr 4.4.0, gcc 13, clang 18; CI (amd64, Zephyr SDK, 4.4.2) will differ. No CI figure is in this file.
- Stage A and Stage B figures of M2 come from the same alternating batches, under another session's load for part of them. Host figures are idle. The whole-preset figure is one run with `ctest -j2`.
- `native_sim` runs one Zephyr thread at a time and has no ThreadSanitizer: M5 and M7 say nothing about data races on `native_sim`.
- Zephyr-specific behaviour not exercised: `k_mutex` priority inheritance under contention, ISR use, a stack overflow of the dedicated stack, the real worker thread under a different priority than 6.
- The Host stub's `k_thread_create`, `k_sem_take` and `k_mutex_lock` are minimal, and no case checks the stub against Zephyr. Their purpose is to let the route and single-thread suites run; the kernel's behaviour is held by `native_sim`.
- `scripts/test-reboot.sh` (protected) was not changed and was not run: its target list still exists after the swap. Its clang-tidy line also lists `components/reboot/src/*.c`, which no longer holds the core. `clang-tidy-18` on the new core, the bridge and the stub, run by me with the Host compile database, reports no finding after two fixes of the stub (`NOLINT` and `_GNU_SOURCE`).
- The placement table of `components/service_priority` still has an entry for the reboot worker (`CRUX_SERVICE_THREAD_REBOOT_WORKER`), now unused, because removing it would change the assertions of `tests/service_priority/zephyr`. A product with the default sees no difference.
- Three cases of `tests/service_priority/test_check_placement.py` were retargeted from the reboot worker to the network settings and web session workers, with the same defect injected; one case was added for the Zephyr-direct worker. No case was removed.
- Stage A's fixes after review (#398, the three `Web.reboot_*` route suites on `native_sim`) are not in this branch yet. They are additive to it; a merge is expected to conflict in `tests/reboot/zephyr/CMakeLists.txt` and `scripts/test-reboot-native.sh`.
