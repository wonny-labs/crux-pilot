# Secure management gate G1: shared decisions

Status: decided by [the owner's decision of 3 November 2026](https://github.com/wonny-labs/crux-pilot/pull/209#issuecomment-6616400894), Issue #183. It collects what the five contract proposals for Issues #178 to #182 need from the
owner and from each other. Baseline: main `4444d70`.

The proposals are, by file under `docs/plans/`: `credential-store-contract-proposal.md` (#178), `file-service-contract-proposal.md`
(#179), `status-configuration-contract-proposal.md` (#180), `cli-contract-proposal.md` (#181) and `security-events-contract-proposal.md` (#182).
Each holds the reasoning behind its decisions.

## What the gate requires

| #183 criterion | State on 4 November 2026 |
| --- | --- |
| #176 decided, stacks pinned in the manifest | The evidence and recommendation in #198, and the pins in #202, merged on 3 November. |
| #177 reviewed contract, fake provider, Host conformance | Proposal merged in #201. The implementation merged in #210 (`473b50c`), and `docs/access-contract.md` says it is not adopted until gate #183 passes. G-D is accepted: [the owner's approval](https://github.com/wonny-labs/crux-pilot/pull/209#issuecomment-6611325037) is quoted on #177. |
| #178 to #182 reviewed contracts, fake providers, Host conformance | **In progress.** #224 merged the File additions of #179, and #232 merged the file service (`b63582c`). Accepting the proposals does not meet this criterion: each contract must reach main with its header, fake provider and Host cases. |
| A rule for later shared contract changes | Decision G-A below, accepted |
| Owner decides whether this runs alongside M7 | Decision G-B below, accepted |

Target implementations can finish later, so the gate does not need PSA (#142), a Configuration target provider, or hardware results.

## Decisions for the owner

The owner's words, on 3 November 2026 at 21:56 UTC in the working session: "추천대로 진행해줘 3번까지만 하고 4번 하기전에 알려줘", in reply to the
recommendation to accept this record's recommendations and approve the headers, fakes and Host tests of #178 to #182.

- **Accepted:** G-A to G-D, AC1, AC3 to AC10, S1 to S7, F1 to F6 (F4 for now), C1 to C8, L1 to L6 and E1 to E6.
- **Declined:** AC2.
- **Also decided:** SSH user keys are Ed25519 and ECDSA only, so `rsa-sha2-256` leaves the user key table of the CLI and SFTP design, [quoted on #210](https://github.com/wonny-labs/crux-pilot/pull/210#issuecomment-6616401439).
- **Still not approved:** target and physical work, which stays behind M7 and needs its own approval (G-B). Hardware operations stay excluded.
- **Merging:** allowed after independent review with green CI, as [recorded on #183](https://github.com/wonny-labs/crux-pilot/issues/183#issuecomment-6617722497).

Accepting these decisions does not pass the gate, because each contract must still reach main with its header, fake and Host cases.

The tables below keep each decision as it was put to the owner.

### Gate level

| ID | Decision | Recommendation |
| --- | --- | --- |
| G-D | Confirm approval for #177's running implementation, which this record cannot cite | Confirm, or stop that work until it is given |
| G-A | After G1, a change to a shared contract is one reviewed PR that both tracks rebase onto. Shared means `crux_access`, the three services, `crux_cli`, the File and Configuration contract additions, the credential adapter, the event identifier table and `west.yml`. M11 does not use `crux_cli`, but a change there is still reviewed once. New event identifiers from a track are such a change, and they stay small. | Accept. This is the design's rule, with a wider list than its "`crux_access`, services, `west.yml`". |
| G-B | Whether M10 work other than #177's running implementation runs alongside M7 | Allow contract work with fakes to run alongside M7, and keep target and physical work behind it. New components are the bulk of that work, and among existing ones it changes only File and Configuration, while target work would compete for the one board. The design's working default was planning only, so this is a change the owner must make. |
| G-C | Build order after acceptance | Follow the dependencies. First the File additions and `crux_access`, including AC1 and AC7. Then events, which take a session, and the credential store and file service, which need File roots. Last the configuration and status services and the CLI. The status service also reads the store's and events' getters. |

Notes since the owner accepted these:

- G-A says M11 does not use `crux_cli`, but `web-frontend-foundation-design.md` puts the serial recovery command of #213 on the #181 registry, so a change there reaches that track too.
- The same rule applies to `web/api/openapi.yaml`, merged in #223, although the accepted G-A list does not name it: the file's header and `web-frontend-foundation-design.md` make a change to it one reviewed PR that both tracks rebase onto.

### Requests to the access control contract

These are changes the other contracts need. The owner accepted all but AC2, and #210 merged them.

| ID | Request | Needed by | Recommendation |
| --- | --- | --- | --- |
| AC1 | Add `crux_access_permitted(session, permission, bool *permitted)` to the public `crux/access.h`, with no event, no touch and no storage. An invalid session gives `SESSION_INVALID`. During a forced change it reports false. | #179, #180, #181 | **Accept.** Hiding items through `crux_access_check` emits a denial per hidden item, so help and a polled status page would flood the event ring and bury real denials. Services call it, so it cannot be adapter-only under A1. It creates no session. |
| AC2 | A query that says provisioning is open | #181, optional | Decline. `!setup` at the console avoids it. |
| AC3 | Add `access.login_busy`, `access.login_no_session`, `access.login_refused`, `access.not_ready`, `access.session_invalid`, `access.provision_refused`, `access.change_failed` and `access.change_rejected` to the emitted events, with the result table in the events proposal | #182 | Accept. The issue requires an event from each rejection path. |
| AC4 | A session keeps its origin, so a session's later events can state a source. Correct the `source` comment, which says "used only for delay tracking". | #182 | Accept |
| AC5 | State what `crux_access_check(session, "access.self")` returns during a forced password change. The two passages differ. | #181 | State that it is denied, and let the account service authorise the change itself. This recommendation is this record's own, and the CLI proposal only needs the answer stated. |
| AC6 | Note that "a blank or reformatted medium reads as `EMPTY`" is true of a started store. A blank card on the microSD composition fails at mount and reads `UNUSABLE`. In the access port, `EMPTY` means that the account record is absent, because device keys can exist first. Limit A7 stands and widens under S6. | #178 | Accept, as wording |
| AC7 | Add `crux_access_session_origin(session, uint8_t *channel, uint8_t source[16], uint8_t *source_length)` to the public header | #182, for `file.*` and `config.*` records, which hold only a session | Accept. Otherwise those records lose the channel and source that the secure management design lists for events. |
| AC8 | "A save failure leaves both copies unchanged" and "state is unchanged" hold for RAM, and for the active image except under S7. Stored bytes can change, and a failed save can win at the next boot if the store cannot then invalidate it. | #178 | Accept, as wording |
| AC9 | Emit events after releasing the state lock | #182 | Accept. An emit may wait up to 10 ms on the ring lock, and every `crux_access_check` takes the state lock. The port on the #177 branch is already called with no lock held, so this only needs stating. |
| AC10 | Add the permission and the acting user to `crux_access_event_t` on the #177 branch, which carries only one `account` name, the target of a change | #182 | Accept. Without them `access.permission_denied` has no subject and an account change has no actor. |

### Credential store, #178

| ID | Decision | Recommendation |
| --- | --- | --- |
| S1 | One sealed image in two fixed-size slots, for accounts, keys and certificates | Accept. It avoids a manifest, and a lost key fails closed. |
| S2 | Pilot protection: AES-256-GCM with a device-ID key, declared not confidential against the medium and chip ID | Accept. It matches the provider decision. |
| S3 | Pilot medium: `secure-a` and `secure-b` on the card, prepared on a host. Depends on F1. | Accept |
| S4 | No unauthenticated wipe, and the Pilot recovers with a card reader | Accept. A serial wipe would lower limit A7. |
| S5 | A key is generated only when absent from a readable store, and is used only after its save | Accept |
| S7 | A failed second write of an added record returns an error, but the record stays active and survives a reset | Accept. A rollback adds states for a rare failure. |
| S6 | Accept that losing both slots' directory entries reads as `EMPTY`, so a failed medium plus serial access can reopen provisioning | Accept for the Pilot. The tag protects contents only. |

### File, #179

| ID | Decision | Recommendation |
| --- | --- | --- |
| F1 | File roots through additive functions, with existing functions on root 0 | Accept |
| F2 | Rename refuses an existing destination | Accept. WinSCP deletes first anyway. |
| F3 | Listing by bounded handle | Accept |
| F4 | Keep the name rule, so WinSCP cannot upload a name with a space, and a name of 55 to 63 characters gets no temporary file above its resume threshold | **Accept for now, unless product users need spaces.** Widening later is additive. |
| F5 | Partial files are removed only when a new write needs the room | Accept |
| F6 | No modification time | Accept. Zephyr's `fs_dirent` has none, and FatFs uses a fixed date here. |

### Status and configuration, #180

| ID | Decision | Recommendation |
| --- | --- | --- |
| C1 | String type with a new init function, and no spec change | Accept. Spec changes break existing positional initialisers under `-Werror`. |
| C2 | UTF-8, 128 bytes, no C0, DEL or C1 controls | Accept |
| C3 | Sensitive means write-only in the service, not encrypted. Never Crux accounts or device keys, while an application's own secret may use it. | Accept |
| C4 | Polled lock, so a control thread never waits on a save | Accept |
| C5 | `crux_mgmt_status`, because `crux/status.h` belongs to Base | Accept |
| C6 | No real target Configuration provider in this contract, and a no-provider port until #16 decides | Accept. Note that M11 and M12 cannot accept `config save` on the target until it is decided. |
| C7 | Status reads System and Time at most once per second, from a snapshot | Accept. A status read can delay a control thread's watchdog feed or clock read, and this bounds how often. The delay is unmeasured, so the design's rule that management must not block control work stays unproven here until target tests. |
| C8 | The boot diagnostic owner passes captured reset causes to the status service | Accept. Each direct read would re-open the System contract's one-clear permission. |

### Command line, #181

| ID | Decision | Recommendation |
| --- | --- | --- |
| L1 | Serial console on `crux_serial`, meaning LPUART3 on the EVKB | **Accept.** LPUART1 is Logging's, and sharing is rejected at build time. It needs an adapter wired to LPUART3, a check of the wiring and a physical approval. |
| L2 | No public access class | Accept |
| L3 | Hidden and unknown commands look the same | Accept |
| L4 | `!setup` at the prompt | Accept. AC2 stays unneeded. |
| L5 | Development is an opt-in mark. Without it, an image with the CLI, web or SSH has no Zephyr shell. No image has a network shell backend or `MCUMGR_GRP_SHELL`. Enforced at build, by script and by image check. | Accept. It lets D9 move to adopted. |
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

The proposals do not change these designs, apart from the user key table of the CLI and SFTP design, where #210 removed `rsa-sha2-256` and #208 added a note on its scope. Each needs its own correction when its track starts, or earlier.

| Finding | Design | Owner of the fix |
| --- | --- | --- |
| `SETSTAT` and `FSETSTAT` are compiled out of `wolfsftp.c` under `WOLFSSH_ZEPHYR`, and the fallback status makes WinSCP report an error after upload, since `PreserveTime` defaults to true there. "Accepted without effect" does not yet hold. | CLI and SFTP | M12, with #192. Source reading only. |
| No Zephyr VFS call sets a modification time, so "applied only if the medium supports it" cannot happen | CLI and SFTP | M12 |
| FatFs rename has a cross-link window on a cut, so stale temporary cleanup must not remove a file that may share clusters with its target | Secure management, web | Handled in #179 |
| A hidden item returns `404`, which refines "`403` means a valid session without permission". Reads of a parameter during a save can give `503`. | Web | M11 |
| The serial console is not on the debug USB UART | CLI and SFTP, operator docs | #181 |
| `account list` becomes a `PERMISSION` command, and `FSTAT` uses the handle size | CLI and SFTP | M12 |
| `PUT /api/v1/config/{name}` runs on the server thread only if validators are quick. `GET /api/v1/status` becomes session-only with the service filtering. An update's response comes from a read after it, never for a sensitive parameter. `GET /api/v1/events` reads the RAM ring. | Web | M11 |
| Rule L3 replaces "hidden from help and rejected if typed" | Secure management | #181 |
| R1 says the CLI uses only encrypted channels, but the serial console is plain. It is a local channel, and the set treats it as a stated exception. | Secure management | Owner, at adoption |

## Rules across the contracts

**Permissions.** These names are fixed:

| Permission | Used by |
| --- | --- |
| `access.manage`, `access.self` | Access proposal |
| `status.read` | Status built-ins |
| `config.save` | Configuration service |
| Per parameter, project-defined | Configuration service |
| Per area, project-defined, such as `file.logs.read` | File service |
| `events.read` | Security events |
| `tls.manage`, `firmware.upgrade` | Web and secure management designs |
| `network.read`, `network.write` | Network settings service, #213 |
| `system.reboot` | Reboot service, in the web frontend design |

**Hiding.** A list uses AC1 and leaves no event. Direct use of something the session may not see returns not-found and produces a denial event through `crux_access_check`, and something it can see but not use returns denied. A sensitive parameter's read returns `WRITE_ONLY`. A CLI command the session cannot run gives the same output as an unknown one. The status service uses Base results, so it skips hidden items and reports an invalid session as `CRUX_ERR_INVALID_STATE`.

**Locks.** Each lock is taken in this order, and none is held across a lower row's wait unless stated.

| Order | Lock | Held across storage |
| --- | --- | --- |
| 1 | Access change lock | Yes, the save |
| 1a | TLS certificate lock, #190 | Yes, the save of an install or regeneration. Taken after `crux_access_check`; under it run the store's operations, the Access session getters, which take the leaf state lock, and the event emit. Polled: nothing waits for it except a material release, for at most 100 ms. |
| 2 | Credential store lock | Yes |
| 3 | Configuration service lock | Yes, during `save` only. Others poll. |
| 4 | File service lock | No |
| 5 | File lock | Yes, each call |
| Leaf | Access state lock, then event ring lock | No. The state lock is a leaf among these, and AC9 asks that emits run outside it. |
| Leaf | The store's published-copy lock | No. It is short, and the store's state getter takes it instead of the store lock. |
| Polled | Status snapshot guard | No. A caller that loses the poll answers from the previous snapshot, so it never waits. |

A service calls `crux_access_check` before taking its own lock. Account operations check permission under the change lock, as the access proposal says. Validators and status sources call no service. `crux_security_event_read` checks permission before the ring lock. Sync's unlock and give take a global metadata lock with no timeout, in every path here.

> Note, 13 November 2026: after #391 Sync has no global metadata lock, so Sync's unlock and give no longer take one in any path above. See [Sync slot metadata](../thread-sync.md#sync-slot-metadata). The decision above is unchanged. Read the lock wording as written at the baseline.

**Shared resources.** These are defaults that the integrated image must fit. Issue #197 measures them, and none is a promise.

| Resource | Default | Claimed |
| --- | --- | --- |
| File handles | `CRUX_FILE_POOL_SIZE` 4, `CONFIG_FS_FATFS_NUM_FILES` 4 | 3 for the service, 1 free for the store and project |
| File directory handles | 2, `CONFIG_FS_FATFS_NUM_DIRS` 4 | File service |
| File roots | 8 | 2 for the store, the rest for root 0 and areas |
| Crux threads | 4, at a uniform 4096 B usable stack per slot on Zephyr | Console, web worker, event drain and up to 2 SSH sessions if those use Crux threads. **This exceeds the default**, so the integrated composition must raise it. The uniform stack is also unlikely to suit: the storage probe alone used 2,264 B of a 4,096 B main stack, and upstream's wolfSSH sample configures 32 KB. |
| Crux mutexes | 8 | Access 2, store 1 or 2, configuration, file service, event ring, status snapshot, which can reach all 8. The TLS certificate module of #190 takes one more, so a composition with every service exceeds the default and must raise it. For #197. |
| Crux semaphores | 8 | Event drain |
| FatFs file objects | 4, shared | A future Settings File provider takes one outside the Crux File pool, so its count must rise then |
| RAM, estimates | | Store image 16 KiB, event ring about 4.1 KB, one CLI context of at least 320 B per session |

**Naming.** Public paths stay unique: `crux/credential_store.h`, `crux/file_service.h`, `crux/config_service.h`, `crux/management_status.h`, `crux/cli.h` and `crux/security_event.h`. Adapter-only headers follow decision A1, and the consumer boundary rule in the access proposal covers them.

**Channel values** are those of `crux_access_channel_t`, and events reuse them. `CRUX_CLI_CHANNEL_*` are table flag bits with different values, and `crux_cli_run` takes the real channel type.

## Limits of this record

Decisions here come from reading source at the stated revisions. Since then the File additions (#224), access (#210) and file service (#232) have run on Host, and no contract has run on a target or been flashed. Issue #142 has since shown that wolfPSA can generate Ed25519 on Linux Host and `native_sim`, and RT1060 is compile only. How FatFs and the card behave across a cut in the store's save sequence, and the timing of status sources on the HTTP thread are all open. WinSCP was read at its current upstream tip, not at a release, and no client trace exists yet.
