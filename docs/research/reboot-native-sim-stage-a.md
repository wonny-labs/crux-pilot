# Reboot pilot, Stage A: the reboot suites on native_sim

Part of the P9 evidence of [ADR-0002](../decisions/0002-platform-direction-zephyr-execution-crux-services.md) (keep a slim Sync/Thread shim, O4, or move services to Zephyr-direct, O2). Stage A runs the existing `reboot` suites on `native_sim/native/64` over the unchanged Crux Thread and Sync Zephyr providers, with AddressSanitizer and UBSan. It changes nothing in `components/reboot`, in a public header or in a test source. Stage B (the swap to `k_mutex`, `k_sem` and `k_thread`) is not part of this file.

**Labels.** *Measured (local)* = measured by the author in a native arm64 `ubuntu:24.04` container on one Mac (8 CPUs, `ZEPHYR_TOOLCHAIN_VARIANT=host`, gcc 13.3, clang-18 for the Host build, Zephyr **4.4.0**; the repository pins 4.4.2, so `CruxZephyrBaseline` warns that its evidence does not apply to this build). *CI* = the numbers of the GitHub Actions run of this PR; they are not in this file. Nothing here is a physical-board result.

## What exists

| Item | Lines |
| --- | --- |
| `tests/reboot/zephyr/CMakeLists.txt`, `prj.conf`, `sanitize.conf` | 141, 15, 2 |
| `compat/pthread.h`, `compat_pthread.c` (the pthread calls of the suites over Zephyr threads) | 75, 134 |
| `zephyr_case.c` (one case for each process) | 103 |
| `main_core.c`, `main_lock_faults.c`, `main_pending.c`, `main_sleep_provider.c`, `main_web.c`, `main_web_no_service.c`, `main_common.h` (ztest wrappers) | 22, 13, 14, 11, 19, 13, 5 |
| `scripts/test-reboot-native.sh` (new, protected path) | 126 |
| `.github/workflows/zephyr.yml` (one added step, protected path) | 5 |
| `tests/README.md` | 12 |

Six images are built from the Host sources of `tests/reboot`, `tests/web/reboot` and `tests/access`, with a suite's `main()` renamed to a function by a compile definition and nothing else changed:

| Image | Suites (cases) | Link wraps |
| --- | --- | --- |
| `core` | `service` 13, `steps` 22, `concurrency` 3, `reason` 2 | none |
| `lock_faults` | `lock_faults` 4 | `crux_mutex_lock` |
| `pending` | `pending` 9 (real network settings and configuration saves as wait sources) | `crux_mutex_lock` |
| `sleep_provider` | `sleep_provider` 1 | `crux_semaphore_take` |
| `web` | `routes` 17, `openapi_fixture` 6 (real Web core and session module, route table with the reboot routes) | `crux_web_respond`, `crux_mutex_lock` |
| `web_no_service` | `no_service` 2 (route table without the reboot routes) | `crux_web_respond`, `crux_mutex_lock` |

79 cases: 13 + 22 + 3 + 2 + 4 + 9 + 1 + 17 + 6 + 2. They are the 10 Host tests `Reboot.service`, `steps`, `concurrency`, `reason`, `lock_faults`, `pending`, `sleep_provider` and `Web.reboot_routes`, `reboot_no_service`, `reboot_openapi`. The script counts `*_run_case(` in each Host source and fails if an image lists a different number. `Web.reboot_openapi` runs the Host's `check_openapi_reboot.py` unchanged, on the `RECORD` lines that the fixture's cases print on native_sim (25 answers, 12 negative controls).

Not run on native_sim: `Reboot.nodiscard` (a compile check of a header attribute), the C++ header compile check, and `ManagementProviders.events` (not a reboot suite).

## What had to be solved

