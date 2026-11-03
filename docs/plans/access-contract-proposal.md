# Portable access control contract proposal

Status: proposal for owner and API review, Issue #177. It refines
[the secure management design](secure-management-design.md) and
[the web design](secure-management-web-design.md), and it replaces their access control sketches and
login status codes where they differ. Nothing here authorises implementation, dependency adoption or
hardware operations. Baseline: main `5569478`, Zephyr v4.4.2.

## Decisions requested

| ID | Decision | Recommendation |
| --- | --- | --- |
| A1 | Split public and adapter-only headers, so that projects cannot create sessions | Accept |
| A2 | Use value handles with a generation in place of the pointer in the earlier sketch | Accept |
| A3 | Verify passwords with PBKDF2-HMAC-SHA256 through PSA, with the cost stored per record | Accept, with the iteration count set by measurement |
| A4 | Reference password rule: 12 to 128 bytes, no composition rules, product can replace it | Accept |
| A5 | A reset by an administrator forces a password change, and refuses key login until then | Accept, or drop to reduce scope |
| A6 | The credential store keeps the old or new account table across power loss and reports three states | Accept, as a requirement on Issue #178 |
| A7 | Accept this limit: anyone who can blank the storage medium and reach the serial console can provision a new first account | Accept for the Pilot. A product that rejects it needs a marker elsewhere, such as protected storage. |

## Trust boundary

