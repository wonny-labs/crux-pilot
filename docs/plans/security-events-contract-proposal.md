# Security events contract proposal

Status: proposal for review, Issue #182. It finalises the event identifiers that the
[access control proposal](access-contract-proposal.md) leaves to this issue, over the existing
[Logging contract](../logging-contract.md). Nothing here authorises implementation, dependency adoption or hardware operations.
Baseline: main `4444d70`, Zephyr v4.4.2 `dccb095`.

## Decisions requested

| ID | Decision | Recommendation | Impact if rejected |
| --- | --- | --- | --- |
| E1 | Emit into a bounded RAM ring, and a low-priority thread drains it to Logging | Accept | A direct log call from each emit is simpler, but Host Logging can block and nothing can count what is lost |
| E2 | Every record carries a boot-local sequence number, so that any loss downstream shows as a gap | Accept | Loss after Logging accepts a record is otherwise invisible, because Logging offers no per-record count |
| E3 | When a ring is nearly full, only change-class events are admitted | Accept | A login flood could push out a record of an account change |
| E4 | Wall-clock time is computed at drain, so an emit never takes the Time authority lock | Accept | `crux_access_check` on the HTTP thread would wait on that lock |
| E5 | RAM retention only in M10, read through `GET /api/v1/events` under `events.read`. Lost at reset. | Accept | Persistence needs a storage decision. It can be added as another drain consumer with no change to emit. |
| E6 | A composition keeps the Logging threshold at `CRUX_LOG_INFO` or more verbose | Accept, as a composition rule | A lower threshold silently filters events, because Logging reports a filtered call as `OK` |

## The rule

**Emitting an event never waits on storage.** An emit copies a bounded record into RAM under a short leaf lock and returns. It does no file, Settings or
credential store access, no verifier work and no log output, and it calls no port that can do any of these. This is the property the access
proposal's event port requires, because `crux_access_check` emits denials and runs on the HTTP server thread.

Lock order: an emit may be called with the access state lock held or not, so the ring lock is a leaf. Nothing is called while it is held, so it cannot take part in a
cycle. An emit never calls back into `crux_access`.

`crux_security_event_emit` returns nothing and cannot fail a caller's operation. A security decision never depends on whether its record was kept.

## Trust boundary

Events give an operator a record of management activity, with no secrets. They are not tamper-proof and not guaranteed to be complete. Code in the image can emit false
records or none, and an attacker with the log transport can read or remove them.

| Threat | Response | Limit |
| --- | --- | --- |
| A secret in a record | No free-text field. Names pass a character rule, and unknown user names are omitted. | A caller that passes a secret as a validated subject. Tests use known values against the real emitters. |
| Log injection | Fields hold only a fixed character set, and `crux_log_write` rejects CR and LF | |
| Flood hiding later events | Admission reserve, then counted loss | Routine events are still lost in a flood, and the count says so |
| Silent loss | Sequence numbers and counters | A gap shows that events were lost, not which ones |
| Misleading time | Each record states the clock's source, or `none` | Time is the device's, with no accuracy claim |
| Reading events | `events.read`, checked by the service | The log transport has no access control of its own |

## Component layout

| Item | Location | Users |
| --- | --- | --- |
| Public header | `components/security_event/include/crux/security_event.h`, target `crux::security_event` | Services, adapters, projects |
| Private test controls | `components/security_event/src/`, exposed only to test targets | Host tests |

It depends publicly on Base and on `crux::access`, for the session type, and privately on Logging, Sync, Thread and Time. The event port of `crux_access`, `crux_access_port_event`, translates to `crux_security_event_emit` in a thin provider selected by the composition. `crux_access` itself does not
link to this component, so there is no dependency cycle and its Host tests keep their recording fake.

## Public contract

