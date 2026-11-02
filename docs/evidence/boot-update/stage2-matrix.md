# Stage 2: update mode and staging medium fit for #141

Date: 1 November 2026. Desk analysis only. No build, no hardware, no tool download.

## 0. Inputs, labels and row identifiers

| Item | Value |
| --- | --- |
| Pins | Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0), MCUboot `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2` |
| crux-pilot | `origin/main` at `f57ff22` (after `git fetch`, read with `git show`). This is newer than the Stage 0 record (`f67e22a`) because #147 merged as `f57ff22`. |
| Stage 1 rows | `A1` to `A10`, `B1` to `B89`, `L1` to `L5` in [stage1-pinned-source-findings.md](stage1-pinned-source-findings.md) |
| Stage 2 supplementary rows | `S2-1` to `S2-14` in section 0.1. Read by this stage from the Stage 1 local copies of the pinned files (kept outside the repository, not committed). |
| Vendor rows | `V1` to `V4` in section 0.2. Vendor SDK documentation collected for Stage 3. All are **Not verified (vendor documentation, not pinned Zephyr or MCUboot source)**. Stage 3 later labelled the same documents "Verified by public vendor document", which means only that the vendor states it (see [stage3-findings.md](stage3-findings.md) C4 to C8). |
| Repository rows | `R1` to `R6` in section 0.3. crux-pilot records at `f57ff22`. They are inputs, not pinned-source evidence. |
| Stage 3 | No Stage 3 result file existed when this matrix was written. HAB, BEE key provisioning and boot-media items are marked **Pending Stage 3**. Stage 3 results are now in [stage3-findings.md](stage3-findings.md) and [stage3-summary.md](stage3-summary.md). |
| Labels | "Verified on pinned source" and "Not verified" as in [plan](../../plans/pre-m8-investigation-plan.md) section 5.2. No cell is "Verified by build". No cell is verified on hardware. |

Abbreviations used in cells: **App** is the application, **BL** is the bootloader (MCUboot), **trailer** is the MCUboot image trailer at the end of a slot, **pending** is a request to boot the other image on the next reset.

### 0.1 Stage 2 supplementary rows (Verified on pinned source)

| ID | Claim | File:line at pin |
| --- | --- | --- |
| S2-1 | Swap modes choose the swap type from trailer state. TEST: secondary magic good and secondary `image_ok` unset. PERM: secondary `image_ok` set. REVERT: primary magic good, primary `image_ok` unset and primary `copy_done` set. | M `boot/bootutil/src/bootutil_public.c`:105-150 |
| S2-2 | In swap and overwrite builds, `boot_set_next` on a non-active slot with unset magic writes the magic, optionally `image_ok` (permanent), and the swap info. On the active slot it writes `image_ok` if unset. | M `bootutil_public.c`:527-593 (inside `#ifndef MCUBOOT_BOOTUTIL_LIB_FOR_DIRECT_XIP` at 506, `#else` at 594) |
| S2-3 | `boot_set_pending_multi` opens the secondary slot. `boot_set_confirmed_multi` opens the primary slot. Zephyr `boot_request_upgrade` calls `boot_set_pending`. Zephyr `boot_write_img_confirmed` opens `ACTIVE_SLOT_FLASH_AREA_ID`, which is the `zephyr,code-partition` in non-RAM-load modes and a bootloader-provided slot in RAM load modes. | M `bootutil_public.c`:690-705, 735-751. Z `subsys/dfu/boot/mcuboot.c`:76-84, 405-410, 457-466 |
| S2-4 | During a swap the BL writes swap progress into the primary slot trailer, or into the scratch area when scratch is in use, with `flash_area_write`. | M `boot/bootutil/src/loader.c`:414-457 |
| S2-5 | After a swap the BL writes `image_ok` for REVERT and PERM and `copy_done` for an upgrade. These writes are compiled out in overwrite-only. An interrupted swap is resumed from the stored status and finished the same way. | M `loader.c`:1251-1320 (`#ifndef MCUBOOT_OVERWRITE_ONLY` at 1278), 1330-1370 |
| S2-6 | Dispatch: TEST and PERM run the downgrade check, then the update. REVERT runs the update (a second full swap). FAIL (invalid secondary) makes the BL write `image_ok` so that no revert follows. | M `loader.c`:1838-1870 |
| S2-7 | An invalid image is erased if it is in the secondary slot, or in any slot when slots are equivalent. In swap modes an invalid primary is not erased and boot stops. An invalid secondary gives swap type FAIL or NONE. | M `loader.c`:647-656, 728-750, 1923-1933 |
| S2-8 | Overwrite-only: the BL erases the primary slot sector by sector, copies the secondary, then scrambles the secondary header (unless `MCUBOOT_OVERWRITE_ONLY_KEEP_BACKUP`) and the secondary trailer. | M `loader.c`:938-1110 |
| S2-9 | Swap using move needs one more usable sector in the primary slot than in the secondary. Swap using move first moves every primary sector up by one. Swap using offset places the update at the second sector of the secondary slot. Both need equal sector sizes. | M `loader.c`:995-996. M `boot/zephyr/Kconfig`:613-634. M `boot/zephyr/CMakeLists.txt`:705-707 |
| S2-10 | MCUboot's trailer estimate: swap status is `sectors x 3 x write size` for scratch and move, and `sectors x 2 x write size` for offset. The trailer is rounded up to the erase size for move and offset. Move adds one erase sector. Overwrite has no swap status. | M `boot/zephyr/CMakeLists.txt`:655-719 |
| S2-11 | `BOOT_MAX_IMG_SECTORS_AUTO` is `default y` and derives the sector count from the partitions. The fixed fallback is 128 sectors. | M `boot/zephyr/Kconfig`:866-885. M `CMakeLists.txt`:509-536 |
| S2-12 | The RT1060 EVK qspi flash node has `write-block-size = <1>` and `erase-block-size = 4 KiB`. | Z `boards/nxp/mimxrt1060_evk/mimxrt1060_evk_mimxrt1062_qspi.dts`:41-42 |
| S2-13 | `BOOT_UPGRADE_ONLY` help: overwriting "prevents the fallback recovery". `BOOT_SWAP_USING_SCRATCH` help: suits heterogeneous sector layouts. Swap using offset help: faster and less wear than swap using move. | M `boot/zephyr/Kconfig`:600-622 |
| S2-14 | Direct-XIP with revert in the App needs `MCUBOOT_BOOTUTIL_LIB_FOR_DIRECT_XIP` for test and confirm. In the BL, `copy_done` is written when a selected image does not yet have it, and an image with `copy_done` set and `image_ok` unset is erased (whole slot). | Z `modules/Kconfig.mcuboot`:306-309, 424-430. M `loader.c`:2199-2229 |

