# Stage 1 answers for #141

Pins: Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0), MCUboot `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2`. Row references (A1, B10 and so on) point to [stage1-pinned-source-findings.md](stage1-pinned-source-findings.md). All answers show what the source allows. None shows what a board does. No build was run.

Correction added when the record was assembled: caveat 1 in section 1.1 and the B8 reference in section 2a are resolved by Stage 3 ([stage3-findings.md](stage3-findings.md) D11). MCUboot has no RT1060 board file at the pin, found by file-name listing. The effect on the MCUboot RAM choice is an inference and is Not verified by build. See the corrections table in [stage1-pinned-source-findings.md](stage1-pinned-source-findings.md#corrections-from-later-stages).

## 1. Stage 1 questions

### 1.1 Is sysbuild MCUboot available for `mimxrt1060_evk@B/mimxrt1062/qspi`?

Yes on source, with two caveats.

| Point | Result | Rows |
| --- | --- | --- |
| Sysbuild gating | MCUboot is offered for every board. `SUPPORT_BOOTLOADER_MCUBOOT_ZEPHYR` is `default y`. | B1, B2 |
| Board partitions | The qspi devicetree has `slot0_partition`, `slot1_partition` and a `mcuboot` partition, and `zephyr,code-partition` is slot 0. It has no scratch partition, so "swap using scratch" would need a devicetree change. | B5, A9 |
| Revision B | One qspi `.dts` serves revisions A, B and C. A revision B overlay changes LEDs only. | B4 |
| MCUboot support for the SoC flash | MCUboot's Zephyr port has an explicit FlexSPI branch for i.MX RT. | B6 |
| MCUboot sample list | `mimxrt1060_evk/mimxrt1062/qspi` is listed in `platform_allow`. The entry has no revision, so it means the default revision C. A listing is not a passing build. | A2 |
| Caveat 1 | Board-specific MCUboot files (`boot/zephyr/boards/...`) could not be searched. Whether MCUboot overrides the board's RAM choice is open. | B8 |
| Caveat 2 | The board defaults put RAM in SDRAM and rely on a DCD (see 3.4). This matters for MCUboot itself, not only for RAM load. | B55, B80, B87 |

The Stage 1 stop condition (RT1060-EVKB absent from MCUboot's Zephyr support) is not triggered.

### 1.2 Which modes exist?

Sysbuild lists eleven modes (A3, correcting Appendix A which listed nine and the range 41-139):

| Sysbuild mode | MCUboot symbol set by sysbuild | Needs | Rows |
| --- | --- | --- | --- |
| Single slot | `SINGLE_APPLICATION_SLOT` | slot 0 | B3, B10 |
| Swap using offset (default) | `BOOT_SWAP_USING_OFFSET` | slot 0 and 1, equal sector sizes | A4, B11, B12 |
| Swap using move | `BOOT_SWAP_USING_MOVE` | slot 0 and 1, equal sector sizes | B10 |
| Swap using scratch | `BOOT_SWAP_USING_SCRATCH` | slot 0, 1 and `scratch_partition` | B10 |
| Overwrite only | `BOOT_UPGRADE_ONLY` | slot 0 and 1 | B10 |
| Direct-XIP | `BOOT_DIRECT_XIP` | slots linked per slot, no encryption | B27, B29 |
| Direct-XIP with revert | `BOOT_DIRECT_XIP` plus `BOOT_DIRECT_XIP_REVERT` | as above, plus trailer magic | B28 |
| RAM load | `BOOT_RAM_LOAD` | image linked for RAM, `--load-addr` | B49, B52 |
| RAM load with revert | `BOOT_RAM_LOAD` plus `BOOT_RAM_LOAD_REVERT` | as above, plus trailer magic | B61 |
| Single app RAM load | `SINGLE_APPLICATION_SLOT_RAM_LOAD` | slot 0 as size source, optional `mcuboot,ram-load-dev` | B78 |
| Firmware updater | `BOOT_FIRMWARE_LOADER` | slot 0 and slot 1 updater | A3 |

Default without a scratch partition is swap using offset (B11). On this board that is the default.

### 1.3 Is validation on every boot the default?

Yes for swap modes. `BOOT_VALIDATE_SLOT0` is `default y` and maps to `MCUBOOT_VALIDATE_PRIMARY_SLOT` (A5). Direct-XIP and RAM load validate the active slot unconditionally, because the check at `loader.c`:2320 sits in the shared loop and has no option guard (A5, B57). In RAM load the check runs on the copy in RAM (B57).

### 1.4 Which crypto backend avoids new Zephyr crypto modules?

| Signature | Default backend | Zephyr crypto module needed? | Rows |
| --- | --- | --- | --- |
| ECDSA P-256 | tinycrypt from `ext/tinycrypt` in the MCUboot repository | No. The sources come from MCUboot's `ext/` directory. When Zephyr's built-in mbedTLS is off, the ASN.1 parser also comes from `ext/mbedtls-asn1` in the same repository. | A6, B32, B35 |
| Ed25519 | tinycrypt SHA plus `ext/fiat` curve code | No. Same reasoning. Default signs SHA-512 over SHA-256 of the image. | B33 |
| RSA (sysbuild default) | mbedTLS from Zephyr | Yes. | B31, A4 |
| None (hash only) | tinycrypt SHA-256 | No. No authenticity. | B34 |

Recommendation for the dependency question: ECDSA P-256 with tinycrypt. Sysbuild's default is RSA, so the choice must be set explicitly (A4, B3). The stop condition "every signature option needs a Zephyr crypto module" is not triggered.

### 1.5 Older-image comparison basis per mode

Comparison function: major, then minor, then revision (uint16). Build number (uint32) counts only when `BOOT_VERSION_CMP_USE_BUILD_NUMBER` is set. Equal returns 0 (B15, B16).

| Mode | Comparison and enforcement | Equal values | Where checked | Rows |
| --- | --- | --- | --- | --- |
| Swap offset, move, scratch | Off by default. With `MCUBOOT_DOWNGRADE_PREVENTION` a secondary image with a strictly lower version (or lower security counter, if that option is set) is erased and no swap happens. | Accepted. The function's own comment says `<=`, the code does not. | `check_downgrade_prevention`, only for test and permanent swaps, before the swap | B13, B18, B19, B20 |
| Overwrite only | With `MCUBOOT_DOWNGRADE_PREVENTION` a strictly lower secondary image is erased. Security counter option not available. | Accepted | `boot_validate_slot` | B22, B23 |
| Direct-XIP | No downgrade prevention (option excluded). Highest version wins. | Slot 0 stays the candidate | `find_slot_with_highest_version` | B14, B24 |
| Direct-XIP with revert | Highest version wins. An unconfirmed selected image is erased on the next boot and the other slot is chosen. This is a fallback to an older image by design. | Slot 0 | `boot_select_or_erase` and the same selection | B28 |
| RAM load, RAM load with revert | As direct-XIP. The Kconfig option `MCUBOOT_DOWNGRADE_PREVENTION` can be set, but the code that uses it is compiled only in the swap block, so it has no effect. | Slot 0 | Selection only | B14, B21, B60 |
| Any mode with hardware rollback protection | Image security counter lower than the stored counter is rejected. | Accepted | `bootutil_img_validate` | B25 |

Hardware rollback protection needs a non-volatile counter backend. None is in the Zephyr port's CMake at the pin (B26).

### 1.6 What does the bootloader provide for SoC XIP encryption?

Nothing at the pin in the files searched. MCUboot's encryption protects images in the secondary slot and decrypts them while it swaps or loads (B70). That is not on-the-fly flash encryption. No BEE reference appears in the two Zephyr i.MX RT Kconfig files or the nine MCUboot files read (A8, B72, B73). A whole-tree negative is Not verified. With swap and encryption, MCUboot may store the decrypted key in swap metadata on flash unless `BOOT_SWAP_SAVE_ENCTLV` is set (B71).

## 2. New questions of 1 November 2026

### 2a. RAM load mode

| Question | Answer | Label | Rows |
| --- | --- | --- | --- |
| How are images copied to RAM? | The bootloader reads the whole image (header, payload, protected TLVs, TLVs) from the flash area into RAM at `ih_load_addr` using `flash_area_read`. It then authenticates the RAM copy and jumps to `ih_load_addr + ih_hdr_size`. | Verified on pinned source | B50, B57, B66 |
| Which images are accepted? | Only images with `IMAGE_F_RAM_LOAD`. imgtool sets it when `--load-addr` is given. | Verified on pinned source | B51 |
| Test and confirm | Plain RAM load has none. Revert variant: the application marks the secondary slot pending (trailer write in NOR). The bootloader writes `copy_done` on selection. The application writes `image_ok` into the active slot trailer to confirm. | Verified on pinned source | B61, B63 |
| Revert | An image selected earlier but not confirmed is erased on the next boot. The other slot is tried. | Verified on pinned source | B61 |
| Older-image rejection | None beyond choosing the highest version. An older valid image boots if it is the only valid one or the newer image is invalid or unconfirmed. | Verified on pinned source | B60, B21 |
| RAM address and size limits | The load address comes from the header. It must lie inside `IMAGE_EXECUTABLE_RAM_START` to `+_SIZE`, which default to MCUboot's `zephyr,sram`. Zephyr's imgtool call also uses `zephyr,sram` as `--load-addr`. The whole image including TLVs must fit. | Verified on pinned source | B52, B53, B54 |
| Address conflict on this board | `zephyr,sram` is SDRAM (0x80000000, 32 MiB). With unmodified devicetree the bootloader's RAM and the image load address default to the same region. A change is needed to separate them. | Inference from verified lines. MCUboot board overlay Not verified | B55, B56, B8 |
| Combined with image encryption? | Yes. The image is copied, decrypted in RAM in 1024-byte chunks, then authenticated. Direct-XIP cannot use encryption but RAM load can. | Verified on pinned source | B58, B29 |
| Does the bootloader write Flash? | Not in a nominal boot of a valid image in plain RAM load (reads only). Yes in the revert variant (`copy_done` write, erase of an unconfirmed image) and yes for any image that fails to load or validate (slot erase, including the primary slot). | Verified on pinned source (read, not run) | B65, B61 |
| Does the application write Flash? | In the revert variant, yes: it writes `image_ok` into the active slot trailer. A pending request writes the secondary slot trailer. | Verified on pinned source | B63 |
| Extra requirement | The application finds its active slot through bootloader information, which needs MCUboot's share-data backend. It is off by default, and the board has no `zephyr,bootloader-info` chosen node. | Verified on pinned source | B64 |
| Operational note | A manually programmed image in the revert variant must be marked confirmed and must have a header. | Verified on pinned source | B62 |

Consequence for update-mode choice: RAM load removes execute-in-place from NOR for the application, but it does not remove NOR writes. Trailer writes by the application (confirm, pending) and bootloader erases of bad images remain. The XIP write analysis still applies to the bootloader and to those trailers. Whether the application's own flash driver can run from RAM while it writes NOR is Not verified.

### 2b. Signature algorithms, determinism and key embedding

| Algorithm | Available in MCUboot Zephyr port | imgtool call | Deterministic? | Rows |
| --- | --- | --- | --- | --- |
| ECDSA P-256 | Yes, tinycrypt default | `ec.ECDSA(SHA256())`, no deterministic option passed | Not verified. Source shows no request for deterministic signing. Library default not read. | B30, B32, B41 |
| Ed25519 | Yes, tinycrypt SHA plus fiat. `--pure` option signs the whole image. | `key.sign(data=digest)` | Not verified in library source. The scheme is deterministic by design (RFC 8032). | B33, B43 |
| RSA 2048 or 3072 | Yes, needs mbedTLS. Sysbuild default. | PSS, `salt_length=32` | Not verified in library source. PSS uses a salt, so signatures are expected to differ per run. | B31, B42 |
| ECDSA P-384 | imgtool can sign. The Zephyr port defines no `MCUBOOT_SIGN_EC384`. | `ec.ECDSA(SHA384())` | Not verified | B30, B44 |

The unsigned payload and its hash are reproducible. imgtool writes no timestamp, and the only random input is the encryption key (B45). Encrypted images are never byte-reproducible (B46). For the plan's reproducibility criterion ("signed images verify against the documented public key") no determinism is needed. imgtool can also export the digest or payload for external signing and accept a fixed signature (B47).

Key embedding path (B39): sysbuild passes `BOOT_SIGNATURE_KEY_FILE` to MCUboot. MCUboot's CMake runs `imgtool.py getpub -k <key>` into `autogen-pubkey.c` in the build directory. That file defines `ecdsa_pub_key[]` and its length. `boot/zephyr/keys.c` puts it in `bootutil_keys[]`, which holds exactly one key in this port (B38). The bootloader matches the key-hash TLV against it unless `BOOT_BYPASS_KEY_MATCH` is set (B40). Sysbuild's default key is MCUboot's public debug key and the build warns (B37).

### 2c. Loading from a non-flash block device

| Question | Answer | Label | Rows |
| --- | --- | --- | --- |
| Stock support for SD or eMMC? | No reference in the MCUboot Zephyr Kconfig, CMake, `main.c`, `flash_map_extended.c`, nor in Zephyr's sysbuild bootloader Kconfig or `Kconfig.mcuboot`. | Verified for the listed files. Whole tree Not verified. | B74 |
| Why not directly? | MCUboot reaches storage only through `flash_area_*`. In the Zephyr port that calls Zephyr's flash API on a flash device, reads page info from the flash driver, and reads erase capability from flash parameters. | Verified on pinned source | B75, B76 |
| What a custom backend needs | One of two routes. (1) A Zephyr flash driver that presents the block device with a flash API (read, write, erase emulation, page layout, parameters), then `fixed-partitions` under it. (2) Replace the `flash_area_*` routines, using `BOOT_FLASH_AREA_HOOKS` or a custom port file. The hooks `BOOT_IMAGE_ACCESS_HOOKS`, `BOOT_GO_HOOKS` and `FIND_NEXT_SLOT_HOOKS` exist for the image access, boot and slot selection parts. Project code must add the source files. | Verified on pinned source (hook options). The effort of each route is Not verified. | B75, B77 |
| Trailer and swap needs | Swap modes need erase and write of trailers on both slots. A read-only block source suits `SINGLE_APP_RAM_LOAD` or a RAM load variant better. The slot size is still read from a devicetree flash partition. | Verified on pinned source for the dependency. Suitability is an inference. | B78 |
| Single slot, runtime source | `FLASH_RUNTIME_SOURCES` allows a hook to pick the source at run time, in single slot mode only. | Verified on pinned source | B79 |

### 2d. SDRAM initialisation

| Question | Answer | Label | Rows |
| --- | --- | --- | --- |
| Does Zephyr's i.MX RT SoC code initialise SEMC? | No. `soc/nxp/imxrt/imxrt10xx/soc.c` has no SEMC or SDRAM code. The SoC option `NXP_IMX_EXTERNAL_SDRAM` only controls access (MPU region). | Verified on pinned source | B84, B85 |
| Does the board use a DCD? | Yes. `DEVICE_CONFIGURATION_DATA` and `NXP_IMX_EXTERNAL_SDRAM` default to `y` for the RT1060 EVK. `dcd/dcd.c` holds the byte array with SEMC register writes. The IVT in the boot header points to it. | Verified on pinned source | B80, B81, B82 |
| Is the DCD in every image? | No. It is built only with `NXP_IMXRT_BOOT_HEADER`. An application built for MCUboot has no boot header and no DCD. MCUboot itself keeps both. | Verified on pinned source | A7, B82 |
| Does the MCUboot Zephyr port initialise SEMC? | Not in the four files read. | Verified for the listed files. Whole tree Not verified. | B88 |
| How does an app loaded to SDRAM depend on this? | It relies on the bootloader's boot header DCD having initialised SDRAM before the bootloader's own startup, and on MCUboot staying in that state. The application has no path of its own to initialise SDRAM at the pin, and `zephyr,sram` is SDRAM on this board. | Verified on pinned source for the build logic. That the boot ROM runs the bootloader's DCD is vendor behaviour, Not verified here. | B55, B86, B87 |
| Warning in the board build | If SDRAM is the RAM base and no DCD is included, the build warns that it may not boot. | Verified on pinned source | B83 |
| SEMC driver and SoC SEMC node | Not found at guessed paths. | Not verified | B89 |

## 3. Licence check

| Component | Licence file path | SPDX identifier | Label | Row |
| --- | --- | --- | --- | --- |
| MCUboot | `LICENSE` | `Apache-2.0` (tag in `boot/bootutil/src/loader.c`:2) | Verified on pinned source | L1 |
| `ext/tinycrypt` | `ext/tinycrypt/LICENSE` | No tag in the files. Text is Intel 3-clause BSD plus micro-ecc 2-clause BSD, which I read as `BSD-3-Clause` and `BSD-2-Clause`. | Text Verified. Identifiers assigned by text comparison. | L2 |
| `ext/mbedtls-asn1` | No licence file found. Guessed names returned 404. | `Apache-2.0` in `src/asn1parse.c`:5, `src/platform_util.c`:6, `include/mbedtls/asn1.h`:8 | Tag Verified. Licence file Not verified. | L3 |
| `ext/fiat` (extra) | `ext/fiat/LICENSE` | `MIT` by text | Text Verified | L4 |

Appendix A said the mbedtls-asn1 licence was not verified. That is now partly closed: the source files carry the Apache-2.0 tag. A licence file for the directory was not found.

## 4. Points for the Stage 2 matrix

1. RAM load is a viable candidate on source. It is the only stock mode that avoids XIP for the application, and it works with encryption (B58).
2. RAM load does not remove NOR writes. Application trailer writes and bootloader erases remain (B63, B65).
3. RAM load has no older-image rejection beyond "highest version" (B60). Rollback protection would need the hardware counter and a backend that the port does not provide (B26).
4. The default RT1060 EVK devicetree puts both the bootloader RAM and the RAM load address in SDRAM (B55, B56). This needs resolving before any RAM load trial.
5. The share-data backend needed for application-side confirm in RAM load has no chosen node on this board (B64).
6. Direct-XIP excludes encryption and downgrade prevention (B14, B29).
7. Sysbuild's default signature is RSA. ECDSA P-256 must be chosen to avoid mbedTLS (A4, B31, B32).
