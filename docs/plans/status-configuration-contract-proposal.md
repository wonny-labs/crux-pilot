# Status and configuration services proposal

Status: proposal for review, Issue #180. It refines the management services in
[the secure management design](secure-management-design.md), and it proposes an extension to the
[Configuration contract](../configuration-contract.md). Nothing here authorises implementation, dependency adoption or hardware
operations. Baseline: main `4444d70`, Zephyr v4.4.2 `dccb095`.

## Decisions requested

| ID | Decision | Recommendation | Impact if rejected |
| --- | --- | --- | --- |
| C1 | Add `CRUX_CONFIG_STRING` with a new `crux_config_init_string` and caller-owned storage, leaving `crux_config_spec_t` unchanged | Accept | Adding spec fields breaks consumers that compile with `-Wextra -Werror`, as shown below |
| C2 | Strings are well-formed UTF-8 up to `CRUX_CONFIG_STRING_MAX`, default 128 bytes, with no NUL, C0 control or DEL | Accept | A value could carry terminal escapes to a CLI user or line breaks into a log |
| C3 | Sensitive means write-only through the service. It is not encryption, and credentials never use it. | Accept | A stronger claim needs storage this contract does not have |
| C4 | The service lock is polled by readers and the control thread, so only a save waits on storage | Accept | A control thread could wait on a save, against the rule that management must not block control work |
| C5 | Name the status service `crux_mgmt_status`, header `crux/management_status.h` | Accept | `crux/status.h` is the Base header and `crux_status_t` the Base result, and public paths must be unique |
| C6 | Leave the target Configuration provider out of this contract. Until it is decided, a save on the target returns not ready. | Accept, and decide under #16 | See "Target provider gap" |

## Trust boundary

These services stop a session from reading or changing what its role does not allow, and they keep sensitive values out of responses
and events. They do not protect values at rest, and they do not isolate code in the image. A project's own code still owns its
parameters and can read them directly.

**Applying a change safely remains application policy.** A successful update changes the accepted in-memory value only. It does not
apply anything to a running service, and every result says so. This follows the Configuration contract unchanged.

| Threat | Response | Limit |
| --- | --- | --- |
| Reading a parameter without permission | Per-parameter read permission, checked by the service | |
| Discovering parameter names | A parameter the session cannot read or write is absent from listings and looks missing when named. Direct access produces an event. | Timing is not equalised |
| Reading a sensitive value | No read path through the service, and no echo in a response or event | The value is plain in RAM and in storage |
| Setting a bad value | Parser limits, then the application's validator | Cross-parameter rules stay the application's |
| Terminal or log injection through strings | Character rule C2 | |
| Saving half of a change | Reported per parameter, with no atomicity promised | The Configuration contract already states this |

## Configuration contract extension

```c
typedef enum { CRUX_CONFIG_BOOL = 1, CRUX_CONFIG_U32 = 2, CRUX_CONFIG_STRING = 3 } crux_config_type_t;

typedef struct {
    crux_config_type_t type;
    union {
        bool boolean;
        uint32_t u32;
        struct {
            const char *text; /* NUL-terminated. Borrowed, see below. */
            size_t length;    /* Excludes the NUL. */
        } string;
    } data;
} crux_config_value_t;

enum { CRUX_CONFIG_STRING_MAX = 128 }; /* A build setting with this documented minimum. */

/** Initialise a string parameter. storage holds the accepted value and outlives the parameter. */
crux_config_result_t crux_config_init_string(crux_config_param_t *parameter, const crux_config_spec_t *spec, char *storage,
                                             size_t capacity);
```

`crux_config_param_t` gains private fields for the storage pointer and capacity. Callers zero-initialise it and treat its fields as
reserved, which the contract already requires.

### Why the spec struct does not change

`tests/configuration/conformance.c` and `tests/configuration_consumer/consumer.cpp` initialise `crux_config_spec_t` by position, for example
`{"report_every_cycles", 0x1234, u32(5), range, &maximum}`. Host targets build with `-Wall -Wextra -Wpedantic -Werror`, and `-Wextra`
enables missing-field warnings for positional initialisers. New spec fields would break those files, and a consumer's code like them. A new
function and new private param fields avoid this. `bool` stays the first union member, so `{CRUX_CONFIG_BOOL, {false}}` still compiles. The test
that rejects a bad type uses `(crux_config_type_t)99`, which remains invalid. The struct size changes, but no binary ABI is promised.

