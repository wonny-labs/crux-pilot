# Manual RT1060 integration gate

This software-only gate is separate from standard hosted CI. Use implementation `1818298`, the pinned Zephyr `684c9e8` and NXP `2c2f28a`, the existing Linux workspace/SDK and a writable output directory. Do not run programming, reset or hardware tools. The actual retained commands/logs and source identities are indexed in [verification.json](verification.json).

## Reproduce

From the source root, adapt the workspace/SDK paths to the documented setup. These commands build the sample; they do not run its ARM image.

```sh
export CRUX_ROOT="$PWD"
export CRUX_WS=/ws
export CRUX_OUT=/tmp/m4-rt1060-integration
export PATH=/opt/crux-cache/venv/bin:$PATH
export ZEPHYR_BASE="$CRUX_WS/zephyr"
export ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1
export ARM_OBJDUMP="$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
west build -b mimxrt1060_evk@B//qspi samples/flash_target_candidate -d "$CRUX_OUT/arm" --   "-DZEPHYR_MODULES=$CRUX_ROOT;$CRUX_WS/modules/hal/nxp;$CRUX_WS/modules/hal/cmsis;$CRUX_WS/modules/hal/cmsis_6;$CRUX_WS/modules/fs/littlefs"   -DUSER_CACHE_DIR="$CRUX_OUT/cache"
python3 tests/native/rom_storage_candidate/prepare_reference.py "$CRUX_OUT/arm" "$CRUX_WS"
export PILOT_ELF="$CRUX_OUT/arm/zephyr/zephyr.elf"
export PILOT_BIN="$CRUX_OUT/arm/zephyr/zephyr.bin"
export PILOT_ORIGINAL_FCB="$CRUX_OUT/original-fcb.bin"
export COMMAND_CLOSURE_JSON="$CRUX_OUT/command-closure.json"
python3 -m unittest discover -s tests/native/rt1060_storage_integration -v
python3 -m unittest discover -s tests/native/rom_command_candidate -v
```

The first suite must report 12 methods, the second 33, no failures or skips. Missing image inputs cause skips and do not satisfy this gate. Independent ARM consumers are compile-only objects. Inspect the exact final disassembly, selected conditional/alias/indirect edges, stack frames, literal/data addresses and deferred native devices alongside the bounded scanner output. The previous 28-function/484-byte result is not a guarantee for a changed image.

Build real-engine simulator fixtures separately:

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=host
for fixture in maintenance_storage rt1060_storage_integration file_littlefs; do
  west build -b native_sim/native/64 "tests/native/$fixture" -d "$CRUX_OUT/$fixture" --     "-DZEPHYR_MODULES=$CRUX_ROOT;$CRUX_WS/modules/fs/littlefs" -DUSER_CACHE_DIR="$CRUX_OUT/cache" || exit 1
done
west build -b native_sim/native/64 tests/native/settings_readiness_gate -d "$CRUX_OUT/settings_readiness_gate" --   "-DZEPHYR_MODULES=$CRUX_ROOT" -DUSER_CACHE_DIR="$CRUX_OUT/cache"
(cd "$CRUX_OUT/maintenance_storage" && ./zephyr/zephyr.exe -flash_in_ram -flash@0_in_ram)
(cd "$CRUX_OUT/rt1060_storage_integration" && ./zephyr/zephyr.exe -flash_in_ram -flash@0_in_ram)
(cd "$CRUX_OUT/rt1060_storage_integration" && ./zephyr/zephyr.exe -flash_in_ram -flash@0_in_ram -testargs refused)
(cd "$CRUX_OUT/rt1060_storage_integration" && ./zephyr/zephyr.exe -flash_in_ram -flash@0_in_ram -testargs unprovisioned)
(cd "$CRUX_OUT/file_littlefs" && ./zephyr/zephyr.exe -flash_in_ram)
(cd "$CRUX_OUT/settings_readiness_gate" && ./zephyr/zephyr.exe -flash_in_ram)
scripts/check-dependencies.sh
```

Expected: maintenance 10, File 7, readiness 18, each full-composition scenario 1/1, all successful. Run executables from writable output directories; running the simulator in read-only source previously failed before the test. Retained local evidence used shorter output names `maintenance`, `native`, `file`, `readiness`; source/configurations and arguments are as above. Toolchains and caches remain outside this repository.

## Retained outcomes and limitations

The local archive retains RED/GREEN logs, current disassembly, linker/map/config/compile commands, ELF/BIN, original FCB reference and manifests. Root independently matched 99 owned sources, 20 pinned inputs and 52 archived artifacts; the task Reviewer also checks staged files. The dependency log is retained separately. Archive SHA-256 is recorded in verification.json; this is local retained evidence, not a published downloadable release.

Earlier failures are preserved: missing implementation/selection/deferral tests, the trial negative control, invalid deferred-init placement on a fixed-partitions node, application-mode library collection and the missed actual NOR parent deferral. Final source resolves these. An early command run skipped six image checks before image inputs were supplied; the final 33-method run has no skips. The final build retains an existing upstream USB duplicate-unit-address warning. Unchanged broad Host/sanitizer and historical ROM suites were not rerun without an affected requirement.

No target execution, physical persistence, adopted deadline, controller startup or production safety is demonstrated. All hardware/admission/production/merge/closure gates remain in force; see the [scope and blockers](../../rt1060-storage-integration.md).
