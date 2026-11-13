# Reboot pilot, Stage A: the reboot suites on native_sim

Part of the P9 evidence of [ADR-0002](../decisions/0002-platform-direction-zephyr-execution-crux-services.md) (keep a slim Sync/Thread shim, O4, or move services to Zephyr-direct, O2). Stage A runs the existing `reboot` suites on `native_sim/native/64` over the unchanged Crux Thread and Sync Zephyr providers, with AddressSanitizer and UBSan. It changes nothing in `components/reboot`, in a public header or in an assertion. Stage B (the swap to `k_mutex`, `k_sem` and `k_thread`) is not started.

**Labels.** *Measured (local)* = measured by the author on one machine: a native arm64 `ubuntu:24.04` container, 8 CPUs, `ZEPHYR_TOOLCHAIN_VARIANT=host`, gcc 13.3, clang-18 for the Host build, Zephyr **4.4.0** (the repository pins 4.4.2, so `CruxZephyrBaseline` warns that its evidence does not apply to this build). *CI* = the numbers of the GitHub Actions run of this PR; they are not in this file and must be added from that run. Nothing here is a physical-board result.

## What exists

| Item | Lines |
| --- | --- |
| `tests/reboot/zephyr/CMakeLists.txt`, `prj.conf`, `sanitize.conf` | 107, 15, 2 |
| `compat/pthread.h`, `compat_pthread.c` (the pthread calls of the suites over Zephyr threads) | 70, 134 |
| `zephyr_case.c` (one case for each process) | 103 |
| `main_core.c`, `main_lock_faults.c`, `main_pending.c`, `main_sleep_provider.c`, `main_common.h` (ztest wrappers) | 22, 13, 14, 11, 5 |
| `scripts/test-reboot-native.sh` (new, protected path) | 111 |
| `.github/workflows/zephyr.yml` (one added step, protected path) | 5 |
| `tests/README.md` | 11 |

Four images are built from the Host sources of `tests/reboot` and `tests/access`, with a suite's `main()` renamed to a function by a compile definition and nothing else changed: `core` (`service`, `steps`, `concurrency`, `reason`), `lock_faults` (link wraps `crux_mutex_lock`), `pending` (real network settings and configuration saves as wait sources, link wraps `crux_mutex_lock`) and `sleep_provider` (link wraps `crux_semaphore_take`). 54 cases: 13 + 22 + 3 + 2 + 4 + 9 + 1. The Host runs the same 54 (`grep` of `*_run_case(` in the sources), and the script fails if an image lists a different count for a suite.

## What had to be solved

1. **No `fork()`.** The Host runs every case in a forked child, because the service has no stop and keeps its state, worker and Sync objects for the life of the process. The image runs one case for each process (`-reboot-list`, `-reboot-case=<n>`), and the script starts it once for each case. A run without a case number fails.
2. **Raw pthreads break the Zephyr kernel.** With the host's glibc, `pthread_create()` starts a host thread that the Zephyr scheduler does not know. Before the shim, `Reboot.concurrency` failed 4 of 60 runs in case 0 (`CRUX_REBOOT_OK` expected) and every run of case 1 with `z_spin_lock_valid` kernel assertions (observed). Zephyr's POSIX layer is not usable here: `CONFIG_POSIX_API` selects options that need picolibc or newlib, and the workspace has neither (the host toolchain variant, and the west allowlist has no picolibc). The shim makes Zephyr threads, a `k_mutex`, a `k_condvar` barrier and a `k_sleep` for `nanosleep`. It is test-only and exists nowhere in a product.
3. **Wrapping works as on the Host.** `crux_mutex_lock` and `crux_semaphore_take` are functions of the Sync component, so `-Wl,--wrap=` reaches them in a native_sim image, as it does in `tests/portable_execution/zephyr`. No `z_impl_k_mutex_lock` wrap was needed at Stage A. It will be at Stage B, when the calls become `k_mutex_lock`.
4. The Host's arm64 problem (the Linux Thread provider asks for a 64 KiB stack, below the 128 KiB `PTHREAD_STACK_MIN` of aarch64) means the Host baseline below used the probe's `LD_PRELOAD` clamp. CI is amd64 and does not need it.

## Results

All of this is *measured (local)* unless it says CI.

| Run | Result |
| --- | --- |
| `scripts/test-reboot-native.sh` (ASan + UBSan, `UBSAN_OPTIONS=halt_on_error=1`) | 54 cases, 0 failed |
| the same with `--plain` | 54 cases, 0 failed |
| Host `host-sanitize`, clang-18, `ctest -R '^(Reboot\|Web\.reboot\|ManagementProviders\.events)'` | 12 of 12 passed (with the stack clamp) |
| CI | **to be added from the PR's run** |

### M1 effort

Agent wall clock, one session, from the first read at 21:03 UTC to this file at 21:50 UTC: about 0.8 h. By phase: reading and design 0.2 h; project, shim and the fork/pthread/libc dead ends 0.3 h; script, CI step, README 0.1 h; Host baseline, timings, flake runs and mutants 0.15 h; `pending` 0.05 h. No human hours are counted. The report's estimate for Stage A was 8 to 14 h.

### M2 turnaround (measured, local, 5 runs)

