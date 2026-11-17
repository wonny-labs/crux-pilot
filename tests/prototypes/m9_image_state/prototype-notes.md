# Prototype notes: questions the proposal leaves open

> **Prototype, not adopted, not for merge. Exists so the team can review the proposed image-state contract in running code.**

Each entry gives the question, the options seen, and the choice the prototype made in order to proceed. Every choice is a **prototype choice, not a decision**. Source: the [image state contract proposal](../../../docs/plans/image-state-contract-proposal.md). Which tests pin which choice:

- Pinned by tests whose case names carry the number: P1 (all codec cases), P2 (start edge cases and read failures), P3 (slot-state rows and the request refusals), P4, P5 (busy cases), P6 (argument order before and after start), P9 (the outcome cases on the other slot, a slot that does not exist, a NULL record).
- Pinned by tests whose names do not carry the number: P7 (confirm), P8 (the order of the request checks), P11 (the end-to-end scenarios).
- Not pinned: P10 for `crux_image_version_compare()`, because a NULL there is a precondition violation and cannot be tested; the NULL record of `crux_image_outcome()` is pinned under P9. P12 is not a behaviour.

Numbering of the slot-state cases: the proposal's slot-state table has 11 rows and the conformance table says 12 cases. The prototype numbers S1 to S6 for the non-running rows, S7 to S10 for the running rows, and splits the table's one INVALID row into S11 (magic bad) and S12 (a flag outside set and unset). This is the prototype's reading of "12", not the proposal's.

## P1. Serialisation of the records

- **Question.** A fixed-size serialisation of the records was considered. The proposal specifies none. The boot record is derived at start and cached in RAM, and never stored. The attempt record is stored by the update agent "in its own storage", and where and how is the update agent's design (Q2).
- **Options.** (a) No serialisation, nothing to test for a torn record. (b) A fixed layout owned by the contract. (c) A layout owned by the update agent, tested there.
- **Prototype choice.** A 16-byte layout for the attempt record only, with a format byte, little-endian fields, a reserved byte and a CRC-32 (`attempt_codec.h`), under `proto_` names that carry no contract identifier. It exists only so that "cut or corrupted at byte N" can be run. No boot-record serialisation was written, because nothing in the proposal stores it.
- **Consequence to review.** A record that fails to decode is no attempt, so the outcome is `NONE` (case 1). The proposal's O8 limits assume that the record itself is intact. Is an unreadable record acceptable as `NONE`, or does the update agent need a separate "unknown" answer?

## P2. Start: missing and unsupported shared data

- **Question.** The proposal gives `ERR_NO_BOOT_DATA` for a missing running-slot key and `ERR_BOOT_STATE` for a mode other than RAM load. It does not say what a missing mode key, a running slot above 1, or a retry after a failed start does, or in which order the checks run.
- **Options.** Missing mode as `NO_BOOT_DATA` or as `BOOT_STATE`. Running slot above 1 as `BOOT_STATE` or `INVALID_ARGUMENT`. A failed start cached (fail for good) or retried.
- **Prototype choice.** Shared data absent, running-slot key missing, or mode key missing gives `NO_BOOT_DATA`, checked in that order. A mode other than RAM load, or a running slot above 1, gives `BOOT_STATE`. A failed start caches nothing, so a later `crux_image_start()` reads the medium again. `crux_image_start()` also fails with `ERR_IO` if reading either slot fails, the running slot or the inactive one (the proposal does not say whether an unreadable inactive slot may be reported as `INVALID` instead and the start allowed to succeed). The live calls pass a read failure on as `ERR_IO` and write nothing. Every other call keeps returning `NOT_READY` until a start succeeds.

## P3. Gaps in the slot-state table

- **Question.** The table does not define: a running slot with an invalid header; a running slot whose `image_ok` is outside set and unset; a running slot with `image_ok` unset and `copy_done` outside the defined values; and a slot that is not running with magic unset and a flag outside the defined values (the STAGED row and the INVALID row both match).
- **Options.** Fail closed (I6) or pick the nearest row.
- **Prototype choice.** The first three make `crux_image_start()` fail with `ERR_BOOT_STATE`. The last is `INVALID`, because I6 prefers refusing. A slot that is not running, with magic unset, is `STAGED` only when `image_ok` and `copy_done` are both unset (an erased trailer). With magic unset and `image_ok` set it is `INVALID`, and so it is with `copy_done` set.
- **Why (review finding).** The first version read the blank cells of the STAGED row as "any" and classified magic unset with `image_ok` set as `STAGED`. A request then wrote the magic and swap info, the slot read magic good, `image_ok` set, `copy_done` unset (`PENDING_PERMANENT`), and the fake bootloader ran it as confirmed with no trial and no revert. That is a permanent request, which I2 excludes, and the header says this contract never creates one. Magic unset with `copy_done` set would read `TRIED` after the request and be erased at the next reset, so the request would be a silent loss. Both are now `INVALID`, and the request gives `ERR_SLOT_STATE`.
- **Other option.** Keep the blank cells as "any" and make `crux_image_request_test()` check the trailer itself before it writes, so that a `STAGED` slot with stale flags is refused with `ERR_SLOT_STATE`. The state would then read `STAGED` for a slot that the contract cannot request, which makes `STAGED` mean two things. Another option is to let the update agent erase the whole slot first and treat any stale flag as its error; the proposal already requires that erase (Behaviour, staging precondition) but does not say what the provider does if it was skipped. The owner decides which of the three.
- **Related, live state.** After start, a live trailer of the running slot can stop being a supported one. `crux_image_slot_get()` then reports `INVALID`, `crux_image_confirm()` returns `ERR_BOOT_STATE`, and `crux_image_request_test()` returns `ERR_BOOT_STATE` and not `ERR_NOT_CONFIRMED`. The proposal says nothing about this.

