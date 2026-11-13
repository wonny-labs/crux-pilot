# Reboot pilot, Stage B: the reboot service on Zephyr kernel objects

Part of the P9 evidence of [ADR-0002](../decisions/0002-platform-direction-zephyr-execution-crux-services.md) (keep a slim Sync/Thread shim, O4, or move services to Zephyr-direct, O2). [Stage A](reboot-native-sim-stage-a.md) ran the existing `reboot` cases on `native_sim/native/64` over the unchanged Crux Thread and Sync providers (54 cases at its first head, 79 after its review fixes added the three `Web.reboot_*` route suites, which this branch has merged: see M4). Stage B swaps the service to `k_mutex`, `k_sem`, `k_thread` and `k_msleep` and runs the same suites again. **This is a Draft PR that is not merged before the owner decides P9.**

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
- **D-C, not applied as written.** A fake `crux_reboot_*` for `tests/web/reboot` would have to re-implement the state machine to keep 65 `CHECK`s unchanged: the route suites call `crux_reboot_work_once()`, read the order of steps, the policy calls, the reset calls and the events, and fail the service's own lock for `503 busy`. I did not write it. Two things made it unnecessary. The real core builds on the Host over a 181-line kernel stub (`tests/reboot/host_kernel`), and the route suites run on it with their sources unchanged. And Stage A's review fixes run the same route suites on `native_sim` (the `web` and `web_no_service` images), which with this branch's bridge pass over the Zephyr-direct service as well: so D-C's alternative, "move those suites to `native_sim`", works. The stub is a test-only pthread stand-in, not a Crux interface, and it is only needed to keep `Reboot.*` and `Web.reboot_*` on the Host. Whether to keep it, to write the fake, or to drop Host coverage of this service is the owner's choice (D-C stays open).
- The worker's own stack is a dedicated one (`CONFIG_CRUX_REBOOT_WORKER_STACK_SIZE`, default 4096, the default of `CRUX_THREAD_STACK_BYTES`), not a slot of the Crux Thread pool.

## M1 effort (measured, agent wall clock; no human hours)