```c
typedef uint16_t crux_security_event_id_t; /* Values below. A value is never reused. Projects use 0x8000 and above. */

typedef enum {
    CRUX_SECURITY_EVENT_OK = 0,
    CRUX_SECURITY_EVENT_DENIED = 1,  /* Credential or permission refused. */
    CRUX_SECURITY_EVENT_DELAYED = 2, /* Inside a delay window. */
    CRUX_SECURITY_EVENT_BUSY = 3,
    CRUX_SECURITY_EVENT_LIMIT = 4,   /* A table or declared bound. */
    CRUX_SECURITY_EVENT_INVALID = 5, /* Input that the rules refused. */
    CRUX_SECURITY_EVENT_FAILED = 6   /* Storage, resource or internal failure. */
} crux_security_event_result_t;

typedef enum {
    CRUX_SECURITY_EVENT_ROUTINE = 0,
    CRUX_SECURITY_EVENT_CHANGE = 1   /* Admitted by the reserve under decision E3. */
} crux_security_event_class_t;

typedef struct {
    crux_security_event_id_t id;
    crux_security_event_result_t result;
    uint8_t channel;        /* Value of crux_access_channel_t, or 0 for none. */
    uint8_t source_length;  /* 0, 4 or 8. */
    uint8_t source[16];     /* As given by the adapter. For IPv6 this is the /64 prefix. */
    const char *user;       /* NULL or a known account name. Copied. */
    const char *subject;    /* NULL, or a permission, parameter, area or command. Copied. */
} crux_security_event_t;

typedef struct {
    crux_security_event_id_t id;
    crux_security_event_result_t result;
    uint8_t channel;
    uint8_t source_length;
    uint8_t source[16];
    char user[32];      /* Empty if none. */
    char subject[49];   /* Empty if none. */
    uint32_t sequence;  /* Starts at 1 each boot, and wraps at 2^32. */
    uint64_t uptime_ms; /* Monotonic. Zero with no_uptime set if the clock read failed. */
    bool no_uptime;
    bool subject_truncated;
} crux_security_event_record_t;

typedef struct {
    uint32_t emitted;       /* Admitted to the ring. */
    uint32_t dropped;       /* Refused at the ring. */
    uint32_t delivered;     /* crux_log_write returned CRUX_OK. Not delivery on the wire. */
    uint32_t log_errors;    /* crux_log_write returned an error. */
    uint32_t invalid_fields;
    uint32_t high_water;
} crux_security_event_stats_t;

void crux_security_event_emit(const crux_security_event_t *event);
crux_status_t crux_security_event_start(void);
crux_status_t crux_security_event_get_stats(crux_security_event_stats_t *out);
crux_status_t crux_security_event_read(crux_access_session_t session, uint32_t *cursor,
                                       crux_security_event_record_t *out);
```

- `crux_security_event_emit` is thread context only and can run before `crux_security_event_start`, which starts the drain. Records wait in the ring until then. The strings
  are copied and not retained. An ISR call is refused and counted, because the Time and Sync contracts are thread context only.
- `crux_security_event_read` needs `events.read`. It returns records in order from the ring without removing them. `*cursor` holds the next sequence to read. If that record is already gone,
  the next retained one is returned with a higher sequence, so that the caller sees the gap. `CRUX_ERR_NO_DATA` marks the end. A denied session gets `CRUX_ERR_INVALID_STATE`, and the check itself produces the denial event.
- `crux_security_event_get_stats` takes no session, because it returns counts only. Management clients reach it through the status service under `status.read`. It is RAM only.
- A public emit lets a project record its own rejections, under its own identifiers. A project's identifier needs a name registered in a composition table, so a line can be read.

## Behaviour

### Fields and character rules

| Field | Rule | If it fails |
| --- | --- | --- |
| `user` | `[a-z0-9_.-]`, 1 to 31 characters. Only a known account's name. | Omitted, `invalid_fields` increases |
| `subject` | `[A-Za-z0-9_./:-]`, up to 48 bytes in the record | Over-long text keeps 47 bytes and ends in `~`, which is outside the character rule, so a truncated field cannot pass for a whole one. Bad characters omit the field, and `invalid_fields` increases. |
| `source` | Only used in the printed form | Omitted |
| `id` | Must be known | Recorded as `event.unknown` |

A login failure for an unknown name omits the name, as the access proposal requires, because people often type a password into the name field. Client input that has failed validation is
never a field: a rejected path, command line or value leaves only a fixed event. A value never appears for any parameter, sensitive or not.

