# Row C2: the UART report of the bench image (RT1060-EVKB rev B)

New evidence record of [`docs/rt1060-management-image-feasibility.md`](../../../rt1060-management-image-feasibility.md#stage-2-row-c2-the-uart-report). **Nothing here ran on a board.** Every item is a host run, a `native_sim` run, or a compile and link for `mimxrt1060_evk@B//qspi`. It is not an approval of any board stage: the image still refuses to start.

| Folder | What it holds | Who ran it, where |
| --- | --- | --- |
| `build/` | For the bench image of this change (`c2-*`) and for `origin/main` at `a47c1590` (`base-*`): configure and build logs, the resolved `.config`, `size` and the section table, the SHA-256 of the artifacts, and `bench-manifest.json`. Also `c2-symbol-diff.txt` (symbols new, gone and changed against the base), `c2-symbols.txt` and `c2-static-chains.json` (static stack chains of the printing path). | The author, in the Dev Container (linux/amd64 under emulation, 2 CPUs, 2 GB, `-j2`, one build at a time, under the shared build lock) |
| `native/` | The `native_sim` run of `tests/native/bench_report` with `-no-rt` (`run-no-rt.txt`), its build and configure logs, and the checker's verdict on reports 1 to 3 and on all four | The author, in the same container |
| `host/` | The C host test and its sample report, the checker on that sample, the unit tests (verbose), `check_bench_guard.py --verbose`, the three boundary scripts, and the mutant runs of the C code (`c-mutants.txt`) and of the checker (`checker-mutants.txt`) | The author, on the host (macOS, Apple clang, Python 3.9) |
| `commands/` | The scripts that ran. `container.sh`, `run_all.sh` (first run: native_sim, the bench image, and the base build) and `run_final.sh` (the run at the final tree, after the review) have `<workspace>` where the host path was. `c-mutants.py` and `checker-mutants.py` produce `host/c-mutants.txt` and `host/checker-mutants.txt`; `host/c-mutants-first-run.txt` is the first run of the C mutants, in which two survived and two test cases were then added. | |

## Build

| Item | Value |
| --- | --- |
| Source | The committed tree **`75934e04`** (`origin/main` at `eefe3811` merged into the C2 branch; the merge reaches nothing in the image), built from a clean worktree of that commit. The first build, from `ff1946db`, was replaced by this one after the review. `build/c2-manifest.json` says `source_tree_modified: false`. The base build is `a47c1590`, also `false`. |
| Zephyr, modules, toolchain | As for C1: Zephyr v4.4.2 in the build-local copy with `zephyr-tls-4.4.1.patch` (read-only, from the C1 build folder, its guard checked by the configure), the `west.yml` pins of the crypto modules, Zephyr SDK 1.0.1, GCC 14.3.0 |
| FLASH, C2 and base | **606,012 B** and 601,004 B: **+5,008 B** |
| RAM (SDRAM), C2 and base | **842,540 B** and 837,804 B: **+4,736 B** |
| `size` text, data, bss | 601,328, 4,668, 837,802 and 596,504, 4,480, 833,254 |
| Steps, warnings | 557 and 551. Three warnings in each, all in wolfSSH `wolfsftp.c`. |
| `zephyr.bin` SHA-256 | C2 `459cab7dff385730259946cb1eb916591ce20d345e070d18ab4008594cf551fe`, base `8b557969dc8c...`, which is the hash that C1 recorded for its own build |
| Resolved configuration | `CONFIG_HTTP_SERVER_STACK_SIZE=8192`, `CONFIG_NET_STATISTICS_PER_INTERFACE=y`, `CONFIG_NET_STATISTICS_TCP=y`, `CONFIG_CRUX_SSH_STACK_REPORT=y`, `CONFIG_SYS_HEAP_RUNTIME_STATS=y` (`build/c2-config.txt`). The manifest records the HTTP stack, the main stack, the arena and the new symbol. |
| `strings -a` | `composition test password` 0 times. The `BENCH-` marker strings of the report are present. |

## What the build does not say

The static chains (`c2-static-chains.json`) follow direct calls only, and the `printk` chain stops at function pointers: 528 B for the printing thread is an estimate and not a measurement. The stack and heap figures of a real run exist only after a board run reads them.

The `native_sim` run's executable never exits by itself: after `BENCH_REPORT_TEST complete` the report thread keeps running, and the driver stops it after 120 s of wall time (exit status 124 in the run's log). The output was complete by then.

## After the merge of C5 (#365)

`origin/main` gained C5 (`f4286f6e`), which adds the entropy boot check to `main.c` and `management_target.cmake`. The branch merged it (`23493da0`) with no conflict, and the merged tree was compiled and linked once more (`build/merged-c5-*`): **FLASH 606,472 B, RAM 842,668 B**, `source_tree_modified: false`. The author also saw three warnings as before and the public test password 0 times in that run's output, which was not kept: only the size record, the memory lines, the hashes and the manifest are in this folder. That is 460 B of Flash and 128 B of RAM more than the C2-only tree (606,012 B, 842,540 B), which is C5's own cost. The C2 deltas in the document are against `a47c1590` and unchanged.
