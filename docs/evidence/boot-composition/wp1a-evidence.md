# WP1a evidence: layout of the boot composition, re-baseline and M7 guard (RT1060-EVKB)

| Field | Value |
| --- | --- |
| Work | Work package WP1a of [issue #296](https://github.com/wonny-labs/crux-pilot/issues/296): Task 0 (re-baseline), Task 1 (MCUboot in the manifest), Task 2 (layout) and the baseline of Task 6. Tasks 3 to 5 and 7 are not done. |
| Date | Builds and checks run on 8 November 2026, 06:50 to 07:50 UTC. |
| Evidence class | **Cross-compilation and `native_sim` builds only (build-only).** This is not Host test, CI or physical-board evidence. No image was programmed, booted or run, and no probe, Flash, reset, fuse, HAB or key-generation operation took place. |
| Labels | **Verified by build**: read from the output of a build that exited 0. **Inference**: derived from source or output, not run. **Not verified**: not checked, or needs a board. |
| Approvals | The scope was approved by the owner on 7 November 2026 for Tasks 0, 1 (steps 1 and 2) and 6 (step 1), with MCUboot's published public test key for build-only signing. The manifest change of Task 1 step 3 and the files of Task 2 were approved by the owner on 8 November 2026. The pull request description quotes both. |
| Not committed | The build logs and the build directories. The commands below reproduce them. |

## 1. Inputs and pins

| Item | Value |
| --- | --- |
| `crux-pilot` at the measurement | `1447750dd3f0dd0bc8a016be6bac8998ee2478c0` |
| Relation to the pull request base | The base is a later `main`, `3ef758d`. `git diff --name-only 1447750 3ef758d` lists only files under `docs/`, `tests/build/` and `tests/target/` (Python tooling). Nothing under `components/`, `samples/`, `zephyr/`, `cmake/`, `scripts/` or in `west.yml` changed, so the firmware inputs are identical (**Inference**: the builds were not repeated at `3ef758d`). |
| Zephyr | `dccb09599635bdff17633fa7e9dab014b91dce90` (v4.4.2), as `west.yml` pins. |
| MCUboot | `6d3b3d2c38ab20c242e5b9abb04d050086383eb2` (`v2.4.0`), the revision in Zephyr's `west.yml` at the Zephyr pin. The #141 trial used `ee39e2d` (`v2.4.0-rc1-3`). |
| Other projects | cmsis `512cc7e`, cmsis_6 `30a859f4`, fatfs `f4ead3b`, hal_nxp `7554bc0f`, littlefs `8f5ca34`. |
| Environment | The Dev Container image (Ubuntu 24.04, amd64, emulated on an arm64 host, 4 GiB, 2 CPUs). Zephyr SDK 1.0.1, `arm-zephyr-eabi-gcc` 14.3.0, CMake 3.28.4, Ninja 1.11.1, west 1.4.0, Python 3.12.3, GNU readelf 2.42. `native_sim` builds used the host toolchain, GNU C 13.3.0. |
| Python for imgtool | A separate virtual environment with the 52 packages of the #141 trial (identical `pip freeze`), imgtool 2.4.0 from the MCUboot clone. |
| Workspace | A scratch west workspace outside the repository, with `crux-pilot` at `1447750`. The repository worktree and the developer's own west workspace were not modified. |

## 2. Task 0: RAM load with revert at the current pins

Command, with the paths of the scratch environment (`<ws>` is the scratch workspace, `<in>` holds the four trial inputs, byte-identical to `docs/evidence/boot-update/trial-inputs/build-a-*`):

```text
cd <ws> && west build --sysbuild -p always -b 'mimxrt1060_evk@B//qspi' -d <out>/t0-build-a <ws>/crux-pilot/samples/controller/zephyr -- \
  -DZEPHYR_EXTRA_MODULES=<ws>/crux-pilot -DEXTRA_ZEPHYR_MODULES=<mcuboot clone> \
  -DSB_CONFIG_BOOTLOADER_MCUBOOT=y -DSB_CONFIG_MCUBOOT_MODE_RAM_LOAD_WITH_REVERT=y \
  -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"<mcuboot clone>/root-ec-p256.pem\" \
  -Dmcuboot_EXTRA_CONF_FILE=<in>/build-a-mcuboot.conf -Dmcuboot_EXTRA_DTC_OVERLAY_FILE=<in>/build-a-mcuboot.overlay \
  -Dzephyr_EXTRA_CONF_FILE=<in>/build-a-app.conf -Dzephyr_EXTRA_DTC_OVERLAY_FILE=<in>/build-a-app.overlay
```

**Verified by build: exit 0, 373 s, no source patch.** The checks of [Stage 5](../boot-update/stage5-trial-builds.md) sections 3 to 6 all hold again:

| Check | #141 trial (v4.4.0, `1d6ac4f`, MCUboot `ee39e2d`) | Now (v4.4.2, `1447750`, MCUboot `6d3b3d2c`) |
| --- | --- | --- |
| MCUboot mode | `BOOT_RAM_LOAD`, `BOOT_RAM_LOAD_REVERT`, `BOOT_VALIDATE_SLOT0`, ECDSA P-256 | Same |
| Boot header of the MCUboot image | `FCFB` at offset 0, IVT at `0x1000`, vectors at `0x60002000` | Identical |
| MCUboot Flash use, 128 KiB partition | 51,760 B (39.49 %) | **51,668 B (39.42 %)**, headroom 79,404 B |
| MCUboot RAM | 14,656 B, `0x20200000` to `0x20203940` | Identical |
| Application | 0 B Flash, 41,536 B RAM | Identical |
| Unsigned application binary | 32,880 B | 32,900 B |
| Signed application image | 33,032 B, `img_size 0x7c70`, TLV area `0x98` | **33,051 B**, `img_size 0x7c84`, TLV area `0x97` (an ECDSA signature varies in length by one byte) |
| imgtool sign arguments | `--header-size 0x400 --slot-size 3145728 --align 1 --load-addr 0x80000000`, no `--pad`, no `--confirm` | Same, and a second command (finding F4) |
| Flash driver in MCUboot | FlexSPI NOR driver in ITCM, 8,544 B ITCM | Same |

The KEYHASH TLV of the signed image, `e30466f6b8470c1f29070b17f1e2d3e94d445e3f608087fdc711e4382bb538b6`, is the SHA-256 of the public key of `root-ec-p256.pem`, MCUboot's published test key. **That key is public and is for build-only use. It is never used on a board.** The SHA-256 of the MCUboot `zephyr.bin` is `fb78e8ee490f5652a5dbf86557323806c03110c280ecf25847e2767f894bd855`, and of the application `zephyr.bin` `73e0220bf2f85d5a6ff68705494bddbbc2bd343597eee434edf5439bd5ef981c`. The signed file's hash varies with the signature and cannot be compared.

## 3. Sysbuild mechanisms at the pinned Zephyr

Paths are in Zephyr `dccb09599635bdff17633fa7e9dab014b91dce90`. The label is the strongest that applies: only the first row was exercised by a build.

| # | Mechanism | Source | What it does | Label |
| --- | --- | --- | --- | --- |
| 1 | `-D<image>_EXTRA_CONF_FILE`, `-D<image>_EXTRA_DTC_OVERLAY_FILE` | `cmake/modules/configuration_files.cmake:95-96` (`zephyr_get(... SYSBUILD LOCAL MERGE REVERSE)`) | A per-image list that is merged with the image's own files and applied after them. | **Verified by build** at v4.4.2 for `mcuboot` and `zephyr` (section 2). |
| 2 | Image name of the main application | `share/sysbuild/cmake/modules/sysbuild_images.cmake:29-30` | `DEFAULT_IMAGE` is the base name of the application directory. `samples/controller/zephyr` is the image `zephyr`, a product's `app` directory is the image `app`. | Inference. Consistent with section 2, where `-Dzephyr_*` took effect. |
| 3 | `sysbuild/<image>.conf`, `sysbuild/<image>.overlay` beside the application | `share/sysbuild/cmake/modules/sysbuild_extensions.cmake:263-306`, with the guard at 280-285 | Picked up automatically, but only when `<image>_EXTRA_CONF_FILE` and `<image>_OVERLAY_CONFIG` are not set. The directory is the application's, so a file in this repository is not reached without copying. | Inference. |
| 4 | `sysbuild/<image>/` as the image's configuration directory, with `boards/<board>.conf` and `.overlay` | `share/sysbuild/cmake/modules/sysbuild_extensions.cmake:264-270`, `cmake/modules/configuration_files.cmake:33-42, 71-72` | Board files per image, again beside the application. | Inference. Same limit as row 3. |
| 5 | `sysbuild.conf` and `SB_EXTRA_CONF_FILE` | `share/sysbuild/cmake/modules/sysbuild_kconfig.cmake:40-50` | The `SB_CONFIG_*` options, from a file beside the application or from a named file. | Inference. The three `SB_CONFIG_*` options were passed with `-D` in section 2, which is the form documented. |
| 6 | Snippets, `SNIPPET`, `<image>_SNIPPET`, `snippet_root` of a module | `share/sysbuild/cmake/modules/sysbuild_snippets.cmake:5-14`, `cmake/modules/snippets.cmake:62, 95-104`, `doc/develop/modules.rst:929-934` | A snippet named with `-D<image>_SNIPPET` could carry a board's conf and overlay and be found through a module's `snippet_root`, so the product would pass a name and no paths. Crux's `zephyr/module.yml` has no `snippet_root`. | Inference. Not tried. |
| 7 | MCUboot module directory and default key | `share/sysbuild/images/bootloader/CMakeLists.txt:6-18`, `share/sysbuild/images/bootloader/Kconfig:197-203` | The bootloader image is built from `ZEPHYR_MCUBOOT_MODULE_DIR`. The ECDSA P-256 key defaults to `<module>/root-ec-p256.pem`. | **Verified by build**: the build used the default path and logged the warning that it is for debug use. |
| 8 | Confirmed image for RAM load with revert | `modules/Kconfig.mcuboot:274`, commit `c219dde148e` (1 July 2026, in v4.4.2, not in v4.4.0) | `MCUBOOT_BOOTLOADER_MODE_RAM_LOAD_WITH_REVERT` implies `MCUBOOT_GENERATE_CONFIRMED_IMAGE`. | **Verified by build** that `zephyr.signed.confirmed.bin` is produced (finding F4). |

## 4. Layout chosen for the composition

**Decision proposed for review.**

| Question | Choice |
| --- | --- |
| Where the files live | `samples/boot_composition/boards/<board>/` with `mcuboot.conf`, `mcuboot.overlay`, `app.conf` and `app.overlay`. For the RT1060-EVK Rev B, the board directory is `mimxrt1060_evk_mimxrt1062_qspi_B`, as in `samples/storage_emmc/boards/`. |
| How a product selects it | The eight arguments of [the build documentation](../../boot-composition.md#the-argument-set): three `SB_CONFIG_*` options and four per-image file arguments (mechanism 1), plus `ZEPHYR_EXTRA_MODULES`. Nothing is copied. |
| Why this | Mechanisms 3 to 5 look only beside the application, so a product repository would have to copy files. Mechanism 1 is the one that was built, and it takes a path. `samples/` is where this repository keeps composition files, so no new top-level directory and no new public contract is needed. The files contain a board's memory placement and nothing of an application, which keeps them valid for any application on that board. |
| Public surface | None added: no header, no CMake target, no Kconfig symbol. The argument set is documentation. `CONTRIBUTING.md` keeps sample-owned files out of the platform API, and the guide says so. |
| Rejected | Files copied by the product (the plan's other option). A `sysbuild/` directory in a Crux sample as the only route (works for a Crux sample, not for a product). Snippets (mechanism 6): an attractive route with a name and no paths, but not tried. It can replace the paths later without changing the files' content. |
| The M7 reference composition | Unchanged. The files are added beside it, and no file under `samples/controller/` is touched. |
| Open | The slot sizes in the overlays are provisional until a product measures its image with the boot loader. Whether the application overlay may stay board-level once a product has its own flash layout is a question for the task that adds the files. |

## 5. Task 1: `mcuboot` in a group `crux-boot` (scratch workspace)

The manifest change was first tried in a scratch clone (the repository `west.yml` was untouched). The pull request makes the same change in the repository.

| State | Result |
| --- | --- |
| Group defined, default filter | `west manifest --validate` exit 0. `west list --all` shows `mcuboot` in `crux-boot`, with no clash with Zephyr's `name-allowlist`. `west update` fetches nothing for it. `west crux-pins` is identical to the unmodified manifest. |
| `+crux-boot` | `west update` clones exactly `6d3b3d2c`. `west crux-pins` adds `OK mcuboot 6d3b3d2c38ab` and reports `OK` for the rest. |
| Filter removed again | `mcuboot` is inactive again, the checkout stays on disk, `west crux-pins` is identical to the first state. |
| Sysbuild configure (`--cmake-only`) | With the group on, the section 2 arguments without `-DEXTRA_ZEPHYR_MODULES` exit 0 and `mcuboot` is a Zephyr module. With the group off and the checkout still on disk, configure exits 1 with `No download info given for 'mcuboot'`. |

All **Verified** by the commands named. The repository tests are listed in the pull request.

## 6. The M7 baseline

Builds of the M7 configurations of `scripts/test-controller.sh` and `scripts/test-controller-integration.sh`, with the scripts' arguments and the build directories outside the repository. All 12 exited 0. `firmware_identity` and its digest come from `firmware_identity()` and `firmware_digest()` of `scripts/record-build-provenance.py`, target `mimxrt1060_evk@B//qspi`. The full records are in [`m7-baseline-1447750.json`](m7-baseline-1447750.json).

| Configuration | `zephyr.bin` B | `zephyr.bin` SHA-256 | `firmware_digest` |
| --- | --- | --- | --- |
| controller-evkb | 38,208 | `ae37237a9cbfdb173e8fd341c7d9a1e612784bbe7ae3c10eb832f77358f024e4` | `5fb5e61747f5200a1704201a2a064e76d4dd7552bb3716f2b71aa4c3ca980db6` |
| m3-probe-evkb | 58,792 | `75620f445a35ccf0464420dbc117a8684dd33a42c625d3e80ad6252f61598fb6` | `9159e00147f8e958e51326ba1193898e39ff7a5600559d7a6968f9d3c03fb99a` |
| m3-normal-evkb | 42,060 | `c0407fa41f1a4366106a57e92ebfb54c68b0a2ad4c86cc04da4e579de9b6cf4e` | `bd071ce9a08de0f43dc3fa408baff85bfae5cbc6645175c17bb831faba554a18` |
| m3-network-evkb | 110,404 | `f9550f094b125a951f5c6aee944edae5eadf57649418d581ce3a3d87e9ed362e` | `1fa67eb27263ee204c40d4e8a6ade8a9f913ce266561f907a0b7eb9b0f120c5c` |
| m3-mdio-candidate-evkb | 110,488 | `5f952f29fb605e63c8f81be566c982f021d3d5ddf0b9711e078671924ea5d01a` | `52e7fae22c14370cd203a772f43e8f13a00a950642ed1b1f9c72bccccad24cf1` |
| m3-mdio-trace-evkb | 111,976 | `ce20bbfe700934fb63daf46c60f07835d5b671ba9c594100f244bdc1a89616a1` | `ac199b9f8e3f434e048e6138d2ee1a6f384da39264a240822a7d98532915baa7` |
| m3-deferred-output-evkb | 117,544 | `200c6a6f00741d3d35727359ca08fe840666fc6c0f3aafd2a80ea13b57f30c5b` | `02d6b5ed6d5e27c5dc30f14d215a25caf8bbe96cc67d9aa5dc1081f85a3464d1` |
| m3-native-output-evkb | 115,664 | `ddd5f865581a1d21ab402d3385e8b0205d63cd01ff99699ff3ce104387b589a1` | `b629cc37220f4d0310e8625c7d3da1dc6637ce5c648c28b60e8ffbc2c348fed6` |
| m3-integrated-probe-evkb | 124,464 | `07d0469cfeceffb3c9924f2cf6858dbd673a3be5afec929c0f04834bb036378e` | `ac08e74b07cc981422b17c7fda1fe54258529efb85b2de61bbb93140f4a6be80` |
| m3-integrated-normal-evkb | 108,132 | `50c10f4fb9c1e04d14b9ab8a892b216a23545056f25a8938e72b4214ffa0efad` | `e4db4298a1bc487820d19d948ab83caaff73e16a61636c0e6d2b03b2cca69f37` |

The two `native_sim` builds (`native_sim/native/64`) have no `zephyr.bin`. Both exited 0 and passed `scripts/check-controller.py`. Their `configuration_sha256` is `1e3fa05a69ef2ad88d619be4d79e2c77a0ab3f0dc53924e77bd07c7be4d4a839` (controller-normal) and `e24110905cfe1c58a1a7e9875f0ddea242912d2dd0caf7cce3253e8f11f46b42` (controller-missing-rtc).

**Reproducibility (Verified by build).** Rebuilding `controller-evkb` and `m3-probe-evkb` at another build path gave identical `zephyr.bin` hashes and identical `firmware_identity`. Rebuilding `controller-normal` at another path gave the same `configuration_sha256` and a different `zephyr.exe` hash, so a `native_sim` image is compared by `configuration_sha256` and the scenario check, never by `zephyr.exe`.

Not built: the Twister suites and `test_driver.py` of `test-controller-integration.sh`, which are tests and not M7 images, and `scripts/record-build-provenance.py` end to end, which needs a build directory inside the repository (only its identity functions ran).

## 7. Findings

- **F1. `main` moved.** The plan cited `ce50bcf`. The measurement is at `1447750` and the pull request base is `3ef758d`, with no firmware input changed in between (section 1).
- **F3. The public test key is the sysbuild default.** No key argument and no key generation is needed for a build-only slice (section 3, row 7).
- **F4. The build also writes a padded, confirmed image.** `zephyr.signed.confirmed.bin` is 3,145,728 B, the slot size, with MCUboot's trailer magic and the confirm flag at the end. It follows from a Zephyr change after v4.4.0 (section 3, row 8). It bears on the earlier note in Stage 5 section 9 that the default signed image would be erased by RAM load with revert, and on owner decision 13 about padded test images: the stock build now makes the padded variant itself. How MCUboot treats it is **Not verified**.
- **F5. imgtool dependencies.** The Python of the bootstrapped Dev Container has no `cryptography` and no `cbor2`, which imgtool imports (**Inference**: no build was run there). A signing build needs a Python environment that has them. Changing the Dev Container is outside this pull request.
- **F6. `native_sim` has no `zephyr.bin`** and its `zephyr.exe` depends on the build path (section 6).

## 8. Not verified

- Anything about booting: ROM acceptance of the layout, the jump into the application, shared boot information, confirm and revert, and the padded image. A board is needed.
- Rejection of an oversized or misaligned image, and reproducibility of the bootloader and signed output (later WP1a tasks).
- A build through the `crux-boot` group alone, and a build from the `samples/boot_composition/` files (the files are not added yet).
- Rank 2 (swap using offset) at the current pin, and the RT1170-EVK Rev A1 bootloader build.
- The `west update` behaviour of an unmodified product repository that imports this manifest, beyond the repository's own workspace test.
