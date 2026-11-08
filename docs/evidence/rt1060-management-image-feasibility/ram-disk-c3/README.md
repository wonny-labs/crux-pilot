# Row C3: the RAM disk file backend and the mount rule of the RT1060-EVKB bench image

Evidence record of [`docs/rt1060-bench-file-backend.md`](../../../rt1060-bench-file-backend.md). **Nothing here ran on a board or on a card.** The image still refuses to start.

| Folder | What it holds | Who ran it, where |
| --- | --- | --- |
| `host/check_bench_guard-verbose.txt` | Every refusal of the three guard levels with the line each printed, then the mutants (each guard weakened in a copy, one at a time). Includes the file backend: 13 CMake and 12 C refusals added by C3, and the microSD state of the declaration accepted as a control | The author, on the host (macOS, Python 3.9, CMake, Apple clang, Kconfiglib from the Zephyr checkout) |
| `host/unittest.txt` | `test_management_bench_medium.py` (the mount rule with fake devices, the glue per backend, the objects, the source scan, the sizing constants), `test_management_bench_declaration.py`, `test_management_target_lists.py` | The author, on the host |
| `host/fatfs_host_size.txt` | The pinned FatFs (`modules/fs/fatfs` at `f4ead3bf`) with the image's options and Zephyr's `fs_mkfs` options, on RAM disks of 1,024, 2,048 and 8,192 sectors, with the worst-case content of the image's File service and credential store | The author, on the host. **A stand-in for the `native_sim` run** (`tests/native/management_rt1060_ram_disk`), made while Docker did not answer. |
| `commands/` | `fatsize.c`, `ffconf_host.h`, `fatfs_host_size.sh` | |
@CONTAINER_ROWS@

## What is not here

| Item | Why |
| --- | --- |
| A board, a card, a card reader | No hardware in this stage |
@NOT_HERE@
