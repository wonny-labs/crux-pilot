# Command registry and serial login proposal

Status: proposal for review, Issue #181. It refines the registry in [the secure management design](secure-management-design.md) and
the command line in [the CLI and SFTP design](secure-management-cli-sftp-design.md), and it is the smallest consumer of the
[access control proposal](access-contract-proposal.md). Nothing here authorises implementation, dependency adoption or hardware
operations. Baseline: main `4444d70`, Zephyr v4.4.2 `dccb095`.

## Decisions requested

| ID | Decision | Recommendation | Impact if rejected |
| --- | --- | --- | --- |
| L1 | The serial console runs on `crux_serial`, which means LPUART3 on the EVKB and not the debug USB port on LPUART1 | Accept. The wiring needs a physical check and its own approval. | LPUART1 belongs to Logging, and `serial_binding.c` rejects sharing it at build time, so another choice needs a change to the Logging or Serial profile and new evidence |
| L2 | The registry has no public access class. Login and first-account setup sit outside it. | Accept | A public command is a new route to unauthenticated code |
| L3 | A hidden command and an unknown command give the same output and exit status | Accept | Commands could be enumerated |
| L4 | First-account setup is entered with `!setup` at the user prompt, which is outside the name alphabet | Accept | The prompt could say that no account exists, but that needs a new access query (AC2) |
| L5 | Development is an opt-in mark. Without `CONFIG_CRUX_DEVELOPMENT_IMAGE`, an image with `crux_cli`, web or SSH has `CONFIG_SHELL=n`, and no image of any kind has a network shell backend or `MCUMGR_GRP_SHELL`. With the mark and management, the Zephyr shell is allowed on a serial backend only. | Accept | Limiting the rule to management images would leave a project's own network shell in place, which D9 and #181 do not allow |
| L6 | Line editing is limited to backspace and line kill, with no history at first | Accept | History needs its own rule for secrets, and the design already makes it optional |

## Trust boundary

`crux_cli` stops a remote or console user from running a command their session does not permit, and from sending the parser beyond its bounds. It
does not isolate code in the image, and a command handler is trusted firmware. Services still check their own permissions, so the registry can
reject and cannot grant.

| Threat | Response | Limit |
| --- | --- | --- |
| Unauthenticated command execution | No public class, and a session is required before the registry runs | |
| Bypassing the registry through the Zephyr shell | Shell rule L5, enforced at three levels | A development image keeps it on serial |
| Overlong or malformed input | A bounded line, tokens and arguments. Excess is discarded and rejected as a whole. | |
| Terminal escape output | Config strings and file names cannot hold C0 or C1 controls under their own rules, and handlers write only such values | A project handler's output is its own responsibility, and review covers it |
| Enumerating commands | Rule L3 | Output timing is not equalised |
| Secret in logs through a mistyped command | Rejected lines are never logged. Only a fixed event. | |
| Secret on screen or in memory | Password input is not echoed, stored in the line buffer, or kept after use | The serial wire itself is not encrypted, so anyone with the wire can read it |
| Session left open on the console | Idle limit, and logout | UART has no reliable hang-up signal, so a session survives an unplugged cable until the idle limit |

## Component layout

| Item | Location | Users |
| --- | --- | --- |
| Public header | `components/cli/include/crux/cli.h`, target `crux::cli` | Projects, for commands and handlers |
| Adapter header | `components/cli/adapter/crux_cli_adapter.h`, target `crux_cli_adapter` | Serial console, SSH adapter |
| Serial console | `components/cli/console/`, over `crux::serial` | Composition |
| Private port | `crux_cli_port_event`: recording fake on Host, thin provider onto Issue #182 on Zephyr | CLI |

The design's table form stays, and the public header holds only what a project needs. Sessions come from adapters, as the access proposal requires.

## Public contract

