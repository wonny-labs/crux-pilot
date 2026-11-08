# Boot composition: MCUboot with an application (build only)

**Status: proposed by the pull request that adds this page, and the documented interface of the composition if that pull request merges.** It is a build recipe for work package WP1a of [issue #296](https://github.com/wonny-labs/crux-pilot/issues/296). It is **build-only**: nothing here was programmed, booted or run on a board, and a successful build does not show that the boot loader accepts the image. The evidence, with the source line of every sysbuild mechanism it relies on, is in [the WP1a evidence record](evidence/boot-composition/wp1a-evidence.md). Statements carry one of three labels: **Verified by build** (read from the output of a build that exited 0), **Inference** (derived, not run) and **Not verified**.

## What this is

A product that wants a boot loader builds its application with [sysbuild](https://docs.zephyrproject.org/latest/build/sysbuild/index.html), which builds MCUboot and the application as two images and signs the application. The composition is a set of arguments to `west build`. It adds files that Crux keeps in this repository, so the product copies nothing. It does not change the [M7 reference composition](../samples/controller/README.md), which still builds and boots without a boot loader.

The first composition is RAM load with revert on the RT1060-EVK Rev B (`mimxrt1060_evk@B//qspi`): MCUboot is configured to run from the QSPI Flash, validate the signed image in slot 0, copy it to the SDRAM, jump to it and revert an image that is not confirmed. That behaviour is the intent of the configuration and is **Not verified** (see the end of this page).

## Prerequisites

| Need | How | Status |
| --- | --- | --- |
| MCUboot source at the revision Zephyr pins | `west config manifest.group-filter -- +crux-boot`, then `west update mcuboot`. The group is defined in [`west.yml`](../west.yml) and described in the [Product Composition Guide](product-composition.md#composition). Do not add a plain `mcuboot` entry or pass `-DEXTRA_ZEPHYR_MODULES`. | The group, its fetch and `west crux-pins` are **Verified** by workspace commands. A sysbuild **configure** without `-DEXTRA_ZEPHYR_MODULES` exits 0 with the group on and exits 1 with it off. A full build through the group was not run: the builds below used `-DEXTRA_ZEPHYR_MODULES` and a plain MCUboot clone at the same revision. |
| imgtool and its Python packages | A Python environment that has what imgtool imports, for example `cryptography`, `cbor2`, `intelhex` and `click`, besides what `west` needs. | **Inference:** the Python of the unmodified Dev Container bootstrap has no `cryptography` and no `cbor2`, which imgtool imports, so the signing step would fail there. The builds below used a separate virtual environment. |
| Zephyr SDK | The Dev Container's. | Zephyr v4.4.2 with SDK 1.0.1, `arm-zephyr-eabi-gcc` 14.3.0. |

## The argument set

The arguments below are the interface of the composition. `<crux>` is the checkout of this repository, `<app>` is the application directory of the product and `<image>` is the **base name of that directory**: sysbuild names the main image after it (`share/sysbuild/cmake/modules/sysbuild_images.cmake:29-30` in the pinned Zephyr), so the controller sample in `samples/controller/zephyr` is the image `zephyr`.

```text
west build --sysbuild -b 'mimxrt1060_evk@B//qspi' <app> -d <build> -- \
  -DZEPHYR_EXTRA_MODULES=<crux> \
  -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
  -DSB_CONFIG_MCUBOOT_MODE_RAM_LOAD_WITH_REVERT=y \
  -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y \
  -Dmcuboot_EXTRA_CONF_FILE=<crux>/samples/boot_composition/boards/mimxrt1060_evk_mimxrt1062_qspi_B/mcuboot.conf \
  -Dmcuboot_EXTRA_DTC_OVERLAY_FILE=<crux>/samples/boot_composition/boards/mimxrt1060_evk_mimxrt1062_qspi_B/mcuboot.overlay \
  -D<image>_EXTRA_CONF_FILE=<crux>/samples/boot_composition/boards/mimxrt1060_evk_mimxrt1062_qspi_B/app.conf \
  -D<image>_EXTRA_DTC_OVERLAY_FILE=<crux>/samples/boot_composition/boards/mimxrt1060_evk_mimxrt1062_qspi_B/app.overlay
```

- **Files.** The four files do not exist at these paths yet. They are added by the next WP1a task. Until then the argument set was built with files of identical content, copied from [`evidence/boot-update/trial-inputs/`](evidence/boot-update/trial-inputs/), outside the repository. The `.conf` and `.overlay` files are board-level: they place the boot-information region, move MCUboot's RAM to OCRAM2 and point its executable window at the SDRAM. They hold nothing of an application.
- **A product's own files.** `<image>_EXTRA_CONF_FILE` and `<image>_EXTRA_DTC_OVERLAY_FILE` are merged lists, so a product adds its own files to the same argument, separated by a semicolon. One that sets either variable also switches off sysbuild's automatic pick-up of `sysbuild/<image>.conf` and `sysbuild/<image>.overlay` beside the application (`share/sysbuild/cmake/modules/sysbuild_extensions.cmake:280-285`), so a product that relies on those files names them in the argument. This is **Inference** from the source; the build did not exercise it.
- **No key argument.** At v4.4.2 the key defaults to MCUboot's published test key `root-ec-p256.pem` (`share/sysbuild/images/bootloader/Kconfig:197-203`) and the build logs a warning that it is for debug use. **That key is public. Images signed with it must never be programmed to a board, and no release is signed with it.** A product signs with its own key, generated and kept outside every repository, and passes it with `-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"<path>\"`. This repository holds no key and no key-generation step.
- **Board.** Only the RT1060-EVK Rev B is composed. Another board needs its own four files, and a build for it is a separate result.

## What the build produces

For the controller sample at current `main` and Zephyr v4.4.2 (Verified by build, exit 0, 373 s in the emulated Dev Container):

| Output | Value |
| --- | --- |
| MCUboot Flash use against the 128 KiB `mcuboot` partition | 51,668 B of 131,072 B (39.42 %), headroom 79,404 B |
| MCUboot RAM | 14,656 B in OCRAM2, `0x20200000` to `0x20203940` |
| Application | 0 B Flash, 41,536 B RAM at `0x80000000` |
| Signed application image | 33,051 B (`zephyr.signed.bin`), RAM-load flag set, load address `0x80000000`, key hash of the public test key |
| `zephyr.signed.confirmed.bin` | 3,145,728 B, the slot size |

**The build also writes a padded, confirmed image.** At v4.4.2, RAM load with revert implies `MCUBOOT_GENERATE_CONFIRMED_IMAGE` (`modules/Kconfig.mcuboot:274`), so the build runs a second imgtool command with `--pad --confirm`. The result has MCUboot's trailer magic and the confirm flag at the end of the slot, so MCUboot is expected to treat it as already confirmed (**Inference**). Neither this file nor `zephyr.signed.bin` has been booted, so how MCUboot treats either is **Not verified**. A flow that tests revert must use the unconfirmed `zephyr.signed.bin`.

## Checking that the reference composition is untouched

The M7 configurations of [`scripts/test-controller.sh`](../scripts/test-controller.sh) and [`scripts/test-controller-integration.sh`](../scripts/test-controller-integration.sh) are the guard. At revision `1447750dd3f0`, ten EVKB configurations and two `native_sim` configurations built with exit 0. The `zephyr.bin` SHA-256 and the `firmware_identity` of the EVKB images are in [the evidence record](evidence/boot-composition/wp1a-evidence.md#6-the-m7-baseline) and in [`m7-baseline-1447750.json`](evidence/boot-composition/m7-baseline-1447750.json). Rebuilding two of them at another build path gave identical hashes.

- **EVKB images:** compare `zephyr.bin` and `firmware_identity` with the record.
- **`native_sim` images:** there is no `zephyr.bin`, and `zephyr.exe` differs with the build path. Compare `configuration_sha256` and the result of `scripts/check-controller.py`.

## Not verified

- Booting: the ROM accepting the MCUboot layout, the jump into the application, the shared boot information, confirm and revert, and the padded image. They need a board.
- Rejection of an oversized or misaligned image, and reproducibility of the bootloader and the signed output.
- The RT1060-EVK Rev B composition with `samples/boot_composition/` files in place, and a build through the `crux-boot` group alone.
- The RT1170-EVK composition.
