# Image-state contract prototype (M9, #297)

> **Prototype, not adopted, not for merge. Exists so the team can review the proposed image-state contract in running code.**

This directory runs the contract of the [image state contract proposal](../../../docs/plans/image-state-contract-proposal.md) as host C17 code, so that a reviewer can read the states, the transitions and the rejections as code and watch them pass or fail. The proposal is not adopted. Nothing here adopts a public interface, and nothing here is a basis for one. Adoption, and the choices that the proposal leaves open, belong to the owner.

## What this is not

- Not a component. There is no `components/image_state/`, no `include/crux/image_state.h`, no `crux::` target, no install rule and no export.
- Not wired into anything. The root `CMakeLists.txt`, the host check scripts and the CI workflows do not know about this directory. `CMakeLists.txt` here is a standalone project that refuses to be added from another project.
- Not a Zephyr provider and not MCUboot. There is no Zephyr, bootutil, board or vendor header, and no Zephyr build or board result.
- Not evidence for the boot rules. The fake bootloader is the proposal's own table of the bootloader's select and erase rules, written as a test double. It shows that the contract is consistent with that table, not that MCUboot behaves like it.
- Not a decision. Every place where the proposal is silent or ambiguous is listed in [prototype-notes.md](prototype-notes.md) with the choice that the prototype made in order to proceed.

## Where it lives and why

`tests/prototypes/m9_image_state/`. Products do not consume `tests/`, and the root `CMakeLists.txt` adds each test directory by name, so a new directory is not built or exported by accident. It needs no edit to an existing file, which keeps the protected paths untouched. A separate `prototypes/` level keeps the prototype apart from the suites that CI runs. The CI formatting check lists tracked C files and so covers these files. The other checks name their sources explicitly and do not.

## Run it

From the repository root, with a C17 compiler and CMake 3.28 or later:

```sh
cmake -S tests/prototypes/m9_image_state -B build/m9-image-state-prototype
cmake --build build/m9-image-state-prototype
ctest --test-dir build/m9-image-state-prototype --output-on-failure
```

Or without CMake:

```sh
cd tests/prototypes/m9_image_state
cc -std=c17 -Wall -Wextra -Wpedantic -Werror -o /tmp/image_state_proto_tests *.c
/tmp/image_state_proto_tests
```

The run prints every failing check with its case name, and ends with the number of checks and failures. It needs no container, no Zephyr and no network.

## Files

| File | Role |
| --- | --- |
| `image_state_proto.h` | The types and functions of the proposal's illustrative signatures, with the proposal's identifiers. Not a public header. |
| `image_state_core.c` | Pure parts: `crux_image_version_compare()`, `crux_image_outcome()` (outcome cases 1 to 7) and the slot-state derivation (slot-state table). |
| `image_state_provider.c` | `crux_image_start()` and the four operations, with the check order, fail-closed start and cached record of the Behaviour section. |
| `image_state_port.h` | Private seam between provider and medium. In the proposal the medium is bootutil and the shared data. |
| `image_state_fake.h`, `image_state_fake.c` | The Host double: two slots, shared data, a write log with failure injection, and the hand model of the bootloader's reset (select, erase, validate). |
| `attempt_codec.h`, `attempt_codec.c` | A 16-byte serialisation of the attempt record with a CRC, so that a torn record can be tested. A prototype choice (P1): the proposal gives none. |
| `harness.h`, `test_main.c` | A small check harness. |
| `test_pure.c`, `test_provider.c`, `test_scenarios.c`, `test_codec.c` | The table-driven cases. |
| `CMakeLists.txt` | Standalone build and one CTest entry. |
| `prototype-notes.md` | Open questions, options, and the prototype's choices. |

## Cases covered

| Proposal case | Where |
| --- | --- |
| S1 to S12 (slot-state table, both running slots; the numbering is explained in the notes) | `test_provider.c`, `test_slot_states_*` |
| B1 to B4 (start: missing key, mode, bootloader version, twice) | `test_provider.c`, `test_start` |
| R1 to R8 (request: inactive slot, running slot, on trial, empty, version, stale trailer, repeat, write order and failure) | `test_provider.c`, `test_request`; R8 cuts also in `test_scenarios.c` |
| C1 to C3 (confirm: running slot only, twice, write failure) | `test_provider.c`, `test_confirm`; a lost confirm in `test_scenarios.c` |
| V1 (version compare vectors, build number ignored) | `test_pure.c` |
| O1 to O7 (outcome table, every slot state, both running slots) | `test_pure.c` |
| O8 (power cut during staging and after `requested`) | `test_scenarios.c` |
| N1-like (full update, revert, refused image, copy_done failure) on the hand model | `test_scenarios.c` |
| The reason for strictly newer (equal image: never selected from slot 0, selected from slot 1) | `test_scenarios.c` |
| The attempt record cut at byte N (shorter, erased tail, zero tail) or corrupted at byte N | `test_codec.c` |

Not covered, because they need what this prototype does not have: V2 (a hand model of `boot_compare_version`, `native_sim`), the real bootutil over a flash simulator, the Zephyr provider, the eMMC trailer alignment (Q5), and every target case T1 to T7.

## Recorded run

See the commit message for the red and green results. CI has not run on this prototype.