| Build | Cold, median (min to max) | Warm, median (min to max) |
| --- | --- | --- |
| Host `host-sanitize` (clang-18), reboot targets and `ctest` | 3.3 s (3.2 to 4.4) | 1.6 s (1.5 to 1.6) |
| native_sim, plain, four images, 54 case runs | 46 s (44 to 46) | 29 s (28 to 30) |
| native_sim, ASan + UBSan, four images, 54 case runs | 78 s (74 to 111) | 55 s (53 to 59) |
| ratio, native_sim ASan + UBSan over Host `host-sanitize`, same suites | about 24 | about 34 |
| ratio over the whole `host-sanitize` preset (178 s local from the probe, 268 s CI run 43321893524) | 0.44 (0.29 against CI) | |

"Warm" re-runs the script with the build directories in place: CMake and Ninja run again, and the 54 cases run (about 7 s of the total; 300 concurrency case runs take 7 s).

The Host figure is configure, build of the reboot targets, and `ctest` of the 12 tests. The native figure is the whole script: four Zephyr builds and 54 process runs. Per case a native run is about 0.1 s; the time is the four image builds (each configures Kconfig and builds the kernel). The ratio therefore measures image count, not the tests.

### M3 lines by zone

`git diff --numstat origin/main...HEAD`: components, public headers, `docs/reboot-service.md`, and every existing test file: 0 changed lines. New: tests 496 lines (above), protected 116 lines (script 111, workflow step 5), docs 11 + this file.

### M4 behaviour and tests identical

`git diff origin/main...HEAD -- components web docs/reboot-service.md` is empty. No existing file under `tests/` changed. `CHECK(` counts of the sources in the images: service 108, steps 135, concurrency 37, reason 8, lock_faults 25, sleep_provider 9, `reboot_test.c` 5, pending 84: 411, as at the base. 54 of 54 Host cases have a native case.

### M5 flakiness (measured, local)

`Reboot.concurrency` (its three cases) on native_sim with ASan and UBSan: 100 runs in a row, 100 passed; 200 runs, 8 at a time, 200 passed. The same two bars on the Host binary: 100 of 100 and 200 of 200.

### M6 fault-injection parity

Every wrapped case of the Host passes on native_sim: `lock_faults` 4 of 4 (waits of 20 ms and 1000 ms, the Nth wait of a length), `pending` 9 of 9 (wait of 20 ms), `sleep_provider` (take answers: timeout, `ERR_IO`, `ERR_INVALID_ARGUMENT`, `ERR_INVALID_STATE`, `OK`), and the fake-sleep failures in `steps`. No case needed a pointer-discriminated wrap. That the wraps really reach the service was shown by the mutants below: removing the state lock fails `lock_faults` on both builds.

### M7 mutation probe (measured, local; `components/reboot/src/reboot.c` edited, run, restored)

| Mutant | Host sanitize | native_sim ASan + UBSan |
| --- | --- | --- |
| R1: the worker's `end_job` writes `svc.state` without the lock | `lock_faults` fails; `concurrency` 0 of 100 failed | `lock_faults` case 3 fails; `concurrency` 0 of 100 failed |
| R2: no state lock anywhere | `lock_faults` fails; `concurrency` 0 of 100 failed | `lock_faults` cases 0 to 3 fail; `concurrency` 0 of 100 failed |
| L1: a second worker may take a running job (`svc.running` test removed) | `concurrency` 100 of 100 failed | `concurrency` case 2 fails, 100 of 100 |

Both builds see the logic mutant and the lock-fault suite's view of a missing lock, and neither sees a data race: ASan and UBSan do not detect races, `native_sim` has no ThreadSanitizer and runs one Zephyr thread at a time, and the reboot suites have no TSan run on the Host either ([platform direction review](platform-direction-review.md), F13). So nothing the Host catches for `reboot` is lost; race coverage is missing on both.

## Gate verdict

**Stage A passes its gate.** The concurrency and fault-injection suites run on native_sim with the same sources, the same assertions, the same wraps, 54 of 54 cases, no flake at the repository's bar, and the same mutant results as the Host. The gate does not say "keep the shim (O4)".

Against the criteria of the P9 report section 3.5 (adopted as D-D), what Stage A can say:

| Criterion | Stage A |
| --- | --- |
| M4 identical | met |
| M5 flake bar | met |
| M6 fault-case parity | met |
| Test shim not larger than the Linux Sync + Thread providers (302 lines) | pthread shim 204 lines; with the per-case harness 307 lines, 5 over. The shim is what the Host's fork and pthreads needed, not a Zephyr provider |
| M2 ratio of 2 or less to `host-sanitize` | **not met on the like-for-like basis**: the reboot set takes about 24 times (cold) and 34 times (warm) the Host's seconds. Against the whole `host-sanitize` preset (178 s local, 268 s CI) the same set is 0.3 to 0.45 times. Which denominator D-D means is for the owner to say; it decides this row |
| Stage B extrapolation, 4 person-weeks or less | not measured: Stage B is not started |

## Limits

- Local numbers are from one arm64 container, Zephyr 4.4.0 and gcc 13; CI (amd64, Zephyr SDK, 4.4.2) will differ. The CI figures are not in this file.
- native_sim runs one Zephyr thread at a time and switches only when a thread blocks or sleeps (simulated time does not advance while a thread runs), so the concurrency cases interleave less than on the Host. Their assertions are the same; their power to expose a race is not stronger, and the mutants above show neither build exposes one.
- The `Web.reboot_*` route suites (3) are not run on native_sim. They link the real core on the Host and need a fake or a native_sim image of their own (decision D-C of the report).
- `Reboot.nodiscard` and the C++ header check are compile checks and stay Host only.
- LeakSanitizer at exit is off (`detect_leaks=0`), as Twister has it for these builds.
- No footprint (M8): Stage B.