```c
typedef enum {
    CRUX_CLI_ACCESS_INVALID = 0,    /* A zero-initialised entry fails crux_cli_init. */
    CRUX_CLI_ACCESS_SESSION = 1,    /* Any valid session. The handler or service must check further. */
    CRUX_CLI_ACCESS_PERMISSION = 2
} crux_cli_access_class_t;

typedef enum {
    CRUX_CLI_OK = 0,
    CRUX_CLI_ERR_INVALID_ARGUMENT = -1,
    CRUX_CLI_ERR_NOT_READY = -2,
    CRUX_CLI_ERR_USAGE = -3,    /* Argument count or form. */
    CRUX_CLI_ERR_FAILED = -4,   /* The command ran and failed. */
    CRUX_CLI_ERR_DENIED = -5,   /* A service inside the handler refused. */
    CRUX_CLI_ERR_OUTPUT = -6,   /* The transport failed, so the session ends. */
    CRUX_CLI_ERR_TOO_LONG = -7
} crux_cli_result_t;

typedef struct crux_cli_call crux_cli_call_t;
typedef crux_cli_result_t (*crux_cli_handler_t)(crux_cli_call_t *call, size_t argc, const char *const *argv, void *context);

enum {
    CRUX_CLI_CHANNEL_SERIAL = 1u << 0,
    CRUX_CLI_CHANNEL_SSH = 1u << 1,
    CRUX_CLI_FLAG_INTERACTIVE = 1u << 2,       /* Reads a prompt, so it is refused for exec. */
    CRUX_CLI_FLAG_FORCED_CHANGE_OK = 1u << 3   /* Also allowed while a forced password change is pending. */
};

typedef struct {
    const char *name;       /* One or two words of [a-z0-9-], for example "config get". */
    const char *summary;    /* One line for help. */
    crux_cli_access_class_t access_class;
    const char *permission; /* Required for PERMISSION, NULL otherwise. */
    uint8_t min_args;
    uint8_t max_args;
    uint8_t flags;          /* Channel bits are required. */
    crux_cli_handler_t handler;
    void *context;
} crux_cli_command_t;

/* access expands to the class and permission, so a command with no access class does not compile.
 * Positional, because designated initialisers are not C++17. */
#define CRUX_CLI_PERMISSION(permission) CRUX_CLI_ACCESS_PERMISSION, (permission)
#define CRUX_CLI_SESSION_ONLY CRUX_CLI_ACCESS_SESSION, NULL
#define CRUX_CLI_COMMAND(name, summary, access, min_args, max_args, flags, handler, context)                \
    {(name), (summary), access, (min_args), (max_args), (flags), (handler), (context)}

crux_cli_result_t crux_cli_init(const crux_cli_command_t *const *tables, const size_t *counts, size_t table_count);

crux_access_session_t crux_cli_session(const crux_cli_call_t *call);
crux_cli_result_t crux_cli_write(crux_cli_call_t *call, const void *data, size_t length);
crux_cli_result_t crux_cli_read_secret(crux_cli_call_t *call, const char *prompt, char *buffer, size_t capacity, size_t *length);
```

`crux_cli_init` validates all tables once: names, duplicates across tables, a one-word name that is also the first word of a two-word name, the class and permission pair, argument bounds, flags with at least one channel,
and a non-null handler. On failure, or before it, every run reports not ready and no command runs. Tables stay borrowed for the program's life. A permission
that no role holds simply never passes, which fails closed, and a check script lists every session-only command for review, as the web design does for routes.

`CRUX_CLI_ACCESS_SESSION` is only for commands such as `logout`, or a handler that calls a service that checks permission itself. A `SESSION` handler reading or
changing protected data must call a service or `crux_access_check`.

## Adapter contract

