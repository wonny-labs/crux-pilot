# Row C6: the network buffers, the TCP windows and the heap arena of the RT1060-EVKB bench image

New evidence record of [`docs/rt1060-bench-network-buffers.md`](../../../rt1060-bench-network-buffers.md). **Nothing here ran on a board.** Every item is a host script, a Dev Container run, a `native_sim` build and run of the report's kernel side, or a compile and link for `mimxrt1060_evk@B//qspi`. The sizes the record supports are paper rules. The image still refuses to start.

| Folder | What it holds | Who ran it, where |
| --- | --- | --- |
| `build/` | For `origin/main` at `616d9437`, the image without RSA (`before.*`), and for the C6 merge commit `d2005b22` (`after.*`; the RSA symbol check of row C9 passed on both, `*.rsa-symbols.txt`): the configure and build logs, `size` and the section sizes, the sizes of the network symbols, the resolved network settings, the manifest, the SHA-256 of the artifacts and the link map | The author, in the Dev Container (`crux-pilot-devcontainer:local`, amd64 emulation), one build at a time under the shared lock |
| `host/red1-…`, `red2-…`, `red3-…`, `red4-…` | The red runs, in order: the guard script before the guard existed (the 39 cases of that first version, none refused), the script with the guard and the old `target.conf` (the real configuration refused), the declaration test on the old `target.conf`, the report tests before the formatter and the checker had the pool lines | The author, on the host |
| `host/check_bench_guard-verbose.txt` | Every refusal of the three guard levels with the line each printed, the controls at the need, then 104 mutants, **104 of 104 killed** | The author, on the host (macOS, Python 3.9, CMake 4.4, Apple clang, Kconfiglib from the patched Zephyr copy) |
| `host/report-mutants.txt` | 18 mutants of `check_bench_uart.py` and 13 of the formatter, **31 of 31 killed**, after an unmutated control (the first run killed 21 of 26: 4 survived and 1 did not compile, and the tests and the mutant were fixed, not the rules) | The author, on the host |
| `host/unittest.txt` | 160 tests of the bench test files after the merge with C9 (declaration, target lists, report, RSA symbols, medium, first account) | The author, on the host |
| `host/ci-like-container.txt` | At `522b6d0d`, before the review's fixes (not repeated after them: CI is the check): the 60 tests of that tree, the guard script without its Kconfig level, and `scripts/check-format.sh` with clang-format 18.1.8, in the Dev Container (CMake 3.28.4, Python 3.12.3): what CI's `host` job looks like. **The docs step did not run**: it needs its pinned lychee, a download that was not approved. | The author, in the Dev Container |
| `host/linkcheck.txt`, `commands/linkcheck.py` | The relative links and anchors of the two changed documents, checked by a small script because the docs step could not run | The author, on the host |
| `native/` | `tests/native/bench_report` (the generic report core, which the image shares) built on `native_sim/native/64` with the C6 change and a fake network read (made-up pools and counters; its third read fails): no warning, the run, the checker over it (report 3 has no pool rows by design, report 4 is the table-overflow report, as in C2), and `native-mutants.txt`: two mutants of the core's network lines, each killed by this run | The author, in the Dev Container |
| `commands/` | `container.sh` with `<workspace>`, `<scratch>` and `<folder outside any repository>` where host paths were; `build.sh`, `native.sh`, `hostci.sh`, `run_before.sh`, `run_after.sh`; `report-mutants.py`; `linkcheck.py` | |

## Build

| Item | Value |
| --- | --- |
| Zephyr | The build-local copy of D8 (`VERSION` 4.4.2, `zephyr-tls-4.4.1.patch`), read-only, which the guard checks at configuration |
| Modules | The pins of `west.yml`, read-only, as in the records of C1 to C5 |
| First account | A password file made by `bench_first_account_file.py generate` in a folder outside every repository, mounted read-only. Its path and content are in none of these files. |
| FLASH | **561,340 B** before, **562,232 B** after (+892 B) |
| RAM (SDRAM) | **1,365,740 B** before, **2,172,076 B** after (+806,336 B) |
| Warnings | The same four in both: three in wolfSSH `wolfsftp.c`, one in wolfPSA `psa_key_storage.c` (unused after the RSA trim) |
| Source tree | Before: `616d9437`, `source_tree_modified: false`. After: `d2005b22`, `source_tree_modified: false`. Earlier builds of this change, at `ccac6aa7` and `55ead852` against `4794f4dd` (before row C9), gave +888 B of Flash and +806,272 B of RAM; they are replaced here by the builds on the merged tree |

## What is not here

| Item | Why |
| --- | --- |
| A board, a client, a pool figure read on a target | No hardware in this stage |
| The docs check | It needs a download that was not approved |
| The CI workflows | `.github/` is protected and not changed |
| The retained ELF and binary | As in the other records: the binary is not reproducible from another path and `*.bin` is ignored by the repository |