The access proposal's comment on `crux_access_origin_t.source` says "used only for delay tracking", while its events section says that events carry the source. This contract follows
the events section, and the comment should be corrected. IPv6 sources are only the /64 prefix that the adapter passes, and the current networking profile is IPv4 only.

### Log line

The drain writes one line through `crux_log_write` with tag `crux.security`. Fields are in this fixed order, and an empty field is omitted:

```text
evt=access.login_failed seq=184 up=93211 wall=1793750400 clock=rtc chan=ssh src=192.0.2.7 res=denied
evt=access.permission_denied seq=185 up=94002 wall=1793750401 clock=rtc chan=web src=192.0.2.7 user=alice subj=config.save res=denied
evt=event.lost seq=186 up=94100 clock=none first=120 count=64 res=failed
```

The longest possible line, including field names, is 245 bytes: `evt` 24, `seq` 10, `up` 13 digits, `wall` 10, `clock` 3, `chan` 6, `src` 39, `user` 31, `subj` 48, and `res` 7 characters, with their keys and spaces. This fits `CRUX_LOG_TEXT_MAX` of 256, and a test asserts it. `up` at 13 digits covers more than 300 years. Identifiers are therefore limited to 24 characters.

Level: `CRUX_LOG_INFO` for results of `OK`, and `CRUX_LOG_WARN` otherwise, including `event.lost`.

### Time

An emit samples `crux_monotonic_now` only, which needs no authority lock and no initialisation. The drain reads `crux_realtime_get_snapshot`. If `is_valid` is true it prints `wall` as its own time less the
record's age, and `clock` from `last_source` (`rtc`, `man`, `ext` or `unk`). Otherwise, or on an error, it prints `clock=none` and no `wall`. A clock set between emit and drain gives a small error, and a record
that waited long gives a computed time. It is never presented as more than the clock's own claim, and `is_synchronized` is not printed, because the Time contract says it is not accuracy. The clock state printed is that at drain.

### Ring and admission

`CRUX_SECURITY_EVENT_RING` is a build setting, default 32 records of about 120 bytes each, which is about 3.8 KB. Each record is pending until the drain handles it, then retained for reading until overwritten. An emit:

1. Samples the monotonic clock outside the lock. A failure sets `no_uptime`.
2. Validates fields and copies them to a local record.
3. Takes the ring lock with `crux_mutex_lock(ring_lock, CRUX_SECURITY_EVENT_LOCK_MS)`, default 10 ms. A timeout counts as dropped, because the contract has no infinite wait. Contention is only other emits and the drain's short copy.
4. Admits or drops, as below, gives it a sequence number and releases the lock.

A record may overwrite a retained record. It may not overwrite a pending one. With `pending` the number of pending records:

| Pending | Routine | Change |
| --- | --- | --- |
| Below ring size minus `CRUX_SECURITY_EVENT_RESERVE` (default 8) | Admitted | Admitted |
| In the reserve | Dropped | Admitted |
| Ring size | Dropped | Dropped |

An event dropped at the ring still takes a sequence number, so a gap exists for it. An event whose lock wait expired cannot take one, and it appears only in `dropped`. The class comes from a fixed table per identifier, and the caller cannot choose it. Every event that changes accounts, keys, configuration or files, along with every successful login, is change-class.

### Drain

`crux_security_event_start` creates one `CRUX_THREAD_PRIORITY_LOW` thread with `crux::thread`. Its loop waits on a semaphore that emit signals, with a timeout to keep a clock check. It takes each pending record under the lock, releases it, formats the line and calls `crux_log_write`, which is deferred and does not wait for capacity on Zephyr.

On `CRUX_OK`, `delivered` increases. On an error, `log_errors` increases, the record stays readable through `crux_security_event_read`, and the drain moves on, with no retry, so that a broken log path cannot stall the ring. While losses are unreported, and when a pending slot is free, the drain writes one `event.lost` with the first lost sequence and the count. It is a line only, not a ring record, and it takes the next sequence number in order to keep the numbering whole.

A test build runs the drain step directly with a private control, with no thread.

### Loss accounting

Loss can occur at several points, and only some can be counted.

