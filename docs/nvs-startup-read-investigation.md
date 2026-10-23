# NVS initial-scan read-error experiment

## Scope and candidate

This software-only PoC follows the accepted Settings investigation in PR #44 and
Crux baseline `753dee7cba8ae89638686a37036b79de474cbd54`. It addresses only the
two Flash comparison calls in the initial sector scan of `nvs_startup()` in
Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (4.4.0).

The [six-line candidate](../tests/native/nvs_startup/startup-read.patch) returns
negative read errors through the existing exit path before classifying a sector.
Positive non-erased and zero erased comparisons retain their existing meaning.
No Flash driver, Settings provider, partition, public API or production build is
changed. The original checkout is never patched: the test hook validates the pin,
clean NVS directory and source SHA-256, rejects destinations inside the checkout,
then patches a disposable build copy with
zero fuzz and replaces exactly one NVS source registration. A fresh staged inode
and atomic file replacement prevent existing output symlinks/hardlinks from
redirecting writes into upstream files. This is a correction
candidate for review, not permission to adopt it.

## Failure and test method

The original scan uses a negative comparison as a closed-sector indication; the
second comparison also fails to distinguish negative errors. Recovery may then
erase sectors and return success. Disabling `NVS_INIT_BAD_MEMORY_REGION` does
**not** make mount read-only or prevent every recovery erase.

The native_sim fixture compiles the actual NVS engine and simulated Flash driver.
A private kernel-mode device/API proxy delegates normal operations to that
simulator and injects `-EIO` on the first read, second read after a closed-sector
marker, or every read. It counts engine write/erase calls and checks a pre-existing
marker through the original simulator device after mount. Setup writes/erases
are deliberately outside these counts. The two-sector test area is simulator
storage only; it is not the proposed QSPI layout. Device copying is test-only,
restricted to native_sim, the Flash simulator and `CONFIG_USERSPACE=n`.

Each bad-region initialization setting (off/on) runs three fault cases and one
normal empty-mount control. Thus eight is the total case count, including two
positive controls. This is not eight negative tests or coverage of every startup
read. A failed mount must return the original `-EIO`, leave `fs.ready` false,
perform no engine write/erase and preserve the marker. The empty-mount control
also verifies that normal initialization still writes its GC-done metadata.

## Reproduce and review gate

Use the documented Linux west workspace, clean pinned Zephyr, host toolchain,
Python/west/Twister, CMake/Ninja, Git and patch. The default `native_sim` target is
32-bit; GCC multilib, 32-bit libc development files and the generic `gcc-multilib`
header setup are required. Set paths for your workspace:

```sh
export ZEPHYR_BASE=/path/to/workspace/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=host
CRUX=/path/to/workspace/crux-pilot
python3 "$CRUX/tests/native/nvs_startup/test_prepare.py"

# Expected RED: two controls pass, six fault cases fail; exit nonzero.
python3 "$ZEPHYR_BASE/scripts/twister" -T "$CRUX/tests/native/nvs_startup" \
  -p native_sim -O /tmp/nvs-original \
  --extra-args=CRUX_NVS_STARTUP_CANDIDATE=OFF -j2 --inline-logs

# Expected GREEN: all eight cases pass.
python3 "$ZEPHYR_BASE/scripts/twister" -T "$CRUX/tests/native/nvs_startup" \
  -p native_sim -O /tmp/nvs-candidate -j2 --inline-logs

# Unchanged upstream healthy tests, using the candidate copy.
python3 "$ZEPHYR_BASE/scripts/twister" -T "$ZEPHYR_BASE/tests/subsys/kvss/nvs" \
  -p native_sim -s keyvalstorage.nvs.cache -s keyvalstorage.nvs.data_crc_cache \
  -O /tmp/nvs-healthy-candidate --extra-args=CRUX_NVS_STARTUP_CANDIDATE=ON \
  --extra-args=CMAKE_PROJECT_INCLUDE="$CRUX/tests/native/nvs_startup/candidate.cmake" \
  -j2 --inline-logs
```

The candidate option defaults on **only in this isolated test application**.
Check generated build commands to confirm the candidate copy is compiled instead
of the original NVS source; ordinary Crux builds do not select this hook.

Rerun all three commands and independently review results after changing the
patch, harness or pin. This documented manual verification gate is proportionate
for an unadopted experiment. Standard Host/Zephyr CI does not execute these tests;
CI success alone cannot satisfy the gate. No new CI infrastructure is introduced.

## Results and retained evidence

See [results](evidence/nvs-startup/results.json) and the adjacent per-suite
handler logs and [independent review](evidence/nvs-startup/review.md) for exact cases, input/candidate hashes, configurations and measured
operation counts. Full builds and reports are retained locally; the repository
retains compact evidence rather than generated binaries.

| Run | Result |
| --- | --- |
| Original fault fixture | 2 controls pass, 6 fault cases fail as expected |
| Candidate fault fixture | 8/8 pass, including 2 controls |
| Unchanged upstream NVS suites with candidate | 36/36 pass |
| Copy-destination safety regressions | 4/4 pass after two RED stages |

For first/second transient faults, the original returns zero and erases two/one
sectors respectively, losing the marker in both initialization modes. Persistent
faults return `-EDEADLK` without erase when bad-region initialization is off, but
return `-EIO` only after an erase when on. The candidate returns `-EIO` on all six
fault cases, with zero engine writes/erases and marker `0xa5` intact.

An independent review caught a header-order build failure after formatting; the
private header now follows public type declarations. Those two blocked builds
are not counted as executed tests. Original/candidate final-source reruns above
supersede them. Initial environment setup also required correcting West workspace
context and 32-bit header dependencies; the baseline counts include only executed
successful runs. Full failed-attempt logs remain in local investigation outputs.
The destination and existing-file symlink/hardlink guards were tested separately and final copy generation
rechecked to match the exact candidate source hash compiled by all candidate runs.

The unmodified upstream baseline separately exercised 36 NVS engine cases on
32-bit native_sim (lookup cache enabled, data CRC off/on), and 14 registered
Settings/NVS functional cases on native_sim/native/64: all 50 passed. These are
healthy baseline tests, not fault-injection or physical recovery qualification.
The engine fixture uses its upstream 1 KiB simulated erase geometry; the startup
probe uses two 4 KiB sectors. Neither adopts target storage geometry.

## Remaining decisions and limitations

- The candidate stops these two scan read failures. Other read, repair, GC,
  write/erase, cache, corruption and interrupted-operation paths are not qualified
  by this experiment. There is no rollback or transactional guarantee.
- Settings traversal/read-length/repair proposals in PR #44 remain separately
  unadopted. Actual provider error semantics (including suppressed handler errors),
  persistence and recovery acceptance remain incomplete under Issue #16.
- Simulator execution establishes neither target Flash behavior nor XIP, startup,
  exception, interrupt or DMA closure. Image/erase protection, production layout,
  native patch adoption and physical validation retain the #14/#15/#16 gates.
  No board programming, erase, reset or other hardware operation was performed.
- Prefer a minimal upstream-compatible correction with these regressions over a
  permanent private fork. No upstream submission or acceptance is claimed.
  Crux Zephyr Native Integration owns any separately approved downstream upkeep:
  one six-line patch, pin/hash checks, simulator fixtures and semantic review on
  each pin change. Human adoption, upstream contribution/sign-off and physical
  validation decisions are separate; passing this PoC does not grant them.
