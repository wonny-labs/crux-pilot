# The file backend of the RT1060-EVKB bench image: a RAM disk, and a mount rule that never formats a card

Status: **row C3 of stage 2** of [the RT1060-EVKB feasibility report](rt1060-management-image-feasibility.md), for review in a Draft PR. Refs #197. Decision D2 of that report, taken by the owner on 2026-11-08 ("추천대로 진행해주고"): **a RAM disk for the first board run, no card, no format risk; microSD in a later run.**
It authorises no hardware operation, and **nothing here ran on a board or on a card**: the results are host tests with fake devices, a `native_sim` run of the real FatFs on the board's RAM disk geometry, and a compile and link for `mimxrt1060_evk@B//qspi`. The image still refuses to start (row C1's switch is untouched).

## Result

| Question | Answer |
| --- | --- |
| What is the file backend of the bench image? | **A RAM disk of 524,288 B in the SDRAM**, named `RAM`, mounted at `/RAM:`, under FatFs. The declared form is `ram_disk` (state `selected` in `bench_declaration.json`). |
| Can any path of the image format a card? | **No.** Four independent barriers, each shown by a test that breaks it: [the barriers](#the-barriers). The image has **no card driver at all** (no SD card driver, no USDHC host, no microSD provider), so there is no card to mount, and the only function that names `fs_mkfs` is compiled in the RAM disk form only. |
| What persists across a reset? | **Nothing.** Accounts, keys, the certificate and the events are lost, and every boot is a first boot: [what persists](#what-persists-and-what-does-not). |
| How big, and what does it cost? | **524,288 B of SDRAM** for the disk, **523,136 B** more than the C1 image in the linker's RAM figure (1,360,940 B against 837,804 B; the disk's 524,288 B less 1,152 B that the card drivers took). The bench needs about 300 KB at the worst: [the sizing](#sizing). |
| Is the microSD provider still there? | **Yes, as a second declared state** (`microsd_card`, `reserved`), that a later run takes by changing the declaration. It is not selectable in this image until then: [the two states](#the-two-declared-states). |
| Can the image run? | **No**, as before. [What still blocks a board run](rt1060-management-image-feasibility.md#what-still-blocks-a-board-run). |

```mermaid
flowchart LR
    decl["bench_declaration.json<br/>ram_disk: selected<br/>microsd_card: reserved"] --> kc["Kconfig.bench<br/>CRUX_BENCH_FILE_RAM_DISK needs the RAM disk driver and no SD driver"]
    kc --> cm["bench_guard.cmake<br/>exactly one backend, provider and drivers agree, no card"]
    cm --> ch["bench_guard.h<br/>the same, at every compile"]
    ch --> prov["File provider<br/>CRUX_FILE_FATFS_RT1060_RAM_DISK<br/>excludes every card driver"]
    prov --> glue["bench_medium_fs.c<br/>mount, and fs_mkfs for the RAM disk only"]
    glue --> core["bench_medium.c<br/>the decision, no Zephyr"]
    core --> ram[("RAM disk RAM:<br/>blank at every boot")]
```

## The mount and format rule

One sentence: **the RAM disk may be formatted, and a card never is.** A RAM disk is memory that every boot finds blank, so a failed mount is answered with one format and one more mount. A card holds somebody's data, so a failed mount is the end: it is reported on the UART and nothing is written.

```mermaid
flowchart TB
    start(["bench_medium_prepare(kind, ops)"]) --> valid{"kind is RAM disk or card,<br/>and a mount function is given?"}
    valid -- no --> invalid["INVALID<br/>no call was made"]
    valid -- yes --> mount1["mount"]
    mount1 -- "returns 0" --> mounted["MOUNTED"]
    mount1 -- "anything else" --> which{"kind"}
    which -- "card" --> refuse["NOT_MOUNTED_NOT_FORMATTED<br/>reported, nothing written"]
    which -- "RAM disk" --> fmt{"a format function,<br/>and it returns 0?"}
    fmt -- no --> ffail["FORMAT_FAILED"]
    fmt -- yes --> mount2["mount once more"]
    mount2 -- "returns 0" --> fm["FORMATTED_AND_MOUNTED"]
    mount2 -- "anything else" --> mfail["MOUNT_FAILED_AFTER_FORMAT<br/>no second format, no loop"]
```

The previous composition did the opposite for every medium. `medium_ready()` of the copy of `crux_native_management.c` (the file of the native images) called `fs_mkfs` after any failed mount, which on a card **would format it**. The stage 0 report found that call still linked in the vehicle; row C1 pinned it with the copy test. C3 removes it: the copy now calls `bench_medium_mount()`, and the copy test pins that edit instead.

| File | What it does | Zephyr in it? |
| --- | --- | --- |
| `src/bench_medium.c`, `.h` | The decision above: `bench_medium_prepare()` with its two function pointers, and the result codes | No. A host test runs it. |
| `src/bench_medium_fs.c` | The glue: the mount structure (`FS_MOUNT_FLAG_NO_FORMAT`, so the file system layer does not format either), the mount point of the form, the format function **for the RAM disk form only**, and one `BENCH file-medium` line on the UART | Yes |
| `ram_disk.overlay` | The `zephyr,ram-disk` node: disk name `RAM`, 1,024 sectors of 512 B | Devicetree |
| `check_mkfs_callers.py` | Reads the object files of a build and says which reference a format function | No |

## The barriers

A card is kept from a format by four independent barriers. Each has a test that breaks it alone and shows that the others still stand or that the break is seen.

| # | Barrier | Where | Shown by |
| --- | --- | --- | --- |
| 1 | **There is no card in the image.** The File provider of the RAM disk (`CRUX_FILE_FATFS_RT1060_RAM_DISK`) depends on `!DISK_DRIVER_SDMMC`, its CMake and `file_native.c` refuse a build with the SD card driver, the USDHC host, a flash disk or the microSD provider, and so do the bench guards at three levels | Kconfig, CMake, C | `check_bench_guard.py`: 13 CMake and 12 C refusals for these settings, each for its reason, and 53 of 53 mutants of the guards killed. **The first configure of the real image showed barrier 1 working**: Zephyr turns the SD card driver on by default on this board, which dropped the RAM disk form to `n`, and the CMake guard refused the build ([evidence](evidence/rt1060-management-image-feasibility/ram-disk-c3/build-first-configure/configure-refused.log)). `target.conf` now turns it off, and a test pins that. |
| 2 | **The decision never passes a card to a format.** `bench_medium_prepare()` calls `format` only for the RAM disk kind, and refuses any other value of the kind before any call | `src/bench_medium.c` | `test_bench_medium.c`: every mount error value for a card, a fake card that a format would cure, every kind value from -1000 to 1000 and every power of two: a format is reached for the RAM disk kind alone |
| 3 | **The glue has no format function for a card.** In the microSD form `BENCH_MEDIUM_FORMAT` is `NULL`, and the identifier `fs_mkfs` does not occur in the object. In the RAM disk form the call names the disk `"RAM:"` and no other | `src/bench_medium_fs.c` | The glue built for each form against a fake file system layer with a fake `RAM:` and a fake `SD:`; `nm` on both objects; a weakened glue that offers a card format function is seen by the object check and still stopped by barrier 2 |
| 4 | **Nothing else names a format function.** `fs_mkfs`, `f_mkfs`, `fatfs_mkfs` and `CONFIG_FS_FATFS_MOUNT_MKFS` occur in no other source of the image, and `CONFIG_FS_FATFS_MOUNT_MKFS` is refused at every level | Every source of the image | `test_management_bench_medium.py` scans the 57 sources of the image (comments removed), and `check_mkfs_callers.py` reads the objects of the real target build: **`fs_mkfs` is referenced by `bench_medium_fs.c.obj` and by no other object, and `f_mkfs` only by Zephyr's FatFs glue `fat_fs.c.obj`** ([output](evidence/rt1060-management-image-feasibility/ram-disk-c3/build/mkfs-callers.txt)). The linked image has no SD card driver, no USDHC host and no `sdmmc` or `usdhc` function ([symbols](evidence/rt1060-management-image-feasibility/ram-disk-c3/build/medium-symbols.txt)) |

```mermaid
flowchart TB
    subgraph b1["1. no card in the image"]
        k["no SD card driver, no USDHC, no microSD provider<br/>Kconfig, CMake, C"]
    end
    subgraph b2["2. the decision"]
        d["a format is reachable for the RAM disk kind only"]
    end
    subgraph b3["3. the glue"]
        g["card form: no format function, no reference to fs_mkfs<br/>RAM disk form: the disk name RAM: only"]
    end
    subgraph b4["4. the rest of the image"]
        s["no other source names a format function<br/>no object but the glue references fs_mkfs"]
    end
    b1 --> b2 --> b3 --> b4
    x["a path that formats a card"] -. "needs every barrier to fail; barrier 4 is a check at build and test time" .-> b1
```

## The two declared states

The declaration carries the file backend as two forms, and the guards accept exactly one of them.

| Form | Symbol | State now | What it is |
| --- | --- | --- | --- |
| `ram_disk` | `CRUX_BENCH_FILE_RAM_DISK` | **selected** | This image. Needs `CONFIG_DISK_DRIVER_RAM`, the RAM disk provider, and **no** SD card driver. |
| `microsd_card` | `CRUX_BENCH_FILE_MICROSD` | **reserved**, row "a later run" | The existing microSD provider (`CRUX_FILE_FATFS_RT1060`) with the SD card driver and no RAM disk. Refused here: a build that sets it fails at the CMake level with `is reserved and nothing implements it`. |

A later run changes the declaration to `ram_disk: reserved` and `microsd_card: selected`, and sets the card's symbols in `target.conf`. `check_bench_guard.py` runs that state as a second control: **the guard accepts the microSD state with the card's provider and driver, and refuses the RAM disk's settings under it.** Even then a mount that fails is reported and the card is never formatted: barriers 2 to 4 hold in that form too. What the card run needs besides this (the M4 microSD tools, the mount guard, a prepared card) is not part of this row.

## What persists and what does not

The RAM disk is the `.bss` of the RAM disk driver, in the SDRAM: the linked image has the symbol `disk_buf_0`, 0x80000 B, in the SDRAM range ([symbols](evidence/rt1060-management-image-feasibility/ram-disk-c3/build/medium-symbols.txt)). Zephyr clears `.bss` at every boot, and all RAM of this image is in the SDRAM, so the SDRAM must be up before that or the image could not run at all. So the disk is zero at every boot and `fs_mount` fails on it, which is the trigger of the format above. **That is a reading of the build, not an observation on a board.**

| Item | Where it lives | After a reset |
| --- | --- | --- |
| Accounts (the credential store, slot files `secure-a` and `secure-b`) | RAM disk | **Lost.** The first account is provisioned again at every boot. |
| The SSH host key and the TLS key (credential store records) | RAM disk | **Lost.** Both are generated again from the TRNG. |
| The listener certificate | Generated at boot from the key | **Lost and generated again.** |
| Security events, and every `file.*` event | The ring in RAM, and Crux Logging to the UART | **Lost** from the device. The UART capture is the record. |
| Files of the File service (`files` area) | RAM disk | **Lost.** |
| Configuration | **Not stored anywhere.** The image links `configuration_no_provider.c`, whose write returns `NOT_SUPPORTED`: there is no Configuration provider | Every parameter keeps its default at every boot. Nothing is saved, on the RAM disk or elsewhere. |
| The image | QSPI Flash | Kept. Flash holds only the image. |

So **every boot is a first boot**, a client sees a **new SSH host key fingerprint and a new certificate fingerprint after every reset**, and a client that remembered the old ones will warn. That is expected. **The changed fingerprints show that the entropy source varies from one boot to the next, which is all they show. They are not a statement on the quality of the entropy**: that needs a test of the generator, which no row of the plan is.

The same text is in the bench manifest (`file_backend` in `bench-manifest.json`: `persists_across_reset: false`, `lost_at_reset`, and the note) and on the UART at boot (`BENCH file-medium ram-disk ... nothing persists across a reset`).

## Sizing

What the disk must hold, from the limits of the image's own File service area (`src/mgmt_services.c`, the SSH composition's area) and of the credential store:

| Item | Limit | Source |
| --- | ---: | --- |
| One file in the area | 65,536 B | `max_file_bytes` |
| The area, partial and temporary files included | 262,144 B | `max_area_bytes` |
| Entries in the area | 16 | `max_entries` |
| The credential store | 2 slot files of 16,384 B = 32,768 B | `CRUX_CREDENTIAL_SLOT_BYTES` |
| Directories | 3 (`secure-a`, `secure-b`, `files`) | `src/mgmt_start.c` |

The File service reserves bytes against `max_area_bytes` before it writes, so the area can never hold more than that, and the disk can only be filled by something that bypasses the service. **The numbers in the owner's brief (1 MiB per file, 3 MiB per area, 16 entries) are the limits that [the SFTP design](sftp-design.md#limits) records for its own image (the WinSCP run), and this image does not declare them**: the image declares 64 KiB, 256 KiB and 16 (the native composition's own area, which the bench copy is pinned to). The disk is sized for what the image declares, and the table below says what those limits would need.

The method: the pinned FatFs (`modules/fs/fatfs` at the `west.yml` pin) with the bench image's options (long names on the stack, 127 characters, code page 437) and Zephyr's `fs_mkfs` options (one FAT, 512 root entries, automatic cluster size), on a RAM disk of a given number of 512 B sectors. The worst case is built in the order that leaves the least room: **the area full first, the store's two slots after it.** Two patterns: four files of 64 KiB, and 16 entries with 63-character names, each one byte over a cluster boundary where the limit allows it (the most slack and the most directory entries).

| Disk | File system | Data clusters | After the worst case: free | Share free (of the data clusters) |
| --- | --- | ---: | ---: | ---: |
| **512 KiB, 1,024 sectors (this image)** | FAT12, 512 B clusters | 987 (505,344 B) | **208,896 B** (pattern A), **205,824 B** (pattern B) | **41 %** (the `native_sim` log prints 39 %, the share of the whole 524,288 B disk) |
| 1 MiB, 2,048 sectors (for comparison) | FAT12, 512 B clusters | 2,008 (1,028,096 B) | 731,648 B, 728,576 B | 71 % |
| 4 MiB, 8,192 sectors, with the SFTP design's limits (1 MiB per file, 3 MiB per area, 16 entries) | FAT12, 2,048 B clusters | 2,038 (4,173,824 B) | 989,184 B, 987,136 B | 24 % |

| Quantity | Value |
| --- | ---: |
| The most the image's limits can put on the disk (pattern B, directories, both slots) | **299,520 B** (505,344 B of data minus 205,824 B free) |
| What the limits sum to before the file system's own use | 262,144 B + 32,768 B = 294,912 B |
| The file system's own use (FAT, three directories, cluster slack) | about 4,600 B |
| The disk of this image | **524,288 B**, which is 1.78 times the 294,912 B of the limits (a unit test requires at least 1.25 times) |
| A disk filled by something that bypasses the File service | refuses with a plain error after 8 files of 64 KiB (the host run: result `FR_DENIED`; `native_sim`: `ENOSPC`), no hang |

**What the bench needs: about 300 KB at the worst, so 512 KiB.** It is also the size of the partition that the native composition has always run this File service on, so no new size is introduced. The bench's SFTP put and get (step 5 of the plan) are files of at most 64 KiB, the image's per-file limit, so a disk larger than this buys nothing the limits would let a client use. **If the area limits were raised to those 1 MiB and 3 MiB, the disk would need about 3.2 MiB, and a 4 MiB disk (8,192 sectors) leaves 24 %**; that is a change of the copy of `mgmt_services.c`, which the copy test pins to the native file, and of `ram_disk.overlay`, and it is not made here.

| RAM cost | Value | Source |
| --- | ---: | --- |
| The disk | **524,288 B** (512 KiB) | `ram_disk.overlay`, the `.bss` of the RAM disk driver, in the SDRAM |
| Share of the SDRAM | 1.6 % of 32 MB | |
| ITCM, DTCM, OCRAM | 0 B | The disk is in SDRAM with everything else, as the linker's report of C1 shows for the image |
| The RAM figure of the image | **1,360,940 B** (4.06 % of the SDRAM) for C3 alone, C1: 837,804 B, so **+523,136 B**; the merged head with C5 is 1,361,068 B | The linker's report of the target build ([the build](#the-target-build)) |


### The target build

The same method as C1 (`commands/tbuild.sh`: the Dev Container under emulation, 2 CPUs, 2 GB, `-j2`, one build; the pinned Zephyr v4.4.2 copy with the wolfSSL TLS socket patch of the C1 build, mounted read-only, which the bench guard checks by marker, pin and hash; the crypto modules of the C1 build, read-only). Two builds, both with the manifest saying `source_tree_modified: false`, and the bench guards passing inside the real configure:

| Build | Tree | Where its files are |
| --- | --- | --- |
| C3 alone | `7c22a9ec` (the review fixes; an earlier build at `052a772d` gave byte-identical `zephyr.bin` and `zephyr.elf`) | [`build-c3-alone/size.txt`](evidence/rt1060-management-image-feasibility/ram-disk-c3/build-c3-alone/size.txt) (sizes, hashes, manifest in the same folder) |
| **C3 with C5 merged: the head of this PR** | **`be54c778`**, after `origin/main` took C5 (#365), which adds the boot check of the random source to `main.c` and the build | [`build/size.txt`](evidence/rt1060-management-image-feasibility/ram-disk-c3/build/size.txt) (everything in the same folder) |

| Item | C1 | C3 alone | **C3 with C5 (head)** |
| --- | ---: | ---: | ---: |
| FLASH (`0x60000000`, 8 MB) | 601,004 B | 590,496 B (7.04 %), 10,508 B less: the SD card driver and the USDHC host are out, the medium sources are in | **590,956 B** (7.04 %), 460 B more: C5's boot check |
| RAM = SDRAM (`0x80000000`, 32 MB) | 837,804 B | 1,360,940 B (4.06 %), **523,136 B** more: the disk's `disk_buf_0` is 524,288 B | **1,361,068 B** (4.06 %), 128 B more: C5 |
| ITCM, DTCM, OCRAM, OCRAM2 | 0 B | 0 B | 0 B |
| `size`: text, data, bss | 596,504, 4,480, 833,254 | 586,308, 4,168, 1,356,706 | 586,768, 4,168, 1,356,866 |
| Steps, warnings | 551, 3 | 546, **the same 3**, all in wolfSSH `wolfsftp.c`, outside `-Werror` | 547, the same 3 |
| `zephyr.bin` SHA-256 | `8b557969...a420` | `8b5a19e6cc5c1aae0c40ccc84831c0cefd887c33669b731719df201d6b91ba0e` | `e43763d577b1638e59c07177f70d4dc74ee14ce1592ab9e5ca86def0426ee466` |
| `zephyr.elf` SHA-256 | `f6765d53...b7fb` | `907fed56056ddbb2114eccfb549246d38e2dfb282850944c876cf7ca284aee9e` | `0f905d00cbe2ff5658ce58a9b772fded937590b2597ccecf9afd26b402838854` (the build path differs, so the hash may) |

The **RAM cost of C3 itself is 523,136 B** (the C3-alone column against C1). The head's figure is C3 and C5 together.

The resolved configuration of the file backend: `CONFIG_DISK_DRIVER_RAM=y`, `CONFIG_DISK_DRIVER_SDMMC` and `CONFIG_SDHC` not set, `CONFIG_FS_FATFS_MOUNT_MKFS` not set, `CONFIG_FILE_SYSTEM_MKFS=y` (the RAM disk's format needs it), the two C3 symbols set. `strings -a` on the image finds the `BENCH file-medium ram-disk ... nothing persists across a reset` line, and finds `composition test password` **0 times**. The manifest has the `file_backend` object above, with `persists_across_reset: false`.

Two things the build showed. The first target build **failed at the first configure**, which is the guard working ([above](#the-barriers)); the second **failed to link** `bench_medium_mount`, because the image's `CMakeLists.txt` did not list the two new sources (fixed, and a unit test lists them now). One observation, not acted on: `disk_buf_0` is a plain byte array, so its address (`0x8002093f`) is not aligned. The RAM disk driver only copies to and from it, so this is harmless on a Cortex-M7 in normal memory, and nothing reads it by DMA.

Evidence: [`evidence/.../ram-disk-c3/`](evidence/rt1060-management-image-feasibility/ram-disk-c3/README.md).

## What was and was not run

| Check | Result | Who ran it |
| --- | --- | --- |
| `python3 -m unittest` of `test_management_bench_medium.py`, `test_management_bench_declaration.py`, `test_management_target_lists.py` | **Pass** (the medium test: the decision core over fake devices, the glue per backend, the objects, the source scan, the sizing constants, the sources of the image, `target.conf`; and the control that each barrier breaks alone). The copy test pins the new edit of `mgmt_start.c`. | The author, on the host |
| `check_bench_guard.py --verbose` | **Pass**: 54 CMake and 29 C refusals each for their reason (13 and 12 are C3's), the declared RAM disk bench passing at every level, **the microSD state of the declaration accepted with the card's provider and driver and refusing the RAM disk's settings**, and **53 of 53 mutants killed** (37 in C1) | The author, on the host |
| The decision core, with fake devices | A card is never formatted: every mount error value, a fake card that a format would cure, every kind value from -1000 to 1000; a RAM disk is formatted once and mounted, never twice, and a format failure stops | The author, on the host |
| `tests/native/management_rt1060_ram_disk` on `native_sim/native/64`, the real FatFs, the disk of `ram_disk.overlay` | **5 of 5 pass**: a blank disk is not formatted by the mount, the geometry is the overlay's, the worst case fits with **208,896 B and 205,824 B free**, a disk filled by something that bypasses the service refuses with `ENOSPC`. The numbers equal those of the host run on the pinned FatFs. | The author, in the Dev Container ([log](evidence/rt1060-management-image-feasibility/ram-disk-c3/host/native_sim-ram-disk-size-run.log)) |
| Compile and link for `mimxrt1060_evk@B//qspi` | **Linked**, [numbers above](#the-target-build) | The author, in the Dev Container |
| `check_mkfs_callers.py` on the objects of that build | **Pass** | The author, in the Dev Container |
| Not run | **A board, a card, a card reader, the image, any client.** The decision core and the glue **on a target**: they were run on the host with fakes and compiled for the target, and the native_sim test does not link them (`bench_guard.h` refuses `native_sim` on purpose). The microSD state of the declaration as a build for the target: the guard accepts it, **no card build was made**. SDRAM clearing of `.bss` on a board. The CI workflows (`.github/` is protected and not changed). | |

## Decisions taken in C3

Taken without the owner, each the narrower choice.

| # | Decision | Why |
| --- | --- | --- |
| C3-1 | The RAM disk has its own File provider selection, `CRUX_FILE_FATFS_RT1060_RAM_DISK`, in `zephyr/Kconfig` and the provider's CMake, and it excludes the SD card driver | The existing RT1060 provider needs the card driver, and the simulator provider needs `native_sim`. The exclusion makes "no card in the image" a property of the build, not a habit. It is the only shared file that changed, and no other image selects it. |
| C3-2 | The mount and format decision is a separate source with no Zephyr in it, plus a thin glue | A host test can then run the decision against fake devices, including a fake card that must never be formatted |
| C3-3 | The copy of the start source is edited, and the copy test pins the new edit | Row C1 said C3 does this. The native file keeps its `fs_mkfs` fallback, which is right for a test disk on `native_sim`. |
| C3-4 | The microSD provider is kept as a second declared form, `reserved`, with its own Kconfig form and its own CMake and C refusals | The task asks for it behind a different declared state; the guards then cannot be satisfied by selecting it |
| C3-5 | A format that fails, or a mount that fails after a format, ends the attempt: no retry, no loop | A RAM disk that cannot be formatted is a fault to see on the UART, not to hide |
| C3-6 | The disk size is the smallest that holds the declared worst case with a stated margin | 512 KiB holds the worst case of the image's own limits (about 300 KB) with 41 % free, and is the size the native composition has always used. A larger disk buys nothing the limits let a client use, and costs SDRAM. |
| C3-7 | `bench_limits.h` of the sizing test is a copy of the image's area limits, compared with `mgmt_services.c` by a unit test | The test links no Crux component, and drift fails a test |
| C3-8 | The new files are separate from the other rows' files wherever possible: this document, `bench_medium*`, `ram_disk.overlay` | Rows C2, C4 and C5 edit the same directory in parallel |

## Reproduce

```bash
python3 tests/zephyr/management_rt1060_compile/check_bench_guard.py --verbose
python3 -m unittest tests/build/test_management_bench_medium.py tests/build/test_management_bench_declaration.py tests/build/test_management_target_lists.py
```

The sizing test and the target build need the Dev Container and the workspace of the [feasibility report](rt1060-management-image-feasibility.md#reproduce): `tests/native/management_rt1060_ram_disk` on `native_sim/native/64`, and `tests/zephyr/management_rt1060_compile` for `mimxrt1060_evk@B//qspi`. The commands that ran are in [the evidence record](evidence/rt1060-management-image-feasibility/ram-disk-c3/README.md).