### Behaviour

- `crux_config_init` on a string spec returns `CRUX_CONFIG_ERR_INVALID_ARGUMENT`, and `crux_config_init_string` on a scalar spec does too.
  `capacity` must exceed the default's length and be at most `CRUX_CONFIG_STRING_MAX + 1`.
- `crux_config_read` returns `data.string.text` pointing at the parameter's storage. It stays valid until the next update or load. The caller
  already serialises the object, so this adds no new rule, and the service copies the text before releasing its lock.
- `crux_config_update` applies C2 and the capacity, then the validator, then copies. A rejected update preserves the old value.
  A candidate that aliases the storage is allowed.
- The validator sees a NUL-terminated candidate. The existing rules on validators stay.

### Encoding

Byte 0 is format version 1, byte 1 is type 3, and bytes 2 and 3 are the schema version in little-endian, as today. The text follows, with no
NUL and no separate length, because a record's length is known from the port. An empty string is 4 bytes. BOOL and U32 records stay
byte-identical, so existing stored records and evidence are unaffected.

`WIRE_MAX` in `configuration.c` grows from 8 to 4 + `CRUX_CONFIG_STRING_MAX` + 1, so `crux_config_load` uses that much more stack for every
type. The wire buffer is cleared before return, because it can hold a sensitive value.

Load follows the existing rules. A record whose advertised length exceeds the parameter's capacity is rejected from its metadata without
reading past the buffer, and that is `CRUX_CONFIG_DEFAULT_REJECTED`. A failed or short transfer of a valid length remains `IO`. A record that
fails C2 is rejected the same way. The simulator provider's `RECORD_CAPACITY` of 256 and `NAME_CAPACITY` of 36 hold for this bound.

### Compatibility

| Item | Effect |
| --- | --- |
| Existing functions on BOOL and U32 | No change in behaviour, and `tests/configuration` passes unchanged |
| `docs/configuration-contract.md` | "Only BOOL/U32 are initially supported" and "no ... strings" change on adoption. It already says that additional types need an evidenced extension. The M4 approval covered the old text. |
| Private port | Unchanged. `crux_config_record_read` already passes a capacity and reports actual and advertised lengths. |
| Native evidence | `tests/native/configuration_settings` is rerun with a string case, and earlier records stay as they are |

## Configuration service

Component `components/config_service`, header `crux/config_service.h`, target `crux::config_service`.

```c
typedef enum {
    CRUX_CONFIG_ACTIVATION_APPLICATION = 0, /* The application picks it up by its own rule. */
    CRUX_CONFIG_ACTIVATION_RESTART = 1,     /* Takes effect after a restart. */
    CRUX_CONFIG_ACTIVATION_SAFE_STATE = 2   /* The application applies it only in a safe state. */
} crux_config_activation_t;

typedef struct {
    crux_config_param_t *parameter;
    const char *read_permission;  /* Required. */
    const char *write_permission; /* NULL for read-only. */
    bool sensitive;
    crux_config_activation_t activation; /* Metadata only. The service never acts on it. */
} crux_config_service_entry_t;

typedef enum {
    CRUX_CONFIG_SERVICE_OK = 0,
    CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT = -1, /* Includes a value that cannot be parsed for the type. */
    CRUX_CONFIG_SERVICE_ERR_NOT_READY = -2,
    CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID = -3,
    CRUX_CONFIG_SERVICE_ERR_DENIED = -4,
    CRUX_CONFIG_SERVICE_ERR_NOT_FOUND = -5,   /* Missing, or a parameter the session may not see. */
    CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY = -6,  /* Sensitive, so never readable. */
    CRUX_CONFIG_SERVICE_ERR_REJECTED = -7,    /* Validator or character rule. Old value kept. */
    CRUX_CONFIG_SERVICE_ERR_BUSY = -8,        /* A save holds the lock. Retry later. */
    CRUX_CONFIG_SERVICE_ERR_STORE = -9        /* A save failed. See the report. */
} crux_config_service_result_t;

typedef struct {
    const char *name; /* The parameter's key. */
    crux_config_type_t type;
    crux_config_activation_t activation;
    bool sensitive;
    bool readable;  /* By this session. Always false when sensitive. */
    bool writable;  /* By this session. */
    bool unsaved;   /* Changed in memory since the last acknowledged save. */
} crux_config_service_item_t;

typedef struct {
    size_t saved;
    const char *failed_name;            /* NULL if every save succeeded. */
    crux_config_result_t failed_result;
} crux_config_service_save_report_t;

crux_config_service_result_t crux_config_service_start(const crux_config_service_entry_t *entries, size_t count);
crux_config_service_result_t crux_config_service_next(crux_access_session_t session, size_t *cursor,
                                                      crux_config_service_item_t *item);
crux_config_service_result_t crux_config_service_read(crux_access_session_t session, const char *name, char *text,
                                                      size_t capacity, size_t *length, crux_config_type_t *type);
crux_config_service_result_t crux_config_service_update(crux_access_session_t session, const char *name, const char *text,
                                                        size_t length, crux_config_service_item_t *applied);
crux_config_service_result_t crux_config_service_save(crux_access_session_t session,
                                                      crux_config_service_save_report_t *report);

/* For the owning application. No session, never waits. */
crux_config_service_result_t crux_config_service_try_read(const char *name, crux_config_value_t *out, char *text_buffer,
                                                          size_t capacity);
crux_config_service_result_t crux_config_service_try_update(const char *name, const crux_config_value_t *candidate);
```

