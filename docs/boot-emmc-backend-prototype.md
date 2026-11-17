# eMMC backend prototype: a flash-API driver over the MMC disk

> **Status:** Draft prototype, not adopted and not for merge without an owner decision; the owner decides D1 to D9 below, each with a recommendation.
> **Verified:** By `native_sim` run against the eMMC simulator (a model): the driver cases in four profiles, the build guard tests, and the power-cut replay over the driver's real write trace (P1 to P6); commands and results are in this page.
> **Not verified:** Any eMMC device or board; MCUboot running on top of the driver; the size and RAM of an RT1060 bootloader with the driver (no cross-build was made); the device's timeouts; the five prepared mutants (not run); nothing runs in CI, because nothing is wired into `scripts/` or the workflows.
> **Read this first / then:** [boot overview](boot-overview.md), then the [eMMC boot and update feasibility](boot-emmc-investigation.md) record and the [trailer replay record](evidence/boot-emmc/trailer-replay/README.md), then "What it is", "What was not done" and "Open decisions for the owner".

| Field | Value |
| --- | --- |
| Claim | **No eMMC support claim.** The driver was built and tested on `native_sim` against the eMMC simulator, which is a model. No hardware, flash, reset or fuse operation was run. |
| Issue context | [#298](https://github.com/wonny-labs/crux-pilot/issues/298) section 2, "What a backend would be", and [#310](https://github.com/wonny-labs/crux-pilot/issues/310). The model results that shape the driver are in the [trailer replay record](evidence/boot-emmc/trailer-replay/README.md). |
| Code | `zephyr/drivers/flash/` (driver, Kconfig, CMake), `zephyr/dts/bindings/flash/crux,flash-disk.yaml`, `tests/native/emmc_flash_disk/`, `tests/native/emmc_flash_replay/` |
| Default | Off. `CONFIG_CRUX_FLASH_DISK` is `n` unless an image selects it. |

## What it is

A Zephyr flash driver, `compatible = "crux,flash-disk"`, that exposes a `disk_access` disk (the MMC disk of an
eMMC) as `flash_read`, `flash_write` and `flash_erase`, with a page layout, a write block size and an erase value.
A `fixed-partitions` child of the node holds the image slots, so MCUboot's port reads and writes slots through the
flash map the way it does on NOR. The port picks one flash device from the chosen node `zephyr,flash-controller`, so
a product points that chosen node at this device. That the port works with its one-device assumption when the bootloader runs from RAM and the slots are on this device is **Not verified** (#298 row 2), and so is its use of the flash base address with a device that is not memory mapped. Neither is exercised here: see [What was not done](#what-was-not-done).

What it does, each point a result of the trailer replay's models and tested against the simulator:

| Behaviour | Why | Test |
| --- | --- | --- |
| Write block of one 512-byte sector. A write or erase that is not whole blocks or pages, or leaves the device, is refused with `-EINVAL` before any command | A smaller block would be a sector read-modify-write that is not atomic | `test_write_rejects_misaligned_and_out_of_range_access`, `test_erase_rejects_misaligned_and_out_of_range_access` |
| Erase is emulated by writing `0xFF` over whole pages, and the device reports an erase value of `0xFF` although the medium reads `0x00` when erased | MCUboot decodes a trailer field equal to the reported erase value as unset, and anything else as bad | `test_erase_writes_the_erase_value_over_whole_pages`, `test_unwritten_medium_reads_zero_not_the_reported_erase_value` |
| Every write and erase call ends with one cache flush (`DISK_IOCTL_CTRL_SYNC`) before it returns, one for the call and not one for each command. An erase of several chunks is also flushed after its first chunk, upwards from the start, so the first 4 KiB of the range (the whole first page with the default page size) is on the medium first | MCUboot orders its trailer writes by assuming that a returned write is durable. The trailer record shows that on a device with no order guarantee the page with an image header has to be erased on the medium before the pages that hold the trailer | `test_each_write_and_erase_call_is_flushed_before_it_returns`, `test_a_multi_page_erase_puts_its_first_page_on_the_medium_first` |
| A failed command, or a failed flush, fails the call with `-EIO`, no command follows a failed one, and a failed command is not followed by a flush. A call that fails part way leaves its range undefined | A call must be acknowledged only when its data is on the medium | `test_a_failed_command_fails_the_call_stops_it_and_is_not_flushed`, `test_a_failed_flush_fails_the_call`, `test_a_failed_read_in_the_middle_of_a_long_read_stops_it` |
| Reads have no alignment restriction. An aligned read goes straight into the destination in commands of at most 128 sectors, any other through a bounce buffer of 8 sectors | The flash API says so, and loading a whole image should not be a copy | `test_read_has_no_alignment_restrictions`, `test_direct_reads_are_limited_and_the_tail_goes_through_the_buffer`, `test_buffered_reads_move_a_bounce_buffer_at_a_time`, `test_access_succeeds_up_to_the_last_byte_of_the_device` |
| Partitions must start and end on a page, checked at build time, and must end inside the disk, checked when the device starts (a failed init leaves the device refusing all access). A write from a NULL source or of length 0 is refused, and the cursor of a call is 64 bits wide whatever `off_t` is | So that no erase of one slot reaches the next, and no caller bug turns a write into an erase | `test_build_guards.py`, `tests/native/emmc_flash_disk_init`, `test_a_write_of_nothing_or_from_no_buffer_is_refused` |
| `CONFIG_CRUX_FLASH_DISK_READ_ONLY` refuses write and erase with `-ENOTSUP` | A bootloader that only reads slots needs no flush. **Plain RAM load is not read only at the pinned MCUboot:** it scrambles a slot that holds an invalid image or one that cannot be loaded (`boot_validate_slot`, `boot_remove_image_from_flash`), through erases whose results it ignores, so a read-only device keeps such a slot and validates it again on every boot (see D3) | `test_read_only_driver_refuses_write_and_erase` |
| The driver requires the MMC disk driver, and the build refuses a writing driver without `CONFIG_CRUX_MMC_CACHE_FLUSH=y` | Without that build-local correction a disk sync does not flush the eMMC cache, and no other disk driver is known to flush on a sync. Nothing checks that `disk-name` names the MMC disk | `test_build_guards.py` |

## Results

Software only, on `native_sim/native/64` in a native arm64 container with `ZEPHYR_TOOLCHAIN_VARIANT=host`, Zephyr
v4.4.2 (`dccb09599635bdff17633fa7e9dab014b91dce90`). See the pull request for the revision and the exact commands.

- `tests/native/emmc_flash_disk`: the driver against the simulator in four profiles (cached, no cache, read only,
  and a control without the flush): 22 cases; 20 run in the cached profile and 2 skip (the other profiles skip the cases they do not apply to).
  `tests/native/emmc_flash_disk_init` holds the case of a partition beyond the disk, and `test_build_guards.py` six
  cases (four refusals with seven subcases, and two accepted configurations; the defaults case configures and does
  not build).
- `tests/native/emmc_flash_replay`: the update steps of RAM load with revert (a test request or a `--pad` image,
  then confirm or revert) run through the flash map and the driver, with the simulator's trace read back.
  The driver's disk writes and flushes equal, event for event, the write path that the trailer record models, and
  the record's power-cut replay over the real trace holds P1 to P6 on both device behaviours. A build with
  `CONFIG_CRUX_FLASH_DISK_FLUSH_CONTROL=y` never flushes, and the same replay then loses acknowledged confirms (P3).
- Mutants of the driver, run on an earlier revision of it: no flush, flush before the data, erase value 0 (each
  fails the cases and the replay), and flush after a failed write (fails the cases only, because the replay has no
  failure injection). Five more were prepared (no alignment check, a wrapping range check, an ignored read error, an
  ignored flush failure, an unchecked erase alignment) and **were not run**, because the free disk of the Mac fell
  under the margin for more local builds. The cases added after the review were not run against mutants.

Every result is an observation of the simulator and of the models of the trailer record. None is evidence about an
eMMC device.

## What was not done

| Item | Why |
| --- | --- |
| Any hardware, flash, reset, fuse or board operation | Not approved for this work |
| A sysbuild cross-build of MCUboot with the driver for the RT1060 | It needs the Zephyr SDK and the SoC's HAL in a west workspace, which the build environment of this work did not have. It is therefore **not** done, and the size and RAM of the bootloader are **Not measured** |
| Timeouts from EXT_CSD | The wait for a flush is the stack's, bounded by `CONFIG_SD_DATA_TIMEOUT` in the build-local correction of `subsys/sd/mmc.c`. The stack does not read the device's own timing values, and a driver on top cannot change that. It would be a change to the correction |
| MCUboot running on top of the driver | The replay issues the `flash_area` calls that MCUboot's functions issue, and is not MCUboot |
| Swap modes, reliable write, the boot partitions | Out of scope (see the trailer record) |
| A 32-bit `off_t`, a disk over 2 GiB, concurrent callers, a page size other than 4096 | `native_sim` has a 64-bit `off_t` and a 4 MiB medium. The cursor is 64 bits wide, which is untested on a 32-bit target. The mutex is not tested with two threads |
| What the `flush_control` profile proves | It shows that a returned write stays in the cache without the flush. What would catch a missing flush in the driver as built is the flush-count and durability cases, the replay control, and the mutants that were run on an earlier revision |
| Zero-length calls | A zero-length write or erase is refused with `-EINVAL`, where many Zephyr flash drivers return 0. MCUboot issues none |
| That slots avoid a file-system volume on the same disk, and that partitions do not overlap | Nothing in the driver or the build knows the volume. This is for the composition |
| That `disk-name` names the MMC disk | The Kconfig symbol needs the MMC disk driver, which does not prove the node names it. A device with another disk name would give no durability |

## Open decisions for the owner

Each has a recommendation. None is taken here.

| # | Decision | Options | Recommendation |
| --- | --- | --- | --- |
| D1 | Where the driver lives | The Crux Zephyr module (as here), Zephyr upstream, or out of tree | **Keep it in the Crux module.** It depends on a build-local correction of the MMC stack (`CONFIG_CRUX_MMC_CACHE_FLUSH`), which upstream does not have, and the module already holds the file system adapter that depends on the same correction. |
| D2 | Upstreaming | Propose the driver and the flush correction to Zephyr, or keep both | **Not now.** Propose them only after the correction is accepted upstream and a test on a real eMMC exists. The driver is small, so a later proposal is cheap. |
| D3 | Read only or full | Read-only (plain RAM load, no revert), or full (test, confirm, revert) | **Full, with the read-only build kept.** #141 ranks RAM load with revert first because it provides revert, which needs `copy_done` writes and a whole-slot scramble. Both builds share the driver, so the choice is a Kconfig symbol of the product. **The read-only build has a cost:** plain RAM load also scrambles a slot with an invalid image or one that cannot be loaded (`loader.c` `boot_validate_slot`, `ram_load.c` `boot_remove_image_from_flash`), ignoring the result, so on a read-only device such a slot stays and is read and validated on every boot. |
| D4 | Slot layout and `BOOT_MAX_ALIGN` | A write block and `BOOT_MAX_ALIGN` of 512 (a 2560-byte trailer, no swap mode), 4096 (a 20480-byte trailer, each field in its own native unit, no swap mode), or reliable write (not in the pinned stack). Note that 4096 puts three 4 KiB buffers on the bootloader's stack (the trailer record) | **512 now, with slots on 8 MiB multiples outside any file system volume**, and a page size to be measured (see D6). The trailer record shows that 512 keeps safety in its models and can cancel an unacknowledged step under a native-unit tear, and that 4096 isolates the fields. **Blocking for the `--pad` flow:** the pinned `imgtool` limits the alignment to 32, so it cannot sign a padded image whose trailer matches a `BOOT_MAX_ALIGN` of 512 or 4096. M8 test images signed with `--pad` (owner decision 13 of the update investigation) would need a signing step that does not exist, or the application-driven request of M9 would be used on eMMC instead. |
| D5 | Bootloader size and RAM budget | Measure before committing, or commit first | **Measure first.** #141 measured 51,760 B for MCUboot with RAM load and revert on NOR. The SD/MMC stack, the SD host controller driver and this driver add an amount that no source here shows. A build-only RT1060 sysbuild of MCUboot with the driver and the correction would give it, and needs the SDK and the HAL. A page of 4096 makes `BOOT_MAX_IMG_SECTORS` at least 2048 for an 8 MiB slot, which is bootloader RAM too. |
| D6 | The page size, which sets how many erase calls and flushes a slot erase costs | 4096 (as the tests use), 512 KiB, 4 MiB | **Measure on a board, then choose; expect a larger page.** MCUboot erases a slot one page at a time, and every call here flushes once, so an 8 MiB slot is 2048 calls at 4096 and 16 at 512 KiB. A larger page also means that the first chunk of an erase, which is flushed first, is the first 4 KiB of the page and not the whole page, which is still enough because the image header is in it. A page must be a multiple of the native unit, and the slot boundaries a multiple of the page. |
| D7 | Flush policy | A flush after every call (as here), or only after calls that touch a trailer and every erase | **After every call.** The cheaper policy needs the driver to know where each slot's trailer is, which a flash driver does not, and the replay shows it is safe only on a device that drains in order. |
| D8 | The `dts_root` setting added to `zephyr/module.yml` | Keep it, or let each application add the root | **Keep it.** It makes the binding visible to every image that builds the module, and a module with no node of this compatible is unaffected. |
| D9 | Timeouts | Leave the stack's constant, or change the build-local correction to read the device's values | **Leave it for the prototype**, and decide after a device is measured. The device's values are only worth reading if the constant turns out to be wrong for a real part. |

## How to run the tests

The tests need `native_sim` and a Zephyr v4.4.2 tree. In a west workspace with the module discovered:

```sh
west build -b native_sim/native/64 tests/native/emmc_flash_disk
./build/zephyr/zephyr.exe
```

Repeat with `-- -DCONFIG_CRUX_EMMC_SIM_CACHE_KB=0`, `-- -DCONFIG_CRUX_FLASH_DISK_READ_ONLY=y` and
`-- -DCONFIG_CRUX_FLASH_DISK_FLUSH_CONTROL=y`. `tests/native/emmc_flash_disk/testcase.yaml` names the four profiles
for Twister. For the replay, build `tests/native/emmc_flash_replay` twice, the second time with
`-DCONFIG_CRUX_FLASH_DISK_FLUSH_CONTROL=y`, and run:

```sh
python3 tests/native/emmc_flash_replay/run_driver_replay.py \
    --exe build-replay/zephyr/zephyr.exe --exe-nofix build-replay-control/zephyr/zephyr.exe
```

Nothing is wired into `scripts/` or the workflows, which are protected paths: wiring the Twister profiles and the
replay into `scripts/test-storage-emmc.sh` and the Zephyr workflow is a follow-up for the owner.
