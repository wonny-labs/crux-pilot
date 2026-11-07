# Portable Configuration contract

Status: approved for software-only development by the human M4 Architecture and API Review on 23 October 2026. Production backend/layout adoption, upstream/native Flash changes, physical operations, PR merge and Issue closure remain separately gated.

Baseline: main `225cd21b5fff37c31c5dff52570e9f26179682a1`.

[String parameters](#string-parameters) and `crux_config_key` were added under decisions C1 and C2 of Issue #180, on Host and `native_sim`. The [services above this mechanism](status-configuration-services.md) use them. No target provider exists: [a composition selects the no-provider port](#no-provider-port) until #16 decides one.

## Recommendation and alternatives

Recommend a small **per-parameter scalar mechanism**, with application-owned
parameter descriptors and objects. It provides defaults, validation, read/update
and explicit save/load; it does not manage a global schema registry, dependency
graph, transactions or application activation.

Alternative: a descriptor-table instance managing all parameters together. That
can stage and cross-validate a complete snapshot, but adds registry/workspace and
batch-result semantics before a current consumer requires them. Applications can
compose the proposed independent scalar operations and own their cross-parameter
validation without promising persistent atomicity.

Alternative: application-only Settings handlers. This reuses native facilities
but fails #16's independent portable reusable mechanism/consumer boundary.

The mechanism remains independent of File and selected Settings storage backend.

## Public data and operations

Owner `components/configuration/include/crux/configuration.h`; public usage target
`crux::configuration`; shared C17 code with C++17 `extern "C"` consumption.
Native-provider ports and injected test controls remain private.

```c
#ifndef CRUX_CONFIG_STRING_MAX
#define CRUX_CONFIG_STRING_MAX 128
#endif
typedef enum { CRUX_CONFIG_BOOL = 1, CRUX_CONFIG_U32 = 2, CRUX_CONFIG_STRING = 3 } crux_config_type_t;
typedef struct {
    crux_config_type_t type;
    union {
        bool boolean;
        uint32_t u32;
        struct { const char *text; size_t length; } string;
    } data;
} crux_config_value_t;
typedef bool (*crux_config_validator_t)(const crux_config_value_t *candidate,
                                        void *context);
typedef struct {
    const char *key;
    uint16_t schema_version;
    crux_config_value_t default_value;
    crux_config_validator_t validate;
    void *validation_context;
} crux_config_spec_t;
typedef struct {
    /* Caller storage; fields reserved to the implementation after init. */
    const crux_config_spec_t *spec;
    crux_config_value_t value;
    char *storage;
    size_t capacity;
} crux_config_param_t;
typedef enum {
    CRUX_CONFIG_OK = 0,
    CRUX_CONFIG_DEFAULT_MISSING = 1,
    CRUX_CONFIG_DEFAULT_REJECTED = 2,
    CRUX_CONFIG_ERR_INVALID_ARGUMENT = -1,
    CRUX_CONFIG_ERR_INVALID_STATE = -2,
    CRUX_CONFIG_ERR_REJECTED = -3,
    CRUX_CONFIG_ERR_NOT_READY = -4,
    CRUX_CONFIG_ERR_IO = -5,
    CRUX_CONFIG_ERR_NO_SPACE = -6,
    CRUX_CONFIG_ERR_NOT_SUPPORTED = -7
} crux_config_result_t;
crux_config_result_t crux_config_init(crux_config_param_t *parameter,
                                     const crux_config_spec_t *spec);
crux_config_result_t crux_config_init_string(crux_config_param_t *parameter,
                                             const crux_config_spec_t *spec, char *storage,
                                             size_t capacity);
const char *crux_config_key(const crux_config_param_t *parameter);
bool crux_config_text_valid(const char *text, size_t length);
crux_config_result_t crux_config_read(const crux_config_param_t *parameter,
                                     crux_config_value_t *out);
crux_config_result_t crux_config_update(crux_config_param_t *parameter,
                                       const crux_config_value_t *candidate);
crux_config_result_t crux_config_load(crux_config_param_t *parameter);
crux_config_result_t crux_config_save(const crux_config_param_t *parameter);
```

- Caller zero-initialises the object. Initialising a live object is an error and
  preserves it. Failed init leaves an uninitialised object unchanged. No heap,
  destructor, resource handle or runtime registry is introduced.
- The immutable descriptor, key and validation context outlive the object. The
  caller does not mutate reserved fields/copy live objects or register conflicting
  live objects for the same composed key. Null validator permits all correctly
  typed values; otherwise init validates the default and update/load validate
  candidates before publication. Validators are synchronous/non-reentrant and
  must not perform persistence or mutate this object.
  Caller serialises any context changes. Load revalidates a fallback default
  immediately before publication; if it is no longer valid, preserve the prior
  value and return ERR_REJECTED. Save revalidates the current value before backend
  dispatch and rejects it without writing if validation now fails. Read returns
  the last accepted value without claiming cross-parameter consistency.
- BOOL, U32 and bounded STRING are supported. Application supplies range and meaning;
  no implicit type conversion, floating-point, arbitrary blobs or schema
  migration. Additional types require an evidenced contract extension.
- Keys are single ASCII leaf names `[A-Za-z0-9_.-]`, excluding `.` and `..`.
  Native provider composition supplies the namespace prefix and documented name
  bound; the initial software qualification profile supports up to 31 characters.
  This bound is an approved initial test profile, not a measured product requirement.
- Application owns cross-parameter validation and coherent runtime snapshots.
  A successful update changes accepted configuration only; it does not
  apply changes to running controller services. Caller serialises the object,
  its validator context and conflicting key accesses. Task context only; no
  ISR use, bounded latency, cancellation or internal worker thread is promised.

## State and error meaning

| Operation/outcome | Accepted in-memory value | Persisted state / acknowledgment |
| --- | --- | --- |
| init OK | Validated application default | No backend access; persistence unknown. |
| update OK | Validated candidate | No write dispatched; no new persistence acknowledgment. |
| rejected/type-invalid update | Previous value preserved | No save; previous acknowledgment is not newly changed by this operation. |
| load OK | Exactly read, decoded and validated stored value | An observed valid stored value; not physical persistence qualification. |
| load DEFAULT_MISSING | Validated default | Provider explicitly reported missing; no implicit save. |
| load DEFAULT_REJECTED | Revalidated default | Complete invalid record or reliable invalid-length metadata was observed; no implicit overwrite. Valid-length failed/short transfer remains IO. |
| fallback default or save-time validation rejected | Previous value preserved | ERR_REJECTED; no write dispatched and no invalid default published. |
| load IO/NOT_READY/other negative result | Previous value preserved (defaults after init) | Outcome unresolved; never recategorise transport/short-read errors as missing. |
| save OK | Unchanged | Provider acknowledged this parameter's encoded bytes; no whole-schema atomicity. |
| save failure | Unchanged | Failure is visible; bytes may already have changed. Do not report rollback or unchanged storage. |

The application retains per-operation results if it needs an active-versus-last-
acknowledged display. The module does not maintain a misleading authoritative
shadow of physical storage. An earlier successful save does not remove a later
uncertain outcome, and a load failure never invokes automatic save/default repair.

Load validates a temporary candidate before changing the object. A record that
advertises an invalid encoding length may be rejected without reading beyond the
small buffer; a valid advertised length but failed/short transfer is IO, not a
complete malformed record. Cross-parameter policy can reject a mixed-key load
before the application publishes a runtime snapshot; no batch API is implied.

## Encoding and provider boundary

Canonical wire format: byte 0 format version 1, byte 1 type, bytes 2–3 application
schema version in little-endian, followed by BOOL byte 0/1 (total 5 bytes), U32
little-endian (total 8 bytes) or type 3 STRING text with no terminator (total 4 +
length bytes). Reject any other length for the parameter's type, including text that
would not fit its capacity, noncanonical BOOL values, type/version mismatches and
invalid values. No C-struct padding/endian encoding, application CRC/authentication
promise or raw-NVS consumer interface.

The shared mechanism calls a composition-selected private read-record/write-record
port. Reads must distinguish explicit missing, complete record and error, carrying
both actual and advertised lengths where needed to reject truncation safely.
Writes acknowledge only explicit provider success. These are internal contracts,
not another public storage service or provider plugin framework.

The Zephyr provider remains based on Settings, but no entry function is selected
as safe merely from its signature. The retained entry assessment shows why generic
subtree/direct returns and current NVS fallback can hide errors. Native provider
acceptance requires actual Settings/backend fault evidence; any upstream change
or altered backend remains a separate ownership/architecture decision.

## String parameters

The declarations are under [Public data and operations](#public-data-and-operations). `bool` stays the first member of the value's union. `crux_config_spec_t` does not change, because new fields would break consumers that initialise it by position under `-Wextra -Werror`. `crux_config_param_t` gains private fields. A C++17 consumer sets a string default at run time, because only the first union member takes a brace initialiser and designators are C++20. `tests/configuration_consumer/consumer_string.cpp` shows how.

| Rule | Behaviour |
| --- | --- |
| Which init | `crux_config_init` refuses a string spec, and `crux_config_init_string` refuses a scalar one, with `INVALID_ARGUMENT` |
| Capacity | Above the default's length and at most `CRUX_CONFIG_STRING_MAX + 1`. The default must not overlap the storage. Otherwise `INVALID_ARGUMENT`. |
| A value | `text` is non-null, `text[length]` is the terminator and no earlier byte is one, and `crux_config_text_valid` holds. This applies to a default at init and at fallback, and to a candidate, so a validator never sees other bytes than those stored. Otherwise `REJECTED`. |
| Character rule | Well-formed UTF-8 with no NUL, C0 control, DEL or C1 control. U+2028, bidirectional controls and similar characters pass, so an adapter still escapes for its format. |
| Read | `data.string.text` borrows the parameter's storage until the next update or load, unlike a scalar's copy |
| Update | The rule and the capacity, then the validator, then a copy. A candidate may alias the storage. A rejected update keeps the old value. |

The [encoding](#encoding-and-provider-boundary) is the same header, with type 3, followed by the text with no terminator and no separate length, because a record's length comes from the port. An empty string is 4 bytes. BOOL and U32 records stay byte-identical.

Load keeps the existing rules. A record whose length is under 4, or whose text would not fit `capacity`, is `DEFAULT_REJECTED` from its metadata, with nothing read past the buffer. A failed or short transfer of a valid length stays `IO`. A record that fails the rule is rejected the same way.

`crux_config_load` and `crux_config_save` use `CRUX_CONFIG_STRING_MAX + 5` bytes of stack for every type, and both clear that buffer on every path after it holds a value, by volatile writes that a compiler cannot remove. The simulator Settings provider's own buffer is not cleared, which fits the absence of a confidentiality claim. It holds a 252-byte string at most, and a build check keeps `CRUX_CONFIG_STRING_MAX` at or below that when it is selected.

`crux_config_text_valid` is a library of its own, `crux_configuration_text`, which `crux::configuration` links publicly. A service that shows text can link it alone: a link that takes a whole archive, as Zephyr's does for each library registered with it, would otherwise pull in the mechanism and need a provider.

## No-provider port

`crux_config_save` and `crux_config_load` call the private port directly, and no target provider exists, so an image using Configuration on a target would fail to link. A composition links `crux_configuration_no_provider` to fix this. Both port functions return `CRUX_CONFIG_ERR_NOT_SUPPORTED`, which `crux_config_load` and `crux_config_save` pass on, and each parameter keeps its default from init. The application treats that as running on defaults. It is objects, so a final link always takes it, and a second provider is a duplicate symbol. Issue #16 decides the real provider.

## Application-owned controller example

A separate optional Host reference-controller scenario uses the actual portable
controller core with two application descriptors: `diagnostics_enabled` BOOL
(default false) and `report_every_cycles` U32 (default 5; validator 1–100).
These are qualification choices for reporting policy, not new production defaults.
The application captures accepted configuration at its chosen cycle boundary;
updating/loading parameters alone does not change that captured runtime snapshot.
The example does not alter accepted M3 schedules, watchdog policy or evidence.


## Result convention

The capability-specific result enum is a separate domain from `crux_status_t`.
As with the base convention, zero denotes full success and negative values are
errors; positive File partial/Configuration default outcomes require explicit
handling. Compare named values from the declaring API, never cast or compare
against base enum values, and do not treat every nonzero value as an error.
Any necessary composition-level translation uses an explicit switch; no generic
conversion service or change to base status values is introduced.