## P4. Bootloader version present with zero fields

- **Question.** B3 asks for "present with zero fields", and I9 gives an explicit "not known" flag. Does 0.0.0.0 mean the build has no `VERSION` file?
- **Options.** Known (the key is present and carries zeros), or unknown (zeros are the Zephyr default for a missing file; the proposal says the default was not read).
- **Prototype choice.** Known, with zero fields. The flag follows the presence of the key only.

## P5. ERR_BUSY and the lock

- **Question.** The proposal has one internal lock and returns `ERR_BUSY` "after a bounded wait". It sets no bound, and one thread cannot wait.
- **Options.** A real lock with a timeout, a try-lock, or no lock in the Host double.
- **Prototype choice.** A single flag stands in for the lock: a request or confirm that arrives inside another (the fake's write hook re-enters the provider) returns `ERR_BUSY` at once. `crux_image_start()`, `boot_record_get()` and `slot_get()` take no lock. The bound is unspecified and untested.

## P6. Order of argument and readiness checks

- **Question.** For `crux_image_request_test()` the proposal lists started first, then the slot argument. It lists no order for the other calls.
- **Prototype choice.** `request_test` and `confirm`: not ready, then busy, then the listed checks. `boot_record_get()` and `slot_get()`: a NULL pointer or a slot above 1 gives `INVALID_ARGUMENT` before `NOT_READY`. A slot above 1 for `request_test` gives `INVALID_ARGUMENT` after the started check and before `ACTIVE_SLOT`.

## P7. Confirm when the running slot is not on trial

- **Question.** The proposal says confirm returns `OK` without writing when the running image is already confirmed. It does not say which live states other than on trial and confirmed can occur.
- **Prototype choice.** `RUNNING_TESTING` writes `image_ok`. `RUNNING_CONFIRMED` returns `OK` with no write. Anything else is the `BOOT_STATE` of P3.

## P8. Idempotent repeat against the version check

- **Question.** The check order puts "strictly newer" before the slot-state check, and the repeat of a pending request is the `PENDING_TEST` case of the last check.
- **Consequence.** A `PENDING_TEST` slot whose version is no longer newer than the running image (for example after the running image changed) returns `ERR_NOT_NEWER` and not `OK`. A stale confirmed trailer on an older image also gives `ERR_NOT_NEWER`, not `ERR_SLOT_STATE`. Both follow the listed order literally.
- **Prototype choice.** The listed order, as written. Tested.

## P9. Gaps in the outcome table

- **Question.** The table reads "first match wins". It has no row for: an attempt on a slot that does not exist; a requested attempt on the other slot when that slot's state at start is `RUNNING_CONFIRMED` or `RUNNING_TESTING` (not possible for a slot that was not running at start); a NULL record. A case 5 attempt (not requested) matches before any state is looked at, so it also matches those running states.
- **Prototype choice.** A NULL record, a slot above 1, and a requested attempt on a `RUNNING_*` state of the other slot give `NONE`. A not requested attempt on any state other than `STANDBY_CONFIRMED` gives `NOT_BOOTED`, as the first-match order says. Cases 2 and 3 compare major, minor and revision only, so a differing build number is the same release.

## P10. NULL pointers to the pure functions

- **Question.** The signature takes pointers and the proposal does not say what a NULL `a` or `b` does for `crux_image_version_compare()`.
- **Prototype choice.** No check. A NULL is a precondition violation, written in the header comment. `crux_image_outcome()` accepts a NULL attempt as the proposal says, and also a NULL record (P9).

## P11. How much bootloader the Host double models

- **Question.** The proposal puts the hand model of the bootloader's select and erase rules at the `native_sim` level (N1) and keeps the Host level as a "controllable provider". O8 is a Host case that needs a reset ("a newer staged slot reads `EMPTY` after a reset").
- **Options.** Script the slot states directly for O8, or run the table as a small model on the Host.
- **Prototype choice.** A small model on the Host (`fake_boot()`): highest version first with the lower slot on a tie, erase when magic is not good or `copy_done` is set without `image_ok`, set `copy_done` (the write can be made to fail), validate, and try the next slot. It does not read the swap-info bytes, so the slot left by a cut after the magic write (R8) boots as a test. Whether the real bootloader does the same is the proposal's "Not verified", and a target case. The fake also does not model "a load failure removes the image from flash": the proposal's load step erases the image on a load failure, and the fake only models a validation failure, which keeps the slot.

## P12. Names, location and placement

- **Question.** The proposal leaves the directory of the Zephyr provider to the boot composition owner (Q6), and the component directory is a proposal.
- **Prototype choice.** The proposal's `crux_` identifiers are kept so that a reviewer can compare them one to one. They live only in `image_state_proto.h`, which is outside every `include/` root and is not exported. The private port, the fake and the codec use `proto_` and `fake_` names. If the contract is adopted, the identifiers, the file names and the directory are the owner's to settle, and this directory is deleted or rewritten.
