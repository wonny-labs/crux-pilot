# File root binding: a composition interface for products

Status: **proposed**, software only. It adds a public CMake target and a header for the Zephyr File root binding. It changes no File behaviour and no portable contract, and it authorises no hardware operation.

## The gap

A product must bind a mounted directory to the File provider before File can be used. The binding functions were declared only in `components/file/ports/zephyr/file_native.h`, reachable only by adding the port's source directory as a private include path. Crux's own samples and tests did that. A product with a strict include boundary could not reach the binding at all, and the rules did not say how product composition code should reach it.

## What kind of interface this is

| | Portable contract | Composition interface (this document) |
|---|---|---|
| Header | `<crux/file.h>` | `<crux/file_zephyr.h>` |
| CMake target | `crux::file` | `crux::file_zephyr` |
| Used by | Application code | The product's platform composition, the code that also mounts the file system |
| Names a provider | No | Yes, on purpose |
| Portable to another provider | Yes | No: `crux_file_linux_bind_root` is its Linux counterpart in `file_linux.h` |
| Native header included | None | None |
| Stability | The contract's compatibility rules | Same naming and compatibility rules as any public Crux interface; the signatures are the ones that already existed |

File cannot be used without native code in the composition: Crux never mounts, formats or selects media, so the product's composition mounts the file system with Zephyr's own services and then binds the root. Binding is therefore composition work by nature. A public header makes that work reachable; it does not make it portable.

```mermaid
flowchart LR
    APP[Application code<br/>native-free] -->|crux/file.h<br/>crux::file| API[File contract]
    COMP[Product platform composition<br/>mounts the file system] -->|crux/file_zephyr.h<br/>crux::file_zephyr| BIND[Root binding]
    BIND --> PROV[Zephyr File provider<br/>selected by Kconfig]
    API --> PROV
```

## What is built

| Change | Where |
|---|---|
| Public header with the three declarations and their ownership, lifetime, context and result documentation | `components/file/ports/zephyr/include/crux/file_zephyr.h` (new) |
| `file_native.h` now only includes that header, so the provider, tests and samples that use the old spelling keep working | `components/file/ports/zephyr/file_native.h` |
| Target `crux::file_zephyr`: an INTERFACE target that adds the include root and links `crux::file`; defined in each provider branch (RT1060 FatFs, Reliance Edge, simulators) | `components/file/ports/zephyr/CMakeLists.txt` |
| The microSD sample and the FatFs `native_sim` test use the public target and no private include path | `samples/storage_sd`, `tests/native/file_fatfs` |
| `sd_storage.c` now includes the public header, so the eight other builds that compile it link `crux::file_zephyr` too | `samples/storage_sd_interrupt`, `samples/storage_sd_probe`, `samples/storage_sd_replace`, `tests/native/file_sd_check`, `file_sd_composition`, `file_sd_interrupt`, `file_sd_probe`, `file_sd_replace` |
| Follow-up: the eMMC composition includes the public header, and the samples `storage_emmc` and `storage_reliance_sd` and the native_sim fixtures `file_emmc_composition` and `configuration_settings_reliance` that compile `emmc_storage.c` link the target. The three microSD samples `storage_sd_interrupt`, `storage_sd_replace` and `storage_sd_probe`, the two eMMC samples and the two fixtures no longer add the port directory as a private include path. Other builds keep the private spelling `file_native.h` | `samples/storage_emmc`, `samples/storage_reliance_sd`, `samples/storage_sd_*`, `tests/native/file_emmc_composition`, `tests/native/configuration_settings_reliance` |
| Host tests | `tests/build/test_file_zephyr_public.py` and `tests/build/test_file_zephyr_boundary.py` |

## Decisions taken

| # | Decision | Why |
|---|---|---|
| 1 | The target and header keep `zephyr` in their names | A native-free consumer check rejects any target or path that names Zephyr. That is the intended result: the target is for composition code, and application code must not link it. A neutral name would hide the role. |
| 2 | The target does not link the provider library | The provider is selected by Kconfig and appended to the Zephyr link by the module. The target only gives the header, so a product that selects a provider through Kconfig does not pick a second one by linking. |
| 3 | Only the declarations that already existed are published | No new function, no rename. The Linux names stay in `file_linux.h`. |
| 4 | The private header stays | Renaming every user is a larger change with no consumer benefit. |
| 5 | The header lives under `components/file/ports/zephyr/include/crux/`, not `components/file/include/crux/` | `CONTRIBUTING.md` places public headers in the owning component's `include/crux/`. This header names a provider, so it stays with the port and is exposed only through `crux::file_zephyr`; the portable `<crux/file.h>` is unchanged. `crux::queue_linux` in the native interface policy is a precedent for a documented composition target. The owner approved the public target and header ("a안으로 진행해줘", chat of the Crux Pilot session, 8 November 2026); the location and the composition role are this change's reading of that approval. |
| 6 | Management and HTTPS composition are not covered | They need a separate design that also depends on the Zephyr TLS socket route and licence decisions of the owner. |

## What a consuming product needs to do

A product that wants a strict boundary for its application code needs a composition zone: a directory of its own that is allowed to include `<crux/file_zephyr.h>` and Zephyr headers, kept apart from application code. Crux does not decide a product's layout, and the boundary check of a product is the product's. This change gives that zone a stable way in.

## Verification and what is untested

| Check | Where | Status |
|---|---|---|
| Header compiles alone with only the File contract include root and pulls in no native header; the private header declares nothing a second time; the public path is unique; the target definition exposes only the include root | `tests/build/test_file_zephyr_public.py`, Host | run locally and in Host CI |
| Every build that compiles `sd_storage.c` links the target; header lines fit the 100-column format limit; the header has a C++ guard and a lifetime rule that matches the File contract | `tests/build/test_file_zephyr_public.py`, Host | run locally and in Host CI |
| The ten builds that use the target (the FatFs `native_sim` test and the nine that compile `sd_storage.c`) | Zephyr CI: `scripts/test-file-rt1060-compile.sh` (RT1060 compile check of the samples) and `scripts/test-storage-simulators.sh` (`native_sim`) | **not run locally** (Docker, disk); left to CI |
| `scripts/check-format.sh` (clang-format 18) | Host CI | **not run locally** (not installed); the changed lines were kept to 100 columns by hand |
| The native-free consumer check rejects a consumer that links the target: a direct link, a transitive wrapper and the usage check, with the real guard and the real target definition, and the same projects pass against a weakened copy of the guard | `tests/build/test_file_zephyr_boundary.py`, Host | run locally and in Host CI |
| The follow-up builds: the three microSD samples (RT1060 compile, `scripts/test-file-rt1060-compile.sh`), `storage_emmc` (RT1060 link, `scripts/test-file-emmc-compile.sh`), `storage_reliance_sd` (RT1060 link, `scripts/test-file-emmc-compile.sh`; `native_sim` build only, `scripts/test-storage-emmc.sh`), and the two eMMC `native_sim` fixtures, which also compile `emmc_storage.c` (`scripts/test-storage-emmc.sh`) | Zephyr CI | **not run locally** (Docker, disk); left to CI |
| A product repository builds against the target | not done; the first product consumer is the evaluation of this interface | open |
| Any run on a board | none | not authorised and not part of this change |