From `git reflog`, UTC, 13 November: branch created 22:06; merge of main (#396) 22:11; container and Zephyr tools ready, Stage A re-run as a baseline 22:14; source, Kconfig and CMake 22:16 to 22:20; placement check and its test 22:19 to 22:21; Host stub, bridge, native project 22:21 to 22:23; first green native run (54 cases) about 22:23; code and tests committed 22:24. **Source, tests and a green run: about 0.3 h.** Measurement batches (two M2 rounds, M5, M7, M8, the Host whole preset), the record and the merge of Stage A's fixes: about 1.3 h more, so **about 1.6 h of wall clock to the last commit of the record**, before the PR text. The report's estimate for Stage B was 22 to 36 h (a person's hours); the agent's time is not comparable to it (see the extrapolation).

| Phase | Agent time |
| --- | --- |
| Reading the contract, the pilot report, the sources and tests | 0.1 h before the branch was made |
| Environment (container, tools, Stage A re-run) | 0.05 h |
| Source swap, Kconfig, CMake | 0.1 h |
| Test side (bridge, Host stub, native project, placement check) | 0.1 h |
| Docs | 0.1 h |
| Measurement (two M2 rounds, M5, M7, M8, Host whole preset) | about 1.0 h, most of it waiting for builds (round 2 alone 0.5 h) |
| Record, merge of Stage A's fixes | 0.3 h (phases overlap, so the rows add to more than 1.6 h) |

## M2 turnaround (measured, local)

**Round 1, before Stage A's review fixes (54 cases, four images).** `scripts/test-reboot-native.sh` as a whole (four image builds and 54 case runs, ASan + UBSan), 5 runs each, Stage A (`cb0fda6f`) and Stage B (`cf8f223b`) alternating in the same container.

| Build | Cold, median (min to max) | Warm, median (min to max) |
| --- | --- | --- |
| native_sim ASan + UBSan, **Stage A** | 99 s (87 to 101) | 62 s (54 to 72) |
| native_sim ASan + UBSan, **Stage B** | 86 s (80 to 100) | 50 s (46 to 63) |
| Host `host-sanitize` replica (clang-18), the reboot targets and 13 or 14 `ctest` entries, before the swap (`f98fe2e0`) | 4.0 s (3.7 to 4.6) | 2.1 s (2.0 to 2.3) |
| the same, after the swap | 4.5 s (4.4 to 5.1) | 2.6 s (2.5 to 2.8) |
| Host `host-sanitize` replica, the whole preset after the swap: configure, build and `ctest -j2` of 317 tests | 137 s (one run; 15 s build, 122 s `ctest`, bound by `NetworkSettings.validation` at 112 s) | |

Load. Cold batch (Stage A and B alternating): load average 2.87 at the start and 4.49 at the end, with another session's container running (`crux-reboot-native`, `docker ps` at the start); **measured under concurrent load**. Warm batch: 4.49 at the start, 1.27 at the end; the other container was running at the start and gone by 22:50; the later warm runs of both stages are faster, which is the load falling. **The Stage A and Stage B figures were taken in the same batches, so their difference is the better number; the absolute seconds are not idle numbers.** The Host batches ran at load 0.5 to 0.9 with no other container (checked): **idle**. The whole-preset run: load 0.8 at the start and 1.3 at the end, idle.

**Stage B against Stage A:** cold 13 % faster (86 against 99 s), warm 19 % faster (50 against 62 s). The spread of each batch (13 to 20 s) is about as large as the difference, so I read this as "no slower, probably a little faster". **Cause: a guess, not measured.** The Crux Thread and Sync providers may no longer be pulled into the `core` image because nothing there references them; they are still compiled (`CONFIG_CRUX_THREAD=y` in `prj.conf`), so the saving is at most link and start-up work. I did not probe it.

**The ratio, two readings.** The protocol had already fixed the like-for-like denominator: Stage 0 of the pilot report (section 3.1) records the Host's `ctest -R '^(Reboot|Web\.reboot)'` on `host-sanitize` for the same suites, and M2 (section 3.2) compares native_sim with those Host numbers against P9's 2 times. That is the first row of each table below. The "whole preset" rows divide by the Host preset across every component (317 tests, 137 s here), while the native side runs only `reboot` and `web/reboot` (six images, 79 cases): **they are different test sets and the ratio is not a comparison of like with like.** With every service moved, the native side would grow by about an image set per service while the Host figure already covers everything (see the projection after round 2). Treat the whole-preset rows as context, not as a pass.

| Reading | Stage B cold | Stage B warm |
| --- | --- | --- |
| Like-for-like: the same suites, native over Host (86 s over 4.5 s, 50 s over 2.6 s) | **about 19** | **about 19** |
| Whole preset, different test sets (137 s measured here) | 0.63 | 0.36 |
| Whole preset, different test sets, as Stage A recorded it (178 s local, 268 s CI run 43321893524) | 0.48 and 0.32 | 0.28 and 0.19 |

**Round 2, after merging Stage A's review fixes (79 cases, six images, the route suites included).** The same method: 5 cold and 5 warm runs, alternating, of the corrected Stage A head (`4282ade9`) and this branch's merged head (`aeba4b3f`).

| Build | Cold, median (min to max) | Warm, median (min to max) |
| --- | --- | --- |
| native_sim ASan + UBSan, **Stage A** at `4282ade9`, 79 cases | 101 s (100 to 111) | 69 s (68 to 70) |
| native_sim ASan + UBSan, **Stage B** at `aeba4b3f`, 79 cases | 95 s (94 to 105) | 61 s (61 to 62) |

Load for round 2: no other container on the VM at 23:10, 23:27 and 23:38 (`docker ps` on the host showed only mine); the load average was 1.9 at the start of the cold batch, 2.9 at its end and 1.2 at the end of the warm batch, which is my own builds. **Idle apart from the measurement itself.** Stage B is 6 % faster cold (95 against 101 s) and 12 % faster warm (61 against 69 s). The warm spread is 1 to 2 s, so the warm difference is outside the noise; the cold one (6 s) is about the spread of the Stage A runs.

Ratios in round 2, with the Host set after the swap (13 entries: 4.5 s cold, 2.6 s warm, idle):

| Reading | Stage B cold | Stage B warm |
| --- | --- | --- |
| Like-for-like: native over Host, the same suites | **about 21** (95 s over 4.5 s) | **about 23** (61 s over 2.6 s) |
| Whole preset, different test sets (137 s measured here) | 0.69 | 0.45 |
| Whole preset, different test sets, as Stage A recorded it (178 s local, 268 s CI) | 0.53 and 0.35 | 0.34 and 0.23 |

**Projection for all services (inference, not measured).** Reboot's set is 6 images and takes 95 s cold and 61 s warm, so about 16 s and 10 s for an image. If each of the 12 services needed 3 to 6 images (reboot has 6; the web suites share one composition), native_sim would run 36 to 72 images: about 580 to 1150 s cold and 360 to 730 s warm, built one after another as the script does. Against the Host preset's 137 s that is **4.2 to 8.4 cold and 2.6 to 5.3 warm**; against CI's 268 s, 2.2 to 4.3 cold and 1.3 to 2.7 warm. So on a like-for-like footing for the whole platform the ratio stays above 2 in every case but the warm low end against CI's figure. The warm ratios divide warm native times by the Host's single cold figure of 137 s; the Host's warm figure (about 122 s of `ctest`, the build being incremental) would make the warm ratios slightly higher, so the stated ones are conservative. Building images in parallel on 8 CPUs would lower it; I did not try. The projection is indicative only.

The like-for-like ratio measures the number of Zephyr images (six configurations of Kconfig and a kernel build, about 16 s cold and 10 s warm each), not the tests: a case runs in about 0.1 s.

## M3 lines by zone

`git diff --numstat -M f98fe2e0 HEAD` (Stage A plus main, then this change), then the record and README.

| Zone | Added / removed | Note |
| --- | --- | --- |
| Portable (`src`, `include`) | 0 / 23 | `src/reboot_sleep.c` is replaced; `src/reboot.c` leaves `src` (444 lines) |
| `ports/zephyr` | 474 lines in two new files | `reboot_zephyr.c` is a rename of `src/reboot.c`: **47 added and 31 removed lines** inside it (460 lines now, 444 before); `reboot_sleep_zephyr.c` 14 |
| Tests, new | 49 + 113 + 68 | `fault_bridge.c`, `kernel_stub.c`, the stub's `kernel.h` |
| Tests, changed | 27/12 + 25/15 + 88/5 + 1/0 + 8/5 + 31/6 + 11/7 | `tests/reboot/CMakeLists.txt`, `tests/reboot/zephyr/CMakeLists.txt`, `main_sleep_provider.c`, `prj.conf`, `tests/web/reboot/CMakeLists.txt`, `check_placement.py`, `test_check_placement.py`; **no assertion source of a suite changed** |
| CMake and Kconfig (non-test) | 11/20 + 28/8 | `components/reboot/CMakeLists.txt`, `zephyr/Kconfig` |
| Protected | **0 new** | `scripts/test-reboot-native.sh` and one `zephyr.yml` step are Stage A's two, unchanged |
| Docs | 17/5 + 1/1 + 8 + 210 (this file at head `3fd43ce6`) | `docs/reboot-service.md`, `docs/thread-sync.md`, `tests/README.md` |

Lines no longer needed in Crux Sync and Thread for this service: **13 lexical Crux Sync or Thread references** leave the service (10 in `reboot.c`, 3 in `reboot_sleep.c`), against 9 `k_*` references in the new files. **Provider lines freed: 0.** The Linux Sync and Thread providers, the Zephyr ones (241 lines) and the common ones (546) are all still used by 10 other services, so none can go until the last of them moves.

**Test-only kernel shims, for the 302-line cap of D-D.** One counting rule, the one Stage A used: everything test-only that the pilot needs to run its suites in an environment, **including the per-case harness** (`zephyr_case.c`, which does what `fork()` did).

| Piece | Lines | Needed in |
| --- | --- | --- |
| `tests/reboot/zephyr/compat/pthread.h` and `compat_pthread.c` (Stage A, `wc -l` of the merged head) | 209 (75 + 134) | native_sim |
| `tests/reboot/zephyr/zephyr_case.c` (one case for each process) | 103 | native_sim |
| `tests/reboot/fault_bridge.c` | 49 | native_sim and Host |
| `tests/reboot/host_kernel/` | 181 | Host |

| Environment | Total | Against 302 |
| --- | --- | --- |
| native_sim: 209 + 103 + 49 | **361** | over |
| Host: 181 + 49 | **230** | under |
| Both: 209 + 103 + 49 + 181 | **542** | over |
| native_sim without the harness: 209 + 49 | 258 | under |
| Both environments without the harness: 209 + 49 + 181 | 439 | over |

Stage A's own figure under the same rule was 312. **Under this rule (per-case harness included, as Stage A counted) the row trips "keep O4" for native_sim (361) and for both environments (542); the Host alone (230) is under.** Two rulings by the owner would pass it: (1) the cap applies to the Host stub alone (230); (2) the 103-line per-case harness `zephyr_case.c`, which replaces `fork()`, is not a kernel shim, which makes native_sim 258 and the Host 230, each under 302 (both environments together would still be 439). That is a choice for the owner, and I do not make it.

## M4 behaviour and tests identical

- `git diff origin/main -- components/reboot/include`: empty. `docs/reboot-service.md`: the behaviour tables and the error rows are unchanged; a note at the top says what became impossible, and four rows that named Sync, the sleep provider or the low class were reworded.
- No existing assertion source changed: the diff of `tests/reboot/*.c`, `tests/web/reboot/*.c` and `tests/access` against Stage A plus main is empty. `CHECK(` counts of the sources: service 108, steps 135, concurrency 37, reason 8, lock_faults 25, reboot_test.c 5, pending 84, web 65.
- `native_sim`: 54 of 54 cases pass before the merge of Stage A's review fixes and **79 of 79 after it** (the three route suites, 17 + 6 + 2 cases, over the Zephyr-direct service), as at Stage A. One of them is not the same case: the image `sleep_provider` runs 3 new ztest cases (below), not the 9 checks of `sleep_provider.c`.
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
| `crux_mutex_lock`, the service's lock for the answer `503 busy` (`web` image, `reboot_web.c`) | passes | passes, through the same bridge |
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

R2 on the Host is caught by `Reboot.concurrency` now, which Stage A (Host over the real Crux Sync) recorded as 0 of 100; the cause is the stub's raw pthreads, over which the unlocked service really runs concurrently. I did not investigate whether it is a data-race effect or a crash. On `native_sim` a data race is still not seen, as at Stage A: one Zephyr thread runs at a time and there is no ThreadSanitizer. So nothing the Host caught is lost for `reboot`, and race coverage is missing on both. **Guess:** the Host over the stub may be the stronger of the two for races; that rests on one mutant (R2) whose cause I did not investigate.

## M8 footprint (measured: `size` on native_sim objects; RT1060 not measured)

| | Stage A | Stage B |
| --- | --- | --- |
| `core` image, `size` (text / data / bss), sanitized | 536270 / 807744 / 3042944 | 527486 / 807640 / 2928224 |
| the service's object (text / data / bss) | 7663 / 8048 / 672 | 7551 / 7656 / 17024 |
| the sleep port's object | one Crux semaphore, created at the first sleep | 156 / 8 / 0 |

The image figures are mostly not the service: the Crux Thread provider (a test-configured pool of 8 stacks of 16 KiB) is most likely no longer pulled into the `core` image, because nothing in it creates a Crux thread any more (inference; the measured part is the size difference), which a product image with other Crux workers would not see. The service's own bss is 17024 bytes in this test configuration, which sets the worker stack to 16384; at the default of 4096 it is about 4.7 KiB. On a product image that still has Crux Thread users, reboot gives back one Thread pool slot (`CRUX_THREAD_STACK_BYTES`, default 4096) and one Sync mutex slot and two semaphore slots, and takes a dedicated 4096-byte stack and a `k_thread`, `k_mutex` and `k_sem`: the RAM is about the same, as the pilot report predicted (inference). **RT1060 flash and RAM need the ARM cross-build in CI; the container has no ARM Zephyr SDK.**

## Extrapolation to the other 11 services (inference, labelled)

Method: the call sites of the pilot report (section 1.2): reboot 9; the other ten Crux Sync/Thread services 6 (access), 1 (cli), 4 (config_service), 9 (credential), 3 (file_service), 3 (mgmt_status), 16 (network_settings), 21 (security_event), 7 (tls_certificate) and 20 (web), 90 in all; `ssh` is already native, 0. Weights as the report's: ×1.5 for the TSan- or wrap-heavy services (security_event, access, credential, config_service, mgmt_status: 43 sites), ×1 for the rest (47 sites): 47 + 64.5 = 111.5 sites, which is **12.4 reboots**.

What Stage B shows about cost, which the straight proportion does not:

1. **The service diff is small and mechanical.** +47 and −31 lines in the core, and the same few lines for each of the four near copies of the worker recipe (`network_settings`, `web_management_save`, `web_session_attempt`, and the drain of `security_event` in a different shape).
2. **The cost is in the test closure, and part of it is paid once.** Once: the fault bridge (49 lines; each service's `--wrap=crux_mutex_lock` suites need it too), the placement check and its test (31/6 and 11/7 lines), the Kconfig pattern, and a Host stub that grows only with the kernel calls used. Per service: the CMake of its suites and, for each, a decision on every assertion that cannot run.
3. **Assertions that cannot run, per service.** This pilot found one suite (9 checks). The services that wrap `crux_mutex_create` on purpose (`config_service`, `credential`, `mgmt_status`) assert a create failure that static objects cannot have: those are the pilot report's B3 case, an owner decision for each, and they are not in the pilot.
4. **What the pilot did not exercise:** ThreadSanitizer suites (B4: `config_service_concurrency`, `mgmt_status_snapshot`, the SSH CLI), test hooks that hand out Crux handles (B5), context rules for ISRs (B6), `k_condvar` or recursive-lock needs, and a mixed-kind image beyond `pending`'s reboot next to Crux-Sync network settings (which passed).

Range. **(a) From the measured agent time:** the first green run took 0.3 h for 12.4 reboots-worth, so 3.7 h; with the whole 1.6 h of this pilot as the unit, 20 h. **That is 4 to 20 agent hours, about 0.1 to 0.5 weeks if a week is 40 hours of the same work**, and it excludes owner decisions, independent review rounds, CI runs and the per-service blockers of point 3 and 4. **(b) From the report's pre-registered person estimate** (22 to 36 h for Stage B): 12.4 × 22 to 36 = 273 to 446 h, **6.8 to 11 person-weeks.** The pre-registered numbers were already 10 to 100 times above what Stage A (0.8 h against 8 to 14 h) and Stage B (0.3 to 1.6 h against 22 to 36 h) cost an agent, so (b) is an upper bound that a person doing this work by hand might still approach. Subtracting the once-only parts for the later services lowers both. I do not have a measured human-to-agent ratio, and the cap's unit (person-weeks) cannot be compared with agent hours without one. **The range I would defend: 1 to 4 person-weeks of person-led work for the 11 services plus their owner decisions, with the TSan and create-wrap services as the uncertain part.** That sentence is a judgement and not a measurement.

## Exit criteria of the report section 3.5 (D-D, approved 2026-11-13 21:02:50 UTC)

**Choose O2 only if all of these hold:**

| Criterion | Verdict | Evidence |
| --- | --- | --- |
| M4 identical: no public change, no deleted assertion | **FAIL, narrowly** | Public header diff empty, no assertion source changed or deleted. But the 9 checks of `sleep_provider.c` are no longer run, which is a deleted assertion in effect. If the owner takes the third option under M4 (a `k_sem_take`-based port and a second bridge) this row passes |
| M2 ratio of 2 or less to `host-sanitize` | **FAIL on the protocol's own basis (Stage 0: the same Host suites): about 19 to 23.** The whole-preset ratio (0.19 to 0.69 over all denominators) divides different test sets and is not a pass; the all-services projection (inference, indicative only) is 2.6 to 8.4 against the same 137 s | M2. The owner may still rule on the denominator, with the mismatch above in view |
| M5 passes | **PASS** | 100 of 100, 200 of 200 |
| M6: every wrapped fault case reproducible on `native_sim` | **FAIL, narrowly** | 13 of 13 mutex-wrapped cases pass; the 9 checks of the semaphore-take wrap cannot exist. Same item as M4 |
| Effort extrapolates within 4 person-weeks for the other 11 services | **Not decidable here; PASS on agent hours (4 to 20 h), FAIL on the report's person estimate (6.8 to 11 person-weeks); my judgement 1 to 4 person-weeks** | Extrapolation |
| Blockers B1 to B4 have an owner answer | **FAIL, no answer exists** | B1: D-A works without a protected edit, but is an unapproved assumption. B2: a 181-line Host stub or `native_sim` keeps the suites, an owner choice (D-C). B3: the `crux_mutex_lock` wraps work through the bridge; the `crux_mutex_create` wraps of three services are not in the pilot. B4 (TSan): untouched by this pilot |

**Keep the slim shim (O4) if any of these:**

| Criterion | Verdict | Evidence |
| --- | --- | --- |
| Stage A fails its gate | no | Stage A passed its gate (its independent review asked for a corrected record, which the Stage A session is writing) |
| M2 ratio above 2 | **yes** on the protocol's basis (about 19 to 23), the whole-preset division, a different test set, is 0.19 to 0.69 (not above 2); the all-services projection (inference, 2.6 to 8.4) is indicative only and is not counted as a trip | M2, rounds 1 and 2 |
| A test-only kernel shim larger than the Linux Sync and Thread providers (302 lines) | **yes** under the rule Stage A used (harness included): native_sim 361, both environments 542. **No** if the owner rules that the cap applies to the Host stub alone (230), or that the per-case harness is not a kernel shim (native_sim 258, Host 230) | See the shim tables in M3. Stage A's figure was 312 |
| M5 flakes | no | |
| The owner refuses B1's exemption and wants Host coverage of services | not asked | the Host stub keeps coverage at the stated cost |

**Inconclusive** (Stage A passes, Stage B passes, but B4 is open): applies only if the owner rules for the whole-preset denominator on M2 **and** passes the shim row by one of the two rulings above.

**Conclusion by D-D as written (inference from the table).** O2 is **not** chosen: M4 and M6 each fail on one suite, and the blockers have no owner answer. Whether O4 follows depends on two rulings that are the owner's. On the Stage 0 reading of M2 the measured ratio (about 19 to 23) trips O4 by itself. On the whole-preset reading the measured M2 does not trip it (0.19 to 0.69, but over different test sets), and O4 then follows only from the shim row, which trips under Stage A's counting (361 on native_sim) unless the owner rules that the cap applies to the Host stub alone or that the per-case harness is not a kernel shim. Only if the owner takes the whole-preset denominator **and** one of those two shim rulings is the result "inconclusive, run a second pilot on `config_service` or `mgmt_status` for B4". The all-services projection (2.6 to 8.4, inference) is indicative only and is not one of the trips; it suggests that the whole-preset ratio would not stay under 2 as more services move. The swap itself is not what decides it: the service changed by 47 and 31 lines, no suite but one lost a case, and the `native_sim` suites were as quick and as stable as at Stage A. What decides it is the test side: **native_sim needs 361 lines of test-only shim and harness, the Host needs a 181-line stub of its own, one suite has no equivalent, and the like-for-like turnaround is about 19 to 23 times the Host's.**

## Behaviour differences that the tests do not catch

- **Worker stack.** The Crux Thread worker took `CRUX_THREAD_STACK_BYTES`; the Zephyr-direct worker has its own `CONFIG_CRUX_REBOOT_WORKER_STACK_SIZE`, default 4096. A product that raised `CRUX_THREAD_STACK_BYTES` for its policy callbacks (`crux-controller`'s `s2.conf` does raise it) silently gets 4096. The native_sim suites use 16384, so 4096 under a real policy callback is never exercised.
- **ISR calls.** `k_mutex_lock` in an interrupt is invalid; Crux Sync answered `BUSY`. The service is documented as thread context only, and no test calls it from an ISR.
- **Thread name.** The new worker has no name, and neither had the old one (`grep thread_name components/thread` finds nothing), so a debugger shows the same; not a difference, checked.
- **The `sleep_provider` image counts as one case.** The protected script counts that image as one case whatever it contains, so "79 of 79" cannot tell the old suite from the new three cases. The change is visible only in this record and in the PR.
- **A gate changed.** `tests/service_priority/check_placement.py` gained an exemption for Zephyr-direct workers (`NATIVE_WORKERS`): a thread may now be created without a placement call if it takes its priority from its Kconfig symbol in `k_thread_create`. This is a check, not a protected path, but the owner should note it.
- The unused `CRUX_SERVICE_THREAD_REBOOT_WORKER` entry remains in `components/service_priority` (see Limits).

## Limits

- One arm64 container, Zephyr 4.4.0, gcc 13, clang 18; CI (amd64, Zephyr SDK, 4.4.2) will differ. No CI figure is in this file.
- **Revisions of each local run.** Host suites, the Host whole preset, the mutants, the bridge control, M5 and M8: `cf8f223b` (the code). Native 79 of 79, and M2 round 2: `aeba4b3f` (the merged head). M2 round 1: `cf8f223b` against `cb0fda6f`. The commits after `aeba4b3f` up to `3fd43ce6` change only this record (`git diff --name-only aeba4b3f 3fd43ce6` lists it alone). Between `cf8f223b` and `aeba4b3f` the merge changed only `tests/reboot/zephyr` (the Stage A session's web images and my conflict resolution), the script, `tests/README.md` and two Stage A records, so the Host figures hold for the later head.
- Stage A and Stage B figures of M2 come from the same alternating batches, under another session's load for part of them. Host figures are idle. The whole-preset figure is one run with `ctest -j2`.
- `native_sim` runs one Zephyr thread at a time and has no ThreadSanitizer: M5 and M7 say nothing about data races on `native_sim`.
- Zephyr-specific behaviour not exercised: `k_mutex` priority inheritance under contention, ISR use, a stack overflow of the dedicated stack, the real worker thread under a different priority than 6.
- The Host stub's `k_thread_create`, `k_sem_take` and `k_mutex_lock` are minimal, and no case checks the stub against Zephyr. Their purpose is to let the route and single-thread suites run; the kernel's behaviour is held by `native_sim`.
- `scripts/test-reboot.sh` (protected) was not changed and was not run: its target list still exists after the swap. **Merge-readiness note (not for now):** it would need an approved edit, because it runs clang-tidy over `tests/reboot/*.c`, which includes `sleep_provider.c` (in no compile database now), and over `components/reboot/src/*.c`, which no longer holds the core. `clang-tidy-18` on the new core, the bridge and the stub, run by me with the Host compile database, reports no finding after two fixes of the stub (`NOLINT` and `_GNU_SOURCE`).
- The placement table of `components/service_priority` still has an entry for the reboot worker (`CRUX_SERVICE_THREAD_REBOOT_WORKER`), now unused, because removing it would change the assertions of `tests/service_priority/zephyr`. A product with the default sees no difference.
- Four cases of `tests/service_priority/test_check_placement.py` were changed: `test_a_missing_placement` now removes the network settings worker's placement instead of the reboot worker's; `test_a_placement_after_other_work` inserts the web session identifier instead of the reboot one as its constant; `test_a_new_zephyr_image_without_the_placement` and `test_a_host_test_needs_no_placement` name `CRUX_NETWORK_SETTINGS_SOURCES` instead of `CRUX_REBOOT_SOURCES`. The same defect is injected in each. One case was added for the Zephyr-direct worker, and none was removed.
- Stage A's review fixes (branch `claude/pilot-reboot-native-sim-5cd9d1` at `4282ade9`) are merged into this branch, without a rebase. The merge conflicted once, in `tests/reboot/zephyr/CMakeLists.txt` (their web images and my bridge), and I resolved it by keeping both; the `web` images link the bridge too. `scripts/test-reboot-native.sh` in this branch is theirs, unchanged by Stage B. If they push further fixes, merge them additively again.
