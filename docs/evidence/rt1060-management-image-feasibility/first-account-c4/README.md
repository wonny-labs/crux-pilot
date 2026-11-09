# Row C4: the first account of the RT1060-EVKB bench image

New evidence record of [`docs/rt1060-bench-first-account.md`](../../../rt1060-bench-first-account.md). **Nothing here ran on a board.** Every item is a Host test, a host script, a configure-only run, or a compile and link for `mimxrt1060_evk@B//qspi`. The image still refuses to start.
**No password is in this record**: every password was generated at run time, outside the repository, and the scan below searched this folder for it and for its encodings. The hash in `build/bench-manifest.json` is the SHA-256 of a throwaway file that no longer exists.

| Folder | What it holds | Who ran it, where |
| --- | --- | --- |
| `build/` | The manifest, `size`, the section table, the symbols of the first account and their section, the SHA-256 of the artifacts, the resolved settings that matter, the first full build log (`first-build.log`) and the incremental rebuild (`rebuild.log`, `reconfigure.log`), and the excerpts of four refused configurations (`configure-refusals.txt`) | The author, in the Dev Container (linux/amd64 under emulation, 2 CPUs, 2 GB, `-j2`, one build at a time under the shared lock) |
| `host/` | `scan-bench.txt` (the scan of the build, the repository and the logs), the Python tests, `check_bench_guard.py --verbose`, the mutants of the scan tool and of the step, the Host Access suite with its sanitizer run | The author, on the host (macOS, Python 3.9, CMake, Apple clang) and in the Dev Container (Python 3.12.3, GCC) |
| `commands/` | The scripts that ran. `<workspace>` and `<folder outside any repository>` stand where host paths were. | |

## What ran, in order

| # | Run | Tree | Result |
| --- | --- | --- | --- |
| 1 | Container run 1: Python tests, four configure-only refusals, the bench build | `75f0f5ed` | **89 tests pass** in the container (`host/python-tests-in-container.txt`). Four refusals as in `build/configure-refusals.txt`. The build linked: Flash 601,744 B, RAM 837,804 B, 553 steps, the same 3 wolfSSH warnings (`build/first-build.log`). The Host Access suite in the same run **failed to compile**: my compile line lacked `_GNU_SOURCE` for the sync port. Fixed in `commands/c4_host.sh`. |
| 2 | Container run 2: clang-format 18, the Host suite, the same under ASan and UBSan | `75f0f5ed`, then formatted | **ASan found an overflow of a buffer in my test** (a 129-character password and its terminator in 129 bytes). Fixed, and a second `snprintf` buffer after GCC's truncation warning. Then **156 checks, 0 failures, plain and sanitized** (`host/host-suite-and-format.txt`). |
| 3 | Commit of the formatted tree | `304206cc` | |
| 4 | The Mac restarted (the main session's notice). The queued run was lost, and a run of the scan tool's mutants that changes the tool in place was cut off. | | One line of the tool was left mutated in the work tree. It was found by `git status` and restored from the commit before anything was built or run from it. |
| 5 | Container run 3, after the restart: mutants of the step on the Host suite, then reconfigure and incremental rebuild with a **new** password file | `304206cc` | The rebuild linked with **the same Flash and RAM** (formatting only), and the manifest has `source_commit` `304206cc` and `source_tree_modified: false`. The mutant part stopped at a pattern that clang-format had wrapped. |
| 6 | Mutants of the step again, patterns fixed (`host/step-mutants.txt`) | `304206cc` | **9 of 11 killed. Two survive, and both are equivalent**: the step's upper length limit and its null-user test repeat what Access checks itself. |
| 7 | The scan of the final build, the repository, this folder and the logs | `304206cc` | `host/scan-bench.txt`: no plaintext and no encoding outside the image files; the raw bytes **once in `zephyr.elf` and once in `zephyr.bin`**; the manifest hash equals the file's hash; with no allowance it lists exactly the five image files |

The mutants of the scan tool (`host/scanner-mutants.txt`, 13 of 13 killed) were run again on a copy of the two files, not on the work tree.

## After the independent review

The first review (REVISE) found two errors in the documents, and no flaw in the secret handling. Both were fixed in the documents (the reason against option B was wrong, since B would keep the plaintext out of the ELF, and the RAM-disk statements are now conditional on row C3, which is not merged). The tool also now refuses a semicolon in the path and reads at most one byte more than its limit, and a test pins the Host suite's account to the declaration. These changes do not touch the image's sources. `build/` and `host/scan-bench.txt` are from the tree `304206cc`, and `build/post-review-*` and `host/post-review-*` are the rebuild and the scan of the tree after those changes The rebuild at `e3bad2b6` gave **the same `zephyr.elf` and `zephyr.bin` SHA-256 values** and the same sizes, the manifest says `source_commit` `e3bad2b6` and `source_tree_modified: false`, 90 Python tests pass in the container, and the scan is clean (`host/post-review-scan.txt`).

The CI `host` job then **failed at `868f393e`**: my tests imported the tool, Python 3.12 wrote a `__pycache__` into the image's folder, and the C1 test `test_the_public_test_password_and_the_native_declaration_are_not_in_the_bench_image` read the bytecode as text (`UnicodeDecodeError`). The host runs did not show it because this Mac sets `PYTHONDONTWRITEBYTECODE=1`, so no bytecode was ever written there, and the container run used a copy of the tree that is mounted read-only. Both of my test files now set `sys.dont_write_bytecode` before the import, and no `__pycache__` is created in the folder. The C1 test is unchanged.

## The merged tree (C4 with C5)

Row C5 merged into `main` while this PR was open (`f4286f6e`), and it changes `main.c` and `management_target.cmake`, which this row's files sit beside. The merge (`a6181ffa`) had one conflict, in the feasibility document's table of rows, which was resolved by keeping both rows' "Done" cells. A fresh configure and build of the merged tree linked: **Flash 602,204 B (7.18 %), RAM 837,932 B (2.50 %)**, 554 steps, the same 3 wolfSSH warnings, 96 Python tests pass in the container, `crux_entropy_boot_check` and `crux_bench_first_account` are both in the image, the manifest says `source_commit` `a6181ffa` and `source_tree_modified: false`, the public test password is 0 times in the ELF, and the scan is clean (`host/merged-scan.txt`, `build/merged-*`). The difference to the C4-only build (601,744 B and 837,804 B) is C5's.

## Image facts (`build/`)

| Item | Value |
| --- | --- |
| FLASH | 601,744 B (7.17 %). C1: 601,004 B. |
| RAM (SDRAM) | 837,804 B (2.50 %), the same as C1 |
| `size` | text 597,244, data 4,480, bss 833,254 |
| `crux_bench_first_account_password` | 25 bytes, `0x6007f798`, section `rodata` (`FLASH`), type `R` |
| `zephyr.bin` | 601,744 B |
| Public test password in `zephyr.elf` | `strings -a` finds it 0 times |
| Settings | `.config` has `CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE=y`, the declaration, `ENTROPY_MCUX_TRNG=y`, no `RTC` (`build/config-excerpt.txt`) |

The ELF and binary SHA-256 values are in `build/sha256.txt`. They depend on the password file's bytes.

## Not covered

A board; the real PBKDF2 for this step (the Host suite uses Access's fake ports); `native_sim`; the CI workflows (run by the PR).