| Point | Counted by | Visible as |
| --- | --- | --- |
| Ring full | `dropped` | `event.lost` line, gap, status item |
| Lock wait expired | `dropped` | `event.lost` line, status item. No gap. |
| `crux_log_write` returned an error | `log_errors` | Status item. Record still readable. |
| Zephyr log queue dropped a record after `OK` | Not by Crux. The Logging contract has no per-record count, and the native aggregate report stays enabled. | Gap in `seq`, and the native "messages dropped" line |
| Filtered by the Logging threshold | Not by Crux. Logging returns `OK` and offers no getter. | All records missing from the log, while still readable, and `delivered` overstates. Decision E6. |
| Reset before drain, or power loss | Not counted | Sequence restarts. Records in RAM are gone. |
| UART or later transport loss | Not by Crux | Gap in `seq` |

So `delivered` means handed to Logging, and results must not describe it as delivered to anyone. Counters wrap at 2^32 and are boot-local.

### Identifiers

Stable values, in the access proposal's list and beyond. A later change adds values and never reuses one. `Class` is `R` for routine and `C` for change.

| Value | Name | Class | Emitter | Fields |
| --- | --- | --- | --- | --- |
| 0x0100 | `access.login_ok` | C | access | channel, source, user |
| 0x0101 | `access.login_failed` | R | access | channel, source, user only if the name exists. The result is the same for a client. |
| 0x0102 | `access.key_failed` | R | access | channel, source, user if known |
| 0x0103 | `access.delayed` | R | access | channel, source |
| 0x0104 | `access.login_busy` | R | access | channel, source |
| 0x0105 | `access.login_no_session` | R | access | channel, source |
| 0x0106 | `access.login_refused` | R | access | channel, source, user. Forced change on key login, a removed role, or `crux_access_login_verified` refused. |
| 0x0107 | `access.not_ready` | R | access | channel. A store `UNUSABLE` or policy invalid. |
| 0x0108 | `access.permission_denied` | R | access | session's channel, source and user, subject is the permission |
| 0x0109 | `access.session_invalid` | R | access | channel if known |
| 0x010A | `access.logout` | R | access | channel, source, user |
| 0x010B | `access.session_expired` | R | access | channel, source, user |
| 0x010C | `access.provisioned` | C | access | channel, user |
| 0x010D | `access.provision_refused` | R | access | channel |
| 0x0110 | `access.account_created` | C | access | actor as user, subject the target |
| 0x0111 | `access.account_removed` | C | access | same |
| 0x0112 | `access.account_disabled` | C | access | same |
| 0x0113 | `access.account_enabled` | C | access | same |
| 0x0114 | `access.role_changed` | C | access | same, with no role names in fields |
| 0x0115 | `access.password_reset` | C | access | same |
| 0x0116 | `access.password_changed` | C | access | user |
| 0x0117 | `access.key_added` | C | access | same as account changes |
| 0x0118 | `access.key_removed` | C | access | same |
| 0x0119 | `access.change_failed` | C | access | user, result `FAILED`. Save failed, so state is unchanged. |
| 0x0200 | `credential.state` | C | store | Result `OK`, `FAILED` for `UNUSABLE`, subject the state |
| 0x0201 | `credential.slot_invalid` | C | store | Subject `a` or `b` |
| 0x0202 | `credential.save_failed` | C | store | |
| 0x0203 | `credential.key_generated` | C | store | Subject the record |
| 0x0204 | `credential.key_replaced` | C | store | Subject the record |
| 0x0205 | `credential.erased` | C | store | |
| 0x0300 | `file.created` | C | file service | user, channel, subject `area/name` |
| 0x0301 | `file.removed` | C | file service | same |
| 0x0302 | `file.renamed` | C | file service | same, new name |
| 0x0303 | `file.partial_removed` | C | file service | Subject `area/name` |
| 0x0304 | `file.recovery_needed` | C | file service | Subject `area/name` |
| 0x0305 | `file.name_rejected` | R | file service | user, channel, subject the area only |
| 0x0400 | `config.updated` | C | config service | user, channel, subject the name, no value |
| 0x0401 | `config.rejected` | R | config service | same |
| 0x0402 | `config.saved` | C | config service | same |
| 0x0403 | `config.save_failed` | C | config service | same |
| 0x0500 | `cli.line_rejected` | R | CLI | user if a session exists, channel. Never the line. |
| 0x0501 | `cli.console_error` | R | CLI | channel |
| 0x0600 | `web.tls_failed` | R | M11 | count in subject, one per period, as the web design says "rate limited" |
| 0x0601 | `web.csrf_rejected` | R | M11 | user, channel, source |
| 0x0602 | `web.certificate_replaced` | C | M11 | user, channel |
| 0x0700 | `ssh.auth_limit` | R | M12 | channel, source |
| 0x0701 | `ssh.host_key_missing` | C | M12 | |
| 0x0F00 | `event.unknown` | R | this component | |
| 0x0F01 | `event.lost` | R | this component | Line only |