1. **No `fork()`.** The Host runs every case in a forked child, because the service has no stop and keeps its state, worker and Sync objects for the life of the process. The image runs one case for each process (`-reboot-list`, `-reboot-case=<n>`), and the script starts it once for each case, each under `timeout 240` as the Host's ctest gives a suite. A run without a case number fails.
2. **Raw pthreads break the Zephyr kernel.** With the host's glibc, `pthread_create()` starts a host thread that the Zephyr scheduler does not know. Before the shim, `Reboot.concurrency` failed 4 of 60 runs in case 0 (`CRUX_REBOOT_OK` expected) and every run of case 1 with `z_spin_lock_valid` kernel assertions (observed). Zephyr's POSIX layer is not usable here: `CONFIG_POSIX_API` selects options that need picolibc or newlib, and the workspace has neither (the host toolchain variant, and the west allowlist has no picolibc). The shim makes Zephyr threads, a `k_mutex`, a `k_condvar` barrier and a `k_sleep` for `nanosleep`. It is test-only. Two differences from POSIX that a suite could notice: a Zephyr `k_mutex` is recursive and inherits priority, so a thread that locks its own mutex twice, which hangs a default Linux mutex, would not hang here; and `pthread_barrier_wait()` never returns `PTHREAD_BARRIER_SERIAL_THREAD`. No suite relies on either.
3. **Wrapping works as on the Host.** `crux_mutex_lock`, `crux_semaphore_take` and `crux_web_respond` are functions that the suites call through the link, so `-Wl,--wrap=` reaches them in a native_sim image. `tests/portable_execution/zephyr` shows the same mechanism for the provider's `crux_*_impl_*` functions. No `z_impl_k_mutex_lock` wrap was needed at Stage A. It will be at Stage B, when the calls become `k_mutex_lock`.
4. The Host's arm64 problem (the Linux Thread provider asks for a 64 KiB stack, below the 128 KiB `PTHREAD_STACK_MIN` of aarch64) means the Host baseline below used the probe's `LD_PRELOAD` clamp. CI is amd64 and does not need it.

## Results

All of this is *measured (local)* unless it says CI.

| Run | Result |
| --- | --- |
| `scripts/test-reboot-native.sh` (ASan + UBSan, `UBSAN_OPTIONS=halt_on_error=1`) | 79 cases, 0 failed |
| Host `host-sanitize`, clang-18, the same 10 ctest tests (with the stack clamp) | 10 of 10 passed |
| CI | **to be added from the PR's run** |

### M1 effort

Agent wall clock, one agent, no human hours: 21:03 to about 22:50 UTC, 1.7 h. By phase: reading and design 0.15 h; project, shim and the fork, pthread and libc dead ends 0.3 h; script, CI step, README 0.1 h; measuring 0.7 h (two M2 batches of about 0.3 h each, mostly waiting, plus flake runs and three mutant rebuilds); `pending` 0.05 h; web route suites and the review fixes 0.3 h; this record 0.1 h. The first measuring phase and the second M2 batch overlapped other sessions' Docker work (see M2). The report's estimate for Stage A was 8 to 14 h.

### M2 turnaround (measured, local, concurrent load)

Final batch, 5 runs each, the Host's 10 tests against the 79 native cases, run one batch after another. **Not idle.** At the start of the native batches (22:18 UTC): the other session's container `crux-stageb-impl` at 0% CPU, the Mac's load average 3.3. At the end (22:43): `crux-stageb-impl` at 55% CPU and load average 3.4, 4.2 over five minutes. The Mac carries other work throughout (load about 3).

| Build | Cold, median (min to max) | Warm, median (min to max) |
| --- | --- | --- |
| Host `host-sanitize` (clang-18): configure, build of the reboot targets, `ctest` of the 10 tests | 3.5 s (3.3 to 3.8) | 1.7 s (1.6 to 2.6) |
| native_sim ASan + UBSan, six images, 79 case runs | 116 s (111 to 136) | 90 s (87 to 94) |
| ratio, native_sim over Host, same suites | about 33 | about 52 |
| ratio over the whole `host-sanitize` preset (178 s local from the probe, 268 s CI run 43321893524) | 0.65 (0.43 against CI) | |
| native_sim plain, 3 runs | 105 s (100 to 116) | 61 s (60 to 62) |

The plain figures were taken last, while the other session's container was busy, and are **very likely inflated**: the same plain build took 46 s cold and 29 s warm with four images in the earlier batch, and six images cost about 1.5 times that. The sanitized batch ran earlier and its spread is tight (111 to 136), so it is probably less affected, but nothing here is an idle-machine figure.

The earlier batch (four images, 54 cases, before the web suites) gave 78 s cold (74 to 111) and 55 s warm (53 to 59) for the sanitized build, 46 s and 29 s plain, while other sessions ran Docker work from about 21:15 to 21:35. It is superseded by the table above. Both batches give about 19 s for each image, so the time is image count, not tests: a case run takes about 0.1 s, and 300 case runs of `Reboot.concurrency` take 7 s. "Warm" re-runs the script with the build directories in place; CMake and Ninja run again.

### M3 lines by zone

`git diff --numstat origin/main...HEAD`: components, public headers, `docs/reboot-service.md` and every existing test source: 0 changed lines. New: tests 567 lines (above), protected 131 (script 126, workflow step 5), docs 12 + this file.

### M4 behaviour and tests identical

