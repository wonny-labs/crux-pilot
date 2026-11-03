# Credential and key store contract proposal

Status: proposal for review, Issue #178. It implements requirement A6 of the
[access control proposal](access-contract-proposal.md) and uses the storage comparison in the provider decision record
(`docs/plans/secure-management-provider-decision.md`, PR #198, not yet on main). Nothing here authorises implementation,
dependency adoption or hardware operations. Baseline: main `4444d70`, Zephyr v4.4.2 `dccb095`, FatFs `f4ead3b`.

## Decisions requested

| ID | Decision | Recommendation | Impact if rejected |
| --- | --- | --- | --- |
| S1 | Keep accounts, device keys and certificates in one sealed image with two fixed-size slots, instead of one pair of files per record | Accept | Per-record files need a manifest to tell "never written" from "lost" (see below) |
| S2 | Pilot protection: AES-256-GCM through PSA with a key derived from the device ID, Option A of the provider decision, declared as not confidential | Accept | A plain checksum removes sealing's #142 dependency and gives the same claim against a reader of the medium and chip ID, but leaves the card readable with no effort. Key generation still needs #142. |
| S3 | Pilot medium: two extra directories on the microSD card, `secure-a` and `secure-b`, prepared on a host like the existing `files` and `settings` | Accept, with decision F1 of the file proposal | Without File roots the store needs its own native provider |
| S4 | No unauthenticated wipe. `crux_credential_erase` exists for a product-defined local trigger, and the Pilot recovers an `UNUSABLE` store by clearing the slots with a card reader. This narrows the access proposal's open question, which allowed a reference serial command. | Accept | A serial command with no login would lower limit A7 from "medium and serial" to "serial alone" |
| S5 | A device key is generated only when its record is absent from a readable store with no damaged slot. A key is not used until its save has succeeded | Accept | |
| S6 | Accept a wider statement of limit A7: damage to the directory entries of both slots also reads as `EMPTY`, so reopening provisioning can follow from a failed medium plus serial access, without an attacker blanking it | Accept for the Pilot. The tag protects slot contents only, and the store cannot see a lost directory entry from a file never created. | A manifest does not help, because it can be lost the same way. Only a marker outside the medium closes this, as A7 already says. |

## Trust boundary

The store keeps secrets across resets and reports honestly what protects them. It does not isolate code inside the
firmware image, and it does not defend a device whose medium an attacker controls.

The pilot basis is `CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY`. `HWINFO_IMXRT` reads the device ID from the OCOTP fuse shadow
registers, and that value is readable by software and by anyone who can read the chip, so it is an identifier and not a
secret. **On the pilot the store is not confidential against someone who can read the medium and the chip ID.**

| Attacker can | Password verifiers | Private keys | Account table integrity |
| --- | --- | --- | --- |
| Read the card casually, without the chip ID | Sealed. This stops casual reading only, because the key is derived from a value that is not secret. | Same | |
| Read the card and the chip ID | Readable. A salted slow verifier still has to be guessed offline, with the cost measured under the access proposal. | Readable. No protection. | |
| Write the card and know the chip ID | | Can be replaced | Can be replaced, and an old copy can be restored. Not detected. |
| Write the card without the chip ID | | | A forged slot fails its tag and is not used, so the other slot is used, or the store reads `UNUSABLE` if neither is valid. Restoring an old copy of the same device's slots is not detected, and neither is removing both files, under S6. |
| Blank the card and reach the serial console | Previous secrets are not exposed by this | New keys are generated | New first account. Accepted limit A7. |
| Read RAM or use debug access | | Readable while loaded. `tls_credential_add` in the volatile backend stores the caller's pointer (`credential->buf = cred`), so the TLS key stays in RAM while the service runs. | |

Fuse and boot state were not assessed, so debug access is not assumed to be closed. A hardware-bound key (Option C of the
provider decision) needs fuse and boot decisions, is a product decision, and is added behind the protection port without
changing callers. Whether the pilot level is acceptable depends on the product's threat model, and compliance owns that.

## Component layout

| Item | Location | Users |
| --- | --- | --- |
| Public header | `components/credential/include/crux/credential_store.h`, target `crux::credential_store` | Status service, projects that display the basis |
| Adapter header | `components/credential/adapter/crux_credential_adapter.h`, target `crux_credential_adapter` | `crux_access` store port, TLS and SSH providers, composition |
| Private ports | `components/credential/src/credential_port.h` | Host fake, Zephyr provider |

The header split follows decision A1. The consumer boundary rule stated in the access proposal applies here too.

## Public contract

```c
typedef enum {
    CRUX_CREDENTIAL_STATE_UNUSABLE = 0, /* Zero, so that uninitialised storage fails closed. */
    CRUX_CREDENTIAL_STATE_EMPTY = 1,    /* No readable image exists: first boot or a blanked store. */
    CRUX_CREDENTIAL_STATE_VALID = 2
} crux_credential_state_t;

typedef enum {
    CRUX_CREDENTIAL_BASIS_NONE = 0,          /* Test fake only. */
    CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY = 1, /* Not confidential against the medium and chip ID. */
    CRUX_CREDENTIAL_BASIS_HARDWARE_BOUND = 2 /* Declared only by a provider with its own evidence. */
} crux_credential_basis_t;

typedef enum {
    CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE = 0,
    CRUX_CREDENTIAL_EVIDENCE_SOFTWARE = 1,
    CRUX_CREDENTIAL_EVIDENCE_PHYSICAL = 2 /* The composition states this after approved target runs. */
} crux_credential_evidence_t;

typedef struct {
    crux_credential_state_t state;
    crux_credential_basis_t basis;
    crux_credential_evidence_t evidence;
    bool confidential_against_medium_reader; /* False for NONE and DEVICE_ID_KEY. */
    bool forgery_detected_without_device_id;
    bool rollback_detected;                  /* False for every provider proposed here. */
} crux_credential_store_info_t;

crux_status_t crux_credential_store_get_info(crux_credential_store_info_t *out);
```

Every result that reports the store carries these fields, and the status service in Issue #180 shows them, so a product
cannot display a store state without its basis. Before `crux_credential_start` this returns `CRUX_ERR_NOT_READY`.
Thread context. It reads a copy published under a short lock of its own, not the store lock, so it never waits on storage.

## Adapter contract

```c
typedef enum {
    CRUX_CREDENTIAL_RECORD_ACCOUNTS = 1,
    CRUX_CREDENTIAL_RECORD_TLS_KEY = 2,
    CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE = 3,
    CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY = 4
} crux_credential_record_t;

typedef enum {
    CRUX_CREDENTIAL_OK = 0,
    CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT = -1,
    CRUX_CREDENTIAL_ERR_NOT_READY = -2, /* Not started. */
    CRUX_CREDENTIAL_ERR_UNUSABLE = -3,  /* Store state is UNUSABLE. Nothing is read, written or generated. */
    CRUX_CREDENTIAL_ERR_NOT_FOUND = -4, /* Record absent from an EMPTY or VALID store. */
    CRUX_CREDENTIAL_ERR_TOO_LARGE = -5, /* Buffer too small, or a record over its limit. */
    CRUX_CREDENTIAL_ERR_ENTROPY = -6,   /* No random data for a key or nonce. Nothing was stored. */
    CRUX_CREDENTIAL_ERR_STORE = -7,     /* Save failed. The previous image stays active. */
    CRUX_CREDENTIAL_ERR_READ = -8       /* A runtime read failed. Store state is unchanged. */
} crux_credential_result_t;

typedef enum { CRUX_CREDENTIAL_KEY_ECDSA_P256 = 1, CRUX_CREDENTIAL_KEY_ED25519 = 2 } crux_credential_key_type_t;

crux_credential_result_t crux_credential_start(crux_file_root_t slot_a, crux_file_root_t slot_b);
void crux_credential_roots(crux_file_root_t roots[2]);
crux_credential_state_t crux_credential_state(void);
crux_credential_result_t crux_credential_read(crux_credential_record_t record, void *buffer, size_t capacity,
                                              size_t *length);
crux_credential_result_t crux_credential_write(crux_credential_record_t record, const void *data, size_t length);
crux_credential_result_t crux_credential_key_ensure(crux_credential_record_t record, crux_credential_key_type_t type);
crux_credential_result_t crux_credential_key_replace(crux_credential_record_t record, crux_credential_key_type_t type);
crux_credential_result_t crux_credential_key_public(crux_credential_record_t record, void *buffer, size_t capacity,
                                                    size_t *length);
crux_credential_result_t crux_credential_erase(void);
```

- The composition calls `crux_credential_start` once on every boot path, whether or not the medium mounted, and before `crux_access_init`
  loads accounts. An unbound root gives `UNUSABLE`. Until start, `crux_credential_state` returns `UNUSABLE`, and other operations return `NOT_READY`, which the access bridge maps to `UNUSABLE`. `UNUSABLE` lasts until the next boot. `EMPTY` becomes `VALID` at the first successful save. Inserting a blank
  medium while running does not reopen provisioning, because the access contract acts on what it loaded at start.
- All operations are thread context and can wait on storage. They serialise on a store lock and hold at most one File
  handle. They run on the console thread, an SSH session thread or the management worker. `crux_access_check` never calls
  them, as the access proposal requires.
- Buffers are borrowed for the call. A caller of `crux_credential_read` that received a secret clears its copy. The image
  buffer is cleared before each operation returns.
- These operations take no session. They are the trust boundary below the services: an M11 or M12 service checks
  `tls.manage`, `access.manage` or ownership, then calls them. No management route, command or file area exposes
  `crux_credential_read` of a private key, so keys are never exported through management interfaces.
- `crux_credential_key_public` gives the public part for a fingerprint on the serial console.

The bridge to the access contract is thin. `crux_access_port_store_*` reads and writes `CRUX_CREDENTIAL_RECORD_ACCOUNTS`.
Its `EMPTY` is `CRUX_CREDENTIAL_ERR_NOT_FOUND` from that record, so device keys generated before the first account do not
close provisioning. Its `UNUSABLE` is `CRUX_CREDENTIAL_ERR_UNUSABLE`, `NOT_READY`, `READ`, or a record that `crux_access` itself finds malformed.

## Behaviour

### Why one image

The requirement is that a missing or invalid key fails closed with no silent regeneration. With one pair of files per
record, an absent host key file looks the same as a host key that was never generated, so telling them apart needs a
manifest with its own power-loss rules. With one sealed image, an absent record inside a readable image means it was never
written, provided the two slots hold the same set of records, which the save rule below and the absent-record rule ensure. Loss of a key then fails the whole image closed.
Every record also changes together, so there are no states across records to reason about.

The cost is one static image buffer and a rewrite of the whole image for each change. Account changes, certificate
replacement and host key rotation are rare, and SD wear from them is negligible. Rough sizing, not a measurement: 8 accounts
with 2 public keys each is about 5.5 KB with RSA-2048 keys, a P-256 key is about 121 B in DER, and a certificate chain is up
to about 2 KB. `CRUX_CREDENTIAL_SLOT_BYTES` defaults to 16 KiB and is a build setting. Issue #197 measures the real cost.

### Slot format

Both slots are fixed-size files named `store.img`, one in each of two File roots.

| Field | Size | Meaning |
| --- | --- | --- |
| Magic | 4 B | `CXCS` |
| Format | 2 B | 1. Any other value is corrupt. A newer image is never read as absent. |
| Protection identifier | 2 B | Must match the build's protection provider, otherwise corrupt |
| Generation | 8 B | Increases by one per save |
| Payload length | 4 B | Bounded by the slot size |
| Nonce | 12 B | Fresh random per seal |
| Sealed payload | | A table of record identifier and length, then record bytes |
| Tag | 16 B | Covers the payload and, as additional data, every field above |
| Padding | to slot size | Zeros, outside the tag. Not checked. |

The Host fake uses protection identifier 0 with a CRC in place of the tag, and it cannot be selected for a product image.

### Save sequence

1. Take the store lock. Read and open the active slot's image into the image buffer.
2. Change the record. Increase the generation, and seal with a fresh nonce.
3. Open the inactive slot with `CRUX_FILE_READ_WRITE`, write the full slot in place, sync and close.
4. Read that slot back and open it.
5. Make it the active slot.
6. If this save adds a record that the image did not have, repeat steps 2 to 5 on the other slot, so that both hold the record before the call returns.
7. Clear the buffer.

If step 3 or 4 fails after any write, the store tries to zero the slot's header and returns `CRUX_CREDENTIAL_ERR_STORE`. The active slot is unchanged, and so is RAM. This matters: FatFs sends whole sectors straight to disk, and `f_sync` updates the directory last, so the image can be complete and valid when a later step reports the error. A save reported as failed could otherwise win at the next boot. If zeroing also fails, because the medium has gone, that can still happen, and it is a stated limit.

The first save differs, because no file exists: create slot A with `CRUX_FILE_CREATE_REPLACE`, write the full size with a zeroed
header, sync, then write the header and sync and close. Then do the same for slot B with the same image. After the first save both
files exist at full size. An absent or short inactive slot found by a later save, which follows a first save cut after slot A, is
created the same way.

The store does not use rename. `fatfs_rename` unlinks the destination before renaming, and the replace-pattern test in
[the microSD composition](../microsd-composition.md) left no destination file in 29 of 100 cuts, with orphaned clusters
afterwards. Once both slots are at full size, a save allocates nothing: `f_write` follows the existing cluster chain and writes no
FAT sector, and `f_sync` on such a file rewrites only its own directory entry (attribute, cluster, size, time and last-access date), and
that entry is in a different directory from the other slot's. This is a source reading of FatFs at the pinned revision and has not been cut on hardware.

**The old-or-new claim holds only from that state.** Creating a slot, which happens in the first save, after a first save cut, or when a build raises
`CRUX_CREDENTIAL_SLOT_BYTES`, allocates clusters, and it writes FAT sectors that can also hold the other slot's chain, while that other
slot may be the only valid copy. A cut there can damage that chain, and the store then reads `UNUSABLE`, not the old image. These are rare and
are stated under Limits. A build that lowers the setting cannot save an image that no longer fits, and returns `TOO_LARGE`.

### Load states

`crux_credential_start` classifies each slot, then the store. A slot is *absent* when File reports `NOT_FOUND`. It is *short*
when it is smaller than the header or its header region is all zeros. It is *valid* when it opens, and *corrupt* in every other case: a
bad magic, an unknown format, a declared end past the file's size or past `CRUX_CREDENTIAL_SLOT_BYTES`, or a failed tag. Both-valid ties,
which a first save produces, take slot A.

| Slot A | Slot B | State | Reason |
| --- | --- | --- | --- |
| Absent or short | Absent or short | `EMPTY` | First boot, an interrupted first save, or a blanked store |
| Valid | Any except unreadable | `VALID`, from the valid slot, or the higher generation if both are valid | Old or new record across power loss |
| Corrupt | Absent or short | `UNUSABLE` | A6: a corrupt record never opens provisioning |
| Corrupt | Corrupt | `UNUSABLE` | Includes a wrong device ID, a card moved between boards, and a format this build cannot read |
| Either unreadable, meaning a File result other than `OK` or `NOT_FOUND` | Any | `UNUSABLE` | A read error cannot prove which generation is newest |

The table is symmetric in A and B. A rotted length field makes a slot corrupt, never short, so it cannot read as absent. The *short* rule is
sound because the first save writes the header last, so a cut before that leaves a zeroed header, and FatFs records a file's size only at sync. A
cut inside the single header write can leave a partial header, which reads as corrupt: that fails closed and needs a local recovery. A zeroed
header after a failed save is short by the same rule, and the other slot is valid then. The conformance suite shows
old-or-new for a fake that cuts at each sector, and fail-closed at each byte.

A corrupt slot next to a valid one loads the valid slot and produces `credential.slot_invalid`. If the newest slot rots, the
older generation is used, so a removed account or a replaced key comes back, and the next save makes that older content permanent. A6 allows the old record, and the event makes it visible.

**Absent records while a slot is damaged.** An older generation can legitimately lack a record that a newer one has. So while a slot is corrupt,
`crux_credential_read` of an absent record returns `CRUX_CREDENTIAL_ERR_UNUSABLE`, and `crux_credential_key_ensure` generates nothing. Otherwise one bad slot could open
provisioning or regenerate a key clients have seen. The save rule above makes the slots agree on the set of records, so this needs a cut
between its two writes followed by damage, and it fails closed. At start, if both slots are valid and the older lacks a record the newer has, the store
rewrites the older from the newer before returning.

File `NOT_READY` is unreadable, so a missing medium or missing layout is `UNUSABLE` and never `EMPTY`.

### Out of reach of management file areas

The slots are in two File roots that no area names. The composition passes the roots to `crux_credential_start`, and it passes `crux_credential_roots` to `crux_file_service_start`, which refuses an area on one of them and refuses an empty reserved list unless the composition declares that it has no store (Issue #179). The provider refuses overlapping root paths. A client's path can only resolve to
a declared area, and FatFs names are hex encoded with a prefix, so a name cannot reach another directory. Removing files
through SFTP or the web interface therefore cannot make the store read as `EMPTY`.

This protects against management clients. Code in the image can still call File on those roots, as stated in the trust
boundary, and a host with a card reader can change anything, which is limit A7.

### Device keys

`crux_credential_key_ensure` returns `OK` if the record exists. If it is absent and the store is `EMPTY` or `VALID` with no corrupt slot, it
generates a key, saves it and only then returns `OK`, so a key is never used before it can survive a reset. In `UNUSABLE`, or while a slot is corrupt, it
returns that error and generates nothing. The composition calls it at first boot for each key that the build needs.
`crux_credential_key_replace` is the deliberate rotation path and produces an event.

An interrupted first save gives `EMPTY`, and the next boot generates again. That key was never used, so this is not silent
regeneration of a key that clients have seen. Blanking the store does regenerate keys, which is part of limit A7 and is
visible to clients as a changed host key.

Generation uses PSA. The composition must have a qualified entropy source under Issue #142: `zephyr,entropy` selects the TRNG
in `nxp_rt10xx.dtsi`, and its quality and start-up behaviour are not yet evidenced. Each save's nonce comes from the same source. If PSA or the random source fails,
the result is `CRUX_CREDENTIAL_ERR_ENTROPY` and nothing is stored. Key types are ECDSA P-256 for TLS and, for SSH, Ed25519
or P-256 as the design states. Whether wolfPSA can generate Ed25519 is unverified, and M12 selects the type from evidence.
Certificates come from M11. The store only holds their bytes.

### Protection port

```c
/* Private. Seals and opens one image. Replacing this does not change callers. */
crux_credential_result_t crux_credential_port_seal(const uint8_t *aad, size_t aad_length, const uint8_t *plain,
                                                   size_t length, uint8_t *sealed, uint8_t nonce[12], uint8_t tag[16]);
crux_credential_result_t crux_credential_port_open(const uint8_t *aad, size_t aad_length, const uint8_t *sealed,
                                                   size_t length, const uint8_t nonce[12], const uint8_t tag[16],
                                                   uint8_t *plain);
const crux_credential_store_info_t *crux_credential_port_declaration(void);
```

| Provider | Key | Declaration | Use |
| --- | --- | --- | --- |
| Fake | None, CRC only | `BASIS_NONE`, `EVIDENCE_HOST_FAKE` | Host tests only |
| Device-ID key | SHA-256 of a fixed label and the hwinfo device ID, used with PSA AEAD | `BASIS_DEVICE_ID_KEY`, not confidential | Pilot |
| Hardware-bound | Product-defined | Only with its own evidence | Product, Option C |

If hwinfo is unavailable the device-ID provider fails, and the store is `UNUSABLE`. It never uses a fixed constant.

Crux calls PSA AEAD directly and does not use Zephyr `secure_storage`. That subsystem's stock store backends are ZMS and
Settings. Neither is available here: no Settings File target provider is part of the microSD composition (owner decision,
2 November 2026), and neither gives the two-slot states that A6 needs. The claim stays the same as Option A in the provider
decision. The AEAD itself needs Issue #142 and is not run yet.

### Medium ports

| Port | Purpose | Host | Zephyr |
| --- | --- | --- | --- |
| Slot access | Stat, read, write in place, sync, create, remove | Fake with a cut at every write, plus File on the Linux provider | Crux File on two bound roots |
| `crux_credential_port_seal`, `_open` | Protection, as declared above | CRC fake | PSA, Issue #142 |
| `crux_credential_port_generate_key(type, public, public_capacity, private, private_capacity, lengths)` | Key generation | Deterministic in tests | PSA, Issue #142 |
| `crux_credential_port_random(buffer, length)` | Nonces | Deterministic in tests | PSA, Issue #142 |
| `crux_credential_port_event(const crux_security_event_t *)` | Report events without waiting | Recording fake | Issue #182 |

Portable store logic runs over the File contract, so it is the same on Host and target. The pilot uses microSD as a Pilot
choice only: a removable medium is a poor home for secrets in a product, and removing it blocks logins. QSPI NOR stays out
until Flash write safety is decided (Issues #141 and #165).

### Pilot card layout

The composition in `samples/storage_sd` never creates a directory, so a host prepares `secure-a` and `secure-b` alongside
`files` and `settings`. Today `sd_storage.c` checks only `files` and `settings`, and an image with the store adds the two new directories to that check through a build setting, so existing cards and images keep working. A blank card fails at mount and a card without them gives
`SD_STORAGE_LAYOUT_MISSING`. Both leave the File roots unbound, and the store reads `UNUSABLE`.

### Erasure

`crux_credential_erase` works in every state after start, including `UNUSABLE`, because it exists to recover from that state. It overwrites both slots with zeros, syncs, removes both files and leaves the state `UNUSABLE` until the
next reset, because provisioning requires a store that was `EMPTY` at start. It returns `OK`, or `ERR_STORE` if a step failed, and it does nothing but report `NOT_READY` before start. It does not prove physical erasure: an SD card's
internal mapping can keep old pages, and results must say so. An interrupted erase can leave a valid slot, or two zeroed slots, which read as short and so `EMPTY`, and the operator
repeats it where needed. The reference gives no command for this. A product wires it to its own local trigger. On the Pilot, an `UNUSABLE`
store is recovered by deleting the slots with a card reader.

## Failure handling

| Failure | Result |
| --- | --- |
| Save fails at write, sync or read back | `CRUX_CREDENTIAL_ERR_STORE`, event, previous image active. The store zeroes the inactive slot's header. If that fails too, the next boot may see the new image, under Limits. |
| File pool full | `CRUX_CREDENTIAL_ERR_STORE`. The file service leaves one handle free, as stated in Issue #179. |
| Medium removed while running | Reads give `ERR_READ` and saves `ERR_STORE`. Existing sessions continue, because they hold no stored state. Reinsertion needs a reset, as the microSD composition states. |
| Power loss during a save | Old or new image, subject to the medium limit below |
| Both slots damaged | `UNUSABLE`, and all logins rejected until local recovery |
| Key missing from a build that needs it | The composition's call to `crux_credential_key_ensure` generates it only in the first-boot cases above. A failure stops that listener: the SSH design already says the server does not start. |

## Testing

| Level | Cases |
| --- | --- |
| Host conformance, on the fake and on File with the Linux provider | Every row of the load table, read and write of each record, limits, `UNUSABLE` refusing every operation but erase, generation order and ties, no key generated in `UNUSABLE` or with a corrupt slot, an absent account record with a corrupt slot reading `UNUSABLE`, a new record reaching both slots, the repair at start, state before start, and each state of an interrupted erase |
| Host faults | A cut at each sector of first and later saves, then a reload that must find the old or new image, and at each byte, then a reload that must find the old or new image or `UNUSABLE`, and never `EMPTY` once a save has completed. A flipped bit in each header field. Reload after a failed sync and after a failed read back, with zeroing working and failing. Full File pool. Runs under sanitizers. |
| Host security | Secret values absent from every event. A forged or reused-generation slot on the fake. A bad tag in both slots is `UNUSABLE` and never `EMPTY`, and in one slot uses the other. |
| Boundary | The adapter header cannot be reached from consumer targets. File service rejects an area on a store root. |
| `native_sim`, with a RAM disk | The same suite on FatFs with `CONFIG_CRUX_FILE_FATFS_SIMULATOR`, which shows the file behaviour but no power loss |
| PSA, under #142 | AEAD and key generation on both PSA providers, with known answers where they exist |
| Target, separately approved | Power cuts during a save on the card, entropy, and timing. The earlier 33-cut and 100-cut runs did not cover this sequence. |

Host tests show contract behaviour. They do not establish entropy quality, confidentiality or behaviour across real power loss.

## Limits

- **Not confidential on the pilot.** See the trust boundary. Results must state this and must not call it secure storage.
- **Rollback is not detected.** Someone who can write the card can restore an older copy. It needs a protected counter, which
  the pilot does not have.
- **The medium can break the two-slot assumption.** Old-or-new holds only once both slots are at full size, and only if a write to one slot does not damage the other. An SD
  card's internal mapping can disturb data outside the sector being written, and the card makes no promise here. Damage to slot contents is
  detected. If it leaves neither valid the store reads `UNUSABLE`. If a cut comes before the inactive header is rewritten and the card also disturbs the active slot, the store can read an earlier generation, which is neither old nor new. A sector torn by a cut can also read back as an error, which gives `UNUSABLE`.
- **Directory damage can read as `EMPTY`.** The tag protects contents only. Losing the directory entries of both slots looks like a first boot. Decision S6.
- **A failed save can still take effect** at the next boot, if the error occurred after the image was complete and zeroing the header also failed.
- **Allocating windows.** Creating a slot writes FAT sectors, as stated under Save sequence.
- A corrupt slot next to an older valid one falls back to the older generation, with an event, and the next save makes that permanent.
- A card that is moved to another board reads `UNUSABLE`.
- This is a Pilot on one board and one card. Product storage is a product decision.

## Consistency with the access proposal

This contract meets A6 subject to the limits above, and three statements in the access proposal need attention at adoption. These are requests. The access proposal is not changed here.

- "A blank or reformatted medium reads as `EMPTY`" is true of the store once the composition has started it. On the microSD
  composition a truly blank card fails at mount, and a reformatted card needs the host to recreate the directories. Those
  cases are `UNUSABLE`. Limit A7 still stands, because a host can prepare a card.
- `EMPTY` in the access port means that the account record is absent, not that the whole store is empty, because device keys
  may exist before the first account. Directory damage can also read as `EMPTY`, under S6.
- "A save failure leaves both copies unchanged" and `CRUX_ACCESS_ERR_STORE`'s "state is unchanged" hold for RAM and for the active image. Stored bytes can change, and a failed save can win at the next boot if the store cannot then invalidate it.

Open question, with a working default: whether a product wants a reference local-reset trigger. The default is none, under S4.