```c
typedef struct {
    /* Blocking with a bound. Returns bytes, 0 on timeout, negative when the transport has ended. */
    int (*read)(void *transport, void *buffer, size_t length, uint32_t timeout_ms);
    int (*write)(void *transport, const void *buffer, size_t length, uint32_t timeout_ms); /* Accepted bytes, 0 on timeout, negative on failure. */
    void *transport;
    bool echo;   /* The CLI echoes input, on serial and on an SSH channel with a terminal. */
} crux_cli_io_t;

typedef struct {
    crux_serial_port_t port;
    crux_serial_config_t serial;
    const char *setup_role; /* Role for first-account setup. It must hold access.manage. */
} crux_cli_console_config_t;

typedef struct {
    char line[CRUX_CLI_LINE_MAX + 1];
    /* Other fields are reserved. */
} crux_cli_context_t;

crux_cli_result_t crux_cli_run(crux_cli_context_t *context, const crux_cli_io_t *io, crux_access_channel_t channel,
                               crux_access_session_t session);
crux_cli_result_t crux_cli_exec(crux_cli_context_t *context, const crux_cli_io_t *io, crux_access_channel_t channel,
                                crux_access_session_t session, const char *line, size_t length, int *exit_status);
crux_cli_result_t crux_cli_console_run(crux_cli_context_t *context, const crux_cli_console_config_t *config);
```

`crux_cli_run` serves one authenticated session until logout, expiry or a transport failure. The thread and context belong to the caller, one per session: the console
thread for serial, and the session thread for SSH, as the design already says. Blocking one session never affects another. Management threads run below every control
priority. The contract never allocates.

`crux_cli_console_run` is the serial loop: open port 0, log in, run, and go back to the login prompt.

## Behaviour

### Bounded parsing

| Limit | Default | When exceeded |
| --- | --- | --- |
| `CRUX_CLI_LINE_MAX`, bytes before the line end | 320 | The rest of the line is discarded and the whole line is rejected as `TOO_LONG`, with nothing run |
| `CRUX_CLI_TOKENS_MAX`, all tokens in the line | 10 | `USAGE`, before any lookup and regardless of the command |
| Command name | Two words at most | Longest match, then the remaining tokens are arguments |
| Quoted value | Double quotes only, `\"` and `\\` inside | An unterminated quote or other escape is `USAGE`, before any lookup |

A build setting can raise these. The default fits `config set <31-byte name> "<128-byte value>"` with every value byte escaped, which is 301 bytes, and a build check keeps it at or above that for the configured string limit.

Input bytes: CR, LF or CR LF ends a line, backspace and DEL remove a byte, Ctrl-U clears the line, and Ctrl-C clears the line or stops an interactive prompt. Other C0 controls are discarded. An
ANSI escape sequence is recognised and dropped by a bounded state machine, so an arrow key does nothing. Bytes above 0x7F are allowed only inside quotes, for UTF-8 strings. The service then applies its own
checks. Parsing happens in place in `line`, and argv points into it. Host tests and fuzz input cover this parser.

A terminal request from SSH is accepted, and its size is ignored. Output is written with `crux_cli_write`, which retries a short write until the transport accepts it or fails, so
a slow client blocks only its own session. Each attempt has a bound, and between attempts the CLI checks the session with `crux_access_session_user`, so the idle and absolute limits end a stuck write. Each write converts nothing, and adapters map line ends for the terminal.

### Dispatch

For each complete line:

1. Call `crux_access_touch`, because this is user activity. If the session is no longer valid, end, and go to the login prompt on serial.
2. Split into tokens. Every tokenising limit applies here, independent of the command.
3. Find the command. An unknown name gives the fixed output of rule L3.
4. Check the channel, then the forced-change rule, then the access class. A `PERMISSION` command calls `crux_access_check`, which produces the denial event. A failed check gives the same fixed output as step 3.
5. For exec, refuse a command with `CRUX_CLI_FLAG_INTERACTIVE`. Then check argument counts. A failure here prints usage, and only a command the session can run reaches it.
6. Run the handler.

Output for an unknown or hidden command is `unknown command`, with exit status 127 for `exec`. Other `exec` statuses are 0 for success, 1 for `FAILED` or `DENIED`, and 2 for `USAGE` or
`TOO_LONG`. A session that ends between dispatch and handler is handled by the service's own check.

**Forced password change.** While `crux_access_session_must_change_password` is true, only commands with `CRUX_CLI_FLAG_FORCED_CHANGE_OK` run, and that means `passwd`, `logout` and `help`. `help` is included because it calls no service and shows only those commands.
The registry applies this itself for every access class, because a `SESSION` command passes no permission check. Each dispatch queries the flag, so it clears as soon as the change succeeds.