M11 and M12 add values in their own ranges through one reviewed change to this table, as G1 requires. Names are limited to 24 characters, which the build checks. A role name is left out of `access.role_changed`, because policy data is the product's and not needed in a line.

## Failure handling

| Failure | Result |
| --- | --- |
| Ring full | Dropped and counted, with a sequence gap. The caller is unaffected. |
| Lock wait expired | Dropped and counted, with no sequence number. The caller is unaffected. |
| Monotonic read fails | Admitted with `no_uptime`, and no line prints `up` |
| Thread cannot start | `crux_security_event_start` returns its error, and the ring still fills and can be read. The composition treats this as a management start failure. |
| Logging returns an error | Counted, no retry, record still readable |
| Realtime snapshot fails | `clock=none`, and the line is still written |
| Field fails its rule | Field omitted and counted, and the event still recorded |

## Testing

| Level | Cases |
| --- | --- |
| Host conformance | Every identifier's name, value, class and length. Field rules, truncation mark and omission. Line format, and the worst-case line at or under 256 bytes. Ordering, sequence and read cursors, including a gap and an overwritten record. |
| Rejection paths | With `crux_access` and the recording provider, each rejection path produces its event: bad password, unknown name, disabled account, bad key, failed signature, delay window, busy, table full, forced change, removed role, stale session, expired session, unpermitted check, refused provisioning, unusable store. This is the acceptance item, and it is shared with #177. |
| Secret absence | Known passwords, verifiers, salts, keys and sensitive parameter values never appear in any event, line or read result, using the real emitters. An unknown user name never appears. A rejected line or path never appears. |
| Never waits | An emit from a test where the storage fake and the log fake both block returns, because the drain is separate. The drain blocked with a full ring still lets emit return at once. |
| Loss | Overflow with the reserve, change admitted when routine is dropped, counters, `event.lost` content, and a gap for each dropped record. Lock timeout with a held lock. |
| Time | Clock unset, set, each source, error on the snapshot, and set between emit and drain, with a controlled clock. No authority lock taken on emit, shown by holding it. |
| Concurrency | Many emitters and the drain under sanitizers |
| `native_sim` | Lines appear on the Logging UART emulator and pass the `CRUX_LOG_TEXT_MAX` rule. Native drop under load shows a gap. |
| Target, separately approved | Emit time on a real thread and loss under real management load |

## Limits

- No persistence. A reset loses everything in the ring. Nothing is tamper-evident.
- Loss after `crux_log_write` returns `OK` is only visible as a gap, or through the native report.
- The clock on a line is the clock at drain time, computed backwards.
- A long subject is truncated, with a mark.
- A flood loses routine events, counted. It can also delay the drain and lengthen retained coverage only as far as the ring allows.
- Worst-case emit time is unmeasured. The lock wait is bounded, and the rest is a memory copy.

## Changes other contracts need

- **Access proposal:** the event list grows by `access.login_busy`, `access.login_no_session`, `access.login_refused`, `access.not_ready`, `access.session_invalid` and `access.provision_refused`, because Issue #182 requires an event from each rejection path. The proposal says that identifiers are finalised here, so this fits it, but `crux_access_port_event` in the running implementation under #177 must report them. Its port should carry the origin's source for session events too, so the session must keep its origin.
- **Access proposal comment:** the `source` comment noted above.
- **Web design:** "Security events: `GET /api/v1/events` when a bounded store exists" is met by the RAM ring, and it reads records that a reset discards.
- **Logging contract:** none. An optional later addition is a threshold getter, so that a result could report that a filter is hiding events.
