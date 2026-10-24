# Maintenance storage candidate verification

Baseline: PR #59 `d073500`; implementation follows the user's conditional
software-design decision. The [profile](../../storage-maintenance-profile.md)
is a limited M4 configuration, not a permanent platform constraint or M7
concurrent-workload qualification. **Physical execution/write: NOT READY.**

## Verified results

| Gate | Result and actual scope |
| --- | --- |
| Four-owner request/image/linker/resolved DTS | 8 methods PASS; scratch excluded from File |
| Actual maintenance File + Configuration/Settings/NVS | 9 PASS; explicit provisioning, four saves, normal backend remount, runtime refusal and failure paths |
| Transparent lifecycle/media control | 2 PASS / 7 intended FAIL on the same nine cases |
| Affected existing readiness | 18 PASS; before private File-media fix, unchanged common inputs verified afterward |
| Existing LittleFS | 7 PASS, including full/reclamation; unchanged by the private fix |
| Copied native driver candidate | 15 PASS; real guards refuse, controlled zero-return test guards exercise otherwise unreachable lower logic |
| RAM-context host/linked-image checks | 6 PASS; one native C check and five exact ARM structural checks |
| Early-snapshot linked-image checks | 3 PASS on final ARM image |
| Accepted driver regression | 4 PASS |
| Historical accepted PoC ELF placement | 3 PASS on its intended original ELF |
| EVKB compile and image bounds/correspondence | PASS; final BIN 41,396 bytes |

The historical SDRAM-only placement validator was also misapplied to the new
DTCM candidate: two negative checks passed but `test_real_elf` errored. Its
intended original accepted ELF subsequently passed all three checks. No region
expectations or validator were weakened. Setup/assembly/test-parser failures
were corrected before final results; they are not passing test evidence.

A material review finding was corrected: LittleFS close calls sync and can
commit dirty data after a File sync error or Configuration fault. Focused RED
was 7 PASS / 2 FAIL, each dirty close forwarding one real File-area write.
The fix fences write, erase **and flatten** before dispatch. Final dirty closes
consume/null the handle, return IO, deny one mutation attempt and forward zero
writes/erases. This does not promise rollback or a persisted old/new value.

The four-save trace records 85 NVS reads, 24 writes and zero erases. Separate
normal GC reaches one erase after 108 pair updates / 216 saves and restores both
values after backend remount. Healthy observed NVS mounts dispatch zero writes
or erases; arbitrary interrupted GC/startup repair is not qualified. These are
simulator API counts, not capacity, timing, wear or target guarantees.

[Identities and input hashes](verification.json) identify the final ARM and
native artifacts. Full local build/test logs remain in the task output; the
retained transparent control and source provide reproducible negative evidence.
Independent task reviews covered source, tests, selected ELF and evidence; see
[review](review.md). These are technical reviews, not adoption/merge authority.

## Manual reproduction

Use the documented pinned Linux west workspace. Commands run simulation or
inspect ARM artifacts; they do not operate the board. Explicit module selection
avoids duplicate discovery of the worktree and the west checkout.

```sh
export PATH=/opt/crux-cache/venv/bin:$PATH
export ZEPHYR_BASE=/ws/zephyr
export CCACHE_DIR=/tmp/maintenance-profile/ccache
export XDG_CACHE_HOME=/tmp/maintenance-profile/cache
export ZEPHYR_TOOLCHAIN_VARIANT=host
cd /ws
west build -b native_sim/native/64 /ws/.worktrees/flash-safety-poc/tests/native/maintenance_storage -d /tmp/maintenance-profile/green -- '-DZEPHYR_MODULES=/ws/.worktrees/flash-safety-poc;/ws/modules/fs/littlefs' -DUSER_CACHE_DIR=/tmp/maintenance-profile/cache
cd /tmp/maintenance-profile/green
./zephyr/zephyr.exe
```

For the identical transparent control, build in a different directory with
`-DCRUX_MAINTENANCE_GATE=OFF`; expect seven failures. Run Flashsim executables
from their writable output directory. Existing suite recipes are in the
[readiness gate](../../settings-readiness-gate.md) and the [test guide](../../../tests/README.md).

```sh
python3 -m unittest discover -s /ws/.worktrees/flash-safety-poc/tests/native/storage_target_guard -v
python3 /ws/.worktrees/flash-safety-poc/tests/native/flash_target_candidate/test_candidate.py -v
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1
cd /ws
west build -b mimxrt1060_evk@B//qspi /ws/.worktrees/flash-safety-poc/samples/flash_target_candidate -d /tmp/maintenance-island/build
export PILOT_ELF=/tmp/maintenance-island/build/zephyr/zephyr.elf
export ARM_OBJDUMP=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump
python3 /ws/.worktrees/flash-safety-poc/tests/native/maintenance_island/test_island.py -v
python3 /ws/.worktrees/flash-safety-poc/tests/native/maintenance_boot/test_snapshot.py -v
```

Geometry needs `ZEPHYR_BASE`, image checks need `PILOT_ELF`; skips without these
inputs do not establish those gates. Focused gates are manual and must be rerun
when inputs change; ordinary hosted CI does not currently execute them.

## Remaining work and limits

The actual target init/read/write/erase guards always refuse. The conditional
RAM context observes only unchanged controller idle; it neither verifies NOR
WIP nor executes program/erase, exceptions or Cortex-M restoration. Host logic
and ELF/disassembly inspection are separate from target execution evidence.

Unfinished software includes the selected NOR mode/read-confirm and elapsed-time
policy, real mutating paths and known-ready restoration, complete startup/cache/
indirect/data closure and operational target binding. Hardware/predecessor state,
other masters, watchdog/external supervisor, fitted NOR maxima, oscillator progress,
AHB/SDRAM/DCDC transitions and the exact provisioning/backup/first-write/recovery
procedure remain separate gates. No first-write readiness is claimed.

The work remains stacked on unmerged PR #59/#56. Latest main `6a58145` adds UART;
a non-mutating merge-tree check reports additive selection conflicts in
`zephyr/CMakeLists.txt` and `zephyr/Kconfig`. Preserve both capability selections
and rerun affected integration checks before eventual main acceptance. No parent
PR was merged or silently changed. M4 and Issues remain incomplete/open.
