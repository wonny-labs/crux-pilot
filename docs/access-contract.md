# Access control contract

Status: Host implementation for review under Issue #177, with fake providers. The contract is not adopted
until this review and gate #183 pass. The decisions and reasoning are in
[the accepted proposal](plans/access-contract-proposal.md), which this document does not repeat.

Owner: `components/access`. Public header `<crux/access.h>`, usage target `crux::access`. Adapter header
`crux_access_adapter.h`, internal target `crux_access_adapter`.

## What exists

| Item | State |
| --- | --- |
| Public and adapter contracts | Implemented as proposed, with the additions below |
| Portable core | Builds on Host. It has no provider, so a product image does not link until one exists. |
| Fake ports | Test composition in `tests/access`. They exist only when Crux builds its own tests. |
| PSA derivation, random and keyed hash | Not implemented, Issue #142 |
| Credential store | Not implemented, Issue #178. Requirement A6 is checked against a fake only. |
| Event identifiers and Logging output | Not implemented, Issue #182 |
| Zephyr composition, target measurements | Not implemented. Target work needs separate hardware approval. |

Host tests show contract behaviour. They do not establish entropy quality, storage confidentiality or timing on
the target. The verifier iteration count, `CRUX_ACCESS_VERIFIER_ITERATIONS`, is an unmeasured placeholder, and is not
a claim of meeting general PBKDF2 guidance.

## Choices the proposal left open

The proposal sets the direction and most of the detail. These points needed a choice to implement it. Each is
open to review, and a changed choice is small while nothing depends on it.

### Interface

| Point | Choice | Reason |
| --- | --- | --- |
| Account operation signatures | `crux_access_account_{list,create,remove,disable,enable,set_role,reset_password,change_password,add_key,remove_key}` in the public header | The proposal names the operations and their session and name arguments only |
| Changing one's own password | Takes the session and no name, because the account is the session's | A name could only disagree with the session |
| Listing more accounts than fit | Fills the buffer, and returns `OK` with the total count | The caller can size a buffer by passing zero capacity |
| Name and permission sizes | `CRUX_ACCESS_NAME_SIZE` is 32 for users and roles, and `CRUX_ACCESS_PERMISSION_MAX` is 63 | The proposal bounds user names only, and requires rejecting an over-long name |
| Public key size | `CRUX_ACCESS_KEY_SIZE_MAX`, default 192 bytes, opaque | Holds Ed25519 and ECDSA SSH blobs up to P-521. **An RSA key is not accepted by default.** |
| Result of a repeated `crux_access_init` | `CRUX_ACCESS_ERR_ALREADY_EXISTS`, and nothing changes | Changing a policy under live sessions has no safe meaning |
| `crux_access_check` during a forced change | Denies `access.self` too | "Every other check is denied". The change and logout operations do not go through a check. |
| Name no account can hold, at login | `INVALID_ARGUMENT`, with no work and no delay state | It reveals nothing, because no account can have it. The rule is public. |
| Invalid policy at `crux_access_init` | `INVALID_ARGUMENT`, and later operations return `NOT_READY` | The proposal lists an invalid policy under `NOT_READY`, and says init "rejects" it. The caller can tell a bad policy from a bad store. |
| Disabling a disabled account, enabling an enabled one, or setting the current role | `OK`, with no save, no event and no session ended | Nothing changed |

### Failure results

| Point | Choice | Reason |
| --- | --- | --- |
| `UNUSABLE` store | `crux_access_init` returns `NOT_READY`, as do logins and other operations, but provisioning returns `DENIED` | The result list says `NOT_READY` for an unusable store, and the provisioning rule says `DENIED` |
| Retrying init after `UNUSABLE` | Refused without reading the store again | Otherwise a blank medium inserted while running would read as `EMPTY`, which the proposal forbids |
| A record the store calls `VALID` that cannot be decoded | Treated as `UNUSABLE` | Fail closed, including an unknown verifier algorithm or a zero iteration count |
| Random, keyed hash or derivation failure | `NOT_READY`, counted as no failure | No guess was evaluated. **This widens `NOT_READY`, whose proposed comment lists only three causes.** |
| Password over 128 bytes at login | `DENIED`, counted as no failure, with an event that names nobody | "Denied without verification". No guess was evaluated. |
| Setting a password over 128 bytes | Always `PASSWORD_REJECTED`, whatever the product rule says | Login denies it, so such an account could never log in |
| `NO_RESOURCES` | Also covers a full key table | It is the same kind of limit |

### Sequencing

| Point | Choice | Reason |
| --- | --- | --- |
| Second delay check at step 3 | Steps 2 and 3 share one lock hold | Claiming the slot never waits, so nothing can change between them |
| Clock | Read under the state lock | Stored times are then in order, so an elapsed time cannot wrap and clear a bucket or expire a live session. This is a new requirement on the provider: the clock must not wait. |
| Clock running backwards | Expires sessions, and never clears a bucket | Fails closed. A monotonic port never does this. |
| Serial bucket after a success | Not cleared | "Success clears the name bucket only", and serial has no name bucket. A user who mistypes keeps counting until the quiet period. |
| Serial when slot 0 is taken | May use any free slot | Slot 0 is reserved against the network, not a limit on serial |
| Own password change | Rule first, then the delay, then the verification slot for the current password only. A new verifier takes no slot, in this or any other operation. | The rule result never confirms the current password, and a login flood cannot block an administrator's reset. |
| Any change to the account during an own password change | `DENIED`, because the verification no longer applies, or `SESSION_INVALID` if the change ended the session | The same rule as login |
| Key pre-check on an account that cannot log in by key | Lists no keys, so a signed request counts as a key failure | Unknown, disabled, forced-change and role-less accounts cannot be told apart. `crux_access_login_verified` still refuses them, for a change after the pre-check. |
| Failed signature reported inside a window | Leaves an event and neither counts nor extends | "An attempt inside a window" applies |
| Unsigned probe inside a window | `RETRY_LATER`, with no event | "An unsigned probe records nothing" |
| Successful current password in an own password change | Clears the name bucket | "Follows the login delay", and "success clears the name bucket" |