`git diff origin/main...HEAD -- components web docs/reboot-service.md` is empty. No existing test source changed; `tests/README.md` gains a section. `CHECK(` counts of the sources in the images: `tests/reboot` 411 (service 108, steps 135, concurrency 37, reason 8, lock_faults 25, sleep_provider 9, `reboot_test.c` 5, pending 84) and `tests/web/reboot` 65 (routes 46, no_service 6, reboot_web.c 13): 476, as at the base. 79 of 79 Host cases have a native case.

### M5 flakiness (measured, local)

`Reboot.concurrency` (its three cases) on native_sim with ASan and UBSan: 100 runs in a row, 100 passed; 200 runs, 8 at a time, 200 passed. The same two bars on the Host binary: 100 of 100 and 200 of 200. Run before the web image was added; `concurrency` is in the unchanged `core` image.

### M6 fault-injection parity

Every wrapped case of the seven `tests/reboot` suites and the three web route suites passes on native_sim: `lock_faults` 4 of 4 (waits of 20 ms and 1000 ms, the Nth wait of a length), `pending` 9 of 9 (wait of 20 ms), `sleep_provider` (take answers: timeout, `ERR_IO`, `ERR_INVALID_ARGUMENT`, `ERR_INVALID_STATE`, `OK`), the fake-sleep failures in `steps`, and the web `busy` answers (`crux_mutex_lock` wrapped, 25 of 25 cases). No case needed a pointer-discriminated wrap. That the wraps reach the service was shown by the mutants below. Not covered: any wrap in a suite outside these ten.

### M7 mutation probe (measured, local; `components/reboot/src/reboot.c` edited, run, restored; run before the web image was added)

| Mutant | Host sanitize | native_sim ASan + UBSan |
| --- | --- | --- |
| R1: the worker's `end_job` writes `svc.state` without the lock | `lock_faults` fails; `concurrency` 0 of 100 failed | `lock_faults` case 3 fails; `concurrency` 0 of 100 failed |
| R2: no state lock anywhere | `lock_faults` fails; `concurrency` 0 of 100 failed | `lock_faults` cases 0 to 3 fail; `concurrency` 0 of 100 failed |
| L1: a second worker may take a running job (`svc.running` test removed) | `concurrency` 100 of 100 failed | `concurrency` case 2 fails, 100 of 100 |

Both builds see the logic mutant and the lock-fault suite's view of a missing lock, and neither sees a data race: ASan and UBSan do not detect races, `native_sim` has no ThreadSanitizer and runs one Zephyr thread at a time, and the reboot suites have no TSan run on the Host either ([platform direction review](platform-direction-review.md), F13). So nothing the Host catches for `reboot` is lost; race coverage is missing on both.

## Gate verdict

**Stage A passes its gate.** The concurrency and fault-injection suites run on native_sim with the same sources, the same assertions, the same wraps, 79 of 79 cases, no flake at the repository's bar, and the same mutant results as the Host. The gate does not say "keep the shim (O4)". Whether Stage A may close with `Reboot.nodiscard`, the C++ header check and the CI run still outstanding is for the owner.

Against the criteria of the P9 report section 3.5 (adopted as D-D), what Stage A can say:

| Criterion | Stage A |
| --- | --- |
| M4 identical | met: 476 `CHECK(`, 79 cases, components diff empty |
| M5 flake bar | met for `Reboot.concurrency` |
| M6 fault-case parity | met for the ten suites that run |
| Test shim not larger than the Linux Sync + Thread providers (302 lines) | pthread shim 209 lines; with the per-case harness 312 lines, 10 over. The shim is what the Host's fork and pthreads needed, not a Zephyr provider |
| M2 ratio of 2 or less to `host-sanitize` | **not met on the like-for-like basis**: the ten suites take about 33 times (cold) and 52 times (warm) the Host's seconds, measured under concurrent load. Against the whole `host-sanitize` preset (178 s local, 268 s CI) the same set is 0.43 to 0.65 times. Which denominator D-D means decides this row, and it is for the owner to say |
| Stage B extrapolation, 4 person-weeks or less | not measured: Stage B is not part of this file |

## Limits

- Local numbers are from one arm64 container, Zephyr 4.4.0 and gcc 13, under concurrent load; CI (amd64, Zephyr SDK, 4.4.2) will differ. The CI figures are not in this file.
- native_sim runs one Zephyr thread at a time and switches only when a thread blocks or sleeps (simulated time does not advance while a thread runs), so the concurrency cases interleave less than on the Host. Their assertions are the same; their power to expose a race is not stronger, and the mutants above show neither build exposes one.
- The workflow step is approved "for the pilot branch" (D-B). Merged to `main` it runs in the `runtime` shard of every PR and adds about two minutes there; the owner must confirm that at Gate 2.
- LeakSanitizer at exit is off (`detect_leaks=0`), as Twister has it for these builds.
- No footprint (M8): Stage B.
