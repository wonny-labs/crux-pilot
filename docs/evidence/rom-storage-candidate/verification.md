# ROM-preserving candidate — software verification

This record extends the historical [maintenance checkpoint](../storage-maintenance/verification.md). It does not replace its artifact identities or turn its read-only context into a qualified Flash operation. **Physical execution/write: NOT READY.**

## Boot/profile slice

Source commit `c1d788d` adds the approved 100 MHz staged FCB and consumed read-only preflight. The exact EVKB-B build passes with a 42,104-byte BIN. Seven ROM/profile/artifact, six island, three snapshot and fifteen copied-driver checks pass (31 test methods, zero skips). Test methods include positive and negative cases; 31 is not a count of negative tests.

The RED record first failed four missing-feature tests, then the missing actual adapter. Updating the real preflight added one call, so the old one-call island assertion failed; its replacement requires the read-confirm call before copying and context entry. Final image checks resolve linker veneers and loop back-edges rather than assuming instruction-address order equals execution order. Initial tests incorrectly assumed FCB placement at `0x60000400`; actual pinned placement is `0x60000000`, BIN offset zero. This correction preserves the inherited layout rather than moving it.

The FCB comparison compiles the unchanged pinned original with the candidate's exact compile options. Its 512-byte block and the final ELF/BIN block differ only at offset `0x46`, enum 7→6; the whole image grew with the new code. [Artifact and input hashes](verification.json) identify this slice independently of later command-path builds. Independent Reviewer: spec PASS / quality PASS, no material findings; commands, identity, WIP and deadlines were outside that slice.

## Reproduce the boot/profile gate

Use the documented pinned Linux west workspace and SDK; `/ws/crux` below denotes this source checkout. Commands only compile software or inspect files. The explicit module list avoids duplicate west/worktree discovery. Output directories must be writable.

```sh
export PATH=/opt/crux-cache/venv/bin:$PATH
export ZEPHYR_BASE=/ws/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export XDG_CACHE_HOME=/tmp/rom-profile/cache
export CCACHE_DIR=/tmp/rom-profile/ccache
west build -b mimxrt1060_evk@B//qspi /ws/crux/samples/flash_target_candidate -d /tmp/rom-profile/build -- '-DZEPHYR_MODULES=/ws/crux;/ws/modules/hal/nxp;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6' -DUSER_CACHE_DIR=/tmp/rom-profile/cache
python3 /ws/crux/tests/native/rom_storage_candidate/prepare_reference.py /tmp/rom-profile/build /ws
export PILOT_ELF=/tmp/rom-profile/build/zephyr/zephyr.elf
export PILOT_BIN=/tmp/rom-profile/build/zephyr/zephyr.bin
export PILOT_ORIGINAL_FCB=/tmp/rom-profile/original-fcb.bin
export ARM_OBJDUMP=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump
for suite in rom_storage_candidate maintenance_island maintenance_boot flash_target_candidate; do
    python3 -m unittest discover -s /ws/crux/tests/native/$suite -v || exit 1
done
```

Use `c1d788d` to reproduce the historical boot/profile artifact; later revisions may intentionally extend the command-context checks and change image hashes. Missing artifact environment inputs can skip exact ARM checks, which does not satisfy the manual gate. Standard hosted CI does not run this focused gate. Rerun affected checks when staged source, command logic, linker/configuration or tests change.

## Command slice and final image

The final command slice builds a 42,880-byte EVKB-B BIN and passes 46 focused test methods, zero skips:

| Gate | PASS |
| --- | ---: |
| Actual selected native helpers/dispatcher and staged metadata | 11 |
| Exact command ARM placement/context/closure/refusal | 4 |
| ROM profile and FCB comparison | 7 |
| Existing island context | 6 |
| Existing early snapshot | 3 |
| Existing copied-driver error/wait regression | 15 |

The controlled fixture executes the selected pinned helper bodies and actual dispatcher with injected transfer/status/idle/refresh/terminal/time effects. It covers initial and post-command Busy, status/command errors, copied program input, invalid page/sector/owner requests, no refresh or ordinary return after uncertainty, and successful ready→idle→refresh ordering. The unchanged lower regression uses real staged native/HAL wait bodies with controlled effects. Controlled time is not a hardware deadline.

Self-review found and corrected three load-bearing gaps with recorded RED/GREEN: initial Ready confirmation before WREN, initial Ready before READ (later Ready cannot validate an earlier Busy read), and final generated-file hashes after the sibling stage. A failing ARM assertion also drove an explicit nonzero-result branch to the RAM terminal before context restoration. The old no-call context test now requires exactly the selected dispatcher call after RAM setup and retains the prior guards, stack and vector assertions.

Final selected static analysis covers 18 transitive functions in ITCM, private command/bounce/native metadata in DTCM, selected literals and a reported sum of 308 bytes of reachable static frames against the 1024-byte operation stack. These are exact-image software observations, not measured stack usage or runtime RAM-copy/exception validation. The 1024-byte exception stack remains separate. No selected call enters the original probe/configuration/reclock/LUT path or non-RAM libc/assert code. Generic refused driver helpers outside this graph are not qualified by that result.

The automated disassembly walk recognises direct B/BL edges; it is not an exhaustive ARM instruction parser. Conditional/CBZ/CBNZ branches and branch aliases/suffixes require supplemental inspection of the exact image. Independent review inspected those omitted instruction classes in this image and found their destinations internal to their own functions, without an escaping call or table/indirect dispatch. This manual step is part of the gate after relevant code, configuration, compiler or image changes; the automated pass alone does not establish closure of another image. Static-frame output likewise requires checking actual prologue forms and absence of recursion/dynamic stack use.

The source still uses Zephyr's relocation initialization for the added driver regions; runtime comparison of all those regions is **not implemented**. JEDEC 9Fh, actual elapsed timing and File/Configuration target binding are also unfinished. Ordinary target submission and init/media guards refuse; even unsupported direct lower preparation reaches the target's deliberately expired leaf before any command. No successful fixture result enables target submission.

The reproduction recipe above applies to the final command image when run at the final source revision. Add `rom_command_candidate` to the suite loop; optionally set `COMMAND_CLOSURE_JSON=/tmp/rom-profile/closure.json` to retain the function/frame audit. Use commit `c1d788d` only for the earlier boot/profile artifact. Final source/staged/artifact identities are in [verification.json](verification.json); source-path/debug differences can change rebuilt ELF hashes, so compare the stated tested inputs and semantic checks rather than promising path-independent binary reproducibility. No unrelated full Host suite was rerun for sample-private changes.

## Scope limits

The candidate's ordinary native init/read/write/erase interfaces remain refused. Register-derived clocks, controlled status/error inputs and ARM disassembly do not establish fitted-part behaviour, elapsed timing, Cortex-M exception execution, actual watchdog/reset state, master exclusion or persistence. No production layout, backend, Flash driver or Settings patch is adopted. M4 and its physical acceptance remain incomplete.