**Help.** `help` lists the commands whose access class the session passes. It uses the quiet permission probe AC1, so filtering the list leaves no denial events. It is itself a `SESSION` command, and
`help <command>` follows rule L3. A `SESSION` command stays listed even if its service refuses a given item, so `config get` appears for any session, and a parameter it cannot see looks missing, by the configuration service's rule. Commands that only one permission can use are `PERMISSION` commands, so they are hidden.

**Exec.** `crux_cli_exec` runs one line through the same steps, and step 5 refuses an interactive command with status 2. There are no prompts, line editing or echo. A forced password change refuses everything else too.

### Reference commands

The CLI design's table stands. This proposal adds these details.

| Command | Access | Flags |
| --- | --- | --- |
| `help`, `logout` | Session | Serial and SSH, `FORCED_CHANGE_OK` |
| `passwd` | Session, and the account service checks ownership, as that design says | Serial and SSH, interactive, `FORCED_CHANGE_OK` |
| `status` | Permission `status.read`, as in that design | Serial and SSH |
| `config list`, added here, `config get`, `config set` | Session, and the configuration service checks each parameter | Serial and SSH. `config set` on a sensitive parameter takes no value argument: it prompts with `crux_cli_read_secret`, and exec refuses it. |
| `account list` | Permission `access.manage`. That design says session-only with the service checking, but a named permission hides it, as L3 requires. | Serial and SSH |
| `config save` | Permission `config.save` | Serial and SSH |
| `device fingerprint` | Permission `access.manage` | Serial and SSH |

A session on serial sees the same registry as one on SSH, except where a project uses channel bits.

### Serial login

The login loop is outside the registry, and `crux_cli_console_run` owns it. Nothing about the device, including a version, appears before login.

1. Open Serial port 0, as the composition configured it. On failure, retry after a pause, with an event.
2. Prompt for a user name, then for a password without echo. A name that fails the alphabet still reaches the password prompt, and `INVALID_ARGUMENT` prints the same `login failed` as a denial. It skips the slow verifier, so timing differs for such a name, and that reveals nothing about accounts, because real names cannot have that form.
3. Call `crux_access_login_password` with the origin channel `CRUX_ACCESS_CHANNEL_SERIAL`. No source is set, because the access proposal keeps serial's single delay state.
4. Clear the password buffer on every path.
5. Report the result as below. Then run `crux_cli_run` on success, and clean up with `crux_access_logout` afterwards.

| `crux_access` result | Console output | Next |
| --- | --- | --- |
| `OK` | Prompt, plus a must-change notice if the session requires it | Run |
| `DENIED`, `INVALID_ARGUMENT` | `login failed` | Prompt |
| `RETRY_LATER` | `try again in N s`, from `retry_after_ms` | Prompt. The console does not sleep on the delay, because the contract never sleeps. |
| `BUSY` | `busy, try again` | Prompt |
| `NO_RESOURCES` | `no session available` | Prompt. One slot is reserved for serial, so this is not expected. |
| `NOT_READY` | `login unavailable` | Prompt, because `UNUSABLE` rejects every login |

The access contract records the outcome as events. The console emits only `cli.console_error`, for its own failures.

A wrong password, unknown name and disabled account give the same output, because the access contract gives them the same result.

A Serial read that returns `CRUX_ERR_IO` is treated as a transport failure. The Serial contract says observed errors and provider receive overflow latch `IO` until close and reopen, so the console ends any session,
closes and reopens the port, and starts at the prompt. A close that cannot finish draining quarantines the binding (`serial_zephyr.c`, documented in the Serial contract), and every later open then returns `IO`. The console reports `cli.console_error` and retries at a slow rate, but only a reset brings it back. Typing faster than the receive ring empties can cause this, so a paste that outruns the 256-byte ring may end a session. That is a limit.

### First-account setup

