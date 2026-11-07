# SFTP over the file service

Status: implemented on Host, Issue #194, for review. [Gate #183](https://github.com/wonny-labs/crux-pilot/issues/183#issuecomment-6617722497) has passed, and the owner approved this work in advance. It authorises no hardware operation.

This document says what is built, what is faked, what is untested, and which decisions were taken for lack of an answer. The evidence is of six kinds, and none implies another.

| Kind | What ran | What it shows |
| --- | --- | --- |
| Host, fake provider | `Ssh.sftp`, on the real file service and Access core, with fake File and crypto | What the port owes the file service |
| Host, real wolfSSH | `scripts/test-ssh-sftp-openssh.sh`: OpenSSH 9.6 against the unedited wolfSSH handlers on Linux, through the real port and file service, on the Linux File provider | That the port works with the real handlers and a real client. **The server stands in for the provider** |
| `native_sim`, real provider | `scripts/test-ssh-sftp-native.sh`: OpenSSH 9.6 against the provider of #192, merged in #265, the real access adapter, the port, the file service, and real FatFs over a RAM disk | The acceptance path of #194. **Run on this branch with `main` merged**: 28 cases and the idle case pass. Access ports and the host key are test ones |
| Target compile | `scripts/test-ssh-wolf-settings.sh` and `scripts/test-ssh-sftp-compile.sh` | That it compiles for RT1060 and `native_sim`. Nothing ran |
| Read from source | wolfSSH v1.6.0 `8221e4e`, WinSCP `246bbd4` | What each of them should do. Each statement says so |
| A recorded WinSCP run | WinSCP 6.7 on Windows 11, over a network, against the `native_sim` image of `main` (`f15f69a`), test accounts, a RAM disk and test Access ports. Evidence: three WinSCP 6.7 Debug 1 logs of the run of 7 November 2026, the tester's report with screenshots, server-side records, and the author's investigation, kept as unpublished notes. **None of it is published**; the comments on #194 summarise the tester's report | What WinSCP sent and showed in that run, **as the author read it**. A second short run with keepalive on is recorded without a WinSCP log. One client version, no board. See [the first run](#the-first-run) |

**OpenSSH on `native_sim` passes on `main`**, through the provider of #192 as merged in #265 (`af4c9e6`): 28 cases and the idle case. See [Acceptance](#acceptance).

## What is built

```mermaid
flowchart TB
    client["OpenSSH sftp or WinSCP"] -->|SSH| provider["Provider, #192<br/>(in one set of runs, the Linux proof server)"]
    provider -->|"wolfSSH_SFTP_read on the worker"| wolf["wolfSSH handlers, wolfsftp.c, unedited"]
    wolf -->|"macros, with fs = the session handle"| macros["port/myFilesystem.h"]
    wolf -->|"SFTP_GetAttributes, SFTP_Name_readdir"| typed["port/crux_ssh_sftp_port.c<br/>wolfSSH types, no decisions"]
    macros --> ops["crux_ssh_sftp.c<br/>handles, flag mapping, attributes"]
    typed --> ops
    ops -->|"every session, permission, limit and name rule"| service["crux_file_service"]
    service --> file["crux_file"]
    service --> access["crux_access"]
    provider -->|"sftp_open, sftp_close"| ops
    provider -->|"session_alive at each read timeout"| access
    ops -->|"crux_access_touch on each request"| access
```

| File | Owns | Tested by |
| --- | --- | --- |
| `components/ssh/sftp/port/crux_ssh_sftp_ops.h` | The operations, with no wolfSSH type | All of the below |
| `components/ssh/sftp/crux_ssh_sftp.c` | `crux_ssh_sftp_open` and `close`, and the operations on the file service | `Ssh.sftp`, and the OpenSSH cases |
| `components/ssh/sftp/crux_ssh_sftp_refuse.c` | The same names, all refusing, for an image with no file service | `Ssh.sftp_stub` |
| `components/ssh/sftp/port/myFilesystem.h` | The macros, each one call | `Ssh.sftp_port_shape`, and the OpenSSH cases |
| `components/ssh/sftp/port/crux_ssh_sftp_port.c` | Translation to `WS_SFTP_FILEATRB` and `WS_SFTPNAME` | The OpenSSH cases. The Host build has no wolfSSH |
| `tests/ssh/sftp/openssh/` | The Linux proof server, and the cases that both kinds of server run | Itself |
| `tests/ssh/sftp/native_sim/` | The image for the provider of #192 | Itself |

**The port is enough, so the mount fallback is not selected.** That is the decision the issue asks for. Each macro receives the session through `fs`, with no thread-local state, and no wolfSSH file is edited.

### Requests and what answers them

Each statement about a status was seen on the wire in `wire_a_refused_open_is_a_general_failure` or a case beside it, except a failure from the port itself in `FSTAT` and in `CLOSE`, which no client can provoke here, and which is read from source (`wolfsftp.c:5958` and the `RecvClose` handler). A client can send an unknown handle, and wolfSSH answers that alone: `CLOSE` gets `FAILURE`, "Close File Error", and `FSTAT` ends that client's connection with no status.