`crux_config_service_start` validates the whole table once: a parameter that is initialised, a unique name, a required permission, and
sensitive only with a write permission. The application loads its parameters first. The service never calls `crux_config_load`. On failure every
operation returns `NOT_READY`.

### Permissions

Each operation checks the session first. A permission is always named, never session-only, and there is no default. The rule for one
parameter and one session:

| Session holds | Listing | Read | Update |
| --- | --- | --- | --- |
| Neither | Hidden, with no event | `NOT_FOUND`, with a denial event from `crux_access_check` | `NOT_FOUND`, with a denial event |
| Read only | Shown | Value, or `WRITE_ONLY` if sensitive | `DENIED` |
| Write only | Shown, `readable` false | `DENIED`, or `WRITE_ONLY` if sensitive | Allowed |
| Both | Shown | Value, or `WRITE_ONLY` if sensitive | Allowed |

Listing uses the quiet probe AC1, so hiding leaves no event. `crux_config_service_save` needs the fixed permission `config.save`.

### Sensitive values

A sensitive parameter's value is never returned by `read`, `next` or `update`, and `applied` carries only its metadata. It never appears in an
event or a log. The update path clears its parse buffer. A response does not say whether a value is set.

This is a statement about the service only. The value stays plain in RAM and in the Configuration record, with no confidentiality claim, and
results must say so. A password or private key belongs in the credential store of Issue #178, never in a parameter. The sensitive class is for
values such as a site password for an application's own outbound link, where write-only handling is still better than a readable field.

### Text forms

Web and CLI share one parser, so they cannot differ. `true` and `false` for BOOL. Decimal with no sign or leading zeros and with an overflow check
for U32. For STRING, the bytes as given. Anything else is `INVALID_ARGUMENT`. A web adapter escapes the text for JSON using `type`.

### Save

`crux_config_service_save` saves each `unsaved` parameter in table order through `crux_config_save` and stops at the first failure. `saved` and
`failed_name` go back to the caller. Parameters saved earlier remain saved, and there is no atomicity. A failed save may already have changed
stored bytes, as the contract says, so the `unsaved` mark stays on. An update event is produced for each accepted change and a save event for each
save, carrying names and no values.

### Concurrency

The Configuration contract requires the caller to serialise each object and its validator context. Once a parameter is in a table, the service
is that caller, under one service mutex.

| Caller | Lock wait | Context |
| --- | --- | --- |
| `next`, `read`, `update` | Polls with timeout 0, `ERR_BUSY` if a save holds the lock | Safe on the HTTP server thread if validators are quick |
| `save` | Finite wait, and holds the lock across storage | Worker, SSH session or console thread only |
| `try_read`, `try_update` | Polls with timeout 0, `ERR_BUSY` if held | A control thread, which keeps its last snapshot on `BUSY` |