With the input `!setup` at the user-name prompt, the console asks for a name and for a password twice, without echo. It calls `crux_access_provision_first_account` with the role that the
composition configures, on the serial origin. `!` is outside the name alphabet, so this cannot be a user. The access contract alone decides: it succeeds only on serial while the table
loaded at start was `EMPTY` and no account has been created since. The result reveals only what someone at the console could
learn by trying. The prompt does not announce the state, so no access change is needed.

| Outcome | Console output | Next |
| --- | --- | --- |
| The two passwords differ | `passwords differ`, with no call | Prompt |
| `OK` | `account created`, and no session is opened | Login prompt |
| `DENIED` | `setup not available` | Login prompt |
| `PASSWORD_REJECTED` | `password not accepted`, with the configured rule's text | Setup prompt again |
| `INVALID_ARGUMENT` | `invalid name`. A bad configured role also lands here and needs a corrected build. | Setup prompt again |
| `STORE` | `could not save, nothing changed` | Login prompt |
| `NOT_READY` | `login unavailable` | Login prompt |

### Zephyr shell rule

The Zephyr shell stays a development facility and never reaches the network. Its command entries have no permission field, `shell_set_root_cmd` applies to every instance, and it brings built-in commands.

Production means the first row below. An image in the third row has no Crux management and is not a production result of this work.

| Image | `CONFIG_SHELL` | Backends |
| --- | --- | --- |
| Has `CONFIG_CRUX_CLI`, `CONFIG_CRUX_WEB` or `CONFIG_CRUX_SSH`, and no `CONFIG_CRUX_DEVELOPMENT_IMAGE` | Must be `n` | None |
| Has one of those, and is marked development | May be `y` | `SHELL_BACKEND_SERIAL` only |
| Has none of those | May be `y`. Existing samples such as `samples/controller/zephyr/m3-network.conf` keep their settings, and they name no network backend. | Anything except the network backends and `MCUMGR_GRP_SHELL`, so `tests/time/zephyr` keeps `SHELL_BACKEND_DUMMY` |

In Zephyr v4.4.2 the network backends are `SHELL_BACKEND_TELNET`, `SHELL_BACKEND_WEBSOCKET` and `SHELL_BACKEND_MQTT`. `MCUMGR_GRP_SHELL` also runs shell commands and depends on `SHELL` and
`SHELL_BACKEND_DUMMY`. Whether it can sit on a network transport was not read, so it is refused without relying on that. With management enabled the rule is an allow list: of `CONFIG_SHELL_BACKEND_*`, only `SHELL_BACKEND_SERIAL` and its options pass, which also covers `RTT`, `RPMSG`, `DUMMY` and `ADSP_MEMORY_WINDOW`. A project's own `SHELL_DEFINE` backend has no symbol. No check here detects one in an image where the shell is allowed, so it is left to review, and a row 1 image cannot have one.

Enforcement has three levels, none of which needs a protected file to change:

1. **Build checks.** The management components refuse the management rows with `BUILD_ASSERT`, which repeats a pattern the Logging binding already uses against the shell. `zephyr/CMakeLists.txt`, which every Crux Zephyr build loads, refuses the network backends and `MCUMGR_GRP_SHELL` in every image, because no management component exists to check an image without management.
2. **A new check script**, `scripts/check-management-shell.py`, reads a built `.config` and fails if a forbidden symbol is set.
3. **Image inspection** on a production image: no shell symbols, no shell backend instance and no `shell_` command sections are linked, as `docs/microsd-composition.md` already does for `f_mkfs`.

A development image must say so in its identifier and in its evidence, and it is never the subject of a production result. The later port scan under R2 checks the running device, with its own approval.

`serial_binding.c` already refuses a shell on the same UART, so a development shell and the CLI console use different UARTs. The EVKB Logging profile also rejects `CONFIG_SHELL` in `logging_binding.c`, so a development image with a shell cannot use that profile unchanged, and it needs its own reviewed composition.

## Failure handling

