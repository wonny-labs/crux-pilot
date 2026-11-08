# Evidence of row C5: the Wolf profile's entropy failure path

Record of [`docs/rt1060-wolf-entropy.md`](../../rt1060-wolf-entropy.md). `native_sim`, Host and a compile for `mimxrt1060_evk@B//qspi` only. Nothing ran on a board.

| Folder | What it holds |
| --- | --- |
| `red/` | The `runtime` image **with the stub boot check** (the test-first step): 3 consumer cases pass (the Wolf profile already fails closed), the 2 boot-check cases fail. Console, resolved entropy configuration, configure and build logs. |
| `green/` | The same images with the real boot check: `runtime` (5 of 5), `init-fails` (2 of 2), `fallback` (control, 1 of 1), `guard` (control, build refused). `first-attempt-all-variants-console.txt` is the first run of all four variants, in which the `fallback` control did not configure (see below). |
| `bench/` | The compile of the bench image for `mimxrt1060_evk@B//qspi` with the check wired in: size, the resolved entropy configuration, the manifest, the entropy symbols, and the call sites in the linked ELF. |
| `commands/` | The container and build commands that ran, with the workspace path replaced. |

## Attempts that did not count

| Attempt | What went wrong | Fix |
| --- | --- | --- |
| First runs | The container had no `cmake` (the cache volume was not mounted), then the PSA Kconfig choice needed the `mbedtls` and `tf-psa-crypto` modules to be found | Wrapper and script module list |
| `fallback` control, first form | `ENTROPY_GENERATOR=n` while the test device selects the driver symbols: a Kconfig error | The control has no test device |
| `fallback` control, second form | `ENTROPY_GENERATOR` on with no chosen entropy node: Zephyr's own `random_entropy_device.c` does not compile | The control is the state `ENTROPY_GENERATOR` off, which is the only one where wolfCrypt's fallback is reachable |

None of these changed a result of the cases.
