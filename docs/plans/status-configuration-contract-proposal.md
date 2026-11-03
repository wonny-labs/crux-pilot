# Status and configuration services proposal

Status: accepted by [the owner's decision of 3 November 2026](https://github.com/wonny-labs/crux-pilot/pull/209#issuecomment-6616400894), Issue #180: C1 to C8. Implementing its headers, fakes and Host tests is approved. Target and physical work stays behind M7 and needs its own approval, and no merge is approved. It refines the management services in
[the secure management design](secure-management-design.md), and it proposes an extension to the
[Configuration contract](../configuration-contract.md). Baseline: main `4444d70`, Zephyr v4.4.2 `dccb095`.

## Decisions

The owner accepted the recommendation of every row below.

| ID | Decision | Recommendation | Impact if rejected |
| --- | --- | --- | --- |
| C1 | Add `CRUX_CONFIG_STRING` with a new `crux_config_init_string` and caller-owned storage, leaving `crux_config_spec_t` unchanged | Accept | Adding spec fields breaks consumers that compile with `-Wextra -Werror`, as shown below |
| C2 | Strings are well-formed UTF-8 up to `CRUX_CONFIG_STRING_MAX`, default 128 bytes, with no NUL, C0 control, DEL or C1 control | Accept | A value could carry terminal escapes to a CLI user or line breaks into a log |
| C3 | Sensitive means write-only through the service. It is not encryption. Crux accounts and device keys never use it. An application's own secret may, with no confidentiality claim. | Accept | A stronger claim needs storage this contract does not have |
| C4 | The service lock is polled by readers and the control thread, so only a save waits on storage | Accept | A control thread could wait on a save, against the rule that management must not block control work |
| C7 | Status reads the System and Time sources at most once per `CRUX_MGMT_STATUS_REFRESH_MS`, default 1000 ms, and answers from that snapshot. This knowingly leaves the design's rule, that management must not block control work, unproven for these two sources until target measurement. | Accept | A client's polling rate sets how often the status path holds locks that control threads also take |
| C8 | The boot diagnostic owner passes captured reset causes to the status service, which never calls `crux_system_reset_cause_get` | Accept | Each status poll would re-open the System contract's one-clear permission |
| C5 | Name the status service `crux_mgmt_status`, header `crux/management_status.h` | Accept | `crux/status.h` is the Base header and `crux_status_t` the Base result, and public paths must be unique |
| C6 | Leave the real target Configuration provider out of this contract. Until it is decided, a composition selects a small no-provider port, and load and save return `CRUX_CONFIG_ERR_NOT_SUPPORTED`. | Accept, and decide under #16 | See "Target provider gap" |

## Trust boundary

These services stop a session from reading or changing what its role does not allow, and they keep sensitive values out of responses
and events. They do not protect values at rest, and they do not isolate code in the image: project code is trusted, and it could read
any parameter. The rules for doing so safely are under Concurrency.

**Applying a change safely remains application policy.** A successful update changes the accepted in-memory value only. It does not
apply anything to a running service, and the `applied` result of an update carries the parameter's `activation`, so an adapter can say so
in its response. This follows the Configuration contract unchanged.

| Threat | Response | Limit |
| --- | --- | --- |
| Reading a parameter without permission | Per-parameter read permission, checked by the service | |
| Discovering parameter names | A parameter the session cannot read or write is absent from listings and looks missing when named. Direct access produces an event. | Timing is not equalised |
| Reading a sensitive value | No read path through the service, and no echo in a response or event | The value is plain in RAM and in storage |
| Setting a bad value | Parser limits, then the application's validator | Cross-parameter rules stay the application's |
| Terminal or log injection through strings | Character rule C2. Events never carry values. | U+2028, bidi controls and similar pass C2, so a terminal or page can still display a value misleadingly. Adapters escape for their format. |
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

#ifndef CRUX_CONFIG_STRING_MAX
#define CRUX_CONFIG_STRING_MAX 128 /* Set as a public compile definition of the target, so that every user agrees. */
#endif

/** Initialise a string parameter. storage holds the accepted value and outlives the parameter. */
crux_config_result_t crux_config_init_string(crux_config_param_t *parameter, const crux_config_spec_t *spec, char *storage,
                                             size_t capacity);

/** The key of an initialised parameter, for a service that holds only the object. NULL otherwise. */
const char *crux_config_key(const crux_config_param_t *parameter);
```

`crux_config_param_t` gains private fields for the storage pointer and capacity. Callers zero-initialise it and treat its fields as
reserved, which the contract already requires.

### Why the spec struct does not change

`tests/configuration/conformance.c` and `tests/configuration_consumer/consumer.cpp` initialise `crux_config_spec_t` by position, for example
`{"report_every_cycles", 0x1234, u32(5), range, &maximum}`. Host targets build with `-Wall -Wextra -Wpedantic -Werror` (`tests/configuration/CMakeLists.txt`), and `-Wextra`
enables missing-field warnings for positional initialisers. New spec fields would break those files, and a consumer's code like them. A new
function and new private param fields avoid this. `bool` stays the first union member, so `{CRUX_CONFIG_BOOL, {false}}` still compiles. The test
that rejects a bad type uses `(crux_config_type_t)99`, which remains invalid. The struct size changes, but no binary ABI is promised. Because `bool` stays first and designators are C++20, a C++17 consumer sets a string default at run time, and the string consumer test shows how.

### Behaviour

- `crux_config_init` on a string spec returns `CRUX_CONFIG_ERR_INVALID_ARGUMENT`, and `crux_config_init_string` on a scalar spec does too.
  `capacity` must exceed the default's length and be at most `CRUX_CONFIG_STRING_MAX + 1`. The default must pass C2 and the rule below, and it must not alias `storage`.
- A string value is accepted only if `text` is non-null, `text[length]` is the NUL and no earlier byte is. This applies to a default at init and at fallback, and to a candidate, so a validator cannot see other bytes than those stored.
- `crux_config_key` is a small addition, because the `spec` field is reserved and `config_service` is a separate component.
- `crux_config_read` returns `data.string.text` pointing at the parameter's storage. It stays valid until the next update or load. Unlike a scalar copy this is a borrow, so a
  caller keeps its serialisation until it has finished with the text. The service copies the text before releasing its lock.
- `crux_config_update` applies C2 and the capacity, then the validator, then copies. A rejected update preserves the old value.
  A candidate that aliases the storage is allowed.
- The validator sees a NUL-terminated candidate. The existing rules on validators stay.

### Encoding

Byte 0 is format version 1, byte 1 is type 3, and bytes 2 and 3 are the schema version in little-endian, as today. The text follows, with no
NUL and no separate length, because a record's length is known from the port. An empty string is 4 bytes. BOOL and U32 records stay
byte-identical, so existing stored records and evidence are unaffected.

`WIRE_MAX` in `configuration.c` grows from 8 to 4 + `CRUX_CONFIG_STRING_MAX` + 1, so `crux_config_load` and `crux_config_save` both use that much more stack for every
type. Both clear the wire buffer on every return path, in a form the compiler cannot remove, because it can hold a sensitive value. The provider's own buffer (`read_context.bytes` in `configuration_settings.c`) is not cleared, which fits the absence of a confidentiality claim.

Load follows the existing rules, with record `length` including the 4-byte header. A record is rejected from its metadata, without reading past the buffer, when `length < 4` or
`length - 4 >= capacity`, and that is `CRUX_CONFIG_DEFAULT_REJECTED`. A failed or short transfer of a valid length remains `IO`. A record that
fails C2 is rejected the same way. The simulator provider's `RECORD_CAPACITY` of 256 holds for up to 252 bytes of text, and its `NAME_CAPACITY` of 36 is unchanged. Above 256 that provider returns `IO`, not a rejection, so a build check keeps `CRUX_CONFIG_STRING_MAX` at or below 252 when it is selected.

### Compatibility

| Item | Effect |
| --- | --- |
| Existing functions on BOOL and U32 | No change in behaviour, and `tests/configuration` passes unchanged |
| `docs/configuration-contract.md` | "Only BOOL/U32 are initially supported" and "no ... strings" change on adoption. It already says that additional types need an evidenced extension. The M4 approval covered the old text. |
| Private port | Signature unchanged. `crux_config_record_read` already passes a capacity and reports actual and advertised lengths. A no-provider implementation is added, as stated under Target provider gap. |
| Test fixture | `RECORD_BYTES` in `tests/configuration/fault_provider.c` is 16 and drops longer records, so that fixture grows for string cases. Its assertions do not change. |
| Native evidence | The suites under `tests/native/` for Configuration are rerun with a string case, and earlier records stay as they are |

## Configuration service

Component `components/config_service`, header `crux/config_service.h`, target `crux::config_service`. The Configuration contract rejects a runtime registry for its own mechanism. This table is a constant above that mechanism, owned by the composition, and the mechanism stays as it is.

| Dependency or port | Purpose | Host | Zephyr |
| --- | --- | --- | --- |
| `crux::configuration`, `crux::access`, `crux::sync` | Public contracts | Real, with a fake Configuration provider and a fake access provider | Real |
| `crux_config_service_port_event` | Report events without waiting | Recording fake | Thin provider onto Issue #182 |

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
    CRUX_CONFIG_SERVICE_END = 1,                   /* Listing finished. Item unchanged. */
    CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT = -1, /* Includes a value that cannot be parsed for the type. */
    CRUX_CONFIG_SERVICE_ERR_NOT_READY = -2,
    CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID = -3,
    CRUX_CONFIG_SERVICE_ERR_DENIED = -4,
    CRUX_CONFIG_SERVICE_ERR_NOT_FOUND = -5,   /* Missing, or a parameter the session may not see. */
    CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY = -6,  /* Sensitive, so never readable. */
    CRUX_CONFIG_SERVICE_ERR_REJECTED = -7,    /* Validator or character rule. Old value kept. */
    CRUX_CONFIG_SERVICE_ERR_BUSY = -8,        /* The service lock or Sync metadata is held. Retry later. */
    CRUX_CONFIG_SERVICE_ERR_STORE = -9,       /* A save failed. See the report. */
    CRUX_CONFIG_SERVICE_ERR_TOO_SMALL = -10   /* Output buffer. CRUX_CONFIG_STRING_MAX + 1 is always enough. */
} crux_config_service_result_t;

typedef struct {
    const char *name; /* The parameter's key. */
    crux_config_type_t type;
    crux_config_activation_t activation;
    bool sensitive;
    bool readable;  /* By this session. Always false when sensitive. */
    bool writable;  /* By this session. */
    bool unsaved;   /* Accepted update since the last acknowledged save. False unless writable. */
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

/* For the owning application. No session. They poll the service lock and never wait on it or on storage. */
crux_config_service_result_t crux_config_service_try_read(const char *name, crux_config_value_t *out, char *text_buffer,
                                                          size_t capacity);
crux_config_service_result_t crux_config_service_try_update(const char *name, const crux_config_value_t *candidate);
/* For the owning application, on a thread that may wait on storage. No session, no permission. */
crux_config_service_result_t crux_config_service_save_local(crux_config_service_save_report_t *report);
```

`crux_config_service_start` validates the whole table once: at most `CRUX_CONFIG_SERVICE_MAX` entries (default 32, which also bounds the private `unsaved` bits), a parameter that is initialised, a unique name, a required permission, and
sensitive only with a write permission. The application loads its parameters first, and reloading after start is not supported. The service never calls `crux_config_load`. On failure every
operation returns `NOT_READY`.

`crux_config_service_next` starts with `*cursor` at zero and returns `CRUX_CONFIG_SERVICE_END` when finished.

### Permissions

Each operation checks the session first. A permission is always named, never session-only, and there is no default. A check or probe that returns anything but success denies, and a NULL `write_permission` is treated as not held without a call. The rule for one
parameter and one session:

| Session holds | Listing | Read | Update |
| --- | --- | --- | --- |
| Neither | Hidden, with no event | `NOT_FOUND`, with a denial event from `crux_access_check` | `NOT_FOUND`, with a denial event |
| Read only | Shown | Value, or `WRITE_ONLY` if sensitive | `DENIED` |
| Write only | Shown, `readable` false | `DENIED`, or `WRITE_ONLY` if sensitive | Allowed |
| Both | Shown | Value, or `WRITE_ONLY` if sensitive | Allowed |

Listing uses the quiet probe AC1, so hiding leaves no event. `crux_config_service_save` needs the fixed permission `config.save`, and without it returns `DENIED`.

### Sensitive values

A sensitive parameter's value is never returned by `read`, `next` or `update`, and `applied` carries only metadata. It never appears in an
event or a log. The update path clears its parse buffer. A response does not say whether a value is set, although `unsaved` tells a session with write permission that an update was accepted.

This is a statement about the service only. The value stays plain in RAM and in the Configuration record, with no confidentiality claim, and
results must say so. Crux account credentials and device keys belong in the credential store of Issue #178, never in a parameter. An application's own secret, such as a password for its own outbound link, has no record in that store and may use this class, where write-only handling is still better than a readable field. A product needing more protects it itself.

An adapter must also keep the value off its own output. The CLI takes a sensitive value by prompt with no echo, as requested of Issue #181 below.

### Text forms

Web and CLI share one parser, so they cannot differ. `true` and `false` for BOOL. Decimal with no sign or leading zeros and with an overflow check
for U32. For STRING, the bytes as given. Anything else is `INVALID_ARGUMENT`. A web adapter escapes the text for JSON using `type`.

### Save

`crux_config_service_save` saves each `unsaved` parameter in table order through `crux_config_save` and stops at the first failure. `saved` and
`failed_name` go back to the caller. Parameters saved earlier remain saved, and there is no atomicity. A failed save may already have changed
stored bytes, as the contract says, so the `unsaved` mark stays on. The mark is set by every accepted update, without comparing values. An update event is produced for each change accepted through `crux_config_service_update` and a save event for each
save, carrying names and no values. `try_update` and `save_local` come from the application and produce no event, so the control thread never meets the event lock.

### Concurrency

The Configuration contract requires the caller to serialise each object and its validator context. Once a parameter is in a table, the service
is that caller, under one service mutex.

| Caller | Lock wait | Context |
| --- | --- | --- |
| `next`, `read`, `update` | Polls with timeout 0, `ERR_BUSY` if held | Safe on the HTTP server thread if validators are quick |
| `save`, `save_local` | Finite wait, and holds the lock across storage | Worker, SSH session or console thread only |
| `try_read`, `try_update` | Polls with timeout 0, `ERR_BUSY` if held | A control thread, which keeps its last snapshot on `BUSY` |

`crux_mutex_lock` already defines zero as a poll, and the native wait runs with the Sync metadata lock released. This way a control thread never waits on the service lock or on storage. It can still wait, briefly and with no stated bound, on the global Sync metadata lock: `crux_mutex_unlock` takes that with no timeout (`components/sync/src/sync.c`), and every Sync call in the image contends for it. A poll also returns `BUSY` if another reader holds the service lock or a thread holds the metadata lock, so a web read can get `503` with `Retry-After` when no save is running.

A `TIMEOUT` from a save's wait gives `ERR_BUSY`. Any other lock failure, such as `IO` or a relock by a validator, gives `ERR_NOT_READY` and changes nothing.

Once a parameter is in a started table, every thread uses the service for it, and direct `crux_config_read`, `_update`, `_load` and `_save` on it are not allowed. The Settings provider is caller-serialised too, so after start no thread calls `crux_config_save` or `crux_config_load` on any parameter: a parameter that the application keeps private goes in the table under a permission no role holds, and `save_local` covers it.

Validators run on the caller's thread with the lock held, so they stay short and never call this service. A validator's context is immutable after start, or guarded by the application without waiting, because the application cannot hold the service lock from outside. Nothing here reaches into a running service: the application's existing rule, capturing accepted values at its own cycle boundary, stays.

## Status service

Component `components/mgmt_status`, header `crux/management_status.h`, target `crux::mgmt_status`.

| Dependency or port | Purpose | Host | Zephyr |
| --- | --- | --- | --- |
| `crux::access`, `crux::base`, `crux::system` | Public contracts. System is public because the config carries `crux_reset_cause_t`. | Real, with a fake access provider | Real |
| `crux::sync` | Private, for the snapshot's guard | Real | Real |
| `crux_mgmt_status_port_*` | Built-in sources: System, netif, Time, and optionally the credential store and events | Fake with fault injection per source | Thin providers. A composition that links no store or event component omits those items. |

```c
typedef enum { CRUX_MGMT_STATUS_BOOL = 1, CRUX_MGMT_STATUS_U32, CRUX_MGMT_STATUS_U64, CRUX_MGMT_STATUS_TEXT } crux_mgmt_status_type_t;

typedef struct {
    crux_mgmt_status_type_t type;
    union {
        bool boolean;
        uint32_t u32;
        uint64_t u64;
        char text[64]; /* NUL-terminated, under rule C2. The service checks it. */
    } data;
} crux_mgmt_status_value_t;

/** Project item source. Must not block or touch storage. Runs on the caller's thread, and may be called concurrently. */
typedef crux_status_t (*crux_mgmt_status_read_t)(void *context, crux_mgmt_status_value_t *out);

typedef struct {
    const char *name;       /* [a-z0-9_.-], 1 to 31 characters. "system.", "reset.", "net.", "version." and "security." are reserved. */
    const char *permission; /* Required. */
    crux_mgmt_status_read_t read;
    void *context;
} crux_mgmt_status_item_t;

typedef struct {
    const char *platform_version; /* Supplied by the build: Crux has no version constant today. At most 63 bytes. */
    const char *project_version;  /* At most 63 bytes. */
    crux_status_t boot_reset_cause_status;  /* What the boot diagnostic owner captured. */
    crux_reset_cause_t boot_reset_cause;
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

`crux_mgmt_status_start` rejects an over-long version string and an invalid table. `crux_mgmt_status_next` starts with `*cursor` at zero and returns `CRUX_OK`, or `CRUX_ERR_NO_DATA` at the end, `CRUX_ERR_INVALID_ARGUMENT`, or `CRUX_ERR_NOT_READY`. A project item that returns text failing C2, or a wrong type, gets `CRUX_ERR_IO` in its entry. A session that is not valid is
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
| `system.clock` | `crux_realtime_get_snapshot`, in the snapshot | `unset`, or the source name. Accuracy is not implied. |
| `system.watchdog_state` | `crux_system_status_get`, in the snapshot | `crux_system_status_t` has per-service statuses, which pass through to each entry. No arm or feed, although the System contract lets an observation update the watchdog lifecycle, including a latched fault after a failed probe. |
| `reset.causes`, `reset.supported` | Config | What the boot diagnostic owner captured. See decision C8. |
| `net.link`, `net.admin_up`, `net.ipv4` | `crux_netif_get_status` | Best effort, as that contract says. IPv4 only. |
| `version.platform`, `version.project` | Config | Strings from the build |
| `security.credential_state`, `_basis`, `_evidence`, `_confidential`, `_forgery_detected`, `_rollback_detected` | `crux_credential_store_get_info`, Issue #178 | Every field of the declaration, so that a state never appears without its basis |
| `security.events_emitted`, `_dropped`, `_delivered`, `_log_errors` | `crux_security_event_get_stats`, Issue #182 | |

### Thread and time

`crux_mgmt_status_next` must be usable on the HTTP server thread, and the design gives small handlers a declared time limit. Its sources each take a short
lock. `crux_system_status_get` uses the System port lock, `crux_netif_get_status` uses `net_if_lock`, and `crux_realtime_get_snapshot` uses the Time
authority lock. None waits on storage, but none promises a time bound, and `net_if_lock` can be held by the network stack. This is unmeasured, and M11 measures it. If it
exceeds the limit, the fallback is that the web adapter runs status on its worker, with no contract change. A project item must not block. Nothing enforces this, so review does.

The opposite direction matters too. `crux_watchdog_feed` takes the same System lock as `crux_system_status_get`, and clock reads share the Time lock, both with an unbounded native wait. A management read can therefore delay a control thread's feed or clock read, which the design says a management operation must not do. Decision C7 bounds how often this happens, however fast clients poll, and it does not remove it. Whether native priority inheritance keeps the delay short is unverified. One `crux_mutex`, polled with timeout 0, guards the snapshot. A caller that wins the poll and finds the snapshot older than the period refreshes it. A caller that loses the poll answers from the previous snapshot and never waits behind a refresh. Before the first successful refresh, affected entries carry `CRUX_ERR_BUSY`, and a failed source read sets that entry's status while keeping the others. Target tests measure feed and clock jitter under a status poll, and if they fail, the snapshot moves to a separate low-rate reader that a control thread can outrun. This is a declared limit until then.

## Failure handling

| Failure | Result |
| --- | --- |
| Invalid table at start | `NOT_READY` everywhere, which fails closed |
| Text cannot be parsed for its type | `ERR_INVALID_ARGUMENT`, old value kept, event with the name only |
| Validator rejects | `ERR_REJECTED`, old value kept. A reason needs a contract change, and the validator returns only `bool`. |
| Save holds the lock | `ERR_BUSY`, nothing waits except another save |
| Save fails | `ERR_STORE`, a report with the name and result, and an event. No retry. |
| Save with the no-provider port | `ERR_STORE`, with `CRUX_CONFIG_ERR_NOT_SUPPORTED` in `failed_result`. See below. |
| One status source fails | That entry's `status` carries it, and the other entries continue |
| Project status callback fails | The same |

## Target provider gap

`crux_config_save` and `crux_config_load` call the private port directly, and there is no target provider today. `components/configuration/ports/zephyr/configuration_settings.c` stops the build
unless the composition is the explicit `native_sim` simulator one, and the owner's decision of 2 November 2026 keeps a Settings File target provider out of
the microSD composition. An image using Configuration on the target would therefore fail to link, not return an error. A small composition-selected no-provider port fixes this: both of its functions return `CRUX_CONFIG_ERR_NOT_SUPPORTED`, which `port_error` passes on. `crux_config_load` then returns that error and the parameter keeps its default from init, and the application treats it as running on defaults. The `sd_settings_recover` step exists but is called by no image, and whether a missing settings file next to a leftover is acceptable is
an open owner question for #16.

G1 does not need a target provider, because target implementations can finish alongside the tracks. M11 and M12 acceptance of `config save` does. Decide it under
#16. The working default is the corrected Settings File path, with the 2 November decision's limit that a setting may be lost after a power cut. Results on a target with
no provider say that a save is not available.

## Testing

| Level | Cases |
| --- | --- |
| Configuration conformance, Host and `native_sim` | String init, read, update and load. Each C2 rejection, including bad UTF-8, an embedded NUL, a `length` that does not match the terminator, C0, DEL and C1 controls. A default failing C2, aliasing storage, or rejected at fallback. Max length and one over, across a reload, so that a 128-byte value survives. A record under 4 bytes, one longer than capacity, a short transfer, a malformed record, and a type mismatch in each direction. Wrong init function. `crux_config_key`. Scalar records byte-identical to the current encoding. Existing suites unchanged. No-provider port on load and save. |
| C++ consumers | `tests/configuration_consumer` still compiles unchanged, plus a string case |
| Service permissions | Every row of the permission table for read, update, listing and save, with success, unauthenticated, unpermitted and invalid input on each operation |
| Validation | Each type with parser limits, including overflow, sign and leading zeros. Rejection keeps the old value. Text with a known secret never appears in an event. |
| Sensitive | No path returns the value. Known values absent from events, logs and `applied`. |
| Concurrency | `BUSY` during a held save, `try_read` and `try_update` returning without waiting on the service lock, under sanitizers with threads. A save failure in the middle of a list. `try_update` producing no event. Table bound. |
| Status | Each built-in, per-entry failure, hidden items, no event from hiding, a project item with each type and with bad text. On `native_sim` with the real System component, because Host reports reset causes unsupported: after any number of status polls, `crux_system_reset_cause_clear` still returns `INVALID_STATE` unless the owner made its own get. Snapshot age bound with a controlled clock, and concurrent polls counting source reads. |
| Boundary | Headers include no native types and no adapter header. The service header includes only public Crux headers. |
| Target, separately approved | Stack use with strings, handler time on the HTTP thread, watchdog feed and clock jitter under a status poll, and a save once a provider exists |

Host tests show contract behaviour. They do not show that applying a change is safe, and results say that this stays application policy.

## Limits

- Accepted is not applied. Applying a change safely is application policy.
- Sensitive is write-only handling, not protection at rest.
- Saves are per parameter, with no atomicity and no rollback.
- The service covers scalars and bounded strings, with no arrays or blobs.
- There is no save provider on the target yet.
- Validators give no reason for a rejection.
- Timing of status sources is unmeasured, and a status read can delay a control thread's watchdog feed or clock read by an unmeasured amount, at the rate set by C7.
- The application cannot reload a table parameter after start.

## Changes other contracts need

- **AC1, access control:** `crux_access_permitted(crux_access_session_t session, const char *permission, bool *permitted)` in the public header `crux/access.h`, with no event, no touch and no storage. Services call it, so it cannot be adapter-only under decision A1. It creates no session, so it does not widen what a project can do. Without it, a poll of status would fill the event ring. This is a request, and the access contract is not changed here.
- **CLI, Issue #181:** `config set` on a sensitive parameter takes its value through `crux_cli_read_secret`, so it is not echoed and not kept in the line buffer.
- **Web design:** the route for `PUT /api/v1/config/{name}` runs on the server thread only if validators are quick, and otherwise on the worker. `503` on `BUSY`, and `404` for a hidden parameter, refine its error table. "Updates respond with the applied value" becomes a `read` after the update, and never for a sensitive parameter. `GET /api/v1/status` becomes session-only with the service filtering, because a project item with its own permission is unreachable behind `status.read`.
- **Configuration contract document:** revised on adoption, as stated above. No change happens in this proposal.