| SFTP request | Port | Service | On refusal the client gets |
| --- | --- | --- | --- |
| `INIT`, `REALPATH` | None. `WGETCWD` gives `/` once | None | Handled by wolfSSH. See [long paths](#long-paths) |
| `OPENDIR`, `READDIR`, `CLOSE` | `WOPENDIR`, `SFTP_Name_readdir`, `WCLOSEDIR` | `area_next` at the root, `dir_*` in an area | `OPENDIR`: `NO_SUCH_FILE`. `READDIR`: see [listing limits](#limits) |
| `STAT`, `LSTAT` | `SFTP_GetAttributes` | `stat`, `area_next` | `FAILURE`, "STAT error" or "LSTAT error" |
| `FSTAT` | `SFTP_GetAttributes_Handle` | `size` | `FAILURE`, "STAT error", from source. An unknown handle ends the connection |
| `OPEN` | `WOPEN` | `open`, after a `stat` that wolfSSH always does first | `FAILURE`, "Open File Error". An area or the root: `FAILURE`, "Not A File" |
| `READ` | `WPREAD` | `read`. Past the end is `EOF` | `FAILURE` |
| `WRITE` | `WPWRITE`, or `WWRITE` for `APPEND` | `write` | `FAILURE`, "Write File Error" |
| `CLOSE` | `WCLOSE` | `close` | `FAILURE` if the close reports an error, from source. An unknown handle: `FAILURE`, "Close File Error" |
| `REMOVE`, `RENAME` | `WREMOVE`, `WRENAME` | `remove`, `rename`. Across areas stops in the port | `FAILURE` |
| `SETSTAT` | `WCHMOD`, `WSETTIME` | `area_next`: accepted with no effect for a visible area | `FAILURE`. Size or owner: `OP_UNSUPPORTED` |
| `FSETSTAT` | `WFCHMOD`, `WFSETTIME` | `size`, which checks the session: accepted with no effect on a live one | `FAILURE`. Size or owner: `OP_UNSUPPORTED` |
| `MKDIR`, `RMDIR` | Macros give `WS_PERMISSIONS` | Only to record a hidden area | `PERMISSION_DENIED`, always |
| `SYMLINK`, `READLINK`, others | None | None | `FAILURE`, "Unknown/Unsupported packet type". The design asked for "permission or unsupported", and wolfSSH has only this |

### Open flags

| SFTP flags | Service mode |
| --- | --- |
| `READ` | `OPEN_READ` |
| `WRITE`, or `READ` and `WRITE` | `OPEN_DIRECT` |
| ... with `CREAT` | `OPEN_DIRECT_CREATE` |
| ... with `CREAT`, `TRUNC` | `OPEN_DIRECT_TRUNCATE` |
| ... with `CREAT`, `EXCL`, and `TRUNC` or not | `OPEN_DIRECT_EXCLUSIVE` |
| ... with `APPEND` added | As above, and each write goes to the current end |
| `TRUNC` or `EXCL` without `CREAT`, or any of `CREAT`, `TRUNC`, `EXCL`, `APPEND` without `WRITE`, or neither `READ` nor `WRITE` | Refused before the service. The file service states no meaning for them |

## How a refused `OPEN` reaches the client

**As `SSH_FX_FAILURE`, code 4, with the text "Open File Error". This is settled, and a permission status is not available without patching wolfSSH.** There are two exceptions. An `OPEN` of an area the session can see, or of the root, gets `FAILURE` with "Not A File", because wolfSSH's own `stat` stops it before the port. And a path of 256 bytes or more ends the connection: see [long paths](#long-paths).

```mermaid
sequenceDiagram
    participant C as Client
    participant W as wolfSSH RecvOpen
    participant P as Port
    participant S as File service
    C->>W: OPEN /data/doc, WRITE
    W->>P: SFTP_GetAttributes
    P->>S: crux_file_service_stat
    S-->>P: result, ignored unless a directory
    W->>P: WOPEN returns -1
    P->>S: crux_file_service_open
    S-->>P: ERR_DENIED, with a denial event
    Note over W: fd below 0, wolfsftp.c:2590
    W-->>C: STATUS 4, "Open File Error"
    Note over C: The session goes on
```

| Question | Answer, from `src/wolfsftp.c` at v1.6.0, and seen on the wire |
| --- | --- |
| Does `WOLFSSH_SFTP_ERRNO` exist? | No, as the contract found. Nothing replaces it |
| What does `WOPEN` returning below zero give? | `WOLFSSH_FTP_FAILURE`, "Open File Error", `:2590` to `:2599`. There is no input to it but the sign |
| Can any path send a permission status for `OPEN`? | Only `GetAndCleanPath` returning `WS_PERMISSIONS`, `:2487`, which only the confinement root can cause. That is one prefix for a session, it runs before the port, and it knows nothing of a session's rights |
| Does the session survive? | Yes. A queued status makes `wolfSSH_SFTP_read` succeed, `:1884` and `:1895`, and each case goes on to the next request |

| Option | Result | Decision |
| --- | --- | --- |
| Accept the general failure | Every refusal looks the same to a client. A security event records the cause for some of them and not all: see [the next table](#each-refusal-and-its-event) | **Chosen** |
| Set the confinement root per request | Not possible: it is one string, and an area and a hidden area sit side by side | No |
| Patch the handler | A security-relevant fork, which the design rejects and the issue forbids without owner approval | Not selected. It would be the only way to a permission status |
| Rewrite the status in the provider | Needs a parser on the data path, for one word | No |

It has one benefit: **a client cannot tell a denial from a missing file**, which the file service contract asks of an adapter for a hidden area, and which holds here for `OPEN`, `STAT` and `REMOVE`. The cost falls on the operator: WinSCP adds its own text about Error code 4, whose list of common reasons has no mention of permissions, so a denied user is told about full disks. The recorded run showed exactly this: see [what WinSCP shows an operator](#what-winscp-shows-an-operator). User documentation should say so.

`REMOVE` and `RENAME` work the same way: `:4996` and `:5088` look only at the sign. `MKDIR` and `RMDIR` are the only two where a port can choose, and the port always says permission denied, since there are no directories to make.

### Each refusal and its event

The design asks for a refusal status and a security event for a rejected file operation, and gate #183's "Hiding" asks for a denial event on direct use of something hidden. **So every use of a hidden area that reaches the port records a denial, and many other refusals record nothing.** The last column says why.

| Refusal | The client gets | Event | Why |
| --- | --- | --- | --- |
| Direct use of a hidden area: `STAT`, `LSTAT`, `SETSTAT`, `OPENDIR`, `REMOVE`, `MKDIR`, `RMDIR`, an area or a file in it, an `OPEN` with flags that mean nothing or past the session's bound, an `OPENDIR` past it | As for a missing one, by request | `access.permission_denied`, once for each call of the port | Hiding. For requests that do not reach the service, the port asks it with a `stat` that stops at the permission, and it changes nothing |
| `OPEN` in a hidden area | "Open File Error" | **Two** denials | wolfSSH's `stat` before `OPEN` is a call of its own |
| `RENAME` naming a hidden area, including across areas | "Rename File Error" | One denial for each hidden area named. **Two** for one hidden area when the other path holds a refused name, as in `RENAME /data/doc /data/my file` | Hiding. The refused name is reported for itself, and the other path is then still direct use |
| Missing area | As for a hidden one | None | No area exists to record it against, and the service says the same. So the two differ in the log, and on the wire they do not |
| Session may read and not write: `OPEN` to write, `REMOVE`, `RENAME` | `FAILURE` | One denial | The service's check |
| Session may write and not read: `OPEN` to read, `STAT` of a file, `OPENDIR` | `FAILURE`, or `NO_SUCH_FILE` for `OPENDIR` | One denial, and two for `OPEN` | The service's check, and wolfSSH's `stat` |
| The same session's successful upload | Success | One denial, from wolfSSH's `stat` before `OPEN` | The port cannot tell it from a client's `STAT` |
| Read-only area (no write permission declared): any write | `FAILURE` | None | The file service contract: the session holds what was checked |
| Write mode that the area does not offer | `FAILURE` | None | The same |
| Name that the rule refuses, in an area the session can read | `FAILURE` | `file.name_rejected`, area only. **Twice** for `OPEN`, as above | The service's rule |
| The same, with write permission alone | `FAILURE` | `file.name_rejected`, area only, by a rename of the text onto itself, which needs write permission and can change nothing | The service's rule. For `OPEN`, one from `stat` and one from `OPEN`, and no denial |
| The same, in a hidden area | As for a hidden area | Denial only | The name is not looked at |
| The same, with no such area, or a bad area name | `FAILURE` | None | No area to record it against |
| `REMOVE` or `RENAME` of an area that the session may write | `FAILURE` | `file.name_rejected`: the name is empty | The service's rule |
| The same, may read and not write | `FAILURE` | One denial | The service's check |
| `OPEN` of an area it can see | "Not A File" | None | wolfSSH stops it before the port. Its `stat` succeeded |
| `OPENDIR` of a file in an area it can see | `NO_SUCH_FILE` | None | No rule was broken, and the file service has no event for it |
| `REMOVE`, `RENAME`, `OPEN` of the root | `FAILURE` | None | No area to record it against |
| `RENAME` across areas it can see | `FAILURE` | None | The port refuses, and `crux_file_service_rename` takes one area. No event identifier fits |
| `OPEN` with flags that mean nothing, in an area it can see | `FAILURE` | None | Not a permission. No event identifier fits |
| Past `CRUX_SSH_SFTP_FILES_MAX` or `DIRS_MAX`, in an area it can see | `FAILURE` or `NO_SUCH_FILE` | None | A resource bound, not a permission. No event identifier fits |
| The service's pools, reservations and a busy name or listing | `FAILURE`, or `NO_SUCH_FILE` for `OPENDIR` | None | The contract gives `ERR_NO_RESOURCES` and `ERR_BUSY` no event |
| A limit, a sparse write | "Write File Error", or "Open File Error" for entries | None | The contract gives `ERR_LIMIT` and `ERR_OUT_OF_RANGE` no event |
| A full medium, a storage failure | `FAILURE` | `file.storage_failed` | The service |
| `MKDIR`, `RMDIR` in an area it can see | `PERMISSION_DENIED` | None, except that a name the rule refuses records `file.name_rejected`, as anywhere | The operation does not exist. The session holds what it holds |
| `SETSTAT` with a size or an owner, in any area, hidden or not | `OP_UNSUPPORTED` | None | wolfSSH stops it before the port |
| `SYMLINK`, `READLINK`, other requests, in any area, hidden or not | `FAILURE` | None | No port call exists |
| `REALPATH` of any path, hidden or not | The normalised path | None | wolfSSH answers it from the text, and it reveals nothing |
| A path of 256 bytes or more, in any area | See [long paths](#long-paths) | None | wolfSSH stops it before the port |
| A session that has ended, by logout or otherwise | `FAILURE`, then the connection ends at the next poll | `access.session_invalid`, **one or more** by request: usually two; one for `STAT` of a missing area, `REMOVE /` and `MKDIR /`; three for `STAT` of an area or a refused name, `SETSTAT` of a file and `MKDIR` in an area; four for `OPEN`, counting wolfSSH's own `stat`; five for a rename across areas | Each comes from the port's `crux_access_touch` or a service check. A close asks nothing |
| A session that the idle limit ended | The same | `access.session_expired` once, from the touch, and `access.session_invalid` from the service. Each later request is as above | Access |

Evidence: `case_each_direct_use_of_a_hidden_area_records_a_denial`, `case_what_the_service_records_for_an_area_used_as_a_file` and `case_a_refused_name_is_recorded_once_with_no_text`, `case_a_session_that_has_ended_is_recorded` and `case_a_session_that_expired_is_recorded_first_as_expired` on Host, and `wire_a_refused_name_is_recorded_with_no_text` on both servers, for `file.name_rejected`. The rows with no event are read from the code. The port reaches events only through the file service: its event port is private, and an identifier for the rows that have none would be a change to the file service and #182, which is outside the files of #194, so I did not add one. [#270](https://github.com/wonny-labs/crux-pilot/issues/270) lists them, as a shared change under G-A and #182.

### Long paths

`WOLFSSH_MAX_FILENAME` is 256. Under that length a long name gets the normal refusal. From it, each handler does its own thing, and a handler that returns with no status leaves the server nothing to answer with, so the connection ends. Measured by `wire_a_long_path_depends_on_the_request`:

| Request | At 256 bytes | At 406 bytes |
| --- | --- | --- |
| `OPEN`, `OPENDIR`, `REALPATH`, `MKDIR` | Connection ends | Connection ends |
| `REMOVE` | `FAILURE` | Connection ends |
| `STAT`, `LSTAT`, `RENAME`, `SETSTAT`, `RMDIR` | `FAILURE` | `FAILURE` |

WinSCP can send one, for a long local name.

## Traversal

| Layer | What it does | Evidence |
| --- | --- | --- |
| wolfSSH `wolfSSH_RealPath` | Resolves `.` and `..` lexically, and stops at `/`, so `/data/../../etc/x` arrives as `/etc/x` | `sftp_traversal_reaches_nothing`, with real `get`, `put` and `cd` |
| `crux_file_service_resolve`, the only parser | Refuses anything but `/area`, `/area/` and `/area/name`, and any name that fails the rule | `case_paths_outside_the_rules_reach_nothing` sends 29 raw shapes to every operation, as if wolfSSH had not cleaned them |
| File | Roots and flat names | The file service's own tests |

**`wolfSSH_SFTP_SetConfinePath` adds nothing here, and the port does not ask for it.** The design calls for it as a second defence. But the virtual root is the client's whole namespace, so the root would be `/`, and at `/` wolfSSH treats a session as unconfined (`:2057`). The real second defence is that `resolve` refuses what `RealPath` should have removed, and a test sends it those shapes. See [decision 6](#decisions-for-review).

## The session, and how it ends

| Event | What happens | Evidence |
| --- | --- | --- |
| A request reaches the port | `crux_access_touch`, ignoring the result, so that the service stays the one that decides | `case_activity_keeps_the_session_and_idleness_ends_it` |
| Nothing for the idle limit | The provider's poll of `session_alive` finds it expired, and it ends the connection | The same, and `idle_session_ends_and_activity_does_not` |
| `REALPATH`, the WinSCP keepalive | It reaches no port, so it does not refresh the session, and a client that only keeps alive still ends at the limit. **Seen once with a real WinSCP, as an outcome only**: with keepalive on and the limit at 120 s, two timed connections of one run (and at least one more) each ended at the limit while packets at about the keepalive's rhythm reached the server ([row 13](#predicted-and-observed)). No WinSCP log was captured, and the packets were not decoded, so that they are the keepalive is an inference. The mechanism rests on the replay and on the source | `idle_session_ends_and_activity_does_not` |
| A quiet WinSCP connection, keepalive off | The connection ended 900 s after its last traffic: see [row 12](#predicted-and-observed) for what each time rests on. The tester reported that the window kept working on the next click with no prompt. No WinSCP log shows either | The recorded run: see [the first run](#the-first-run) |
| A quiet WinSCP connection, keepalive on, limit lowered to 120 s | Two timed connections of one run (and at least one more) each ended about 120 s after login, with packets reaching the server about every 30 s. No WinSCP log, packets not decoded, one run | [A second run](#a-second-run-with-keepalive-on) |
| A session ends mid-transfer | The next chunk fails, as the file service says. A close needs no session | `case_a_session_that_ends_fails_the_next_chunk` |
| A client disconnects | Every transfer and listing comes back. A partial direct write stays where it is, and counts against the area | `case_disconnect_keeps_...`, `wire_disconnect_gives_every_handle_back`, `sftp_disconnect_...` |
| The stale rule needs room | Files with the area's `partial_suffix` go, as the file service states (F5) | `case_partial_counts_until_the_stale_rule_needs_its_room`, Host only |

### Bounds

| Bound | Default | Past it |
| --- | --- | --- |
| `CRUX_SSH_SFTP_SESSIONS_MAX`, sessions at once | 4 | `sftp_open` fails, and the connection ends. A composition sets it to at least `max_connections` |
| `CRUX_SSH_SFTP_FILES_MAX`, files per session | 2, below the service's 3, so that no session takes them all | `OPEN` fails before the service, and nothing is created |
| `CRUX_SSH_SFTP_DIRS_MAX`, listings per session | 2 | `OPENDIR` fails |
| `CRUX_FILE_SERVICE_TRANSFER_MAX`, `DIR_MAX`, across sessions | 3, 1 | `OPEN` or `OPENDIR` fails |
| `WOLFSSH_MAX_FILENAME`, each path buffer | 256 | By request: see [long paths](#long-paths) |

A descriptor holds a generation of the session slot. wolfSSH can close what a client left open as it frees the session, so a descriptor from an earlier connection must not match the slot's next owner, whose files the service would close with no session check. It is a defence: the contract already has the provider free wolfSSH first.

## How this fits the provider of #192

Read from `crux_ssh_wolf_provider.c` as merged in #265 (`af4c9e6`), and run through it on `native_sim`. Nothing here asks for a change to it.

| Item | What the provider does | Why it matters here |
| --- | --- | --- |
| When `sftp_open` runs | As soon as `wolfSSH_accept` has recorded the request, and before `INIT` is answered | The handle is set before the loop, so that no request reaches the port without it. The Linux proof server opens after `accept` returns, which also works, as `wolfSSH_accept` answers `INIT` itself and a second `wolfSSH_SFTP_accept` would return `WS_FATAL_ERROR` with no error set |
| Order at the end | `wolfSSH_SFTP_free`, then `wolfSSH_SetFilesystemHandle(NULL)`, then `sftp_close`, then later `wolfSSH_free` | wolfSSH closes what the client left through the port, with a good handle, and the header's rule holds |
| Channel window and packet | 16 KiB each, by default | Below `WOLFSSH_MAX_SFTP_RW`. WinSCP sizes a read by throughput up to the channel's packet (`SftpFileSystem.cpp:2243`), and a `READ` above 32768 makes wolfSSH return with no status (`wolfsftp.c:4650`), which ends the session. **A composition must not raise the packet above 32768** |
| A window smaller than a reply | OpenSSH asks for 32 KiB blocks, and a 1 MiB transfer passes both ways | So the `WS_WINDOW_FULL` path in `serve_sftp`, which #192's document says had never run, must have run in these cases. No counter shows it |
| `wolfSSH_SFTP_read` failing with nothing to send | Ends the connection | Some requests with a long path come back this way: see [long paths](#long-paths) |
| Receive budget | A receive returns would-block after 16 KiB while data waits, and sleeps 1 ms. At `native_sim`'s 100 Hz that is a 10 ms tick, about 1.6 MB/s of what the server receives | A 1 MiB upload ran at 0.62 to 0.77 MB/s, under the budget, and a download at 4.0 to 6.6 MB/s, above it, because the server then receives only requests. Simulation figures only. Why uploads reach about half the budget was not isolated |
| `session_alive` | Polled each `io_timeout_ms` with no request | `idle_session_ends_and_activity_does_not`, with a 4 s limit |
| `max_connections` | The image uses 4 | `CRUX_SSH_SFTP_SESSIONS_MAX` is 4, and a composition keeps it at least as large |
| `CRUX_SSH_SFTP_FILE_SERVICE` | The composition sets it before including `wolf_ssh_settings.cmake` | Without it SFTP is refused, and #192's own image, with its stand-ins, is unaffected: its `sftp_reaches_the_stand_in` and `repository_stand_ins_refuse` still pass with this branch merged |

### Simulation figures

From the image's heap statistics and #192's stack probe, over the 28 cases on `main`, the same as before #265 merged. **`native_sim` only: a 64-bit image, with a host stack under each thread.** They say nothing about a Cortex-M7, and FatFs with `CONFIG_FS_FATFS_LFN_MODE_STACK` adds its own use there.

| Measure | Result |
| --- | --- |
| Host stack, deepest on a worker | 8,768 B, with key login and every SFTP path above |
| Heap peak, with up to four connections | 92,560 B, in a 512 KiB arena, at a 16 KiB window |
| Heap with no client, before and after | 728 B both times, which the script checks |

## WinSCP

**A first run of WinSCP 6.7 is recorded**: see [the first run](#the-first-run), and [predicted and observed](#predicted-and-observed) for what matched and what did not. It is one run of one client version on `native_sim`, and several things were not run. The table below was read from WinSCP's source at `246bbd4` before the run (`source/core/SftpFileSystem.cpp` unless named), and each request sequence in it is replayed over real OpenSSH in `tests/ssh/sftp/openssh/cases.py`, on Linux and on `native_sim`. A replay shows that the server answers those requests as expected, and it cannot show what WinSCP then does. The run could, for the cases it covered.

| Behaviour | Source | Checked by | Result |
| --- | --- | --- | --- |
| Asks for SFTP version 3 from a server it does not know, and accepts 3 with no extensions | `:3028` to `:3042` | Source, and `winscp_session_start_and_listing` | Version 3, no extensions |
| Starts with `REALPATH .`, and tries a directory with `LSTAT` | `:3485` | Replay | `/`, and a directory |
| An empty listing makes it read `<dir>/..`, except at the root, and a failure there is an error | `:3660` to `:3714` | Replay | wolfSSH resolves it to `/`, which answers |
| It adds `..` itself when the listing has none | `:3718` | Source | The port sends none |
| Below 100 KiB, new file: `OPEN WRITE,CREAT,EXCL`, writes, `CLOSE`, `SETSTAT` with the time | `:5128`, `:5015` | Replay | Works |
| Below 100 KiB, existing file: `EXCL` fails, `LSTAT` finds it, and after confirmation `TRUNC` | `:5163` to `:5198` | Replay | Works. A failed `LSTAT` throws the first error |
| From 100 KiB: `LSTAT` of target and `.filepart`, `OPEN .filepart WRITE,CREAT,TRUNC`, `CLOSE`, `RENAME` | `:4657`, `:4709`, `:4964` | Replay | Works |
| The two `LSTAT`s treat any status but a lost connection as "not there" | `:3786` to `:3811` | Source and replay | `FAILURE` does for `NO_SUCH_FILE` here |
| Over an existing target: `REMOVE`, then `RENAME`, since there is no `posix-rename` here | `:4939` to `:4964` | Replay | Works. The service refuses a rename over a name |
| Resume: reads the partial size, `OPEN WRITE,CREAT`, writes from that offset | `:4709`, `:4848` | Replay, after a killed client | Works, content verified |
| A partial name it cannot create falls back to the target name, in the default smart mode | `:5153` | Replay, with a 60-byte name | Works. So a name of 55 to 63 bytes uploads directly, with no resume |
| `SETSTAT` with the time on the target. A refusal stops the upload with an error | `:4994`, `:5015`, `TextsCore1.rc:107` | Replay | Accepted with no effect |
| It takes a small file with all rights set for a link, and stops resuming | `:4616` | Replay | The port never sends all rights |
| Writes are pipelined in order, and `CLOSE` goes out before the answers are read | `:4888` | Replay in part | wolfSSH handles requests in order, so no sparse write results |
| Block size follows throughput, limited by the channel's maximum packet | `:2243` to `:2296` | Source | See [the provider](#how-this-fits-the-provider-of-192) |
| `REALPATH /` as a keepalive, only if the session is set to send a dummy command | `:2063` to `:2070` | Replay. **Run once with WinSCP, as an outcome only**: keepalive on, limit 120 s, two timed connections of one run (and at least one more) ended at the limit (row 13) | Does not refresh the idle timer. The replay shows the mechanism. The run shows the same outcome and does not prove the mechanism |
| Uses `RENAME`, not `posix-rename`, by default, and no `limits@openssh.com` | `:3352`, `:3884` | Source | This server advertises neither |

**Not settled by the first run:** whether sorting and "synchronize by time" behave sensibly for entries with no time (the run showed only the dates in [the table](#predicted-and-observed), and the long name, which always says "Jan  1 00:00", was not looked at); how the listing's long name parses; the order of its pipelined requests beyond the two uploads in the logs; and the dialogs that the run did not meet.

### The first run

**What ran.** WinSCP 6.7 (build 17845) on Windows 11, SFTP, against the `native_sim` SFTP image of `main` (`f15f69a`) over a network. The server had the test accounts `root` (administrator), `vera` (viewer, read-only), `dora` (dropper, write-only, sees only `inbox`), `audit` and `stan`, a limit of 1 MiB per file, 3 MiB per area and 16 entries per area, a RAM disk and test Access ports. Nothing ran on a board. WinSCP's own keepalive was off: the sampled TCP counters showed no packet for the first minutes of the quiet period (see row 12), and `Ping type: Off` is in logs A, B and C, which are other sessions with the same WinSCP configuration. A second, short run on the same image, with keepalive on, is described in [a second run](#a-second-run-with-keepalive-on).

| Evidence | What it covers |
| --- | --- |
| WinSCP 6.7 Debug 1 log, run of 7 November 2026, **log A** | The dropper uploads a 2-byte file to `/inbox` under a name that was already there. The log starts with the tail of an older session and has no earlier `OPEN` of that name. That the name existed rests on the tester's account and on the server log of the run, where an earlier dropper session shows a create event for the same name before the logged session |
| The same, **log B** | The administrator uploads a 2,077,166-byte file to `/data`, past the 1 MiB per-file limit. It ends at `Answer: Abort` |
| The same, **log C** | The dropper uploads a new 2-byte file to `/inbox` |
| The tester's report: what the WinSCP window showed, in screenshots and a chat report | Name refusals, read-only refusals, the dates, the buttons of the error window, that Reconnect works, and that the window kept working after the idle close. **Not a log** |
| Server-side records of the run | The server log (denial and create events, and session and worker-end lines, with no per-line times), a read-back of the stored files as the auditor, an owner-side monitor that polled every 5 s for an established connection to the SSH port, and TCP counters of that connection read inside the container with `ss` |
| The second run (keepalive on, limit 120 s): the owner-side monitor, TCP counters, the server log and the tester's chat report | See [a second run](#a-second-run-with-keepalive-on). **No WinSCP log was captured** |
| The author's investigation, kept as unpublished notes and summarised here | What was tried to reproduce the channel error, and how the code reads. See [the limit](#an-upload-past-the-per-file-limit-with-winscp) |

The logs and screenshots are held by the tester, and the notes by the author. **None is published**: the logs hold the tester's account, host and path names. So what this document says about them **rests on the author's reading, and a reader of this repository cannot check it against them**.

| Step | Run | Not run, or not recorded |
| --- | --- | --- |
| Administrator | Connect, list the root, enter an area, upload, download (content identical), rename, delete | A rekey, two clients at once |
| A name outside the rule | A name with a space, and a Korean name | The other characters that [the F4 section](#a-file-name-outside-the-rule-f4) names |
| Read-only role | Upload, rename and remove, all refused | Download, as that role (no file was reported) |
| Write-only role | List `/inbox` (refused), upload a new file, upload a name that exists | **Download and delete.** The role cannot list, so no file could be selected, and the path box could not be edited. A first upload of 100 KiB or more, which takes a different path in the source |
| Idle | One quiet connection, keepalive off, 900 s limit, measured by the owner-side monitor and TCP counters (no WinSCP log). A second run with keepalive on and a 120 s limit | **Still open for keepalive:** no WinSCP log for either run, the keepalive packets were not decoded, one run (two timed connections, and at least one more), one limit value (120 s) with keepalive on |
| Upload past the per-file limit | Twice, with the same result | A retry of the same upload, which would meet the partial file |

### Predicted and observed

Each prediction is from the table above, from the design, or from the rows named. "Matched" means that the run showed what the prediction said. It does not mean the cause was checked.

| # | Predicted | Observed | Verdict |
| --- | --- | --- | --- |
| 1 | Version 3 with no extensions, `REALPATH`, list the root, enter an area | As the administrator, all worked | Matched, from the tester's report |
| 2 | Upload, download, rename and delete work for the administrator | All four worked, and the downloaded content was identical | Matched, from the tester's report |
| 3 | From 100 KiB, `LSTAT` of the target and of `.filepart`, with a refusal read as "not there", then `OPEN` of the `.filepart` | Log B: both `LSTAT`s got status 4, "LSTAT error", and WinSCP went on to `OPEN` | Matched |
| 4 | A name outside the rule is refused as code 4, with the generic dialog | A space and Korean characters: `Error code: 4`, server text `Open File Error`, with WinSCP's generic list of common reasons. This is what the window showed, in the tester's report and screenshots, with server-side checks. No log is cited | Matched, from the tester's report |
| 5 | A read-only session's refusals are `FAILURE` | Upload, rename and remove: status 4, with `Open File Error`, `Rename File Error` and `Remove File Error`. One denial event each in the server log. The refusals are what the window showed, in the tester's report and screenshots. No log is cited | Matched, from the tester's report |
| 6 | The write-only role cannot list, and so cannot navigate | Logs A and C: `OPENDIR /inbox` got status 2, "Unable To Open Directory", and WinSCP showed `Error code: 2` | Matched |
| 7 | A write-only upload of a new file works, and `SETSTAT` is accepted | Log C: `OPEN` gets a handle, one `WRITE` gets 0, `CLOSE` 0, `SETSTAT` 0, and WinSCP logs `Transfer done`. The file is stored complete (read back as the auditor, server side). WinSCP sent `WRITE`, `CLOSE` and `SETSTAT` before the first answer came back | Matched |
| 8 | Not predicted: what WinSCP shows after that upload | It refreshes the directory, `OPENDIR /inbox` gets status 2, and WinSCP shows `Error listing directory '/inbox'` | **New**: see [below](#a-session-with-write-permission-alone) |
| 9 | An existing name: `EXCL` fails, `LSTAT` is tried, a failed `LSTAT` throws the first error (the row for existing files above) | Log A, as the dropper, for a name that the server log shows an earlier session had created: `OPEN` got status 4, "Open File Error", `LSTAT` of the file got status 4, "LSTAT error", and WinSCP reported `Cannot create remote file` with the `OPEN` error | Matches the sequence. **Why the `OPEN` was refused is not yet explained as a decision**: see [below](#a-session-with-write-permission-alone) |
| 10 | No times (F6). The text above this section said that the long name always says "Jan  1 00:00", and that sorting was suspect | Folders and the files in the `data` area show `30/12/1899` on the tester's screen. In logs A, B and C, folders carry `1899-12-30T00:00:00`, which is the zero of WinSCP's date type. `logs/boot.log`, in a read-only area, showed `03/01/0001`. No log lists a file or shows 0001, so that comes from the screenshots and the tester's report. **Why 0001 appears for that one file is not understood** | Matched (no time was sent). **How WinSCP shows it is new**: it is not "Jan  1 00:00" |
| 11 | Rights are never all set, so WinSCP does not read a link (decision 13) | `inbox`, which the write-only role sees, shows as `-wx------` (the listing in log A) | Matched |
| 12 | A quiet session ends at the idle limit | The connection ended 900 s after its last traffic. **Each time has its own source.** The close time, 12:59:02, was recorded by the owner-side monitor, which polled every 5 s whether an established connection to the SSH port existed and saw it disappear then. The server log has the matching session-end and worker-ended lines but **no time**. The last traffic, 12:44:02, was computed from the TCP counters of that connection (time since last send and receive, read with `ss` inside the container) at 12:44:53. **No WinSCP log shows the close or a re-login.** The tester reported that the window kept working after the next click, with no prompt. **No keepalive was sent, for the minutes sampled:** the counters were read every 10 s from 12:45:38 to 12:47:09, with packet counts unchanged and the time since last traffic growing steadily. For the rest of the 15 minutes the only support is `Ping type: Off` in the configuration of logs A, B and C. The close time alone could not have shown a keepalive, because this document claims that WinSCP's `REALPATH` keepalive does not refresh the idle timer | Matched on the server side, for the close. The silent re-login is **new**, and is from the tester's report. A second run with keepalive on is row 13 |
| 13 | A `REALPATH` keepalive does not refresh the idle timer | Run once, keepalive on (30 s, as the tester was instructed), limit 120 s: two timed connections of one run (and at least one more) each ended at the limit while packets reached the server about every 30 s. The packets were not decoded. See [a second run](#a-second-run-with-keepalive-on) | **Consistent, outcome only (server side).** Mechanism not shown by this run: the replay shows it |
| 14 | A write past the per-file limit gets "Write File Error", and the session goes on (the `WRITE` row and the limit row of the tables above) | The refusal came as predicted. **The session then ended**: [the limit](#an-upload-past-the-per-file-limit-with-winscp) | **Not matched** |
| 15 | Download and delete as the write-only role | Not run | **Not run** |

### A second run, with keepalive on

A second, short run on 7 November 2026, with WinSCP 6.7 and the same image and accounts. **Two things differ from the first run: keepalive was on, and the idle limit was lowered to 120 s.** Each fact has its own source. All times are UTC: the server log's times are UTC, and the monitor's local clock was GMT (the host's zone is GMT+0).

**Caveats on the setup.** The tester reported turning keepalive on. The menu entry (Advanced, Connection, "Executing dummy protocol commands") and the 30 s interval are as the tester was instructed, and no screenshot or log confirms them. The 120 s limit is the value written to the server's idle setting before a restart, which the server log records at 17:18:15. It is consistent with the ends seen 114 to 125 s after login.

| What | Source | Value |
| --- | --- | --- |
| An earlier short connection | The monitor, and the server log | Absent at 17:23:02, seen at 17:23:07, 17:23:13 and 17:23:18 with the counters unchanged at 1838 sent and 3036 received, absent at 17:23:23. The server log has `end of file` for it, so the client closed it. By the tester's account it was before keepalive was turned on (the monitor and the server log cannot show the setting), and **it is not part of the idle measure** |
| Login of the first timed connection | The owner-side monitor, polling every 5 s with `ss` inside the container, for a connection from the tester's machine | Last poll without it at 17:24:45, first poll with it at 17:24:50 |
| Packets on it | The TCP byte counters of that connection (the server's sent and received) | At 17:24:50: 1838 sent, 3036 received. Changes at 17:25:21 (1918, 3100), 17:25:47 (1998, 3164) and 17:26:18 (2078, 3228): each time 80 more sent and 64 more received, 26 to 31 s apart at 5 s sampling. Unchanged from 17:26:18 to 17:26:44 |
| End of the first timed connection | The monitor, and the server log, which has no times | Last poll with it at 17:26:44, first poll without it at 17:26:49. The server log has an access event with result -4, then "SSH worker ended: I/O callback would read block error" |
| Time from login to end | Computed from the rows above | 114 to 119 s after the 17:24:50 poll, and 114 to 124 s counting the 5 s window around the login: about 120 s |
| The second timed connection | The monitor | Absent at the 17:26:54 poll and first seen at 17:27:00, with a different remote port. Steps of 80 sent and 64 received at 17:27:31, 17:28:02 and 17:28:33, 26 to 31 s apart. Last poll with it at 17:28:54 (2158 sent, 3292 received), first poll without it at 17:28:59. Its login lies between the 17:26:54 and 17:27:00 polls, so it ended 114 to 125 s after login. The server log again has the result -4 event and the same "SSH worker ended" line |
| A third connection | The monitor, and the server log | Absent at 17:29:04, first seen at 17:29:09 with another remote port, and seen again at 17:29:14. **Monitoring stopped after 17:29:14, so its end time was not measured.** |
| The server log, final copy | The server log, which has no times | After the restart: one login that ended with `end of file` (the short connection), then four logins, each followed by the result -4 event and "SSH worker ended: I/O callback would read block error", then **a fifth login, closed by the client** (`end of file`). Its time is not known. In order, the four match the two timed connections, the third and a fourth that was not monitored. That order is the author's inference, and so is any link between the fifth login and the reconnection that the tester reported |
| What the tester saw | The tester's chat report. No screenshot, no time, and the message text was not captured | For the first timed connection: a message that the connection was lost, and a few seconds later it was connected again. Whether the others showed a message is not known. Later: another disconnect, after which the tester "waited 10 s and reconnected". What the 10 s was is not recorded |

What it shows, and what it does not:

- **With packets reaching the server about every 30 s, two timed connections of this run, one after the other, each ended about 120 s after login, at the lowered limit.** That fits the claim that WinSCP's `REALPATH` keepalive does not refresh the idle timer. **The replay `idle_session_ends_and_activity_does_not` is what shows that mechanism.** This run shows the same outcome with a real WinSCP. It does not prove the mechanism.
- **The packets were not decoded.** That they are WinSCP's keepalive is an inference from their size, their regularity (about 30 s) and the setting, and the setting itself is not confirmed.
- **No WinSCP log was captured for this run.** That WinSCP logged in again is inferred from the new connections, which the server log shows as logins as the same account. Whether WinSCP used stored credentials or prompted is not recorded.
- **The counters of the later connections are not understood.** Each was first seen at 1918 sent and 3100 received, which is 80 and 64 above the first login's 1838 and 3036, although it was seen within 6 s of its login (the poll before saw nothing). Why is not explained.
- **The two runs differ in more than one thing.** In the first run (keepalive off, 900 s) the tester reported that the window kept working on the next click, with no prompt. In this run (keepalive on, 120 s) the tester reported a connection-lost message and then a reconnection. Keepalive and the limit both differ, so **the difference is not attributed to either one**. Whether the tester was watching the window at the close in the first run is not recorded.
- It is two timed connections of one run (and at least one more), with one limit value.

### A session with write permission alone

Its `OPENDIR` is refused with `NO_SUCH_FILE`, and only the area itself answers `STAT` (`wire_a_write_only_session_cannot_list_or_stat`). WinSCP's window reads a directory to enter it, so **that role cannot navigate in the WinSCP window**, though it sees the area at the root. The run confirmed that (rows 6 to 8 above), and showed what an upload looks like:

```mermaid
sequenceDiagram
    participant W as WinSCP 6.7, write-only role
    participant S as Crux image
    W->>S: OPEN a new name in /inbox
    S-->>W: HANDLE
    W->>S: WRITE 2 bytes, CLOSE, SETSTAT with the time
    S-->>W: STATUS 0 for each
    Note over W: Transfer done
    W->>S: OPENDIR /inbox, to refresh the window
    S-->>W: STATUS 2, Unable To Open Directory
    Note over W: Error listing directory /inbox
```

- **The upload succeeded.** The error comes after `Transfer done`. A user can take it for a failed upload, and nothing in the window says otherwise: see [what WinSCP shows an operator](#what-winscp-shows-an-operator).
- **Three denials in the server log, one of them for the upload.** The server log showed three denials around one first upload by the write-only role: the listing of `/inbox` before it, wolfSSH's `stat` before `OPEN`, and WinSCP's directory refresh after it. The one denial that [the table of refusals](#each-refusal-and-its-event) gives for a successful write-only upload belongs to the upload itself, wolfSSH's `stat`. The other two come from WinSCP's directory reads. The breakdown is the author's inference, since the server log does not say which request caused each denial, and it is consistent with the design.
- **An upload by path was not run.** The run entered `/inbox` from the root and uploaded to it. The sequence above is what WinSCP sent.
- **A name that already exists is refused.** That the name existed is **not shown by log A**, which holds no earlier `OPEN` of it. The basis is the tester's account and the server log, where an earlier dropper session created the name before the logged session. Log A: `OPEN` got status 4, "Open File Error", and so did WinSCP's `LSTAT` of the file, since the role cannot read. The design says two things that fit: a new file below 100 KiB is opened with `WRITE`, `CREAT` and `EXCL` (the table above), and `EXCL` maps to `OPEN_DIRECT_EXCLUSIVE`, which the file service contract defines as "Stat, then `ERR_EXISTS` or create". So the refusal is what an exclusive create gives for a name that exists. **Two things are not established.** Debug 1 does not print the flags of an `OPEN`, so that WinSCP sent `EXCL` here is from its source, not from the log. And the reason the service gave for this refusal was not read from the server's events in this run. **This document does not say that a write-only role may not replace a file, so it is not claimed as a decision.** What the design does show is the consequence: WinSCP's way to an overwrite is `LSTAT`, a confirmation and `TRUNC`, and `LSTAT` is refused to this role. An upload of 100 KiB or more takes a different path in the source (`OPEN` of a `.filepart` with `TRUNC`, then `REMOVE` and `RENAME`) and was not run for this role. Whether a dropper should be able to replace a file is for the owner to decide if it matters.

### A file name outside the rule (F4)

Accepted decision F4 keeps the name rule, which allows **only `[A-Za-z0-9_.-]`, from 1 to 63 characters**. So a space is one case of many: parentheses, `+`, `,`, `~`, accented letters and Hangul all fail the same way, which covers ordinary names such as `report (1).pdf` and any name in Korean. WinSCP cannot upload such a file. From its source, with each request refused as shown in `winscp_name_with_a_space_is_refused_at_every_step`, for `my file.txt`:

```mermaid
flowchart TD
    start["Upload of my file.txt"] --> size{"100 KiB or more?"}
    size -->|"No"| excl["OPEN my file.txt, WRITE CREAT EXCL"]
    size -->|"Yes"| lstat["LSTAT target and .filepart: both fail, so 'not there'"]
    lstat --> part["OPEN my file.txt.filepart: Failure"]
    part -->|"logs 'trying to create target file'"| excl
    excl -->|"Failure, 'Open File Error'"| check["LSTAT my file.txt: Failure"]
    check --> dialog["Dialog: Cannot create remote file<br/>Error code: 4<br/>Error message from server: Open File Error<br/>with the common reasons for code 4<br/>Abort, Retry, Skip"]
```

- It sends nothing that a retry could change, and there is no automatic replacement: `ReplaceInvalidChars` applies to local names on download only (`CopyParam.cpp`, `ChangeFileName`).
- The way round it is a rename by the user, or a file mask in the transfer settings. Nothing about the dialog says that the name is the cause.
- The session goes on, so a batch with Skip continues with the next file.
- Each refused request records `file.name_rejected` with the area and no text, and the `OPEN` records it twice, because wolfSSH's `stat` before it counts too. See [the table](#each-refusal-and-its-event) for the sessions that record something else.
- Such a name that is already on the medium is foreign: it is not listed and cannot be fetched.
- The run confirmed the refusal for a space and for Korean characters (row 4 above). It did not try the other characters named in this section.

### What WinSCP shows an operator

From the first run. The server's own events are in [the table of refusals](#each-refusal-and-its-event).

| WinSCP shows | What it means here | Evidence |
| --- | --- | --- |
| `Error code: 4`, with the server text `Open File Error`, `Rename File Error` or `Remove File Error`, and a list of "common reasons": a name that exists, a directory that exists, another file system, a full file system, a user disk quota | **Any refusal**: a name outside the rule, a missing permission, a name that exists for the write-only role, or a bound. The server sends one general failure by design, and WinSCP's list never mentions a name rule or a permission, so **a user is pointed at disks and quotas**. The cause is in the server's security events for the refusals that have one | Rows 4, 5 and 9 above: the tester's report for names and refusals, log A for the existing name |
| Dates `30/12/1899` or `03/01/0001` for entries | **No time was sent** (F6). `30/12/1899` is the zero of WinSCP's date type, so it is how WinSCP shows an unset time, not a clock fault. Why a file in a read-only area showed `03/01/0001` is **not understood** | Row 10, from the tester's screen |
| `Error listing directory '/inbox'`, `Error code: 2`, right after an upload by the write-only role | The role cannot list. **The upload had finished** (`Transfer done` in the log, and the file complete when read by someone who may read). Check the result as an account that can read | Log C |
| `Cannot create remote file ... Error code: 4 ... Open File Error`, by the write-only role, for a name that was already in the area (by the server log, not by log A) | A name that exists is refused: see [above](#a-session-with-write-permission-alone). It is not a full disk | Log A |
| After a quiet period, the next click works with no prompt | The connection ended at the idle limit (900 s in the run, from the server side: see row 12). The tester reported that the window kept working with no prompt. **WinSCP logged in again, as seen from the server side. Whether it used stored credentials or a prompt is not recorded, and no WinSCP log shows it.** With keepalive on, see the next row | Row 12 |
| Keepalive on, and a connection idle past the idle limit | **An idle connection still ended at the idle limit** in the second run. For the first connection the tester reported a message that the connection was lost (its text was not captured), and a few seconds later it was connected again. A later disconnect was also reported, after which the tester waited 10 s and reconnected (chat report, no time, no log). So an operator should not expect WinSCP's keepalive to hold a session open on this server (by the replay: this run shows the outcome). Two timed connections of one run (and at least one more) ended at the limit, with one limit value of 120 s and no WinSCP log. The message was reported for the first connection only | [A second run](#a-second-run-with-keepalive-on), the tester's chat report |
| An error window with `Received SSH2_MSG_CHANNEL_DATA for nonexistent channel 0` and Abort, Reconnect and Help, in the middle of a large upload | An upload past the per-file limit: the session is dropped. See [the limit](#an-upload-past-the-per-file-limit-with-winscp) | Log B for the message, the tester's screenshot for the buttons |

## What is faked and what is untested

| Item | State |
| --- | --- |
| The provider | Faked on Host. On Linux, the proof server has its own loop. On `native_sim`, the real one, as merged in #265 |
| Authentication | Real key login on `native_sim`, through the real adapter and `crux_access`, on #192's test ports: a RAM store, a test random generator, and no PSA. On Linux, the proof server accepts every key and takes a session from the user name, with fake crypto |
| Host key | A test key generated at each boot, on `native_sim`. A file on Linux |
| Medium | Fake File in `Ssh.sftp`, the Linux File provider in the Linux run, and real FatFs over a RAM disk on `native_sim`. No card |
| What the cases see of the medium | On Linux, the directory itself. On `native_sim`, the same server's answers, to an account that may read each area |
| WinSCP | One run of 6.7 on Windows 11, and a second short run with keepalive on: see [the first run](#the-first-run) and [the second](#a-second-run-with-keepalive-on). Not run: download and delete as the write-only role, an upload of 100 KiB or more by that role, a rekey, two clients at once, any other WinSCP version or any other Windows client. Keepalive on was run once (two timed connections, and at least one more), without a WinSCP log and without decoding its packets. The cause of the drop past the per-file limit is open |
| Composition | The image is a test one. #242's composition is not used, and no product composition holds this yet |
| The target | Compiled, never linked with the service, never run. No board was touched |
| Concurrency | Two sessions at once were held open in the cases, but nothing stresses a race |
| Stack and heap on a target | Not measured. See [the simulation figures](#simulation-figures) |
| Throughput | On `native_sim` only: see [the receive budget](#how-this-fits-the-provider-of-192). Not on a target |
| Power loss | Not measured. Direct writes promise nothing across power loss, as the file service says |
| Leaks | ASan and UBSan ran during the Linux cases, but that server is killed, so no leak check runs at its end. On `native_sim` the heap comes back to its baseline |
| The `crux_ssh_callbacks` table on the real operations | Not linked on Host, as it needs a composition. `Ssh.CallbackTable` links the refusing ones |

## Limits

- **A listing error looks like the end of a listing.** wolfSSH ends the batch on any result but success and sends `EOF` for what it has (`wolfsftp.c:4043`, `:4080`), so a storage error or an expired session gives a shorter or empty list, with no error to the client. The port can do nothing about it. A sync tool could take that as files missing, and an event records a storage failure.
- **A listing is one NAME packet.** wolfSSH sets its end flag after the first reply, so the port cannot page. Heap and packet size grow with the entries, which `max_entries` bounds. By reading the code, and not measured, an entry with the longest name takes about 450 bytes of heap at the peak. A failed allocation for an entry also ends the list early.
- **A listing holds the service's only listing handle from `OPENDIR` until read to the end, and a client can keep it.** One is the default. Every other client's `OPENDIR` of an area then gets `NO_SUCH_FILE`, which reads as a missing directory, though the root still lists. A normal client holds it for as long as a read takes. A client that opens one and never reads holds it until it closes or disconnects, or its session ends. **The idle limit does not bound this**, because any request refreshes the session: a held listing outlived the idle limit by sending `STAT` (`idle_session_ends_and_activity_does_not`). The real bound is the session's absolute limit, and a reconnect can then take the handle again. So **any session with read permission on an area, a viewer for one, can stop an administrator listing any area**. See [decision 5](#decisions-for-review).
- **A long path can end the connection**, depending on the request: see [long paths](#long-paths).
- **A write-only session records a denial for each upload**, because wolfSSH's `stat` before `OPEN` needs the read permission. See [the table](#each-refusal-and-its-event) for each refusal. The port cannot tell this `stat` from a client's. Security event counts run higher than the operations.
- **No times.** F6 leaves them out. `SETSTAT` with a time reports success and stores nothing, so a client that preserves timestamps will not find them. The listing's long name always says "Jan  1 00:00". WinSCP shows `30/12/1899` or `03/01/0001`: see [what WinSCP shows an operator](#what-winscp-shows-an-operator).
- **`declared_size` stays zero.** wolfSSH parses the size that WinSCP sends with `OPEN`, and hands it to no port. So an upload that cannot fit fails when it crosses the limit, not before the data. **With WinSCP that ends the session**: see [the next section](#an-upload-past-the-per-file-limit-with-winscp).
- **Direct writes only.** A managed-only area refuses every SFTP write, as the mode table says.

### An upload past the per-file limit with WinSCP

**For operators.** A WinSCP upload larger than the per-file limit does not end with an error for that file. **It ends the session**, and the cause is open.

| What | State |
| --- | --- |
| The session | Dropped. Log B records `Received SSH2_MSG_CHANNEL_DATA for nonexistent channel 0` and ends at `Answer: Abort`. The error window with Abort, Reconnect and Help is from the tester's screenshot. According to the tester, after Abort the file list is gone until Reconnect, and Reconnect works |
| The partial file | **It stays.** It was `<name>.filepart` in the `data` area, 1,045,504 bytes both times, and it counts against the area's bytes and entries. A later create or write that would hit a limit may remove it, under the stale rule of the file service (F5), because the `data` area of this image declares `.filepart`. An area with no partial suffix never removes it |
| Stored data | Nothing was seen to be damaged, but **the only thing checked was the size of the partial file**, which is exactly the writes that were accepted. The refused write stored nothing, as the file service says. The partial's content was not compared with the source and the files around it were not compared. Because the cause is open, and one candidate is damage in the server's memory, "no damage" is not shown |
| What to tell a user | Keep a file under the per-file limit (1 MiB in this image), reconnect, and remove the partial |
| How often | Twice out of two with WinSCP 6.7 |
| The cause | **Not understood.** No fix is made: see decision 18 |

```mermaid
sequenceDiagram
    participant W as WinSCP 6.7
    participant S as Crux image
    W->>S: LSTAT of the target and of the .filepart
    S-->>W: STATUS 4 for each, read as not there
    W->>S: OPEN the .filepart
    S-->>W: HANDLE
    loop 64 writes, up to 5 outstanding
        W->>S: WRITE 16,336 bytes
        S-->>W: STATUS 0
    end
    W->>S: WRITE at offset 1,045,504, which would cross 1 MiB
    Note over S: The file service refuses it whole and writes nothing
    S-->>W: STATUS 4, Write File Error
    Note over W: Four more writes are already sent
    Note over W: 18 ms later WinSCP logs a CHANNEL_DATA message it does not accept (field 0). Received SSH2_MSG_CHANNEL_DATA for nonexistent channel 0, fatal
    W->>S: Closes the connection
    Note over W: Error window with Abort, Reconnect and Help, from a screenshot
    Note over S: The .filepart stays with 1,045,504 bytes
```

What the log shows (WinSCP 6.7 Debug 1 log, run of 7 November 2026, log B):

| Observation | Value |
| --- | --- |
| File | 2,077,166 bytes, uploaded to a `.filepart` name, which is the path for 100 KiB or more |
| Limits | 1 MiB per file, 3 MiB per area, 16 entries per area. The `data` area declares `.filepart` as its partial suffix |
| WinSCP's writes | `SSH_FXP_WRITE` of 16,336 bytes each, up to 5 outstanding |
| Accepted | 64 writes, each answered with status 0: 64 x 16,336 = 1,045,504 bytes, which is the size of the partial file |
| Refused | The 65th write, at offset 1,045,504, got status 4, "Write File Error". It is refused whole because it would cross 1,048,576, as the file service contract says. OpenSSH with 32 KiB writes stopped at exactly 1,048,576 |
| After the refusal | Four more writes were already sent (the last at offset 1,110,848). About 18 ms after the status, WinSCP logged `Received SSH2_MSG_CHANNEL_DATA for nonexistent channel 0`, an SSH-level error that it treats as fatal, and closed the connection |

**What the investigation found.** This is the author's investigation of 7 November 2026, kept as unpublished notes and summarised here, so it cannot be checked from this repository. It could not reproduce the drop, and no code that it read explains it:

| Question | Answer |
| --- | --- |
| Reproduced without WinSCP? | **No.** About 40 runs, on the image and on the Linux proof server, with a client that uses channel id 256 as WinSCP does and keeps reading after the first error, with delays and chunking, and with PuTTY 0.81 `psftp`, whose SSH layer WinSCP derives from. **In every paramiko run** the refusal got status 4, later writes got status 4, every server message named the client's channel, and the session went on. `psftp` stops at the first error, so channel messages are not applicable to it. The runs with a read-only account and a closed handle tested other error paths, not the limit |
| What does the message mean? | WinSCP prints the first 32-bit field of the packet. Its first channel id is 256. PuTTY's reader returns 0 for a packet shorter than 4 bytes. So the packet had recipient 0 **or** a body shorter than 4 bytes |
| Is the packet the server's? | Probably, as an inference with no capture: with the cipher and MAC of the run (aes128-ctr, HMAC-SHA-256) a packet changed on the way would have failed the MAC first |
| Does the code send one? | None that it read. The refused write takes the same path as an accepted one, except for the status text, and every data packet is addressed to the channel's peer id, which is set once when the channel opens |
| Where is it then? | **Not located.** The candidates are memory damage in the server that zeroes a field, a path that only WinSCP's traffic reaches, and the network path of the test. Each is a hypothesis, not a finding |

**What would settle it**, and what it needs:

| Step | Needs |
| --- | --- |
| The same WinSCP upload against a debug image with wolfSSH logging on (a build setting, `DEBUG_WOLFSSH`, which needs no edit to wolfSSH, as the investigation used on a scratch copy), and the server log | The tester at the Windows machine, and a rebuilt image |
| The same upload with PuTTY's `psftp.exe -sshrawlog`, which logs the decrypted packet that breaks the session | The tester at the Windows machine |
| A diagnostic log of the peer channel and payload size inside wolfSSH's data send | **A patch to wolfSSH, which needs the owner's approval**, as issue #194 says, even for a scratch build |

**Not run:** a retry of the same upload (it would find the partial file), an upload past the limit as the write-only role, an area with no partial suffix, and any WinSCP other than 6.7.

## Decisions for review

Each was taken without asking, as the narrow and safer choice.

| # | Decision | Why | Alternative |
| --- | --- | --- | --- |
| 1 | A refused `OPEN` is a general failure | No other path without a patch, and it hides denial from absence | Ask the owner to approve a wolfSSH patch |
| 2 | The port is selected, not the mount fallback | It works | None needed |
| 3 | A composition opts in with `CRUX_SSH_SFTP_FILE_SERVICE`, and without it SFTP is refused | So the settings probe and #192 keep linking with no file system, and an image that forgets is closed. No shared file changes | Default on, and the probe opts out, one line in a shared file |
| 4 | 2 files and 2 listings per session, 4 sessions | Each tested client holds one at a time | Larger, at the cost of one session starving others |
| 5 | A listing holds its service handle from `OPENDIR`, not from the first read | wolfSSH turns a read failure into an empty list, so a busy service would look like an empty area, and a sync tool could act on that. A wrong listing is worse than a stalled one. **The cost is that a viewer can stop an administrator listing, up to the session's absolute limit** | Take it at the first read, with a chance of false empty lists. Or a composition raises `CRUX_FILE_SERVICE_DIR_MAX`, which only helps with `CRUX_FILE_DIR_POOL_SIZE` raised too: File's pool is 2, and without that a second listing takes the handle that the stale rule needs. Neither removes the risk, as each session can hold two |
| 6 | `wolfSSH_SFTP_SetConfinePath` is not used | At `/` it has no effect, and the second defence is `resolve` | Put the areas under a prefix, which changes what clients see |
| 7 | Flag sets the service states no meaning for are refused | Narrow | Follow POSIX for each |
| 8 | `SETSTAT` is accepted with no effect for an area the session can see, with no file check | A write-only client must finish an upload, and it costs no storage | Check the file, which breaks write-only uploads |
| 9 | Size and owner in `SETSTAT` are `OP_UNSUPPORTED` | Silently ignoring a truncate would be a false success | Accept them |
| 10 | `WOLFSSH_MAX_FILENAME` is 256, up from the refusing port's 64 | 64 could not hold the 80 bytes the service accepts, and more room means more long names get a refusal and not a disconnect | Lower, for the stack |
| 11 | A refused name reaches the service by cutting the first component, only to record the event, with a `stat` if the session may read and a rename of the text onto itself if it may only write | `resolve` is the only parser and emits nothing, and the contract wants the event. The text goes only to the rule that refuses it, and the path is refused whatever the answer | Skip the event |
| 12 | Every request that reaches the port counts as activity, refused ones included | It is a client that is there, and the absolute limit still applies | Count only those that succeed |
| 13 | Rights: file `0644`, `0444` or `0200`, area `0755`, `0555` or `0300`, root `0555` | The design derives them from the session, and these are never all set, which WinSCP would read as a link | Other values |
| 14 | A Linux proof server and its cases live in `tests/ssh/sftp/openssh` | The issue asks for proof first, and #192 did not exist when this began. It builds only from its script. It stays because a run takes seconds, and because only it can look at the medium itself | Remove it now that the image runs |
| 15 | The `native_sim` image is in `tests/ssh/sftp/native_sim`, and reuses #192's test Access ports and keys by path | So that it stays within the files of #194, and adds no copy of them. It needs the provider, which `main` holds since #265, and on a tree without it the script says so | Add cases to `tests/zephyr/ssh_provider`, as #192's document suggests, which changes #192's files |
| 16 | The port records direct use of a hidden area with a service `stat` that stops at the permission, and sends `REMOVE` and `RENAME` of an area to the service with an empty name | So that Hiding holds with no new interface, and the one rule that decides stays in the service | A new service call for each |
| 17 | No new event identifier for the refusals with none, and a rule outside the file service does not emit one | The event port is the service's, and a new identifier changes the service and #182, outside the files of #194 | Add identifiers in [#270](https://github.com/wonny-labs/crux-pilot/issues/270), as a shared change under G-A and #182 |
| 18 | The upload past the per-file limit is documented as a limit, and no code changes for it | No cause was found, so a change would be a guess. A debug build with wolfSSH logging needs no wolfSSH edit. Only a diagnostic patch inside wolfSSH would, and that needs the owner's approval | A diagnostic wolfSSH build, or a composition change (a larger window, which costs heap per connection), neither with evidence that it helps |

## Follow-ups on #179

They are in the file service, outside the files of #194, and none blocks this.

| Item | Effect here |
| --- | --- |
| A STAT hook test for `rename` during the stale rule | None. It is a test of the service |
| `scripts/test-file-service.sh` is not in CI | None |
| A client removal during the stale rule gives one false `ERR_LIMIT`, and a retry succeeds | One write can fail with "Write File Error". An SFTP client stops that transfer, and a retry resumes it. The service's rule stands |

## Acceptance

| Criterion of #194 | State |
| --- | --- |
| Prove the port first | **Met**, with the unedited wolfSSH handlers |
| Attributes, directory reads and permission errors | **Met**. `WOLFSSH_SFTP_ERRNO` does not exist, and permission errors are as above |
| Virtual root and direct write mode | **Met** |
| Confinement as a second defence | **Not as written**: [decision 6](#decisions-for-review) |
| Traversal, permission, capacity and disconnect with OpenSSH on `native_sim` | **Met** on `main`, with the provider as merged in #265: 28 cases and the idle case |
| Rename and partial files follow the file service | **Met** on Host, on Linux and on `native_sim`. The stale rule's removal is Host only |
| A recorded decision if the port cannot do this | **Recorded**: it can, and the fallback is not selected |
| WinSCP | **A first run and a second, short run are recorded, and two things stay open.** WinSCP 6.7 on Windows 11 ran against the `native_sim` image of `main` on 7 November 2026 (see [the first run](#the-first-run), [predicted and observed](#predicted-and-observed) and [a second run](#a-second-run-with-keepalive-on)). Rows 1 to 7 and 10 to 12 matched, and row 13 is consistent as an outcome only (server side, mechanism not shown by the run). **Open:** an upload past the per-file limit drops the session, cause not understood ([the limit](#an-upload-past-the-per-file-limit-with-winscp)); and download and delete as the write-only role were not run. **Run once (two timed connections, and at least one more):** keepalive on, with no WinSCP log and no decoded packets. Why a write-only upload to an existing name is refused is consistent with an exclusive create and is not recorded as a decision |

## How to run

| Command | Needs | Result here |
| --- | --- | --- |
| `ctest -R '^Ssh\.sftp'` in the Host build | Nothing more | In the PR |
| `scripts/test-ssh-sftp-openssh.sh` | `wolfssl` and `wolfssh` at their pins, `ssh`, `sftp`, `openssl` | In the PR |
| `scripts/test-ssh-sftp-native.sh` | That, and `fatfs`. The provider is in `main` since #265 | In the PR |
| `scripts/test-ssh-wolf-settings.sh --target --native`, then `scripts/test-ssh-sftp-compile.sh` | The `crux-secure-management` west group | In the PR |