| Failure | Result |
| --- | --- |
| `crux_cli_init` not called or failed | `crux_cli_run` returns `NOT_READY`. Console prints `login unavailable`. |
| Overlong line | Discarded and rejected as a whole, with no partial run |
| Session expires while idle | Found at the next line or read timeout. On serial, back to the prompt. On SSH, the channel closes. |
| Transport write fails | `CRUX_CLI_ERR_OUTPUT`, session ended |
| Serial IO error | Close, reopen and prompt |
| Serial quarantined after a failed close | Every open returns `IO`. `cli.console_error`, slow retries, and the console stays down until a reset. |
| Handler returns a service error | One fixed line per result, with no internal path or native error |
| `crux_access_check` returns `NOT_READY` | Same as a denial for the user, and the session ends |

## Testing

| Level | Cases |
| --- | --- |
| Host parser conformance | Line and token limits at and over the bound, quotes, escapes, CR, LF and CR LF, backspace at the start, Ctrl-U, Ctrl-C, ANSI sequences split across reads, bytes above 0x7F, NUL, embedded controls, and an overlong line followed by a good one. Fuzz input with sanitizers. |
| Host rejection | Unknown, hidden and wrong-channel commands give identical output and status, including hidden with too many tokens, with wrong argument counts, an interactive one under exec, and a hidden two-word command next to a visible first word. No access class fails at init. Duplicates, a one-word and two-word overlap, bad names and bad bounds. A zero-initialised entry. |
| Host permissions | Each access class with no session, expired, unpermitted and permitted. Forced change for every class, including `SESSION`. Help never produces an event, and a typed denial does. Exec refusing an interactive command. |
| Host login | Every row of both tables with a fake access provider, then with the real `crux_access` and a fake store. Password buffer cleared. Known secrets absent from output and events. Serial `IO` and quarantine handling with a fake transport. A stuck write ended by session expiry. A sensitive `config set` prompt that leaves nothing in the line buffer. |
| Shell rule | The script against sample `.config` fixtures that pass and fail, for each row. |
| Zephyr build | A management composition without the mark fails with `CONFIG_SHELL=y`, and any composition fails with each network backend and `MCUMGR_GRP_SHELL`. A marked composition with the serial backend builds. Existing samples keep building. |
| `native_sim` | Console over a UART emulator, which Serial already admits. |
| Physical, separately approved | Serial login on the EVKB showing permitted and rejected outcomes, after the LPUART3 wiring is checked. The Serial contract states its electrical suitability is unverified. |

Host tests do not establish behaviour on the wire or that a production image is free of the shell. The build and image checks do that part.

## Limits

- The serial wire is not encrypted, and physical access to it is outside this contract.
- A session survives an unplugged serial cable until the idle limit.
- A paste beyond the receive ring can latch `IO` and end the session, and a quarantined Serial binding keeps the console down until a reset. The access proposal treats serial as the local recovery path, so a product should know this.
- A sensitive value cannot be set through `exec`.
- No history, completion or line editing beyond what is listed.
- Output from a project handler is the project's responsibility.
- LPUART3 wiring on the EVKB is unverified. The Serial contract calls it a candidate.

## Changes other contracts need

- **AC1, access control:** `crux_access_permitted(session, permission, bool *permitted)` in the public `crux/access.h`, with no event, no touch and no storage, for help. This is a request. The access contract is not changed here. If AC1 is refused, help must list everything a session could see, or fill the event ring with denials.
- **AC2, access control, optional:** a query that says whether provisioning is open, for a friendlier prompt. L4 avoids needing it.
- **Access proposal wording:** `access.self` is said to allow changing one's own password and keys and logging out, while the forced-change text allows only a password change and logout. The result of `crux_access_check(session, "access.self")` during a forced change is therefore unclear. The registry avoids relying on it: `passwd` and `logout` are `SESSION` commands with `FORCED_CHANGE_OK`. The access contract should state the result.
- **CLI and SFTP design:** "the same registry runs on the serial console" holds, but on LPUART3. `account list` becomes a `PERMISSION` command.
- **Secure management design:** rule L3 replaces "hidden from help and rejected if typed" with identical output, which is stricter for `PERMISSION` commands.
- **Secure management design:** decision D9 is met by L5, and it can move from proposal to adopted when this is accepted.