### 0.2 Vendor documentation rows (all Not verified, vendor documentation)

These come from public NXP MCUXpresso SDK documents read for Stage 3 (repository `nxp-mcuxpresso/mcuxsdk-examples` at `3151bc057b8924db54c7395d39b213c05ab94dc3`, see [stage3-findings.md](stage3-findings.md#pinned-and-fixed-sources)). They describe NXP's own MCUboot port and extensions, not stock Zephyr MCUboot.

| ID | Claim | Source file |
| --- | --- | --- |
| V1 | The vendor SDK's MCUboot example for this board uses direct-XIP with a hardware flash-remap feature, a 256 KiB bootloader region and 2 MiB slots. | `_boards/evkbmimxrt1060/ota_examples/mcuboot_opensource/example_board_readme.md` |
| V2 | The vendor encrypted-XIP option for BEE uses a modified overwrite-only mode. The BL re-encrypts the staged image into the execution slot. Revert is not possible. A separate metadata sector holds the BEE configuration and a confirmation magic. | `ota_examples/_doc/encrypted_xip.md` (sections 3.1, 3.3), `_boards/evkbmimxrt1060/ota_examples/mcuboot_opensource/example_board_readme.md` |
| V3 | The vendor table lists BEE as supported with OVERWRITE_ONLY only, not with FLASH_REMAP. | `ota_examples/_doc/encrypted_xip.md` section 4 |
| V4 | BEE uses AES-CTR for the execution region. The upper 96 bits of the counter come from the configuration and the address field is supplied by hardware. BEE has two regions. The default key selection is a software key, usable without fuse programming. | `ota_examples/_doc/encrypted_xip_bee.md` sections 1 and 1.1 |

### 0.3 Repository rows (crux-pilot `f57ff22`)

| ID | Record | What it says that matters here |
| --- | --- | --- |
| R1 | #14 decision of 30 October 2026 | microSD on uSDHC1 with VFS and FatFs is the M4 writable medium. Firmware stays on QSPI NOR XIP. microSD avoids M4 data writes to the XIP NOR but not firmware image programming hazards. |
| R2 | `docs/flash-xip-evidence.md` (#22 AC3) | Phase matrix for Flash writes during XIP. Concrete inherited data-closure failure in the busy phase (FlexSPI configuration read from Flash). Startup, exception, DMA and other bus-master closure unknown. Physical-write blocker. |
| R3 | `docs/platform-m1-readiness.md` | Standalone image at `0x60000000` with FCB, IVT, boot data and DCD. `CONFIG_BOOTLOADER_MCUBOOT=n`. Upstream partitions are declarations only. |
| R4 | `docs/microsd-composition.md` (#147, merged `f57ff22`) | Card formatted FAT32 on a host. `/SD:/files` and `/SD:/settings` must exist. No format, no directory creation, fail closed, no retry, no unbind (card swap needs reboot). RAM is SDRAM. Not verified: card detect, power, timing, removal, uSDHC1 sharing with the M.2 SDIO path, cache and DMA placement, persistence. |
| R5 | #14 comment of 1 November 2026 | Open items on `main`: two File handles on the same name are not rejected, FatFs locking off. |
| R6 | `docs/physical-verification-plan.md` (M6) | Target `mimxrt1060_evk@B//qspi`. Enabling `lpspi1` for SPI takes the microSD pins away from M4 storage. Decision needed before P3. |

## 1. Modes in scope

Stage 1 found eleven sysbuild modes (A3, B10, B49). This matrix covers the eight that keep two application slots. Single slot, single app RAM load and firmware updater are excluded because they offer no in-field fallback to a second application image (A3, B10, B78).

| Mode | Sysbuild symbol to MCUboot symbol | Rows |
| --- | --- | --- |
| Swap using offset (sysbuild default here) | `BOOT_SWAP_USING_OFFSET` | A4, B11, B12 |
| Swap using move | `BOOT_SWAP_USING_MOVE` | B10, S2-9 |
| Swap using scratch | `BOOT_SWAP_USING_SCRATCH` | B10, B5 |
| Overwrite-only | `BOOT_UPGRADE_ONLY` | B10, S2-8 |
| Direct-XIP | `BOOT_DIRECT_XIP` | B27 |
| Direct-XIP with revert | `BOOT_DIRECT_XIP` + `BOOT_DIRECT_XIP_REVERT` | B28, S2-14 |
| RAM load | `BOOT_RAM_LOAD` | B49 to B52 |
| RAM load with revert | `BOOT_RAM_LOAD` + `BOOT_RAM_LOAD_REVERT` | B61 |

Common facts for every mode: the BL keeps the NXP boot header and DCD and executes in place from QSPI NOR at `0x60000000` (A7, A9, B87, and MCUboot `boot/zephyr/app.overlay`:1-5, which sets `zephyr,code-partition = &boot_partition`). Every BL write listed below therefore happens while the BL executes from the same NOR. The #22 closure obligations (R2) apply to the BL's FlexSPI NOR driver path. Whether MCUboot links the RAM-relocated driver path is **Not verified (no build)**.

## 2. Mode matrix

### 2.1 Who writes NOR, and when

| Mode | App NOR writes | BL NOR writes | Rows |
| --- | --- | --- | --- |
| Swap using offset | Whole image into the secondary slot from its second sector. Pending trailer fields in the secondary slot. `image_ok` in the primary trailer to confirm. All while the App executes in place from the primary slot. | Next boot: swap of both slots sector by sector up to the larger image size (plus trailer and status sectors), progress status in the primary trailer, then `copy_done`. On revert: a second swap of the same extent plus `image_ok`. Invalid secondary: slot erase. Corrected after independent review 1: the swap covers the larger image size, not the whole slot (M `loader.c`:1141-1197, 1227). | B12, S2-2, S2-3, S2-4, S2-5, S2-6, S2-7, B5 |
| Swap using move | As swap using offset, but the image starts at the first sector of the secondary slot. | As swap using offset, plus an initial move of every primary sector up by one. More erase cycles than offset. | S2-9, S2-13, S2-4 to S2-7 |
| Swap using scratch | As swap using offset. | Full swap through a scratch partition. Status writes go partly to scratch. The board devicetree has no scratch partition. | B5, B10, S2-4, S2-13 |
| Overwrite-only | Whole image into the secondary slot plus pending fields. No confirm write is needed (see 2.2). | Next boot: erase the primary slot up to the secondary image size, copy the secondary, scramble the secondary header and trailer. With downgrade prevention, a strictly older secondary is erased. Corrected after independent review 1: `BOOT_UPGRADE_ONLY` also defines `MCUBOOT_OVERWRITE_ONLY_FAST`, so the erase stops at the image size (M `mcuboot_config.h`:82-85, `loader.c`:977-991). | S2-2, S2-5, S2-8, B22 |
| Direct-XIP | Whole image into the inactive slot while executing in place from the active slot on the same NOR. No pending write is needed because the highest version wins. | Nominal boot reads only. An image that fails validation is erased, in either slot. | B24, B27, B65 |
| Direct-XIP with revert | Image into the inactive slot, with trailer magic. Pending request. `image_ok` in the active slot to confirm. All during XIP. | `copy_done` into the selected slot on its first selection. Whole-slot erase of a selected but unconfirmed image on the next boot. Erase of an invalid image. | B28, B63, B65, S2-14 |
| RAM load | Image into a slot. The App executes from SDRAM at that time, not from NOR. Whether every App section, vector and constant is outside NOR is **Not verified (no build)**. | Nominal boot reads only (copy to RAM with `flash_area_read`). Erase of an image that fails to load or validate, in either slot. | B49, B50, B65, B66, B68, B68a |
| RAM load with revert | Image into the inactive slot, pending request, `image_ok` into the active slot. App executes from SDRAM, same caveat as RAM load. Corrected after independent review 1: the stock pending request always writes slot 1, so it needs application code that marks the inactive slot, or images with trailer magic (B63a). | `copy_done` on first selection. Whole-slot erase of an unconfirmed image, or of a selected image without GOOD trailer magic. Erase of an invalid image. | B61, B63, B63a, B64, B65, S2-14 |

### 2.2 Test and confirm, including the App-side confirm path

| Mode | Test | Confirm and what the App needs | Rows |
| --- | --- | --- | --- |
| Swap (offset, move, scratch) | `boot_request_upgrade(0)` marks the secondary pending. The BL sees TEST and swaps. | `boot_write_img_confirmed()` writes `image_ok` into the primary trailer, from the App, while the App executes in place from that NOR. Needs Zephyr's flash driver, flash map and the DFU boot library in the App, and the #22 write-path closure (R2). `boot_request_upgrade(1)` skips test (PERM). | S2-1, S2-2, S2-3, R2 |
| Overwrite-only | None. TEST and PERM both lead to an overwrite. `image_ok` and `copy_done` writes are compiled out, so no REVERT state can arise. | No confirm step. Every accepted pending request is permanent. | S2-1, S2-5, S2-6, S2-8, S2-13 |
| Direct-XIP | None. | None. Highest valid version boots. | B10, B24, B27 |
| Direct-XIP with revert | Pending request on the inactive slot. BL writes `copy_done` when it first selects it. | App writes `image_ok` into its active slot, during XIP. Needs `MCUBOOT_BOOTUTIL_LIB_FOR_DIRECT_XIP`. The active slot comes from the `zephyr,code-partition` of the per-slot build. That each per-slot build sets this correctly is **Not verified (no build)**. | B28, B63, S2-3, S2-14 |
| RAM load | None. | None. | B60 |
| RAM load with revert | Pending request. BL writes `copy_done` on first selection. Corrected after independent review 1: the stock `boot_request_upgrade()` always opens slot 1. When the App runs from slot 1, the request writes nothing and the BL then erases the new image in slot 0 (B63a, Inference for the consequence). Rank 1 needs either App code that opens the inactive slot (from `BLINFO_RUNNING_SLOT`) and calls `boot_set_next(fa, false, permanent)`, or images signed with trailer magic (imgtool `--pad`). Whether mcumgr image management avoids this is **Not verified**. | App writes `image_ok` into the active slot (swap variant of `boot_set_next`, B63 as corrected). The App learns the active slot from bootloader information (`BLINFO_RUNNING_SLOT`). That needs MCUboot's share-data backend (off by default) and a `zephyr,bootloader-info` chosen node, which this board does not have. A retained memory region is needed. Its suitability on this board is **Not verified**. | B61, B63, B63a, B64, S2-3 |

### 2.3 Recovery from an unconfirmed or failed image

| Mode | Unconfirmed test image | Image that fails validation | Interrupted BL operation | Rows |
| --- | --- | --- | --- | --- |
| Swap (offset, move, scratch) | REVERT on the next boot: full swap back, then `image_ok`. | Invalid secondary is erased and FAIL is recorded. If the primary fails validation after a swap, it is not erased and boot stops (no second chance). | Resumed from the trailer status. | S2-1, S2-5, S2-6, S2-7 |
| Overwrite-only | No revert. The old image is erased when the copy starts. A valid but faulty image stays. | Invalid secondary erased, primary kept. | The secondary header and trailer are scrambled only after the copy, so a reset during the copy should find the request again. **Not verified** for every path (inference from the order in S2-8). | S2-7, S2-8, S2-13 |
| Direct-XIP | No revert. A valid but faulty newer image keeps booting. | Erased. The other valid image boots. | No BL copy exists. | B24, B65 |
| Direct-XIP with revert | Whole slot erased on the next boot. The other slot boots. | Erased. Other slot boots. | `copy_done` is written before validation. A failed validation erases the whole slot including the trailer. | B28, S2-14, B65 |
| RAM load | No revert. A valid but faulty newer image keeps booting. | Erased. The other valid image is loaded. | Copy is to RAM only, so a reset repeats it. | B60, B65, B50 |
| RAM load with revert | Whole slot erased on the next boot. The other slot is loaded. | Erased. Other slot loaded. | As direct-XIP with revert. | B61, B65 |

Watchdog-driven recovery of a valid but hanging image is not part of any stock path read. A test image that hangs before it confirms is recovered only after a reset. Who resets it (watchdog or operator) is an application and board decision. **Not verified** that the board watchdog survives into the BL in a way that helps.

The BL itself has one copy. Recovery of a damaged BL depends on boot ROM paths. **Pending Stage 3**. Corrected after independent review 1: Stage 3 did not answer this. Recovery of a corrupted bootloader through boot ROM paths is **Not verified** (reference manual, hardware).

### 2.4 Older-image rejection and its comparison basis

Comparison: major (uint8), minor (uint8), revision (uint16). Build number (uint32) only with `BOOT_VERSION_CMP_USE_BUILD_NUMBER`. Equal versions compare as 0 (B15, B16). imgtool's `--security-counter auto` uses major, minor and revision only (B48).

| Mode | Rejection by the BL | Equal values | Rows |
| --- | --- | --- | --- |
| Swap (offset, move, scratch) | Optional `MCUBOOT_DOWNGRADE_PREVENTION`, off by default. A strictly older secondary (or lower security counter, if that sub-option is set) is erased before TEST or PERM. Not applied to REVERT. | Accepted. The code comment says `<=`, the code does not. | B13, B18, B19, B20, S2-6 |
| Overwrite-only | Optional. A strictly older secondary is erased. Security counter option not available. | Accepted | B22, B23 |
| Direct-XIP | Not available (option excluded). Highest valid version is selected. Nothing rejects an older image when it is written. | Primary stays the candidate | B14, B24 |
| Direct-XIP with revert | As direct-XIP. Fallback to the older image after an unconfirmed boot is by design. | Primary | B24, B28 |
| RAM load, RAM load with revert | Option can be set but has no effect (code compiled only in the swap block). Highest valid version selected. Zephyr marks these modes `MCUBOOT_BOOTLOADER_NO_DOWNGRADE` because of the selection. | Primary | B14, B21, B60 |
| Any, with hardware rollback protection | Lower image security counter than the stored counter is rejected. Needs a non-volatile counter backend, which the Zephyr port does not provide at the pin. A trusted counter on RT1060 would need fuses. | Accepted | B25, B26 |

Consequence: in every stock mode an older **signed** image can be made to boot under some condition. In swap and overwrite, it happens if downgrade prevention is off. In direct-XIP and RAM load, it happens when the newer image is invalid, unconfirmed or erased. The update request path in the App must therefore apply its own version policy before it writes NOR.

### 2.5 Slot sizing on the upstream RT1060 EVK partitions

Upstream: `mcuboot` 128 KiB, two 3 MiB slots, `storage` 1,920 KiB, 4 KiB sectors (768 sectors per slot), write block 1 byte (A9, S2-12). Current Crux images use 53,576 to 107,872 bytes of Flash (Stage 4, [stage4.md](stage4.md) section 4.2). Sector count per slot is derived automatically by default (S2-11). Trailer figures below are arithmetic from the S2-10 formula with a write size of 1 byte. MCUboot's effective write alignment may be larger. **Not verified by build.**

| Mode | Slot constraint | Estimated trailer and overhead | Rows |
| --- | --- | --- | --- |
| Swap using offset | Equal sector sizes. Update starts at the second secondary sector, so one sector of the secondary is not image space. | Status 768 x 2 x 1 = 1,536 B, rounded to one 4 KiB sector. | B12, S2-9, S2-10 |
| Swap using move | Equal sector sizes. Primary needs one extra sector. | Status 768 x 3 x 1 = 2,304 B, rounded to 4 KiB, plus one 4 KiB move sector. | S2-9, S2-10 |
| Swap using scratch | Needs a `scratch_partition`, absent from the board devicetree. A devicetree change is needed. | Status 2,304 B, not rounded. Plus the scratch partition. | B5, S2-10 |
| Overwrite-only | Image plus magic and swap info. | No status area. | S2-10 |
| Direct-XIP, with or without revert | Each image is linked for its own slot address, so every release is built twice. `IMAGE_F_ROM_FIXED` lets the BL refuse an image linked for the other slot. | Magic and flags for revert. | B27, B28 |
| RAM load, with or without revert | Whole image including TLVs must fit the executable RAM window (`IMAGE_EXECUTABLE_RAM_START`/`_SIZE`, default MCUboot's `zephyr,sram`). Board `zephyr,sram` is 32 MiB SDRAM. The slot is still a 3 MiB NOR flash area. | Magic and flags for revert. | B52, B53, B55, B61 |

Every mode fits current images with large headroom. The bootloader size against the 128 KiB `mcuboot` partition is **Not verified (Stage 5)**. The vendor SDK example uses 256 KiB for its own MCUboot port (V1), which is a warning sign for headroom but not evidence about the Zephyr build.

### 2.6 Compatibility with address-bound XIP encryption (BEE)

MCUboot at the pin has no BEE support in the files read (A8, B72, B73). MCUboot image encryption is a different feature: at-rest protection of a staged image, decrypted by the BL during swap or load (B70). BEE's AES-CTR counter includes the access address (V4), so a ciphertext only decrypts at the address it was encrypted for. Key source and fuse needs are **Pending Stage 3**.

| Mode | Fit with stock MCUboot | Notes | Rows |
| --- | --- | --- | --- |
| Swap (offset, move, scratch) | Not compatible without re-encryption on every sector move. Offset and move also shift images by one sector inside a slot. | MCUboot image encryption works with swap but leaves the plaintext image in NOR after the swap. With swap and encryption the key may be stored in swap metadata unless `BOOT_SWAP_SAVE_ENCTLV`. | B70, B71, S2-9, V4 |
| Overwrite-only | Not compatible with stock MCUboot. The vendor's BEE path is a modified overwrite-only with BL re-encryption and no revert. | Closest to a vendor-supported encrypted path. That path is not stock and not Zephyr. | V2, V3 |
| Direct-XIP, with or without revert | Possible in principle if each slot image is encrypted offline for its own address and BEE regions cover both slots. MCUboot image encryption is rejected at compile time. Vendor lists BEE with overwrite-only only. | BEE has two regions. The vendor example reserves one for the bootloader. | B29, V3, V4 |
| RAM load, with or without revert | BEE is not needed for the App because the App executes from SDRAM. MCUboot image encryption works: the image is decrypted and authenticated in RAM. The plaintext then sits in external SDRAM. The decryption key sits in the BL. | BEE could still protect the BL region. Key protection is **Pending Stage 3**. | B58, B59, V4 |

## 3. microSD staging variants

| Variant | NOR writes that remain | Bootloader customisation | Fit by mode | Rows |
| --- | --- | --- | --- | --- |
| (a) App copies the image from microSD into the secondary NOR slot | All App NOR writes of section 2.1 remain: the image copy, the pending fields and the confirm. The microSD only moves the network receive phase away from the NOR write phase and allows a check of the complete file before any NOR write. | None. Stock flash-area access. | Any mode. Only in RAM load modes does the App write NOR while not executing from it. | B75, S2-2, S2-3, B63 |
| (b) Bootloader reads the image from microSD | If both slots lived on the card, trailers and erases would go to the card and no App or BL NOR write would remain after provisioning. If only the secondary lived on the card, swap and overwrite still write the primary NOR and confirm still writes the primary trailer. | Yes. MCUboot reaches storage only through `flash_area_*` on a Zephyr flash device. Needs either a flash-API driver over the SD disk (read, write, erase emulation, page layout, parameters) or replacement routines through `BOOT_FLASH_AREA_HOOKS` and related hooks. The BL would also need the SD host driver and, if the image is a file, a filesystem. | Best suited to RAM load modes, where the slot is only read and the trailer is small. Out of scope for #141 (plan 1.2). | B74, B75, B76, B77, B78, B79, S2-3 |
| (c) No microSD staging | All App NOR writes of section 2.1 remain. A network image goes straight to the NOR slot, or first to an SDRAM buffer and then to the NOR slot. | None. | Any mode. | S2-2, S2-3, B55 |

Variant (b) is the only route that can remove all NOR writes after provisioning. It needs bootloader customisation in every mode. No stock mode avoids both App and BL NOR writes during XIP while still providing test, confirm and recovery (sections 2.1 to 2.3). This is the expected result in plan section 6 and is recorded as a constraint, not as a design.

## 4. Execution model: QSPI XIP versus RAM load into SDRAM

| Aspect | XIP from QSPI (swap, overwrite, direct-XIP) | RAM load into SDRAM | Rows |
| --- | --- | --- | --- |
| Where the App executes during its own NOR writes | From the same NOR. Full #22 closure is needed for every App write (download, pending, confirm). | From SDRAM. The #22 App-side obligation reduces to showing that no App code, data, vector or literal used during the write is in NOR. **Not verified by build.** Corrected after independent review 1: the RAM link comes from sysbuild `CONFIG_XIP=n` and `CONFIG_FLASH_SIZE=0` (B68a), not from B68. | R2, B66, B68a |
| BL NOR writes during BL XIP | Swap: both slots rewritten up to the larger image size per update, and again per revert. Overwrite: erase and copy up to the image size. Direct-XIP: none nominally, erases on failure. | Plain: none nominally, erases on failure. Revert: one `copy_done` write per new image and a whole-slot erase (3 MiB) per revert. Corrected after independent review 1: swap and overwrite write per update only up to the image size, while the RAM load and direct-XIP revert paths erase the whole slot (M `loader.c`:1227, 977-991, 2209). Revert erase duration and watchdog behaviour are **Not verified**. | S2-4 to S2-8, B65 |
| Test, confirm, recovery | Swap modes and direct-XIP with revert provide them. | Only the revert variant provides them. It needs the share-data backend and a retained memory region. | B61, B64 |
| Older-image rejection | Optional in swap and overwrite. None in direct-XIP. | None by the BL. | Section 2.4 |
| Image encryption | Swap: MCUboot encryption works, plaintext in NOR after swap. Direct-XIP: not allowed. BEE: not with stock MCUboot. | MCUboot encryption works, decrypted into SDRAM. BEE not needed for the App. | B29, B58, B70 |
| SDRAM initialisation | The App keeps the boot header and DCD only in the standalone (no bootloader) image. With MCUboot, the BL's DCD initialises SDRAM for both. | Same. The App has no own path to initialise SDRAM. That the boot ROM runs the BL's DCD is vendor behaviour. | A7, B80 to B88 |
| RAM address conflict | BL and App both default `zephyr,sram` to SDRAM, but the App does not load there. | BL RAM and the image load address both default to the start of SDRAM. A devicetree or Kconfig change is needed to separate them, for example BL RAM in on-chip RAM and `IMAGE_EXECUTABLE_RAM_START`/`_SIZE` set explicitly to SDRAM. Whether an MCUboot board overlay already changes this is **Not verified** (B8). | B53 to B56, B8 |
| Boot time | No copy. | Whole image copied from QSPI to SDRAM at every boot, then hashed in RAM. **Not measured.** | B50, B57 |
| Runtime performance | Current evidence base. | Code fetch from SDRAM instead of cached QSPI and ITCM. Whether Crux's ITCM and DTCM placement is kept in a RAM-linked image is **Not verified**. | Stage 4 placement |
| RAM cleanup before jump | Not relevant. | `MCUBOOT_CLEANUP_RAM` is limited to Cortex-M4 and M33 and is not offered for this Cortex-M7 part. Consequence **Not verified**. | B69 |
| SDRAM execution analogue | Differs from designs that execute from external SDRAM loaded from block storage. | Close analogue for that execution path. The block storage itself is still NOR here. | [Plan](../../plans/pre-m8-investigation-plan.md) 1.4 |

### 4.1 Consequences for the #14 and #22 XIP write hazards

1. #14 removed M4 data writes from the XIP NOR (R1). Firmware update brings NOR writes back in every stock mode. #14 explicitly kept those hazards open.
2. In XIP modes, App-side update writes are exactly the case that #22 found blocked: a concrete busy-phase data-closure failure in the inherited driver image, and unknown exception, DMA and bus-master closure (R2). A network download adds Ethernet DMA activity, which #22 lists as an open bus-master item.
3. In RAM load modes, the App-side hazard changes form. The App no longer fetches from NOR, so the busy-phase fetch and data-closure items shrink to a placement check of the RAM-linked ELF. Bus masters that reference NOR, cache coherence after writes and FlexSPI reconfiguration still need review. **Not verified.**
4. The BL writes NOR while it executes in place in every mode that offers revert. The #22 matrix applies to the BL's FlexSPI driver path. No evidence exists for it. Plain RAM load and plain direct-XIP have the smallest BL write set (failure-path erases only) but no revert.
5. Variant (b) staging is the only way to take NOR out of the update write path. It needs bootloader customisation.

## 5. SDRAM, DCD and the `zephyr,sram` concern

| Finding | Effect on Stage 2 | Rows |
| --- | --- | --- |
| The board enables DCD and SDRAM by default. The DCD is linked only with the NXP boot header. | With MCUboot only the BL image carries the DCD. Every App image, XIP or RAM load, depends on the BL's DCD having run. | B80, B81, B82, A7, B86 |
| Zephyr's SoC code and the MCUboot files read do not initialise SEMC. | No second path brings up SDRAM. If the BL's DCD is wrong, both the BL (whose RAM is SDRAM by default) and the App fail. | B84, B88, B87 |
| `zephyr,sram` is 32 MiB SDRAM at `0x80000000`. Zephyr's imgtool call uses it as `--load-addr`. MCUboot's executable RAM window defaults to its own `zephyr,sram`. | RAM load needs the BL's RAM moved out of the load window, or the window and load address moved. Configuration change, not source change. **Not verified by build.** | B53, B54, B55, B56 |
| The share-data backend needs a `zephyr,bootloader-info` chosen node, absent on this board. | RAM load with revert needs a board overlay with a retained memory region. Whether SDRAM or on-chip RAM keeps it across the BL-to-App jump is **Not verified**. | B64 |

## 6. Network-delivered image and staging

A web-driven or network-delivered update means the image arrives over Ethernet while the App runs, and must be stored somewhere that survives a reset before the BL can use it.

| Question | Finding | Rows |
| --- | --- | --- |
| Staging medium | Stock MCUboot only reads slots that are flash areas on a flash device. The image must end in a NOR slot before reset (variants a and c), unless the bootloader is customised (variant b). | B74, B75 |
| Stream to NOR or buffer first | Streaming interleaves network DMA with NOR program and erase. In XIP modes this is the open #22 bus-master case. Buffering the whole image in SDRAM first allows a complete signature and version check, then one bounded NOR write phase with network activity quiesced. | R2, B55 |
| RAM capacity | 32 MiB board SDRAM. Images up to the 3 MiB slot size. In RAM load the running App also occupies SDRAM. Capacity looks sufficient for one image buffer. Network stack and App RAM needs are **Not verified**. | B55, A9, R4 |
| microSD as receive buffer (variant a) | Survives reset and allows resumable download. Needs an owned staging path on the card, which the #147 layout does not define. Does not remove App NOR writes. | R4 |
| Authorisation before network exposure (M9) | The BL verifies the signature at boot, so unsigned code cannot run. An unauthenticated request could still trigger NOR writes and erases (wear and #22 exposure), mark an image pending or permanent, or install an **older signed** image in modes without rejection (section 2.4). The update request must be authenticated and authorised, and the App should check signature, version and size before writing NOR. An App-side signature check needs an application crypto provider (#142, unscheduled). | Section 2.4, B25, B26 |

## 7. eMMC analogue: microSD on uSDHC

| Point | Finding | Label |
| --- | --- | --- |
| Why it is an analogue | Both are non-XIP block media on the uSDHC controller. Code cannot execute from them, so an image must be copied to RAM. This matches RAM load with a block source (variant b). | Plan 1.4. Inference. |
| What stock MCUboot supports | No SD, MMC or disk backend in the files read. A block source needs bootloader customisation. | B74 to B77, Verified for listed files |
| What it cannot show | eMMC boot partitions, RPMB, eMMC bus modes, managed-NAND wear behaviour and eMMC power-loss behaviour. A removable card also has card-detect, removal and card-variety issues that soldered eMMC does not. | Not verified (no eMMC on the board) |
| Boot ROM boot from microSD without fuse writes | **Pending Stage 3** (plan question 18). | Pending |
| Board limits | uSDHC1 may be shared with the M.2 SDIO path. `lpspi1` for SPI takes the microSD pins. Card detect, power, timing, cache and DMA placement are not verified. | R4, R6 |

## 8. Conflicts and interactions with #147

| Item | Conflict or interaction |
| --- | --- |
| Card layout | #147 requires `/SD:/files` and `/SD:/settings` and never creates directories (R4). A staging file (variant a) needs a new owned path or a decision to use `/SD:/files`. A raw slot area for variant (b) would conflict with the host-formatted FAT32 model. |
| Mount ownership | #147 gives the App composition sole ownership of mount, readiness and recovery. A BL that reads the card (variant b) would touch the card before the App, outside that policy. |
| Concurrent access | Two File handles on the same name are not rejected and FatFs locking is off (R5). A staging writer and a reader of the same file need an App-level rule. |
| DMA and cache | USDHC buffer and DMA cache placement is not verified (R4). Copying multi-megabyte images from the card makes this more important. |
| Pin and controller ownership | M.2 SDIO sharing (R4) and the `lpspi1` pin conflict (R6) could remove the microSD path from some compositions. A staging design that depends on the card inherits these decisions. |
| Card removal | No unbind hook. A card swap needs a reboot (R4). A staged image on a removable card can disappear between download and reset. |
| RAM | #147 RAM already lives in SDRAM (R4). In RAM load the whole App lives there too. No conflict found, but budgets must be combined. |

No direct contradiction with #147 was found. #147 does not provide a staging path, and variant (b) would conflict with its ownership model.

## 9. Not verified items carried forward

| Item | Needs |
| --- | --- |
| MCUboot size against 128 KiB, per candidate mode | Dev Container build (Stage 5, A1) |
| Effective write alignment and real trailer sizes | Dev Container build |
| RAM-load App ELF has no NOR-resident code, data, vectors or literals | Dev Container build and `readelf`/`nm` |
| BL `zephyr,sram` and executable RAM window after overlay, and whether MCUboot ships an RT1060 board overlay | Dev Container build with MCUboot checked out (also resolves B8 by listing) |
| Share-data backend and retained memory on this board | Dev Container build, then hardware |
| Which FlexSPI driver path the BL links and its #22 closure | Dev Container build and ELF analysis (Stage 6). Closure itself is not expected to close in #141. |
| Boot ROM runs the BL's DCD. SDRAM retention across the jump. RAM load copy time. Any revert or recovery behaviour | Hardware, not in #141 scope |
| BEE key source, fuse needs, region use. HAB. Boot from microSD | Stage 3 |
| Vendor rows V1 to V4 | Vendor documentation only |
| RAM load with revert upgrade request reaches the inactive slot (App code or padded images), and whether mcumgr image management handles it (added after independent review 1) | Stage 5 or 6 check of the App configuration and source path |
| Duration of a whole-slot revert erase under BL XIP, and watchdog behaviour during it (added after independent review 1) | Stage 6, then hardware |
| Recovery of a corrupted bootloader through boot ROM paths (added after independent review 1) | Reference manual, hardware |