`crux_access` protects management operations from remote and console users. It does not isolate code
inside the firmware image. Crux promises no user-mode threads (`docs/thread-sync.md`), so handles do not
defend against in-image code. The web session token is a separate random secret owned by the web adapter,
which maps it to a handle (Issue #186).

| Threat | Response | Limit |
| --- | --- | --- |
| Password guessing over the network | Non-sleeping delay and slow verifier | A reset clears delay state |
| Account discovery | Delay state ignores whether a name exists, and failure results are equal | A record with an older cost can differ in timing until its next login |
| Flooding logins to stall other clients | One network verification at a time, and web login polls | Other network logins get a busy result |
| Permanent administrator lock-out | Serial has its own delay and verification slot | While a flood lasts, network login can be delayed up to the cap or get a busy result |
| Exhausting the session table | A cap per account, a reserved serial slot, and expired slots reclaimed at login | Three valid accounts can fill the seven network slots until their sessions expire |
| Stale or reused session reference | Generation check | Generation wraps after 2^24 uses of one slot |
| Change racing a login | Record generation checked again after verification | None known |
| Secrets in logs | No password, verifier or key in any event, and unknown names are not logged | Depends on callers not logging their own input |
| Reading stored verifiers or keys | Declared by the store under Issue #178 | No protection claim here |
| Replacing the account table by writing to storage | Not addressed without a hardware-bound key | Product decision, Issue #178 |
| Blanking the medium to reopen provisioning | Local serial channel only | Accepted limit A7 |

## Component layout

| Item | Location | Users |
| --- | --- | --- |
| Public header | `components/access/include/crux/access.h`, target `crux::access` | Projects, services |
| Adapter header | `components/access/adapter/crux_access_adapter.h`, target `crux_access_adapter` | CLI, web and SSH adapters |
| Private ports | `components/access/src/access_port.h` | Host fake, Zephyr provider |

Projects must not include the adapter header. `crux::web` links the adapter target privately, and such a
link is visible to the consumer boundary check as `LINK_ONLY`, so rejecting the target by name would also
fail projects that use `crux::web`. The check needs a rule that looks at include directories reaching the
consumer. Changing the protected check needs approval when implemented, or a new check script can do it.

The design sketch made login public. Projects do not need it: they receive a session from an adapter and
check permissions. Moving login, logout and provisioning to the adapter header closes a route for project
code to create or end sessions by mistake.

## Public contract

```c
typedef struct { uint32_t value; } crux_access_session_t; /* Slot and generation. Zero is never valid. */

typedef enum {
    CRUX_ACCESS_OK = 0,
    CRUX_ACCESS_ERR_INVALID_ARGUMENT = -1,
    CRUX_ACCESS_ERR_NOT_READY = -2,       /* Not initialised, invalid policy or store unusable. */
    CRUX_ACCESS_ERR_DENIED = -3,          /* Bad credential, or no permission. */
    CRUX_ACCESS_ERR_SESSION_INVALID = -4, /* Never valid, ended or expired. */
    CRUX_ACCESS_ERR_RETRY_LATER = -5,     /* Delay window; nothing was verified. */
    CRUX_ACCESS_ERR_BUSY = -6,            /* Another verification is running. */
    CRUX_ACCESS_ERR_NO_RESOURCES = -7,    /* Session or account table full. */
    CRUX_ACCESS_ERR_NOT_FOUND = -8,
    CRUX_ACCESS_ERR_ALREADY_EXISTS = -9,
    CRUX_ACCESS_ERR_LAST_ADMINISTRATOR = -10,
    CRUX_ACCESS_ERR_PASSWORD_REJECTED = -11, /* Fails the password rule. */
    CRUX_ACCESS_ERR_STORE = -12           /* Save failed; state is unchanged. */
} crux_access_result_t;

typedef struct {
    const char *name;
    const char *const *permissions;
    size_t permission_count;
} crux_access_role_t;

typedef struct {
    const crux_access_role_t *roles;
    size_t role_count;
    uint32_t session_idle_limit_s;
    uint32_t session_absolute_limit_s;
    bool (*password_acceptable)(const char *password, size_t length); /* NULL selects the reference rule. */
} crux_access_policy_t;

crux_access_result_t crux_access_init(const crux_access_policy_t *policy); /* Policy must outlive use. */
crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission);
crux_access_result_t crux_access_session_user(crux_access_session_t session, char *name, size_t size);
crux_access_result_t crux_access_session_role(crux_access_session_t session, char *name, size_t size);
crux_access_result_t crux_access_session_must_change_password(crux_access_session_t session, bool *required);
```

Account operations take the caller's session and a name. They are list, create, remove, disable, enable,
set role, reset password, change own password, add public key and remove public key. Changing one's own
password requires the current password and follows the login delay. All account operations can wait on storage,
and those that set a password also take as long as the verifier, so a web adapter runs them on its worker.
`crux_access_check` and the session queries never wait on storage or derivation, so they are safe on the HTTP
server thread.

All operations are thread context only. Buffers are borrowed for the call, and the policy stays borrowed
for the program's life. `crux_access_init` validates the whole policy and must succeed once before anything
else works. It rejects an empty table, an empty or over-long name, a duplicate role or permission, zero
limits, an idle limit above the absolute limit, and a policy with no role holding `access.manage`. Then
every operation returns `CRUX_ACCESS_ERR_NOT_READY`.

## Adapter contract

```c
typedef enum {
    CRUX_ACCESS_CHANNEL_SERIAL = 1,
    CRUX_ACCESS_CHANNEL_WEB = 2,
    CRUX_ACCESS_CHANNEL_SSH = 3
} crux_access_channel_t;

typedef struct {
    crux_access_channel_t channel;
    uint8_t source[16]; /* Network address, used only for delay tracking. */
    uint8_t source_length;
} crux_access_origin_t;

crux_access_result_t crux_access_login_password(const char *user, const char *password, size_t length,
                                                const crux_access_origin_t *origin,
                                                crux_access_session_t *session, uint32_t *retry_after_ms);
crux_access_result_t crux_access_key_is_authorised(const char *user, const uint8_t *key, size_t key_length,
                                                   const crux_access_origin_t *origin,
                                                   uint32_t *retry_after_ms);
crux_access_result_t crux_access_login_verified(const char *user, const uint8_t *key, size_t key_length,
                                                const crux_access_origin_t *origin,
                                                crux_access_session_t *session);
void crux_access_report_key_failure(const char *user, const crux_access_origin_t *origin);
crux_access_result_t crux_access_provision_first_account(const char *user, const char *password,
                                                         size_t length, const char *role,
                                                         const crux_access_origin_t *origin);
crux_access_result_t crux_access_touch(crux_access_session_t session);
void crux_access_logout(crux_access_session_t *session);
```

- User names are C strings of 1 to 31 characters from `[a-z0-9_.-]`. A protocol's user name is
  length-delimited, so an adapter rejects an embedded NUL before calling this contract.
- For IPv6, an adapter passes the /64 prefix as the source, so that address rotation inside one prefix does
  not create new delay entries. The current Crux networking profile is IPv4 only.
- `crux_access_key_is_authorised` serves the SSH pre-check before a signature is verified, and wolfSSH calls
  it on the signed request too. It applies the source delay but not the name delay, and never creates a session.
- `crux_access_report_key_failure` records a failed signature against the source delay, and the provider calls
  it from the failure branch of its result callback. Without this, key guessing would leave no event or delay.
- `crux_access_login_verified` must be called only after the provider has verified the signature. It checks
  again that the key belongs to the account, refuses a disabled account or one with a forced password change,
  and clears the name delay. It uses no verification slot, and the name delay does not apply to key login, so a
  password flood on a name does not block its key login. A source bucket collision can still delay the pre-check
  up to the cap.
- `crux_access_provision_first_account` succeeds only on the serial channel, while the table loaded at start
  was `EMPTY` and no account has been created since, and for a role holding `access.manage`. Inserting a blank
  medium while running does not reopen it. Otherwise it returns `CRUX_ACCESS_ERR_DENIED`, or
  `CRUX_ACCESS_ERR_INVALID_ARGUMENT` for the role. Until an account exists, every login is denied the same way.
- `crux_access_check` does not refresh the idle timer. An adapter calls `crux_access_touch` on user activity,
  so that a page polling status does not keep a session alive for ever. The absolute limit always applies.

## Behaviour

### Sessions

Sessions live in a static RAM table of `CRUX_ACCESS_SESSION_MAX`, default 8, with one slot reserved for the
serial channel. The handle holds an 8-bit slot and a 24-bit generation, which increases when a slot is
released and skips the value that would make the handle zero. Expired sessions are rejected when used, and
their slots are reclaimed whenever a login needs one, so abandoned sessions cannot exhaust the table.

An account holds at most `CRUX_ACCESS_SESSIONS_PER_ACCOUNT`, default 3. A further login by that account
ends its oldest session. Sessions do not survive a reset. Changing an account's password or role, resetting
its password, removing one of its keys, disabling it or removing it ends all its other sessions.

### Password login sequence

1. Check the argument bounds. Passwords over 128 bytes are denied without verification.
2. Check the delay state. Inside a window, return `CRUX_ACCESS_ERR_RETRY_LATER` with the remaining time. This
   attempt neither counts nor extends the window.
3. Claim the channel's verification slot, or return `CRUX_ACCESS_ERR_BUSY`. The network channels share one
   slot, and serial has its own. Check the delay state again, because it may have changed while waiting.
4. Reserve a session slot, or release the verification slot and return `CRUX_ACCESS_ERR_NO_RESOURCES`. This
   comes before verification, so this result never confirms a password.
5. Read the record and its change number under the state lock, then release the lock. Derive against the
   record, or against a fixed dummy record at the current default cost for an unknown name. A disabled account
   is derived the same way and then fails, and a dummy match always fails.
6. Compare in constant time and clear internal copies.
7. Take the state lock and read the record again. If its change number differs or it is now disabled, the attempt
   fails, so a change racing a login cannot produce a session. Every account change takes a new number from a
   single counter, so removing and recreating a name also differs.
8. Create the session and clear the name delay, or record a failure and release the reserved slot. Events
   follow the outcome.

Failure results for wrong passwords, unknown names and disabled accounts are the same. Callers own and clear
their own password buffers. Web login runs this on the management worker, SSH on the connection's thread and
serial on the console thread.

### Web login results

`POST /api/v1/session` only queues the attempt, so it can return `503` when the queue is full, but it cannot
report a result from `crux_access`. The poll carries the outcome: pending, rejected, retry later with its
time, or success with the session cookie. This replaces the `429` on `POST` in the web design.

### Delay state

The effective delay for a password login is the larger of the source and name delays. Both double from a base
value after a free attempt count and decay with time. A rejected attempt inside a window does not extend it.

| State | Key | Bound |
| --- | --- | --- |
| Source | Keyed hash of the network channel and source address | `CRUX_ACCESS_SOURCE_BUCKETS`, default 32 |
| Name | Keyed hash of the supplied name, whether or not it exists | `CRUX_ACCESS_NAME_BUCKETS`, default 32 |
| Serial | The whole serial channel, with no source or name state | One |

The hash key is chosen at each boot. Buckets are fixed, and nothing is evicted, so failures on other names cannot
reset the state of an account, and rotating sources cannot reset the state of another source. Colliding names or
sources share a bucket. A real account and an unknown name therefore behave the same, and a probe cannot tell them
apart. The reference cap is 60 seconds for all states. A success clears only the name bucket. Source delay ends by
decay, so that a valid account does not help an insider spray guesses at others.

Because buckets are shared, a flood from many sources can keep network password login delayed up to the cap, and
hold the network verification slot, while it lasts. Delay ends by decay after the flood stops, and the flood never
affects the serial channel, which is the local recovery path. A cap slows guessing and does not stop it, so the
verifier cost and password rule still matter. Delay values are reference settings that a product can replace.

### Password verifier

Each record stores an algorithm identifier, iteration count, 16-byte random salt and 32-byte derived value.
This allows a later algorithm or cost change without a format break. An account is upgraded at its next
successful login. Until then its cost can differ from the dummy record, which is the timing limit declared
above. Tests count iterations as well as derivations.

PBKDF2-HMAC-SHA256 is proposed because the PSA API defines it and both candidate providers contain it
(`PSA_ALG_PBKDF2_HMAC`, read in source and not yet run). Memory-hard functions such as Argon2 resist hardware
attacks better, but neither provider's PSA headers offer one. Current general guidance for PBKDF2 asks for far
more iterations than a controller may tolerate per login, so the count is a measured trade-off between login
time and offline guessing cost. The result must be declared and must not be described as meeting that guidance
unless it does. Measuring on the target needs hardware approval.

### Roles and permissions

Each account has one role, stored by name. Permissions are compared as exact strings against the role's list,
with no wildcards or inheritance. If a firmware update removes an account's role, that account can no longer log
in, and the event says so. An administrator role is any role holding `access.manage`. The last enabled account
with such a role cannot be disabled, removed or moved to another role.

This component defines `access.manage` and `access.self`, and every role implicitly holds `access.self`, which
allows changing one's own password and keys and logging out.

A password reset by an administrator sets a forced-change flag. While it is set, password login gives a session
that may only change the password and log out, and every other check is denied, including key changes. Adapters
detect this with `crux_access_session_must_change_password`. Key login is refused until the change, so a key added
by an attacker does not bypass a reset. A reset does not remove keys. An administrator who suspects compromise
removes them as well, and the account listing shows the key count.

### Persistence

The whole account table is saved as one versioned record through a private store port, implemented under Issue
#178. Two locks are used. A change lock serialises every account change from its checks to its completion: check
permission and last administrator, save, then apply to RAM. Two administrators disabling each other therefore run
in order, and the second finds its session ended. A state lock guards the RAM tables, is held only briefly, and is
never held across storage or derivation, so that `crux_access_check` never waits on either. Applying a change takes
the state lock after the save. Password derivation runs before the change lock is taken and is re-validated
afterwards. A login that completes during a save sees the old record, and applying the change then ends its session.
A save failure leaves both copies unchanged. The bound is `CRUX_ACCESS_ACCOUNT_MAX`,
default 8, with `CRUX_ACCESS_KEYS_PER_ACCOUNT`, default 2.

Requirements on the store (A6):

- A save either completes or leaves the previous record readable across power loss, and corruption is detected. The
  File contract does not promise atomic replacement, so the store needs its own arrangement, such as two generations.
- It reports exactly one of `EMPTY`, `VALID` or `UNUSABLE`. A missing medium, a read error or a corrupt record is
  `UNUSABLE`. Only `EMPTY` opens provisioning, and `UNUSABLE` rejects all logins until a local recovery.
- The record is out of reach of the management file areas, so that removing files through SFTP or the web
  interface cannot make the store read as `EMPTY`.

A blank or reformatted medium reads as `EMPTY`, because the File contract leaves format policy to composition and
offers no way to tell it from a first boot. This is limit A7. Reopening needs physical access to the medium and to
the serial console, and it does not expose a previous account's secrets.

Existing sessions continue if the store fails later, because they hold no stored state. Account changes return
`CRUX_ACCESS_ERR_STORE`.

### Ports

| Port | Purpose | Host | Zephyr |
| --- | --- | --- | --- |
| `crux_access_port_derive` | Derive and compare a verifier | Fast fake, plus PSA where available | PSA Crypto, Issue #142 |
| `crux_access_port_random` | Salts and the name hash key | Deterministic in tests | PSA Crypto |
| `crux_access_port_now_ms` | 64-bit monotonic time for expiry and delay | Controllable clock in tests | Native uptime |
| `crux_access_port_store_*` | Load and save the table | In-memory fake with fault injection | Issue #178 |
| `crux_access_port_event` | Report events | Recording fake | Issue #182 |

The fake derivation lets contract work proceed before Issue #142 is complete. A Host build with a fake verifier is a
test composition only, and it must not be selectable for a product image.

### Events

Events are login success, login failure, key failure, delay applied, permission denied, logout, session expired,
provisioned, and each account change. They carry the channel, source, result and known account name.

A login failure for an unknown name omits the supplied name, because people often type a password into the name
field. No event contains a password, verifier, salt or key. Identifiers are finalised under Issue #182.

## Testing

| Level | Cases |
| --- | --- |
| Host conformance | Permitted, unauthenticated, unpermitted, expired, stale handle, reused slot, missing and invalid policy, each failing closed. Two different reference policies run the same suite. |
| Host security | Unknown and disabled names behave like wrong passwords, with equal results and iteration counts at default cost. A table-filling probe cannot tell real names from unknown ones. Also delay windows and caps, bucket collisions, no reset of another entry by flooding, serial independence, last administrator under concurrent changes, disable and password change during verification, forced change with password and key login, and provisioning refused for a non-administrator role and for `UNUSABLE`. |
| Host faults | Session exhaustion and reclaim, save failure leaves state unchanged, interrupted save, corrupt record, 64-bit time limits, concurrent logins, all under sanitizers |
| Secret absence | Known password and key values must not appear in any recorded event or log |
| Boundary | The adapter header cannot be reached from consumer targets, and public headers contain no native types |
| Target, separately approved | Verifier timing and stack use on RT1060-EVKB, serial login |

Host tests show contract behaviour. They do not establish entropy quality, storage confidentiality or timing
behaviour on the target.

## Out of scope

Central authentication, multi-factor login, password history and expiry, resource-level permissions and tokens for
automation. None of these is precluded, and each needs a consumer first.

## Open questions, with working defaults

| Question | Default |
| --- | --- |
| Reference limits: 15 minutes idle and 8 hours absolute | Use them, with product override through policy |
| Product level model | Viewer, operator and administrator as reference policy only |
| Local reset that erases secrets | Product-defined, with a reference serial command under Issue #181 if wanted |
