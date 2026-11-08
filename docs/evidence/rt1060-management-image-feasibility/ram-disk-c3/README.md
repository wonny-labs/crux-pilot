# Row C3: the RAM disk file backend and the mount rule of the RT1060-EVKB bench image

Evidence record of [`docs/rt1060-bench-file-backend.md`](../../../rt1060-bench-file-backend.md). **Nothing here ran on a board or on a card.** The image still refuses to start.

| Folder | What it holds | Who ran it, where |
| --- | --- | --- |
| `host/check_bench_guard-verbose.txt` | Every refusal of the three guard levels with the line each printed, then the mutants (each guard weakened in a copy, one at a time). Includes the file backend: 13 CMake and 12 C refusals added by C3, and the microSD state of the declaration accepted as a control | The author, on the host (macOS, Python 3.9, CMake, Apple clang, Kconfiglib from the Zephyr checkout) |
| `host/unittest.txt` | `test_management_bench_medium.py` (the mount rule with fake devices, the glue per backend, the objects, the source scan, the sizing constants), `test_management_bench_declaration.py`, `test_management_target_lists.py` | The author, on the host |
| `host/fatfs_host_size.txt` | The pinned FatFs (`modules/fs/fatfs` at `f4ead3bf`) with the image's options and Zephyr's `fs_mkfs` options, on RAM disks of 1,024, 2,048 and 8,192 sectors, with the worst-case content of the image's File service and credential store | The author, on the host. **A stand-in for the `native_sim` run** (`tests/native/management_rt1060_ram_disk`), made while Docker did not answer. |
| `commands/` | `fatsize.c`, `ffconf_host.h`, `fatfs_host_size.sh` | |
| `host/native_sim-ram-disk-size-run.log`, `host/native_sim-configure.log` | The run of `tests/native/management_rt1060_ram_disk` on `native_sim/native/64`: 5 of 5 pass, with the worst-case numbers of the real FatFs on the board's disk geometry | The author, in the Dev Container (linux/amd64 under emulation, 2 CPUs, 2 GB) |
| `build/` | The target build of the bench image on the committed tree `052a772d`: configure and build logs, the resolved `.config`, `size` and the sections, the link map, the 400 largest symbols, the disk and format symbols, the SHA-256 of `zephyr.bin` and `zephyr.elf`, `strings` check, the output of `check_mkfs_callers.py`, and `bench-manifest.json` with its `file_backend` object | The author, in the same container. Zephyr: the read-only patched copy of the C1 build (same pin and patch, which the bench guard checked). The build directory was deleted afterwards. |
| `build-first-configure/configure-refused.log` | The first configure of the real image: Zephyr's default turned the SD card driver on, the RAM disk form dropped to `n`, and the CMake guard refused. Fixed in `target.conf`. | The author, in the same container |
| `commands/` | Also `tbuild.sh`, `nbuild.sh` (the container scripts) and `c3-run.sh` (the lock and the container) | |

## What is not here

| Item | Why |
| --- | --- |
| A board, a card, a card reader | No hardware in this stage |
| The decision core and the glue running on a target | Run on the host with fake devices and compiled for the target. The `native_sim` test does not link them, because `bench_guard.h` refuses `native_sim` on purpose. |
| A card build of the target | The guards accept the microSD state of the declaration. No card build was made. |
| Whether the SDRAM is cleared at boot | Read from the build (all RAM is in the SDRAM). Not observed. |
| The CI workflows | `.github/` is protected and not changed |
