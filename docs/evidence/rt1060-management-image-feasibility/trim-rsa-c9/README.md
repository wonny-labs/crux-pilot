# Evidence of row C9: RSA trimmed from the RT1060-EVKB bench image

Record: [`docs/rt1060-bench-trim-rsa.md`](../../../rt1060-bench-trim-rsa.md). Nothing here ran on a board. Build directories were removed after each build; the link maps, size reports, symbol tables, resolved configurations, manifests and logs are kept, and the ELF and binary are not.

| Folder or file | What it holds |
| --- | --- |
| `build/base/` | The bench image of `origin/main` at `4794f4dd`: `size`, sections, resolved configuration, manifest, `nm -S --size-sort` listing, link map, static stack chains (by name, and by address), configure and build logs, the SHA-256 of `zephyr.bin` and `zephyr.elf`. |
| `build/after/` | The same for this branch at `daa65dc4`, built from a clean detached checkout of that commit (`source_tree_modified: false` in the manifest). `build.log` holds the line of the symbol check that ran after the link and passed. The stack-chain files in this folder are from the first build of the after tree: its `zephyr.bin` has the same SHA-256 as the one here, and only the ELF's build-dependent bytes differ. |
| `build/first-after-build/` | The first build of the after tree, whose post-link check failed on five names (`psa_asymmetric_sign_rsa` and four PEM labels). The failing lines only. The allowance by kind and size followed from it. |
| `build/config-diff.txt` | The resolved configurations differ in one line: `CONFIG_WOLFSSL_RSA=y` to not set. |
| `build/symbol-diff.txt`, `build/map-diff.txt`, `build/stack-chains-diff.txt` | The diffs that the record quotes, made by `commands/symbol_diff.py`, `map_diff.py` and `chain_diff.py`. |
| `native/entropy-plain/`, `native/entropy-norsa/` | `scripts/test-wolf-entropy.sh all` on `native_sim`, unmodified and with `-DCONFIG_WOLFSSL_RSA=n` (`script.diff` is the one changed line), the console of each variant and the resolved RSA setting. |
| `native/ssh-norsa/` | `scripts/test-ssh-integrated.sh all` with the image built with `CONFIG_WOLFSSL_RSA=n`: log, the driver's `integrated-results.json` (86 checks, 0 failures), the symbol check on `zephyr.exe`. |
| `other-composition/` | A non-bench composition for the same board (`tests/zephyr/ssh_provider` with `rt1060.conf`, RSA on), built from the base tree and from the after tree: the two `zephyr.bin` files have the same SHA-256, and the resolved configuration and the symbol table are identical. |
| `host/` | The red run of the guard script (`check_bench_guard-red.txt`), the green run with its mutants, the unit tests, the boundary scripts, the symbol check on both ELFs, and the host probe of wolfCrypt with and without `NO_RSA`. |
| `commands/` | The scripts that ran. `<workspace>` stands for the folder that holds the repository, its worktrees, the west workspace and the module trees. The first-account password file was a throwaway file outside every repository. `run_*.sh` take the build lock with `mkdir`, start one container with the module trees and the patched Zephyr copy of the C1 build mounted read-only, and release the lock. |

The build lock was held by an earlier run of these scripts for about 17 minutes: the first wrapper wrote a file into the lock folder and its `rmdir` then failed. The wrapper no longer writes into it.
