# Row C1: the bench composition of the integrated management image (RT1060-EVKB rev B)

New evidence record of [`docs/rt1060-management-image-feasibility.md`](../../../rt1060-management-image-feasibility.md#stage-2-row-c1-the-bench-composition). **Nothing here ran on a board.** Every item is a compile and link for `mimxrt1060_evk@B//qspi`, a configure-only run, or a host script.
It is not an approval of any board stage: the image still refuses to start.

| Folder | What it holds | Who ran it, where |
| --- | --- | --- |
| `build/` | The configure and build logs, the resolved `.config`, `size` and the section and segment tables, the 400 largest symbols, the link map, the SHA-256 of the artifacts, the `strings` check, and `bench-manifest.json` | The author, in the Dev Container (linux/amd64 under emulation, 2 CPUs, 2 GB, `-j2`, one build) |
| `configure-breaks/` | Six configure-only runs of the real image with one setting broken each, and their summary | The author, in the same container. Nothing was compiled. |
| `host/` | The output of `check_bench_guard.py --verbose` (every refusal, every mutant), the unit tests, and the unit-test mutations | The author, on the host (macOS, Python 3.9, CMake, Apple clang, Kconfiglib from the Zephyr checkout) |
| `commands/` | The scripts that ran. `container.sh` has `<workspace>` where the host path was. | |

## Build

| Item | Value |
| --- | --- |
| Source | The **committed tree `26433622`** (`origin/main` at `5bb75453` plus the C1 commits). `bench-manifest.json` records `source_commit`, `source_tree_modified: false`, and the SHA-256 of every source and of the 15 files that configure the image. The reviewer found that the first build's manifest was not written by the committed code; this build replaces it. |
| Zephyr | v4.4.2 `dccb0959...`, in a build-local copy with `zephyr-tls-4.4.1.patch` (`prepare-wolf-tls-zephyr.sh`), which the bench guard checked at configuration: the marker, the pin, the patch hash and the hash of `sockets_tls.c` |
| Modules | The pins of `west.yml`: wolfSSL `3c5eead4`, wolfSSH `8221e4ee`, wolfPSA `55ae0ec5`, mbedtls `a3e190fe`, tf-psa-crypto `dc575a2d` (fetched with `git fetch --depth 1`), fatfs, hal_nxp, cmsis and cmsis_6 from the earlier workspace, whose revisions match |
| Result | Configured, 551 steps, linked, three warnings, all in wolfSSH `wolfsftp.c`, as in stage 0 |
| FLASH | **601,004 B** of 8 MB (7.16 %). Stage 0, committed tree: 601,252 B. |
| RAM (SDRAM) | **837,804 B** of 32 MB (2.50 %). Stage 0: 833,708 B. The 4,096 B more are the HTTP server stack, `CONFIG_HTTP_SERVER_STACK_SIZE` 4,096 B to 8,192 B, the owner's request; the first build of this PR, with the default, was 833,708 B. ITCM, DTCM, OCRAM, OCRAM2: 0 B. |
| `size` | text 596,504, data 4,480, bss 833,254 |
| `zephyr.bin` | 601,004 B, SHA-256 `8b557969dc8c34fbb015d2f361bf3e8837af7d7cbe1623b56c87a86c8fb3a420` |
| `zephyr.elf` | SHA-256 `f6765d538a68c6132a00be08aa1508cf229c5032f647b4dc7bba6c754a1bb7fb`. `build/stack-symbols.txt` shows `CONFIG_HTTP_SERVER_STACK_SIZE` as 0x2000. |
| Public password | `strings -a zephyr.elf` finds `composition test password` **0 times**. The banner, the image identity and `192.168.77.2` are found (`build/strings-check.txt`). |

## Configure-only breaks

Each run configures the real image with `-D` of one broken setting and must stop. `configure-breaks/summary.txt` has the excerpts and `*.log` the full output. **They ran against the first head (`9bd66430`)**: the guard lines they quote are unchanged in the committed tree, which differs only in the manifest function, the extra script case, the SSH bind line and the stack.

| Run | Setting | What stopped it |
| --- | --- | --- |
| `dhcp` | `CONFIG_NET_DHCPV4=y` | Kconfig **warns** that the declaration and both selected forms were assigned `y` and dropped to `n`, **and goes on**. The CMake level stops it: `The Kconfig declaration CONFIG_CRUX_MANAGEMENT_BENCH_IMAGE is not set`. |
| `rtc` | `CONFIG_RTC=y` | Kconfig warns about `CRUX_BENCH_NO_TIME_SOURCE`. CMake: `The bench image has no time source (D7)` |
| `bind` | `CONFIG_CRUX_WEB_BIND_ADDRESS="0.0.0.0"` | CMake: `The HTTPS listener must listen on the bench address only` |
| `sshbind` | `CONFIG_CRUX_SSH_BIND_ADDRESS=""` | CMake: `The SSH listener must listen on the bench address only` |
| `ramdisk` | `CONFIG_CRUX_BENCH_FILE_RAM_DISK=y` | CMake: `The test form ram_disk (D2) is reserved and nothing implements it` |
| `unpatched` | `ZEPHYR_BASE` is the unpatched Zephyr | Stopped earlier by the module's own guard, `CONFIG_CRUX_SSH needs the Wolf profile`, because an unpatched Zephyr has no wolfSSL TLS socket backend. The bench guard's own patch check is exercised in script mode only. |

So **the Kconfig level does not refuse on its own**: Zephyr only warns about a dropped assignment. It is the CMake level that makes it a refusal.

## Host

- `host/check_bench_guard-verbose.txt`: 41 CMake and 17 C refusals with the line each printed, then 37 mutants (each guard weakened in a copy, one at a time), **37 of 37 killed**, and the control that the declared bench passes at every level.
  The first run of the mutants found two weaknesses in the script itself (a `#warning` in place of an `#error` passed because the text still appeared on stderr, and a mutant in a comment); both were fixed and the run above is after the fixes.
- `host/unittest.txt`: 14 tests pass (declaration consistency, the copy test, and `check_bench_guard.py` without the Kconfig level, which needs Kconfiglib).
- `host/unit-test-mutations.txt`: six mutations of the files, each failing a unit test, and the restored files passing.
- `host/ci-like-container-red.txt` and `ci-like-container-green.txt`: the same script and unit tests in the Dev Container (CMake 3.28.4, Python 3.12.3, no Kconfiglib), which is what CI's `host` job looks like. **The first push failed there**: `bench_guard.cmake` used `IN_LIST`, and a script run with `cmake -P` and no `cmake_minimum_required` does not have policy CMP0057, so five CMake cases and one unit test failed (the red file). The macOS CMake 4 did not show it. The fix is `cmake_policy(SET CMP0057 NEW)` in `bench_guard.cmake` and `cmake_minimum_required(VERSION 3.20.0)` in each script the checker writes (the green file). Without Kconfiglib the checker skips the Kconfig level and says so (`32 of 37 killed, 5 skipped`).

