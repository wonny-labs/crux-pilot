# Row C8: the evidence of the bench image plan

The record is [`docs/rt1060-bench-image-plan.md`](../../../rt1060-bench-image-plan.md). Refs #183. **Nothing here ran on a board, and nothing here is an approval.** This folder holds one real cross-build of the bench image of `origin/main` at `9882ebdfe2f30e0dc3b5b4373a2e1bafff291239`, host reads of it, and host-only checks of client commands. The image **refuses to start** (`run_refused: true`): it is the image of the main branch as it is, not the image a board run would program.

## What ran, and who ran it

| What | Result | Who |
| --- | --- | --- |
| One cross-build for `mimxrt1060_evk@B//qspi` of a clean detached checkout of the commit, all bench forms set, a throwaway first-account password file outside every work tree, C1's patched Zephyr copy and the module trees read-only | Linked: Flash 562,232 B, RAM 2,172,076 B, the guards passed inside the configure, 4 warnings (wolfSSH `wolfsftp.c` and wolfPSA `psa_key_storage.c`) | The author, in the Dev Container |
| The same build once more, from the same tree and the same file | The same `zephyr.bin` and `zephyr.elf` SHA-256 and the same manifest ([`build/sha256.txt`](build/sha256.txt), [`build/sha256-run1.txt`](build/sha256-run1.txt)) | The author |
| C4's scan of the build, the repository and the logs | Clean with `--allow-in-image` and `--expect-in-image`; without the allowance it lists exactly the five image files ([`build/scan-bench.txt`](build/scan-bench.txt), [`build/scan-bench-no-allowance.txt`](build/scan-bench-no-allowance.txt)) | The author |
| The first run's scan | **A false positive**: it flagged C6's command file, which holds the same in-container path as the password file. The file moved to another path and the build ran again ([`host/scan-first-run-false-positive.txt`](host/scan-first-run-false-positive.txt)) | The author |
| `strings` on the image | The public test password 0 times; the commit prefix 0 times; the banner string and the image id once each ([`build/strings-checks.txt`](build/strings-checks.txt), [`build/uart-strings-in-bin.txt`](build/uart-strings-in-bin.txt)) | The author |
| The recorder's own functions on the build: the source snapshot of the clean tree and the firmware identity | [`build/source-snapshot.json`](build/source-snapshot.json), [`build/firmware-identity.json`](build/firmware-identity.json). **The full freeze was not run**: it needs a west workspace with the crypto group on, a build directory inside the repository, `rom.json` and `ram.json`, and a way to see the patched Zephyr copy (blockers B2 of the record). | The author |
| The read-only probe of the profile input, on the built BIN, ELF and `.config` | [`host/profile-facts.out`](host/profile-facts.out): geometry passes, the M5 and the second profile's checks refuse, 17 of 28 M5 lines hold, the 65 manifest settings all equal the `.config`, the stack objects, a candidate profile constructs and its 600 s window is refused | The author, on the host |
| Client commands against a local `openssl s_server` (TLS 1.3 only) | [`host/local-client-checks.txt`](host/local-client-checks.txt). The bench link and the bench address were not touched. | The author, on the host |
| Independent review | See the PR | A fresh read-only Opus reviewer |
| **Not run** | A board, a probe, a serial port, any freeze, any plan or approval document, `sudo`, SSH or SFTP against anything, the CI workflows | |

## Files

| File | What |
| --- | --- |
| `build/bench-manifest.json` | The manifest as the build wrote it |
| `build/sha256.txt`, `build/sha256-run1.txt`, `build/file-sizes.txt`, `build/manifest-sha256.txt` | Hashes and sizes of the BIN and the ELF, both runs, and the SHA-256 of both runs' manifests (equal) |
| `build/size.txt`, `build/size-sections.txt`, `build/size-run1.txt`, `build/memory-report.txt` | `size`, section sizes, the linker's memory report |
| `build/config.txt`, `build/zephyr.map.gz` | The resolved configuration, the link map (compressed). They hold no password. |
| `build/configure.log`, `build/build.log` | Whole logs of the second run (container paths only) |
| `build/dts-chosen.txt`, `build/dts-trng.txt` | The device tree's chosen nodes and the TRNG node, from which the first-read line's `source` and `compatible` come |
| `build/stack-symbols.txt` | The stack objects of the linked image (`nm -S`) |
| `build/source-snapshot.json`, `build/firmware-identity.json` | The recorder's source snapshot and firmware identity |
| `commands/` | The container, the build script and the runner, with the host's paths replaced by `<workspace>` and `<scratch>`. The script ran under the name `build2.sh` (the second run; the first was the same script without the lines that keep the BIN and the device tree). |
| `host/` | The probe, its output, the client checks, the first run's false positive |

## Which values are illustrative

A stage image is built **again**, from a pinned commit with the refusal lifted and with the owner's own password file: the hashes, sizes, the firmware identity, the password file's hash, the commit, the snapshot and the manifest's source hashes are then different. [The table in the record](../../../rt1060-bench-image-plan.md#what-a-board-image-rebuilds) says which are illustrative and which are procedures to repeat.

## Redactions

Container paths (`/ws/...`, `/build/...`) stay. The host's paths in the commands are `<workspace>` and `<scratch>`. The throwaway password file was deleted with the build directories after the last scan. No serial number, bench password, employer or product name is in this folder.
