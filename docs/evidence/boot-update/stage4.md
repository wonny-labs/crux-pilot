# Stage 4: RT1060-EVKB placement re-verification (read-only)

Evidence status labels: **Verified by artefact** = read from a committed build output or map (or a retained CI artefact, none read). **Verified on source** = pinned upstream file or repository test assertion, not a built output. **Not verified** = no artefact or source read.

## 4.1 Compositions and how they are built

| Composition | Build path | Notes |
| --- | --- | --- |
| CI zephyr job (`zephyr.yml`) | `scripts/test-controller.sh` (native_sim plus `mimxrt1060_evk@B//qspi`), `scripts/test-controller-integration.sh` (`mimxrt1060_evk@B//qspi`, M3 profiles), `scripts/test-m5-integrated-smoke.sh` (`CRUX_BOARD_TARGET`, default `mimxrt1060_evk@B//qspi`), `scripts/test-file-rt1060-compile.sh` (`mimxrt1060_evk@B/mimxrt1062/qspi`), `scripts/test-system.sh`, `scripts/test-execution.sh`, `scripts/test-portable-execution.sh`, `scripts/test-gpio.sh`, `scripts/test-i2c.sh`, `scripts/test-spi.sh`, `scripts/test-serial.sh` | Wired in `.github/workflows/zephyr.yml` lines 82 to 161. `test-m3-rt1170.sh` is RT1170 (deferred) and out of scope. |
| Manual or single-board builds | `scripts/build-target.sh` (board parameter, writes `.config`, `zephyr.dts`, `zephyr.stat`, `zephyr.map`), `scripts/test-flash-poc.sh` (`mimxrt1060_evk@B//qspi`, PoC), `scripts/record-build-provenance.py` | Provenance recording script. Not run here. |
| Any others | `scripts/test-networking.sh`, `scripts/test-networking-physical.sh`, `scripts/test-logging.sh`, `scripts/test-messaging.sh`, `scripts/test-time-busy-wait.sh`, `scripts/measure-messaging.sh` reference `mimxrt1060_evk` in some form | Not each traced to a composition. |

Upload list for the zephyr job: `.github/workflows/zephyr.yml` lines 193 to 215 retain `zephyr/.config`, `zephyr/zephyr.dts` for EVKB direct cross-builds, `zephyr/zephyr.map` for `system-evkb`, and `m5-integrated-evkb` `provenance.json`, `rom.json`, `ram.json`.

## 4.2 Placement table

| Composition | CONFIG_FLASH_LOAD_OFFSET | CONFIG_USE_DT_CODE_PARTITION | CONFIG_BOOTLOADER_MCUBOOT | CONFIG_NXP_IMXRT_BOOT_HEADER | FCB / IVT / boot data / DCD | Highest Flash load end (exclusive) | Load segments | Source | Evidence status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `reference-normal` (baseline fd9e341, historical) | `0` | `n` | `n` | `y` (CONFIG_XIP `y`) | `0x60000000` / `0x60001000` / `0x60001020` / `0x60001030` | `0x6001a560` (bin 107,872 bytes) | Flash `0x60000000` filesz `0x1a1b0`. RAM VMA `0x80000000` LMA `0x6001a1b0` filesz `0x3ac`. Flash `0x6001a55c` filesz `4` | `docs/evidence/storage-configuration/verification.json` `.builds.reference-normal` (config, selected_symbols, load_segments) | **Verified by artefact** (committed record. `.config` sha256 `c505bab7…` recorded, file not committed) |
| `execution_runtime` (map, origin/main `efd40de`, source `b9df57d`) | Not verified (null) | Not verified (null) | Not verified (null) | Not verified (null) | `0x60000000` (map 1880) / `0x60001000` (1884) / `0x60001020` (1888) / `0x60001030` (1893) | `0x6000d148` (`_flash_used` 0xd148, map 5065. `.last_section` LMA 0x6000d144, map 5062) | `.itcm` LMA `0x600022c0` (map 1914). `.dtcm_data` LMA `0x6000d144` (map 3940 region) | `docs/evidence/m5-execution/execution_runtime-evkb.map.gz` | **Verified by artefact** for addresses. CONFIG values **Not verified**. |
| `portable_execution` (map, origin/main `efd40de`, source `b9df57d`) | Not verified (null) | Not verified (null) | Not verified (null) | Not verified (null) | `0x60000000` (1898) / `0x60001000` (1902) / `0x60001020` (1906) / `0x60001030` (1911) | `0x60010348` (`_flash_used` 0x10348 = 66,376 bytes, map 5289) | `.itcm` LMA `0x600022c0` (map 1932). `.ramfunc` LMA `0x600102e4`. `.dtcm_data` LMA `0x60010344` | `docs/evidence/m5-execution/portable_execution-evkb.map.gz` | **Verified by artefact** for addresses. CONFIG values **Not verified**. |
| Latest main CI EVKB builds (run `41351833959`, head `f67e22a`) | Not verified | Not verified | Not verified | Not verified | Not verified | Not verified | Not verified | Retained artefact `zephyr-41351833959-1` (id 13103136355, listed, not expired) | **Not verified**: blob download refused (HTTP 403, egress proxy `connect_rejected`). Nothing was rebuilt. |

