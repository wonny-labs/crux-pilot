# Secure management gate G1: shared decisions

Status: working record for Issue #183, for review. It collects what the five contract proposals for Issues #178 to #182 need from the
owner and from each other. It decides nothing itself. Baseline: main `4444d70`.

The proposals are, by file under `docs/plans/`: `credential-store-contract-proposal.md` (#178), `file-service-contract-proposal.md`
(#179), `status-configuration-contract-proposal.md` (#180), `cli-contract-proposal.md` (#181) and `security-events-contract-proposal.md` (#182).
Each holds the reasoning behind its decisions.

## What the gate requires

| #183 criterion | State on this baseline |
| --- | --- |
| #176 decided, stacks pinned in the manifest | Evidence and recommendation in draft PR #198, and pins in draft PR #202. Neither is merged. |
| #177 reviewed contract, fake provider, Host conformance | Proposal merged in #201. Implementation under way in a separate session, not reviewed yet. |
| #178 to #182 reviewed contracts, fake providers, Host conformance | **Proposals only.** Accepting them does not meet this criterion: each still needs its header, fake provider and Host cases, as its own approved work. |
| A rule for later shared contract changes | Decision G-A below |
| Owner decides whether this runs alongside M7 | Decision G-B below. M7 has two open issues. |

Target implementations can finish later, so the gate does not need PSA (#142), a Configuration target provider, or hardware results.

## Decisions for the owner

Each has a recommendation. Accepting all recommendations gives a consistent set.

### Gate level

| ID | Decision | Recommendation |
| --- | --- | --- |
| G-A | After G1, a change to a shared contract is one reviewed PR that both tracks rebase onto. Shared means `crux_access`, the three services, `crux_cli` where M11 uses it, the credential adapter, the event identifier table and `west.yml`. New event identifiers from a track are such a change, and they stay small. | Accept. This is the design's rule, with the list made explicit. |
| G-B | Whether M10 work runs alongside M7 | Allow contract work with fakes to run alongside M7, and keep target and physical work behind it. New components are the bulk of that work, and among existing ones it changes only File and Configuration, while target work would compete for the one board. The design's working default was planning only, so this is a change the owner must make. |
| G-C | Build order after acceptance | Events and the File extension first, because neither needs another of these contracts. Then the credential store, which needs File roots, and the config and status services and CLI, which need `crux_access`. |

### Requests to the access control contract

These are changes the other contracts need. None has been made. #177 is being implemented now, so the earlier these are decided, the less rework.

| ID | Request | Needed by | Recommendation |
| --- | --- | --- | --- |
| AC1 | Add an adapter-only `crux_access_permitted(session, permission, bool *permitted)`, with no event, no touch and no storage | #179, #180, #181 | **Accept.** Hiding items through `crux_access_check` emits a denial per hidden item, so help and a polled status page would flood the event ring and bury real denials. |
| AC2 | A query that says provisioning is open | #181, optional | Decline. `!setup` at the console avoids it. |
| AC3 | Add `access.login_busy`, `access.login_no_session`, `access.login_refused`, `access.not_ready`, `access.session_invalid` and `access.provision_refused` to the emitted events | #182 | Accept. The issue requires an event from each rejection path. |
| AC4 | A session keeps its origin, so a session's later events can state a source. Correct the `source` comment, which says "used only for delay tracking". | #182 | Accept |
| AC5 | State what `crux_access_check(session, "access.self")` returns during a forced password change. The two passages differ. | #181 | State that it is denied, and let the account service authorise the change itself |
| AC6 | Note that "a blank or reformatted medium reads as `EMPTY`" is true of a started store. A blank card on the microSD composition fails at mount and reads `UNUSABLE`. Limit A7 stands. | #178 | Accept, as wording |

### Credential store, #178

| ID | Decision | Recommendation |
| --- | --- | --- |
| S1 | One sealed image in two fixed-size slots, for accounts, keys and certificates | Accept. It avoids a manifest, and a lost key fails closed. |
| S2 | Pilot protection: AES-256-GCM with a device-ID key, declared not confidential against the medium and chip ID | Accept. It matches the provider decision. |
| S3 | Pilot medium: `secure-a` and `secure-b` on the card, prepared on a host | Accept |
| S4 | No unauthenticated wipe, and the Pilot recovers with a card reader | Accept. A serial wipe would lower limit A7. |
| S5 | A key is generated only when absent from a readable store, and is used only after its save | Accept |

### File, #179

| ID | Decision | Recommendation |
| --- | --- | --- |
| F1 | File roots through additive functions, with existing functions on root 0 | Accept |
| F2 | Rename refuses an existing destination | Accept. WinSCP deletes first anyway. |
| F3 | Listing by bounded handle | Accept |
| F4 | Keep the name rule, so WinSCP cannot upload a name with a space, and names over 54 characters fail above its resume threshold | **Accept for now, unless product users need spaces.** Widening later is additive. |
| F5 | Partial files are removed only when a new write needs the room | Accept |
| F6 | No modification time | Accept. Zephyr's `fs_dirent` has none, and FatFs uses a fixed date here. |

### Status and configuration, #180

| ID | Decision | Recommendation |
| --- | --- | --- |
| C1 | String type with a new init function, and no spec change | Accept. Spec changes break existing positional initialisers under `-Werror`. |
| C2 | UTF-8, 128 bytes, no controls | Accept |
| C3 | Sensitive means write-only in the service, not encrypted, and never holds credentials | Accept |
| C4 | Polled lock, so a control thread never waits on a save | Accept |
| C5 | `crux_mgmt_status`, because `crux/status.h` belongs to Base | Accept |
| C6 | No target Configuration provider in this contract. Decide it under #16. | Accept. Note that M11 and M12 cannot accept `config save` on the target until it is decided. |

### Command line, #181

| ID | Decision | Recommendation |
| --- | --- | --- |
| L1 | Serial console on `crux_serial`, meaning LPUART3 on the EVKB | **Accept.** LPUART1 is Logging's, and sharing is rejected at build time. It needs an adapter wired to LPUART3, a check of the wiring and a physical approval. |
| L2 | No public access class | Accept |
| L3 | Hidden and unknown commands look the same | Accept |
| L4 | `!setup` at the prompt | Accept |
| L5 | Production: no Zephyr shell. Development: serial backend only. Enforced at build, by script and by image check. | Accept. It lets D9 move to adopted. |
| L6 | No history at first | Accept |

### Security events, #182

| ID | Decision | Recommendation |
| --- | --- | --- |
| E1 | RAM ring and a low-priority drain | Accept |
| E2 | Sequence numbers on every record | Accept. A gap is the only signal of loss after Logging accepts a record. |
| E3 | Reserve for change-class events | Accept |
| E4 | Wall time computed at drain | Accept |
| E5 | RAM only in M10, with `GET /api/v1/events` reading it | Accept. Persistence would be a later addition. |
| E6 | Logging threshold kept at INFO or more verbose | Accept, as a composition rule |

## Findings that affect the track designs

The proposals do not change these designs. Each needs its own correction when its track starts, or earlier.

| Finding | Design | Owner of the fix |
| --- | --- | --- |
| `SETSTAT` and `FSETSTAT` are compiled out of `wolfsftp.c` under `WOLFSSH_ZEPHYR`, and the fallback status makes WinSCP report an error after upload, since `PreserveTime` defaults to true there. "Accepted without effect" does not yet hold. | CLI and SFTP | M12, with #192. Source reading only. |
| No Zephyr VFS call sets a modification time, so "applied only if the medium supports it" cannot happen | CLI and SFTP | M12 |
| FatFs rename has a cross-link window on a cut, so stale temporary cleanup must not remove a file that may share clusters with its target | Secure management, web | Handled in #179 |
| A hidden item returns `404`, which refines "`403` means a valid session without permission". Reads of a parameter during a save can give `503`. | Web | M11 |
| The serial console is not on the debug USB UART | CLI and SFTP, operator docs | #181 |

## Rules across the contracts

**Permissions.** These names are fixed, and each comes from the proposal named:

| Permission | Used by |
| --- | --- |
| `access.manage`, `access.self` | Access proposal |
| `status.read` | Status built-ins |
| `config.save` | Configuration service |
| Per parameter, project-defined | Configuration service |
| Per area, project-defined, such as `file.logs.read` | File service |
| `events.read` | Security events |
| `tls.manage`, `firmware.upgrade` | M11 and M9 designs |

**Hiding.** A list uses AC1 and leaves no event. Direct use of something the session may not see returns not-found and produces a denial event through `crux_access_check`, and something it can see but not use returns denied.

**Locks.** Each lock is taken in this order, and none is held across a lower row's wait unless stated.

| Order | Lock | Held across storage |
| --- | --- | --- |
| 1 | Access change lock | Yes, the save |
| 2 | Credential store lock | Yes |
| 3 | Configuration service lock | Yes, during `save` only. Others poll. |
| 4 | File service lock | No |
| 5 | File lock | Yes, each call |
| Leaf | Access state lock, event ring lock | No. An emit may run with the state lock held. |

A service calls `crux_access_check` before taking its own lock. Validators and status sources call no service.

**Shared resources.** These are defaults that the integrated image must fit. Issue #197 measures them, and none is a promise.

| Resource | Default | Claimed |
| --- | --- | --- |
| File handles | `CRUX_FILE_POOL_SIZE` 4, `CONFIG_FS_FATFS_NUM_FILES` 4 | 3 for the service, 1 free for the store and project |
| File directory handles | 2 proposed, `CONFIG_FS_FATFS_NUM_DIRS` 4 | File service |
| File roots | 8 proposed | 2 for the store, the rest for root 0 and areas |
| Crux threads | 4 | Console, web worker, event drain and up to 2 SSH sessions if those use Crux threads. **This exceeds the default**, so the integrated composition must raise it. |
| Crux mutexes | 8 | Access 2 or more, store, configuration, file service, event ring, leaving little for a project |
| RAM, estimates | | Store image 16 KiB, event ring about 3.8 KB, one CLI context of at least 320 B per session |

**Naming.** Public paths stay unique: `crux/credential_store.h`, `crux/file_service.h`, `crux/config_service.h`, `crux/management_status.h`, `crux/cli.h` and `crux/security_event.h`. Adapter-only headers follow decision A1, and the consumer boundary rule in the access proposal covers them.

**Channel values** are those of `crux_access_channel_t`, and events reuse them.

## Limits of this record

Everything here comes from reading source at the stated revisions. No contract has run, and nothing was flashed or built. Whether wolfPSA can generate Ed25519, how FatFs and the card behave across a cut in the store's save sequence, and the timing of status sources on the HTTP thread are all open. WinSCP was read at its current upstream tip, not at a release, and no client trace exists yet.
