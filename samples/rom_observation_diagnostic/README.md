# Private ROM identity/status observation diagnostic

**Software preparation only. NOT READY for physical use.** This is a separate
image from `flash_target_candidate`. Its approval does not permit attachment,
programming, boot/reset, physical GPT2/LUT changes, production adoption or merge.
The original candidate remains inert with unconditional `-EACCES` submission.
No fitted-part records are available. Raw JEDEC bytes cannot establish revision,
grade, supply voltage, or program/erase timing qualification.

`main` owns one fixed observation request, once. It reuses the existing pre-C
snapshot, read-confirm predicate, relocation checks, RAM PSP/MSP/vector context,
pinned native status/ID helper bodies, GPT2 candidate, and temporary ID slot.
Diagnostic source copies expose only preflight/raw capture and status/ID helpers;
upstream and original candidate policy sources are unchanged. The diagnostic HAL
copy refuses every request except A1/address-zero/count-one READ: slot 1/one byte
or slot 15/three bytes. No arbitrary media read or backup API is present.

The supported software profile requires valid early snapshot; disabled watchdogs,
MPU and D-cache; privileged thread context with no active/inherited FP state;
the existing sole-A1 ROM controller/clock/LUT profile and locked LUT; and exclusive
controller, bus-master and debugger ownership. Disabled drivers and exact LUT
checks support the fixed software composition; they do not prove physical master
quiescence or probe exclusion. Those remain physical-use prerequisites.

The fixed sequence checks the full inherited LUT/controller state, reads status
once, installs the four-word ID slot only with positive WIP=0/WEL=0 and idle,
reads three JEDEC bytes, reads status once more, and requires WIP=0/WEL=0 and idle
before exact slot/lock restoration. A matching `9d 70 17` sets an informational
flag; unexpected/zero/FF identity is still reported and grants no qualification.
Initial WIP=1, a transfer/source/deadline error, changed state or failed restoration
stays in RAM containment. Initial WIP=0/WEL=1 returns a refusal only after an
unchanged full-table/idle check and healthy deadline finish; it never installs ID
or clears WEL. No unsupported initial state is polled/reconfigured into acceptance.

GPT2 requires already-enabled gates, a nominal 24-MHz external crystal declaration
and stopped zero registers, then performs a bounded peripheral SWR, free-run setup,
start and observed-progress checks. It never changes CCM gates or oscillator
selection. Zero registers are necessary but cannot prove physical ownership.
Unavailable gates/source refuse; they are not silently enabled. The single budget
is **2,400,000 nominal ticks (100 ms at the declared 24 MHz)**, a conservative
synthetic diagnostic limit, **not** a qualified NOR deadline or measured duration.
Existing 1024-observation progress/selected lower-loop fallbacks remain. MMIO can
stall; software checks do not establish absolute fault immunity or wall-clock
completion. GPT2 intentionally remains running without interrupts/output after
success/refusal following setup; no prior stopped state is claimed restored.

The intentional effects are GPT2 reset/configuration/start and private metadata;
FlexSPI IP read-command/FIFO/interrupt-acknowledgement effects from the pinned SDK;
and key/unlock/write-four-words/lock for temporary slot 15 followed by exact
restoration after known-ready evidence. No NOR/system/controller reset, NOR WREN,
program, erase, status/configuration write, clock change, watchdog feed/disable,
File/Settings mount, format, provision or scratch trial is emitted. The ROM FCB
still contains its original unused command LUT words as witness data; its mode
and config command enable bytes are zero. This does not qualify every ROM/loader
or ordinary startup hardware effect.

The sole output is `volatile struct crux_observation_record
crux_observation_result`, version 1, in relocated internal DTCM. `pre_c[16]` is
copied separately from current runtime registers; a zero derived/root/LUT field
on a refused profile may mean collection stopped early. `timer_available` means
registers were readable under enabled gates, not exclusive ownership. GPT arrays
are CR/PR/IR/SR/CNT, raw observations, separate from `declared_ticks/nominal_hz`.
`id[3]` has one byte value per 32-bit word. Status/ID fields are valid only when
their corresponding validity flag is set. Other flags record events, not current slot
state. `RESTORED` means LUT/ready/deadline completion; the assembly caller context
is still being restored. Only outer `COMPLETE` publishes after caller return.
The C terminal records `CONTAINED`; an assembly exception can leave `ENTERING`.
There is no UART/shell/probe service. Later readout needs a separately reviewed
physical procedure. Early refusal may occur before this record initializes, so
an absent/stale record is possible; magic/version alone do not establish freshness.
No indefinite safe-halt or protection against an externally armed watchdog/reset
is claimed. Do not reset/retry or return to normal XIP after uncertainty by assumption.

For software-only verification in the owned Linux build environment:

```sh
export PATH=/opt/crux-cache/venv/bin:/usr/local/bin:/usr/bin:/bin
export ZEPHYR_BASE=/ws/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
O=/tmp/m4-observation-diagnostic
west build -b mimxrt1060_evk@B//qspi samples/rom_observation_diagnostic -d "$O/arm" -- \
  '-DZEPHYR_MODULES=/ws/.worktrees/flash-safety-poc;/ws/modules/hal/nxp;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6' \
  -DUSER_CACHE_DIR="$O/cache"
# PILOT_ORIGINAL_FCB is a retained pristine 512-byte pinned FCB reference.
export PILOT_ELF="$O/arm/zephyr/zephyr.elf" PILOT_BIN="$O/arm/zephyr/zephyr.bin"
export PILOT_ORIGINAL_FCB="$O/original-fcb.bin"
export ARM_OBJDUMP="$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump"
python3 -m unittest discover -s tests/native/rom_observation_diagnostic -v
python3 -m unittest discover -s tests/native/maintenance_boot -v
```

The exact-image tests are a manual review gate, not a universal ARM dataflow
verifier or hardware authorization. Review the final instruction/data/exception/
stack closure and retained hashes, repeat affected default-candidate checks, then
obtain independent Reviewer/Challenger assessment. The later physical packet must
resolve exact probe/loader/boot/readout effects, approved firmware preservation,
backup/recovery, exclusive reservation and hand-back before asking for hardware
approval. The ordinary Flash/Attach launch profiles do not satisfy that gate.
