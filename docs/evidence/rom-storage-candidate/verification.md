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

## Command slice at `7ef53d9`

That command slice builds a 42,880-byte EVKB-B BIN and passes 46 focused test methods, zero skips:

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

At `7ef53d9`, the source used Zephyr relocation without consumed validation of the added driver regions. The follow-up below closes that software gap. JEDEC 9Fh, actual elapsed timing and File/Configuration target binding are also unfinished. Ordinary target submission and init/media guards refuse; even unsupported direct lower preparation reaches the target's deliberately expired leaf before any command. No successful fixture result enables target submission.

The reproduction recipe above applies to the final command image when run at the final source revision. Add `rom_command_candidate` to the suite loop; optionally set `COMMAND_CLOSURE_JSON=/tmp/rom-profile/closure.json` to retain the function/frame audit. Use commit `c1d788d` only for the earlier boot/profile artifact. Final source/staged/artifact identities are in [verification.json](verification.json); source-path/debug differences can change rebuilt ELF hashes, so compare the stated tested inputs and semantic checks rather than promising path-independent binary reproducibility. No unrelated full Host suite was rerun for sample-private changes.

## Consumed relocation verification follow-up

The next software slice adds an actual PRE_KERNEL_1 verifier after Zephyr copies/zeros relocated storage and before guarded POST_KERNEL drivers. At that initial phase it compares TEXT, RODATA and DATA against their NOR loads and verifies zero BSS. Each later preflight compares immutable TEXT/RODATA again, validates all region extents before access, and preserves legitimate mutable command/native/bounce changes. It does not recopy mutable storage. Invalid extents, unsupported early/repeated initialization and comparison failures latch refusal, including the retained assembly/vector copy checks.

All six RAM sections and the separate stacks are checked for overflow-safe bounds, alignment and overlap before verifier access; file-backed loads must stay in the firmware envelope. Exact ELF/BIN checks verify actual file-backed LMAs, selected function/literal/state coverage and startup/init-array order. ITCM address zero is valid and is tested as such; the Linux controlled harness uses a permitted nonzero stand-in mapping, while ELF evidence establishes the actual zero VMA.

Final affected gates pass **55 methods, no skips**: command 15, ROM/FCB 7, island 15 (6 retained, 7 controlled relocation, 2 linked-image additions), snapshot 3 and copied driver 15. RED/GREEN covers corruption at region ends, invalid/unmapped/overflow/overlap cases, phase-correct re-entry, and sticky refusal after original assembly/vector copy failures. EVKB-B build passes; BIN **43,528 bytes**. ELF SHA256 `e37efab94e11dbe20b045f432f0b6858cfe78e496cb33597a2b1d64f41c1e3b3`; BIN `062c9c8aab11dd8967d52335ab3787e244570f0cd2a41295e60bfede4f08c636`. The configuration and 512-byte FCB properties remain unchanged.

All 18 selected busy functions retain the exact VMAs, sizes and instruction/literal bytes of the independently reviewed command image. Six consumed-data symbols retain layout; initialized config and owner bounds retain bytes. That identity, the rerun closure gate and the reviewed consumed data policy permit reuse of the prior manual edge/prologue inventory; the new verifier itself executes before the busy window. See [identities](verification.json). This is implemented verification with controlled-function and final-image evidence, not physical execution or authentication of NOR contents. The initial mutable check does not promise detection of arbitrary later corruption or concurrent native writes; the selected composition still needs an exclusive prepare-to-dispatch policy before admission.

The manual commands above discover the added tests through the existing island suite. Published source `51a3c82` separately passed all four hosted Host/Zephyr Push/PR checks; that historical result does not cover this follow-up until its own new-head CI completes.

## GPT2 and identity follow-up

Source `b2d21c1` adds the approved disabled GPT2 software candidate and nine selected lower waits. Explicit source/readback/progress/wrap checks, fault latching and finish-before-rearm semantics are tested. Failed initialization cannot silently retry, and an unfinished or expired-but-unobserved budget cannot be extended with another begin. Already-ready lower paths cannot escape a stopped timer. The 64-method gate and renewed 22-function/380-byte review apply to that revision. Declared synthetic ticks are not fitted NOR maxima or hardware timing qualification.

Source `96a70ac` adds the approved temporary identity slot and the newly reachable SDK LUT-update wait. Full inherited table/controller/port/AHB/IP proof precedes even status, using the compiled original FCB as a 64-word witness. Slot15 is reserved only by this narrow software composition, not by observing zeros alone. A locked-only supported state is saved, checked, restored and verified after successful ID transfer plus positive NOR-ready/controller-idle. Unknown table/configuration/metadata, deadline/transfer/readback failures and unowned changes stay RAM-terminal without repair. Identity bytes use separate RAM storage; unexpected IDs cannot reach WREN. Known WIP/WEL status must permit the subsequent page/sector; neither ID nor WEL proves actual data acceptance.

Final EVKB-B build passes, **44,936-byte BIN**. All **73 methods pass, zero skips**: command33, ROM/FCB7, island15, snapshot3, copied-driver15. These include positive and negative cases; the count is not all-negative tests. Exact FCB comparison still changes only byte0x46,7→6. Final ELF `85f4c7f74f94044d8ea054e41f0f9729141778f89a821d7512e2230eef480e2c`; BIN `23d9b073b6a02af26bc557c8b6b36144fd878d7f519dff5e3ad0f865dd9f2ba2`. Config remains unchanged.

The changed selected graph receives a fresh manual conditional/alias/return/SP/data audit: **28 ITCM functions**, conservative sum **484 bytes** against the 1024-byte operation stack. All new tables, saved slot/configuration, ID/bounce and timer state are DTCM or operation-stack storage. The actual native ID helper's three-byte copy inlines; SDK location/assert paths optimize out in this exact image. This graph is not claimed byte-identical to earlier18/22-body graphs. Incoming generic SDK configuration callers outside the selected closure are not qualified by it. See [identities](verification.json) and [reviews](review.md).

The existing five-suite manual recipe discovers these added tests; no new infrastructure or unrelated full Host suite is required. All four hosted checks passed at prior published `e367676`; updated-head CI is separate and must not inherit that result. No source/product change followed the final focused gate, aside from retained documentation/evidence. Setup/begin/finish have no target callers; NONE returns without timer/LUT/command work, and submit/outer qualification remain constant refusals. Real provider guards and missing read-result/device-initialization behavior remain next integration work, not fulfilled by this candidate.

## Scope limits

The candidate's ordinary native init/read/write/erase interfaces remain refused. Register-derived clocks, controlled status/error inputs and ARM disassembly do not establish fitted-part behaviour, elapsed timing, Cortex-M exception execution, actual watchdog/reset state, master exclusion or persistence. No production layout, backend, Flash driver or Settings patch is adopted. M4 and its physical acceptance remain incomplete.