`crux_mutex_lock` already defines zero as a poll. This way a control thread never waits on storage. A web response to `BUSY` is `503` with `Retry-After`.

The application must use `try_read` and `try_update`, and it must not call `crux_config_read` on a live table entry from another thread. Validators run on the caller's thread with the lock held, so they stay short and never call
this service. Nothing here reaches into a running service: the application's existing rule, capturing accepted values at its own cycle boundary, stays.

## Status service

Component `components/mgmt_status`, header `crux/management_status.h`, target `crux::mgmt_status`.

```c
typedef enum { CRUX_MGMT_STATUS_BOOL = 1, CRUX_MGMT_STATUS_U32, CRUX_MGMT_STATUS_U64, CRUX_MGMT_STATUS_TEXT } crux_mgmt_status_type_t;

typedef struct {
    crux_mgmt_status_type_t type;
    union {
        bool boolean;
        uint32_t u32;
        uint64_t u64;
        char text[64]; /* NUL-terminated, under rule C2. */
    } data;
} crux_mgmt_status_value_t;

/** Project item source. Must not block or touch storage. Runs on the caller's thread. */
typedef crux_status_t (*crux_mgmt_status_read_t)(void *context, crux_mgmt_status_value_t *out);

typedef struct {
    const char *name;       /* [a-z0-9_.-], 1 to 31 characters. "system.", "reset.", "net.", "version." and "security." are reserved. */
    const char *permission; /* Required. */
    crux_mgmt_status_read_t read;
    void *context;
} crux_mgmt_status_item_t;

typedef struct {
    const char *platform_version; /* Supplied by the build: Crux has no version constant today. */
    const char *project_version;
    const crux_mgmt_status_item_t *project_items;
    size_t project_item_count;
} crux_mgmt_status_config_t;

typedef struct {
    const char *name;
    crux_status_t status;           /* Per item, so one failure does not hide the rest. */
    crux_mgmt_status_value_t value; /* Meaningful when status is CRUX_OK. */
} crux_mgmt_status_entry_t;

crux_status_t crux_mgmt_status_start(const crux_mgmt_status_config_t *config);
crux_status_t crux_mgmt_status_next(crux_access_session_t session, size_t *cursor, crux_mgmt_status_entry_t *entry);
```

### Results

`crux_mgmt_status_next` returns `CRUX_OK`, or `CRUX_ERR_NO_DATA` at the end, `CRUX_ERR_INVALID_ARGUMENT`, or `CRUX_ERR_NOT_READY`. A session that is not valid is
`CRUX_ERR_INVALID_STATE`. An item the session may not read is skipped using the quiet probe AC1 and leaves no event, because a page polling status would
otherwise fill the event ring. The session check itself runs each call. The call does not refresh the idle timer, as the access proposal requires: the adapter
touches only on user activity.

A status service uses Base results, because its failures are all of a kind that Base already names. This is the reason the type differs from the other
services' dedicated enums.

### Built-in items

All built-in items need `status.read`.

| Name | Source | Notes |
| --- | --- | --- |
| `system.uptime_ms` | `crux_monotonic_now` | Boot-local |
| `system.clock` | `crux_realtime_get_snapshot` | `unset`, or the source name. Accuracy is not implied. |
| `system.watchdog_state` | `crux_system_status_get` | `crux_system_status_t` has per-service statuses, which pass through to each entry. No arm or feed. |
| `reset.causes`, `reset.supported` | `crux_system_reset_cause_get` | Latched masks. The service never clears them, because `crux_system_reset_cause_clear` consumes evidence and stays the application's. |
| `net.link`, `net.admin_up`, `net.ipv4` | `crux_netif_get_status` | Best effort, as that contract says. IPv4 only. |
| `version.platform`, `version.project` | Config | Strings from the build |
| `security.credential_state`, `security.credential_basis` | `crux_credential_store_get_info`, Issue #178 | States that storage is not confidential, in the basis's own name |
| `security.events_emitted`, `security.events_dropped` | `crux_security_event_get_stats`, Issue #182 | |

### Thread and time