Upstream partitions (DTS, verified on source at `684c9e8`, `boards/nxp/mimxrt1060_evk/mimxrt1060_evk_mimxrt1062_qspi.dts`): `mcuboot` `0x000000–0x020000` (lines 53 to 56), `image-0` `0x020000–0x320000` (58 to 61), `image-1` `0x320000–0x620000` (63 to 66), `storage` `0x620000–0x800000` (68 to 71). The chosen code partition is `slot0_partition` (line 13). Matches the plan. The DTS node names are `boot_partition`, `slot0_partition`, `slot1_partition` and `storage_partition`, with labels `mcuboot`, `image-0`, `image-1`, `storage`.

128 KiB check: `reference-normal` is 107,872 bytes, `execution_runtime` uses 53,576 bytes and `portable_execution` uses 66,376 bytes. None overlaps `image-0` (`0x60020000`) by used bytes. The power-of-two region alignment for `portable_execution` reaches `0x60020000` (map 3940). That is an alignment artefact, not overlap.

## 4.3 What the existing documents state

| Document | Section | What it states | Check result |
| --- | --- | --- | --- |
| `docs/platform-m1-readiness.md` | Lines 51 to 105, "ROM, image placement and writable storage" | FCB `0x60000000`, IVT `0x60001000`, boot data `0x60001020`, DCD `0x60001030`. `CONFIG_FLASH_LOAD_OFFSET=0`, `CONFIG_USE_DT_CODE_PARTITION=n`, `CONFIG_BOOTLOADER_MCUBOOT=n`, `CONFIG_NXP_IMXRT_BOOT_HEADER=y`. Partitions table. Standalone image 65,964 bytes Flash, 8,448 bytes RAM | Header addresses and CONFIG values **verified by artefact** (`reference-normal` record and both maps). The 65,964 byte figure is **not verified**: it matches no retained map or record, and the composition is not identified (see 4.4). |
| `docs/storage-configuration-assessment.md` | Lines 70 to 73 and table | Same header addresses and CONFIG values as the readiness doc. `CONFIG_FLASH_LOAD_SIZE=0` | Matches the `reference-normal` record. |
| `docs/flash-xip-evidence.md` | Lines 17 to 37 (compositions I and P) | ITCM relocation Flash LMA `0x600022c0` size `0x2160` for I. Vectors at Flash `0x60002000`. P is a PoC ELF | Lines 30 and 36 are about different inherited and PoC ELFs. They do not state FCB, IVT or DCD addresses for the EVKB compositions. `.itcm` LMA `0x600022c0` also appears in both EVKB maps, which is consistent. |

## 4.4 Unreconciled or not verified

1. The 65,964 byte standalone Flash figure in `docs/platform-m1-readiness.md` line 70 has no matching artefact. Neither `reference-normal` nor the two maps gives it.
2. The `*-evkb.config` files cited by `docs/evidence/m5-execution/README.md` are retained locally only. The `.config` for `execution_runtime` and `portable_execution` cannot be checked.
3. The provenance JSON (`scripts/record-build-provenance.py`) for these maps is not retained, so no hash match was possible.
4. The latest main run's `.config`, `zephyr.dts`, `zephyr.map` and `m5-integrated-evkb` JSON files were not read. The blob host is refused by policy. No rebuild was attempted: there is no SDK or Dev Container in this environment.
5. Kconfig defaults for `FLEXSPI_CONFIG_BLOCK_OFFSET` and `IMAGE_VECTOR_TABLE_OFFSET` on `mimxrt1062` were not found. Only the MIMXRT1011 default (`0x400`) appears in `soc/nxp/imxrt/imxrt10xx/Kconfig.defconfig`. The effective addresses are verified from the maps, not the symbol values.
6. `tests/target/m5_image.py` and `tests/target/m3_image.py` assert `CONFIG_FLASH_LOAD_OFFSET=0`, `CONFIG_XIP=y`, `CONFIG_NXP_IMXRT_BOOT_HEADER=y` and `# CONFIG_BOOTLOADER_MCUBOOT is not set` for the M3 and M5 images. These are **Verified on source** (test assertions), not built output. It is not confirmed that these checks run against the latest main build.
7. Fuse and lock state, ROM recovery and authentication paths are out of scope here (`docs/platform-m1-readiness.md` lines 98 to 100).