### Providers and events

| Point | Choice | Reason |
| --- | --- | --- |
| Locks | The core uses `crux::sync`, and the ports do not include a lock | Reuses an existing capability, as `AGENTS.md` asks. `CRUX_BUILD_ACCESS` requires `CRUX_BUILD_SYNC`. |
| Derive port | Derives only, and the core compares | The proposal's port table says "derive and compare". Step 6 of the login sequence needs the comparison and the clearing in one place, for every provider. |
| Store port | Takes and returns bytes, and the core owns the versioned encoding | Each provider would otherwise repeat it. Issue #178 owns atomic replacement and corruption detection. |
| Change numbers | RAM only | They detect a change racing a login, and a reset ends every login |
| Stack | The pending table and record buffer are static, guarded by the change lock | A full table is about 4 KB, and a management worker's stack should not have to hold it |
| `delay applied` event | Emitted when an attempt is rejected, naming nobody | A rejected attempt was not looked up, and its name may be a mistyped password |
| No event | A session ended by the per-account cap, a cost upgrade, `BUSY` or `NO_RESOURCES` at password login, an account operation refused as `LAST_ADMINISTRATOR`, `NOT_FOUND` or `ALREADY_EXISTS`, and a refused provisioning | The proposal's list has none of these. **The design's requirement R9 says rejected operations produce events, so Issue #182 should decide them.** |
| Events | Carry the target account, and not the acting user | The proposal's list. An audit trail probably wants the actor, for Issue #182 to decide. |

## Declared limits

- A lock that cannot be taken means misuse, such as a call outside thread context. Every operation then fails closed,
  with these effects. After a failed lock at the end of a verification, that channel stays `BUSY` until a reset. After
  a failed lock between a save and applying it, the medium holds a newer table than RAM until a reset. Tests cover a
  lock that fails from the first call of an operation, not these two.
- The record's integrity across power loss is the store's. The core checks the shape of what it is told is valid.
- The `host-sanitize` preset is ASan and UBSan, which do not detect data races. See the pull request for a separate
  ThreadSanitizer run and its limits.

## Bounds

Compile-time defaults in `components/access/src/access_port.h`, each overridable:

| Macro | Default |
| --- | --- |
| `CRUX_ACCESS_SESSION_MAX` | 8, with slot 0 reserved for serial |
| `CRUX_ACCESS_SESSIONS_PER_ACCOUNT` | 3 |
| `CRUX_ACCESS_ACCOUNT_MAX` | 8 |
| `CRUX_ACCESS_KEYS_PER_ACCOUNT` | 2 |
| `CRUX_ACCESS_KEY_SIZE_MAX` | 192 |
| `CRUX_ACCESS_SOURCE_BUCKETS`, `CRUX_ACCESS_NAME_BUCKETS` | 32 each |
| `CRUX_ACCESS_DELAY_FREE_ATTEMPTS` | 5 |
| `CRUX_ACCESS_DELAY_BASE_MS`, `CRUX_ACCESS_DELAY_CAP_MS` | 1,000 and 60,000 |
| `CRUX_ACCESS_DELAY_QUIET_MS` | 900,000 |
| `CRUX_ACCESS_VERIFIER_ITERATIONS` | 10,000, an unmeasured placeholder |

## Boundary

`crux_access_core` links `crux_access_adapter` and its own `src` directory `PRIVATE`, so CMake records the adapter
target as `$<LINK_ONLY:crux_access_adapter>` and neither directory reaches a consumer of `crux::access`.
`cmake/CruxAccessBoundary.cmake` adds the rule the proposal asked for: it follows the include directories and `-I`
options a target exposes, and ignores `LINK_ONLY`, because that is what a consumer's compilation can see. A target
such as `crux::web` can therefore link `crux_access_adapter` privately. The rule rejects the adapter directory, a
directory below it, and a parent from which the header resolves by a longer path. The root build applies it to every
public target, and a designated consumer can call it. It cannot see a target that nothing asks it to check.
`scripts/check-access-boundary.py` tests it in both directions.

The test composition cannot be selected by any provider setting. A build that sets `CRUX_BUILD_TESTS=ON` creates the
target `crux_access_test_composition`, which a superproject could still link by name, so it must not use that build
for a product image.

## Tests

`scripts/test-access.sh` runs the suites under the `host-debug` and `host-sanitize` presets, clang-tidy on the
Access sources, and the boundary check. The root build also runs the suites as `Access.*` under
`scripts/check-host.sh`.

| Suite | Covers |
| --- | --- |
| `tests/access/conformance.c` | Two different policies: permitted, unauthenticated, unpermitted, expired, stale, reused slot, generation wrap, invalid policy, provisioning |
| `tests/access/accounts.c` | Each operation, session ending rules, forced change, last administrator including concurrent changes, save failure, power loss, a login during a save, a check that does not wait on a save or derivation |
| `tests/access/security.c` | Equal treatment of unknown and disabled names, table-filling probe, delay rules, guessing bound, collisions, flooding, serial independence, verification slots, races with a login, cost upgrade |
| `tests/access/keys.c` | Signed and unsigned attempts, failed signatures, provider-verified login |
| `tests/access/faults.c` | Session exhaustion and reclaim, store states, every truncation of a record and a chosen set of field corruptions, 64-bit time, provider failures, lock failure, concurrent logins |
| `tests/access/secrets.c` | Known password and key values absent from every event type, and no plain password on the medium |