`crux_mgmt_status_next` must be usable on the HTTP server thread, and the design gives small handlers a declared time limit. Its sources each take a short
lock. `crux_system_status_get` uses the System port lock, `crux_netif_get_status` uses `net_if_lock`, and `crux_realtime_get_snapshot` uses the Time
authority lock. None waits on storage, but none promises a time bound, and `net_if_lock` can be held by the network stack. This is unmeasured, and M11 measures it. If it
exceeds the limit, the fallback is that the web adapter runs status on its worker, with no contract change. A project item must not block. Nothing enforces this, so review does.

## Failure handling

| Failure | Result |
| --- | --- |
| Invalid table at start | `NOT_READY` everywhere, which fails closed |
| Text cannot be parsed for its type | `ERR_INVALID_ARGUMENT`, old value kept, event with the name only |
| Validator rejects | `ERR_REJECTED`, old value kept. A reason needs a contract change, and the validator returns only `bool`. |
| Save holds the lock | `ERR_BUSY`, nothing waits except another save |
| Save fails | `ERR_STORE`, a report with the name and result, and an event. No retry. |
| Save with no provider | `crux_config_save` returns `NOT_READY` or `NOT_SUPPORTED` in `failed_result`. See below. |
| One status source fails | That entry's `status` carries it, and the other entries continue |
| Project status callback fails | The same |

## Target provider gap

`crux_config_save` needs a provider, and there is none on the target today. `components/configuration/ports/zephyr/configuration_settings.c` stops the build
unless the composition is the explicit `native_sim` simulator one, and the owner's decision of 2 November 2026 keeps a Settings File target provider out of
the microSD composition. The `sd_settings_recover` step exists but is called by no image, and whether a missing settings file next to a leftover is acceptable is
an open owner question for #16.

G1 does not need a target provider, because target implementations can finish alongside the tracks. M11 and M12 acceptance of `config save` does. Decide it under
#16. The working default is the corrected Settings File path, with the 2 November decision's limit that a setting may be lost after a power cut. Results on a target with
no provider say that a save is not available.

## Testing

| Level | Cases |
| --- | --- |
| Configuration conformance, Host and `native_sim` | String init, read, update and load. Each C2 rejection, including bad UTF-8, NUL through length, controls and DEL. Max length and one over. A record longer than capacity, a short transfer, a malformed record, and a type mismatch in each direction. Wrong init function. Scalar records byte-identical to the current encoding. Existing suites unchanged. |
| C++ consumers | `tests/configuration_consumer` still compiles unchanged, plus a string case |
| Service permissions | Every row of the permission table for read, update, listing and save, with success, unauthenticated, unpermitted and invalid input on each operation |
| Validation | Each type with parser limits, including overflow, sign and leading zeros. Rejection keeps the old value. Text with a known secret never appears in an event. |
| Sensitive | No path returns the value. Known values absent from events, logs and `applied`. |
| Concurrency | `BUSY` during a held save, `try_read` never waiting, under sanitizers with threads. A save failure in the middle of a list. |
| Status | Each built-in, per-entry failure, hidden items, no event from hiding, a project item with each type. Reset causes never cleared. |
| Boundary | Headers include no native types, and the service header includes only the public access header |
| Target, separately approved | Stack use with strings, handler time on the HTTP thread, and a save once a provider exists |

Host tests show contract behaviour. They do not show that applying a change is safe, and results say that this stays application policy.

## Limits

- Accepted is not applied. Applying a change safely is application policy.
- Sensitive is write-only handling, not protection at rest.
- Saves are per parameter, with no atomicity and no rollback.
- The service covers scalars and bounded strings, with no arrays or blobs.
- There is no save provider on the target yet.
- Validators give no reason for a rejection.
- Timing of status sources is unmeasured.

## Changes other contracts need

- **AC1, access control:** the quiet permission probe, as in the file proposal. Without it, a poll of status would fill the event ring.
- **Web design:** the route for `PUT /api/v1/config/{name}` runs on the server thread only if validators are quick, and otherwise on the worker. `503` on `BUSY`, and `404` for a hidden parameter, refine its error table.
- **Configuration contract document:** revised on adoption, as stated above. No change happens in this proposal.
