# Target storage integration and first-write proposal

Status: software candidate under review, **not authorised for hardware**. This
work follows the user's acceptance of PR #56 at `b31f4f0` and next-stage software
approval. No production patch, provider or layout is adopted. PR #51's separate
[acceptance/integration check](evidence/storage-target-candidate/plan-integration.md)
must accompany the final human PR #56 decision; neither PR is merged here.

## Composition and ownership

Keep the approved portable File/BOOL/U32 Configuration contracts and their
separate owners. Use actual Zephyr VFS/LittleFS and CRC-enabled Settings/NVS;
reuse the simulator evidence instead of repeating capacity investigations.
Mount/bind/format/provisioning belongs to native composition, not the public
contracts. No automatic LittleFS format or unqualified destructive recovery is
permitted. The Settings candidate is documented separately; generic upstream
corrections and Crux-specific policy must remain distinguishable.

Candidate NOR offsets remain firmware `[0,0x620000)`, LittleFS
`[0x620000,0x7f0000)`, NVS `[0x7f0000,0x800000)`, from the existing assessment.
These are proposals only. Fitted device geometry, current contents, image growth,
representative workload/headroom and ownership still require approval.

The sample-only driver fence rejects negative/zero/overflowed, firmware/header,
owner-crossing and out-of-device write/erase requests before any native effect.
Erase needs whole 4-KiB sectors. Chip erase is rejected. The final image gate
checks all file-backed ELF load addresses, BIN/header limits and ELF/BIN load-byte
correspondence, including Flash load images for RAM execution. These protections
are experimental sample enforcement; they do not fence an external programmer,
prevent status-register writes during probe, or establish execution closure.
The driver fence separates address extents, not caller identities: a direct
native caller can still address either proposed data owner. Final composition
must bind File and Settings to their own regions and exclude other Flash clients.

## Proposed first-write envelope — not an executable instruction

Only after every mandatory safety prerequisite and separate physical approval:

- Exclusive M4 ownership of RT1060-EVKB B; M5 software work remains independent.
  Agree the reservation and exact hand-back image through the user. Do not send
  messages to another chat or access its board session without authorisation.
- Identify board/probe/fitted NOR and existing contents. Preserve an approved
  bootable recovery image and backups, including the sacrificial sector and
  adjacent sentinels. Unknown contents are not permission to erase.
- Candidate scratch sector: relative `[0x7ef000,0x7f0000)` / CPU
  `[0x607ef000,0x607f0000)`, within the proposed LittleFS owner. It is not the NVS
  region. Confirm it contains no retained filesystem/user data before approval.
- Proposed operation budget: one explicit 4096-byte scratch erase, one aligned
  16-byte page-contained program, then full programmed-byte/remaining-erased-byte
  readback and adjacent-sector sentinel comparison. No chip erase, blind retry,
  implicit format, extra reset, power cut or recovery erase.
- Proposed bytes at scratch offset zero: `43 52 55 58 50 49 4c 4f 54 00 aa 55
  12 34 56 78`; the rest must equal the fitted part's confirmed erased value.
  This is a proposed single operation, not permission to launch a programming
  tool. The dormant compile-only candidate does not execute it.
- Positive ready/mapping restoration is required before ordinary XIP return.
  Transport failure or exhausted observation budget after a possibly issued
  command is uncertain: remain in the reviewed RAM-contained disposition.
  Elapsed time alone never authorises reset or ordinary Flash execution.
- Abort further operations on identity, bounds, sentinel, ready, data or controller
  health mismatch. The exact safe abort/watchdog/recovery action must be approved
  first; an unqualified reset/power cycle is not an assumed recovery procedure.

This envelope does not yet authorise an image or a physical command. The final
image hashes, exact selected patches and unresolved closure blockers are recorded
in the linked candidate reports. If a mandatory item remains unresolved, the
first-write decision is **NOT READY**, even if software tests pass.

## Later persistence session, after separate acceptance

After the first-write gate, explicitly approved File formatting/NVS provisioning
and readiness, exercise File mount/create/write/sync/read/delete and retain a
second acknowledged file for one declared normal reboot. Save representative
BOOL/U32 parameters via the public Configuration API and verify exact restore.
Record operation timing and controller/watchdog health under the approved
operating mode; do not disable an armed watchdog or weaken freshness checks.
Simulation remains insufficient for #15/#16 physical reboot acceptance.

## Exact candidate evidence and next human decisions

The [Settings/NVS proposal](settings-target-proposal.md) narrows the corrections
to three pinned source files and preserves the approved public contract. Its
healthy/error fixtures are software evidence; missing count now refuses freshly
erased media too, so explicit provisioning is required. Mount may mutate storage
before that count check. Choose a bounded provisioning/inspection/recovery policy
and the required full-record integrity preflight before adopting writable storage.
Do not treat a valid-looking count or a partial read as full-store integrity.

The [Flash candidate](flash-target-proposal.md) is an exact compile-only image,
not the final operational persistence image. Its current ELF SHA-256 is
`c830176753fa28515d358365c3c9dba9c9d5b8c6055029d4a356f1629bfdb99c` and BIN SHA-256
is `3870a8ff65ba0d8b61b1539c6763088d538106de8f29845a97f2c10b3ae5a640` (39,552
bytes). It remains **NOT READY**: copied RAM vectors still point to Flash fault
handlers; startup/transitive code/data/stack/cache closure, watchdog/master state
and qualified busy deadlines are incomplete. Do not program or boot it.

Recommended next decision: accept the bounded evidence, then explicitly select
the Pilot execution-safety design and recovery/provisioning limits before
adopting any target patch/provider/layout. A small fully reviewed hazardous RAM
island with contained exception paths preserves the existing XIP architecture;
whole-application RAM execution would be a separate architecture/memory/boot
choice. Neither is silently selected here. Watchdog policy must not permit an
unqualified reset while NOR availability is uncertain. Patch maintenance belongs
to the separately approved Crux Zephyr Native Integration owner, with focused
upstream corrections preferred over a permanent fork.

Only after the chosen design closes the mandatory gates can a new exact,
write-armed image and bounded procedure be presented for physical approval.
The current dormant candidate/hash is not permission to bypass those gates.
