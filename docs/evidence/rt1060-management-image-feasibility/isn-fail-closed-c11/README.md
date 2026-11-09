# Row C11: the evidence of the fail-closed TCP initial sequence number

The record is [`docs/rt1060-isn-fail-closed.md`](../../../rt1060-isn-fail-closed.md). Refs #183. **Nothing here ran on a board, and nothing here is an approval.** The folder holds the red and green runs on `native_sim`, the host checks of the patch and of the guards, the Zephyr copies, and two real cross-builds of the bench image for `mimxrt1060_evk@B//qspi` (before and after).

## What ran, and who ran it

| What | Result | Who |
| --- | --- | --- |
| `tests/native/isn_fail_closed/test_prepare.py` before `scripts/prepare-isn-fail-closed.py` existed | Red: 10 of 10 errors ([`host/test-prepare-red-no-script.txt`](host/test-prepare-red-no-script.txt)) | The author, on the host |
| The same, with the patch and the script | Green: 10 of 10 ([`host/test-prepare-green.txt`](host/test-prepare-green.txt)) | The author, on the host |
| The native_sim test on the **unpatched pinned tree** | **Red, as intended**: with a failed read the stack answers a SYN with a SYN\|ACK and a connect sends a SYN, and the sequence number is never made from the key, not even after the read works ([`native-sim/unpatched-run.txt`](native-sim/unpatched-run.txt)) | The author, in the Dev Container |
| The same test on a **copy with the patch** | **Green**: RST for the SYN, `-EIO` and no SYN for the connect, keyed numbers after the read works again ([`native-sim/patched-run.txt`](native-sim/patched-run.txt)) | The author, in the Dev Container |
| `check_bench_guard.py` with the new cases against the **original** guard files | Red: every new case fails, in CMake, in C and in Kconfig ([`host/check-bench-guard-red-original-guards.txt`](host/check-bench-guard-red-original-guards.txt)) | The author, on the host |
| `check_bench_guard.py` with the new guards | Green: CMake, C, Kconfig ok, **127 of 127 mutants killed** (104 before this row, 23 new) ([`host/check-bench-guard-green.txt`](host/check-bench-guard-green.txt)) | The author, on the host |
| The Zephyr copies | The TLS patch leaves `tcp.c` as pinned; the new script patches it to the declared hash, is a no-op the second time and refuses a Git checkout ([`copy/`](copy/)) | The author, in the Dev Container |
| The real configure of the bench image on a copy with the TLS patch only | **Refused** by the new guard ([`copy/control-tls-only-configure.log`](copy/control-tls-only-configure.log)) | The author, in the Dev Container |
| Two cross-builds: **before** (`origin/main` `52e894e8`, TLS patch only) and **after** (this branch, both patches), each with a throwaway first-account password file outside every repository, deleted afterwards | Both link, the guards passed inside the configure, manifest `source_tree_modified: false`, the same `zephyr.bin` and `zephyr.elf` SHA-256 in two runs of each ([`build/`](build/)) | The author, in the Dev Container |
| The call-site scan and the disassembly of `tcp_init_isn` in each ELF | Before: the result of `sys_csrand_get` is never read and the hash buffer is 16 bytes. After: the result is tested before the key is marked as read, and the buffer is 32 bytes ([`build/*/disasm-tcp_init_isn.txt`](build/after/disasm-tcp_init_isn.txt)) | The author |
| C4's scan for the throwaway password | Clean in both builds ([`build/*/scan-bench.txt`](build/after/scan-bench.txt)) | The author |
| Independent review | See the PR | A fresh read-only Opus reviewer |
| **Not run** | A board, a probe, a serial port, `sudo`, SSH or SFTP against anything, the CI workflows (the PR's CI does that), IPv6 at run time, wolfPSA at run time on `native_sim` | |

## Files

| File | What |
| --- | --- |
| `native-sim/` | The two runs (`*-run.txt`), the exit codes, the resolved settings, the configure and build logs, the hash of `tcp.c` that each built |
| `host/` | The red and green runs of the host checks |
| `copy/` | `prepare-tls.txt`, `prepare-isn.txt`, `prepare-isn-again.txt`, `prepare-isn-refuses-checkout.txt`, `markers.txt`, `tls-copy-hashes.txt`, `bench-copy-hashes.txt`, the control configure log |
| `build/before/`, `build/after/` | `size.txt`, `size-sections.txt`, `sha256.txt` (second run) and `sha256-run1.txt`, `file-sizes.txt`, `bench-manifest.json`, `isn-config.txt` (the resolved settings), `call-sites.txt`, `disasm-tcp_init_isn.txt`, `scan-bench.txt`, `configure.log`, `build.log`, `source.txt` |
| `commands/` | The container, the runner, the script of all steps and the build script, with the host's paths replaced by `<workspace>` and `<scratch>` |

## Redactions

Container paths (`/ws/...`, `/build/...`) stay. The host's paths in the commands are `<workspace>` and `<scratch>`. The throwaway password file was deleted with the build directories. The manifests hold the SHA-256 of that file and never the password. No serial number, bench password, employer or product name is in this folder.
