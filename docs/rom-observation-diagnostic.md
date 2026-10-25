# ROM status and identity diagnostic

Status: **software preparation only; NOT READY for physical use**. The user
approved this separate diagnostic composition, its software verification and a
Draft PR. No target/probe access, programming, boot/reset, production adoption,
merge or Issue closure is authorised. No fitted-part identification record exists.

## Bounded composition

`samples/rom_observation_diagnostic` executes one fixed status → three-byte JEDEC
→ final-status sequence. It reuses the reviewed snapshot, RAM context, GPT2
candidate and temporary identity slot. It has no arbitrary media-read/backup,
File/Settings, provisioning or scratch interface. Its selected source copies
exclude native Flash initialisation and write/reset helpers. The original
`flash_target_candidate` remains inert, with unconditional submission refusal.

Completion requires ready/idle evidence, exact slot/lock restoration and a
healthy deadline finish before restoring the caller and publishing COMPLETE.
Initial WIP is not polled into acceptance. Uncertainty stays in RAM; a failure
does not license reset, retry or return to XIP. The result is a versioned,
560-byte volatile DTCM record. Pre-C capture and later observations are distinct;
early refusal can leave an absent/stale record. A matching JEDEC ID is
informational and cannot establish revision, grade, voltage or write timing.

This is not a side-effect-free register reader. Deliberate peripheral effects
are GPT2 setup/start, FlexSPI IP read/FIFO/acknowledgements and temporary LUT
slot-15 installation/restoration. Before each accepted read, the SDK writes
`FLSHCR2[A1].CLRINSTRPTR` (bit 31), clearing the instruction-sequence pointer
internally saved by `JMP_ON_CS`. This peripheral pointer clear is distinct from a
controller, NOR or system reset. GPT2 remains running after setup. The
2,400,000-tick budget is synthetic (nominal 100 ms at the declared 24 MHz), not
measured elapsed time or a NOR deadline. MMIO stalls, watchdog/power resets,
physical ownership and actual source availability remain unqualified. See the
[sample procedure and result schema](../samples/rom_observation_diagnostic/README.md)
for exact software preconditions, effects and reproducible commands.

## Verified software evidence

Compiled inputs: implementation `22534b7`, warning-policy correction `aebc737`;
base `822f6c6`. C1 is a documentation-only effects-disclosure correction: code,
configuration, tests and retained image bytes are unchanged; no rebuild was needed.
Pinned Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` and hal_nxp
`2c2f28ac333e995d7279777409817c4b4f92c1ec` were not edited.

| Check | Observed result |
| --- | --- |
| Exact diagnostic tests | 16 passed; zero failures/skips |
| Early snapshot tests | 3 passed |
| Affected default candidate | 84 passed; rebuilt BIN identical to PR #66 |
| ARM build | EVKB B; 26,076-byte BIN; image bounds pass |
| Selected RAM graph | 18 functions; conservative all-frame sum 368 / 1,024-byte PSP; separate 1,024-byte exception MSP |
| Formatting and source boundaries | Passed; no new public API or native consumer escape |

The exact graph, data, startup/copy, vectors, saved-return epilogue and stack
were manually reviewed alongside automated checks. The scanner is not a general
dataflow proof. The FCB differs from its pristine pinned reference only at the
approved clock byte (`0x46:7→6`); mode/config command enables are zero. Unused
write-command LUT words remain witness data, not reachable commands.

Independent review found one material build-policy omission: three owned RAM
sources were compiled in the native library without all owned-code warning
flags. A new actual-compile-command assertion failed before the fix; scoped
source options now enforce `-Wall -Wextra -Werror`, without applying that policy
to staged upstream sources. The 16-test gate passes after correction and both
ELF and BIN are byte-identical to the reviewed pre-fix image.

A disposable integration used main `b32b858` and dependencies #51 → #56 → #59 →
#62 → #63 → #66 → this diagnostic. Four existing additive conflicts were
resolved only in the temporary integration. Ten concurrent capability subtrees,
including System, exactly match main. System/reset/watchdog providers are not
selected in this diagnostic. Its ARM build, 16 diagnostic and 3 snapshot tests
pass; BIN matches the branch exactly. ELF debug/source metadata differs.
The first parent test invocation omitted `ZEPHYR_BASE` and recorded five setup
errors; the corrected invocation passed. Both logs are retained. No branch or
PR was merged and no broad unrelated M5 matrix was repeated.

[Verification identities](evidence/rom-observation-diagnostic/verification.json)
identify source, pins, images, retained local archive and integration evidence.
Exact-image/fault tests remain a documented manual gate; ordinary CI alone does
not execute this diagnostic or qualify its physical safety.

## Required physical decision

The retained hardware packet is incomplete and grants no authority to run.
Before proposing a physical session, establish:

- Exact board/probe/loader identities, programming erase/write footprint and
  preservation of every unapproved region; a small BIN does not constrain a loader.
- Approved existing firmware/data backup, recovery image and hand-back procedure.
- Exact boot/reset/attach/read/disconnect effects, including device-hook changes
  to watchdogs; a debugger-conditioned snapshot is not a cold-boot observation.
- Safe RAM readout, freshness/refusal handling, exclusive reservation and
  controller/GPT2/master ownership. Do not force unsupported state to pass.

Existing Flash/Attach launch profiles are not approved passive readout procedures.
SEGGER documents distinct initial-register, reset, halt and disconnect behaviour;
its RT1060 hooks can alter watchdog observations. See
[GDB Server](https://kb.segger.com/J-Link_GDB_Server) and
[RT1060 device notes](https://kb.segger.com/NXP_i.MX_RT1060).
No programmer binary or hardware tool was executed here.

Later Flash writes need separate fitted-part/timing and storage safety decisions.
This result neither qualifies physical persistence nor completes M4.
