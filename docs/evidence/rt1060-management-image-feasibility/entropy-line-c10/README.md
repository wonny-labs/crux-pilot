# Row C10: the entropy line before the refusal (D-a)

**What this is.** `main()` of the RT1060-EVKB bench image now calls `crux_entropy_boot_check()` right after the banner and **before** the refusal (`run_refused`). The refused image therefore prints the banner, one `CRUX-ENTROPY` line, then its refusal. The refusal still stops the address, the store, HTTPS and SSH. Owner decision of 2026-11-09, quote: "1-8까지 너의 추천대로 진행해줘". **Nothing here ran on a board, on a serial port or with a J-Link.** The only runs are host unit tests, the guard mutation script, and one cross-build.

## Result

| Item | Result |
| --- | --- |
| Source of the build | commit `d051ccf564b2` (code identical to the head of the PR: the later commits only rebase onto newer `main` and add this folder), clean, `source_tree_modified: false`, `run_refused: true` in the manifest |
| Flash / RAM (`size`) | text 557,864 + data 4,348 B; bss 2,167,650 B. Zephyr's regions: **FLASH 562,232 B (6.70 %)**, **RAM 2,172,076 B (6.47 %)**. `zephyr.bin` 562,232 B |
| Against row C8's build | **Identical** `size` line to the byte (the move changes call order, not code size): `docs/evidence/rt1060-management-image-feasibility/image-plan-c8/build/size.txt` |
| Reproducible | **One build is kept** (`build/sha256.txt`; the hashes depend on the throwaway password file, which is deleted). A first build of the same tree, made before the call-order listing was added to `build.sh`, gave a `sha256.txt` that `cmp` found byte-identical; its copy was not kept, so this rests on the author's run and not on a file here |
| Stage-2 guards in that build | CMake, C and Kconfig guards passed at configure and compile; `check_bench_rsa_symbols`: 5,554 defined symbols, none RSA code; the password scan is clean (`build/scan-bench.txt`, exit 0); the public test password is 0 times in the ELF and the BIN |
| `check_bench_guard.py` | **104 of 104 mutants killed**, the control passes (`host/check-bench-guard.txt`) |
| Order in the compiled image | `build/bench-main-calls.txt`: the calls of `main()` (with `bench_main` inlined) are `printk` (banner, twice), then `crux_entropy_boot_check`, and only after it the address calls (`net_if_*`), the store, the first account, the services and SSH. The compiler lays out the failure path and `crux_native_management_start` at the end of the function, so this is a layout, and the order that counts is the source order pinned by the tests |
| UART strings in the BIN | `build/uart-strings-in-bin.txt`: banner, both refusal lines and the `CRUX-ENTROPY` format are present |

## Red, then green

| Step | Result |
| --- | --- |
| Red | `tests/build/test_wolf_entropy_boot.py` with the new order and the old `main.c`: the order test failed with `[34, 185, 48, ...] != [34, 48, 185, ...]` (the refusal at 185, the check at 48) (`host/red-unittest.txt`). `test_wolf_entropy_boot.py` later got one more test (review finding): banner, check and refusal must be adjacent and unconditional, and `run_refused` is read once; it kills two mutants that the order test missed (the check wrapped in `if (!run_refused)`, and an early `if (run_refused != 0) return 1;`). `test_management_bench_declaration.py` got stricter assertions (the refusal precedes the address, store, first account, services, SSH and report, and returns 1); they pass on both orders, because C1 already had that |
| Green | After the move of the call in `main.c`, the bench-related test files pass (`host/green-unittest.txt`) |

## Not covered

| Item | Why |
| --- | --- |
| A run of the image | No board work in this row. What a refused image prints is read from the source and the BIN strings, not from a UART |
| `native_sim` | No existing test runs the bench `main()` (it needs the board's devices); `scripts/test-wolf-entropy.sh` tests the check itself and `entropy_boot.c` is unchanged |
| The whole `tests/build` suite on this Mac | 58 failures and 6 errors, all in Linux-port, consumer, debug-target, time and provenance tests that need Linux; none names a bench, entropy or declaration test (`host/macos-host-failures-by-class.txt`). The Linux CI jobs are the judge |

## Files

| File | Content |
| --- | --- |
| `commands/container.sh`, `commands/build.sh`, `commands/run.sh` | The mounts and the build of one container run (scratch and workspace paths are placeholders); the lock was taken for the run and released |
| `build/` | Size reports, manifest, configuration, map (gzip), hashes, scan, symbols and strings of the image |
| `host/` | Red and green runs, the guard script, the macOS failure classes |
