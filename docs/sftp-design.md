# SFTP over the file service

Status: implemented on Host, Issue #194, for review. [Gate #183](https://github.com/wonny-labs/crux-pilot/issues/183#issuecomment-6617722497) has passed, and the owner approved this work in advance. It authorises no hardware operation.

This document says what is built, what is faked, what is untested, and which decisions were taken for lack of an answer. The evidence is of four kinds, and none implies another.

| Kind | What ran | What it shows |
| --- | --- | --- |
| Host, fake provider | `Ssh.sftp`, on the real file service and Access core, with fake File and crypto | What the port owes the file service |
| Host, real wolfSSH | `scripts/test-ssh-sftp-openssh.sh`: OpenSSH 9.6 against the unedited wolfSSH handlers on Linux, through the real port and file service, on the Linux File provider | That the port works with the real handlers and a real client. **The server stands in for the provider** |
| Target compile | `scripts/test-ssh-wolf-settings.sh` and `scripts/test-ssh-sftp-compile.sh` | That it compiles for RT1060 and `native_sim`. Nothing ran |
| Read from source | wolfSSH v1.6.0 `8221e4e`, WinSCP `246bbd4` | What each of them should do. Each statement says so |

**Not run: OpenSSH on `native_sim`, which is the acceptance criterion of #194.** It needs the provider of #192, which had no branch when this was written. See [Acceptance](#acceptance).

## What is built

```mermaid
flowchart TB
    client["OpenSSH sftp or WinSCP"] -->|SSH| provider["Provider, #192<br/>(on Linux, the proof server)"]
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
| `tests/ssh/sftp/openssh/` | The proof server and the cases | Itself |

**The port is enough, so the mount fallback is not selected.** That is the decision the issue asks for. Each macro receives the session through `fs`, with no thread-local state, and no wolfSSH file is edited.

### Requests and what answers them

Each statement about a status was seen on the wire in `wire_a_refused_open_is_a_general_failure` or a case beside it.

| SFTP request | Port | Service | On refusal the client gets |
| --- | --- | --- | --- |
| `INIT`, `REALPATH` | None. `WGETCWD` gives `/` once | None | Handled by wolfSSH. A path of 256 bytes or more ends the connection |
| `OPENDIR`, `READDIR`, `CLOSE` | `WOPENDIR`, `SFTP_Name_readdir`, `WCLOSEDIR` | `area_next` at the root, `dir_*` in an area | `OPENDIR`: `NO_SUCH_FILE`. `READDIR`: see [listing limits](#limits) |
| `STAT`, `LSTAT` | `SFTP_GetAttributes` | `stat`, `area_next` | `FAILURE`, "STAT error" or "LSTAT error" |
| `FSTAT` | `SFTP_GetAttributes_Handle` | `size` | `FAILURE`, "STAT error" |
| `OPEN` | `WOPEN` | `open`, after a `stat` that wolfSSH always does first | `FAILURE`, "Open File Error". A directory: `FAILURE`, "Not A File" |
| `READ` | `WPREAD` | `read`. Past the end is `EOF` | `FAILURE` |
| `WRITE` | `WPWRITE`, or `WWRITE` for `APPEND` | `write` | `FAILURE`, "Write File Error" |
| `CLOSE` | `WCLOSE` | `close` | `FAILURE` if the close reports an error |
| `REMOVE`, `RENAME` | `WREMOVE`, `WRENAME` | `remove`, `rename`. Across areas stops in the port | `FAILURE` |
| `SETSTAT`, `FSETSTAT` | `WCHMOD`, `WSETTIME` and the handle forms | None: accepted with no effect for a visible area | `FAILURE`. Size or owner: `OP_UNSUPPORTED` |
| `MKDIR`, `RMDIR` | Macros give `WS_PERMISSIONS` | None | `PERMISSION_DENIED`, always |
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

**As `SSH_FX_FAILURE`, code 4, with the text "Open File Error". This is settled, and a permission status is not available without patching wolfSSH.**

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
| Accept the general failure | Every refusal looks the same to a client. The cause is in a security event: a permission denial from `crux_access_check`, `file.name_rejected` or `file.storage_failed`. A limit, a busy name and a read-only area record none, as the file service states | **Chosen** |
| Set the confinement root per request | Not possible: it is one string, and an area and a hidden area sit side by side | No |
| Patch the handler | A security-relevant fork, which the design rejects and the issue forbids without owner approval | Not selected. It would be the only way to a permission status |
| Rewrite the status in the provider | Needs a parser on the data path, for one word | No |

It has one benefit: **a client cannot tell a denial from a missing file**, which the file service contract asks of an adapter for a hidden area, and which holds here for `OPEN`, `STAT` and `REMOVE`. The cost falls on the operator: WinSCP adds its own text about Error code 4, whose list of common reasons has no mention of permissions, so a denied user is told about full disks. User documentation should say so.

`REMOVE` and `RENAME` work the same way: `:4996` and `:5088` look only at the sign. `MKDIR` and `RMDIR` are the only two where a port can choose, and the port always says permission denied, since there are no directories to make.

## Traversal

| Layer | What it does | Evidence |
| --- | --- | --- |
| wolfSSH `wolfSSH_RealPath` | Resolves `.` and `..` lexically, and stops at `/`, so `/data/../../etc/x` arrives as `/etc/x` | `sftp_traversal_reaches_nothing`, with real `get`, `put` and `cd` |
| `crux_file_service_resolve`, the only parser | Refuses anything but `/area`, `/area/` and `/area/name`, and any name that fails the rule | `case_paths_outside_the_rules_reach_nothing` sends 26 raw shapes to every operation, as if wolfSSH had not cleaned them |
| File | Roots and flat names | The file service's own tests |

**`wolfSSH_SFTP_SetConfinePath` adds nothing here, and the port does not ask for it.** The design calls for it as a second defence. But the virtual root is the client's whole namespace, so the root would be `/`, and at `/` wolfSSH treats a session as unconfined (`:2057`). The real second defence is that `resolve` refuses what `RealPath` should have removed, and a test sends it those shapes. See [decision 6](#decisions-for-review).

## The session, and how it ends

| Event | What happens | Evidence |
| --- | --- | --- |
| A request reaches the port | `crux_access_touch`, ignoring the result, so that the service stays the one that decides | `case_activity_keeps_the_session_and_idleness_ends_it` |
| Nothing for the idle limit | The provider's poll of `session_alive` finds it expired, and it ends the connection | The same, and `idle_session_ends_and_activity_does_not` |
| `REALPATH`, the WinSCP keepalive | It reaches no port, so it does not refresh the session, and a client that only keeps alive still ends at the limit | `idle_session_ends_and_activity_does_not` |
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
| `WOLFSSH_MAX_FILENAME`, each path buffer | 256 | The connection ends |

A descriptor holds a generation of the session slot. wolfSSH can close what a client left open as it frees the session, so a descriptor from an earlier connection must not match the slot's next owner, whose files the service would close with no session check. It is a defence: the contract already has the provider free wolfSSH first.

## What the provider of #192 needs from this

These are facts that the proof server had to get right, for #192 to take or disprove.

| Item | Why |
| --- | --- |
| Call `crux_ssh_sftp_open` after `wolfSSH_accept` returns `WS_SFTP_COMPLETE`, and do not call `wolfSSH_SFTP_accept` again | `wolfSSH_accept` has already answered `INIT` by then (`src/ssh.c`, `ACCEPT_SERVER_CHANNEL_ACCEPT_SENT`). A second call returns `WS_FATAL_ERROR` with no error set. No request reaches the port before `wolfSSH_SetFilesystemHandle`, as the loop has not run yet |
| Call `wolfSSH_free` before `crux_ssh_sftp_close` | It closes open files through the port (`src/ssh.c:223`), and the header says that no file operation follows `sftp_close` |
| Keep the channel's maximum packet at 32768 or below, which is `DEFAULT_MAX_PACKET_SZ` | WinSCP sizes a read by throughput, up to that limit (`SftpFileSystem.cpp:2243`). A `READ` above `WOLFSSH_MAX_SFTP_RW` makes wolfSSH return with no status (`wolfsftp.c:4650`), which ends the session |
| `CRUX_SSH_SFTP_FILE_SERVICE` before including `wolf_ssh_settings.cmake` | Otherwise the image refuses SFTP. Then it must link `crux_file_service`, an Access core, a File provider and `crux_file_service_port_event` |
| Treat `wolfSSH_SFTP_read` returning an error with nothing to send as the end | A path of 256 bytes or more, or a bad `READ` size, comes back this way |
| Measure the stack with these buffers | `RENAME` holds three 256-byte paths, and `OPEN` two |
| Measure the heap with a full listing | See [listing limits](#limits) |

## WinSCP

No Windows machine is available, so **WinSCP has not run, and the recorded run stays open**. What follows was read from its source at `246bbd4` (`source/core/SftpFileSystem.cpp` unless named), and each request sequence below is replayed over real OpenSSH in `tests/ssh/sftp/openssh/cases.py`. A replay shows that the server answers those requests as expected, and it cannot show what WinSCP then does.

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
| Block size follows throughput, limited by the channel's maximum packet | `:2243` to `:2296` | Source | See [what the provider needs](#what-the-provider-of-192-needs-from-this) |
| `REALPATH /` as a keepalive, only if the session is set to send a dummy command | `:2063` to `:2070` | Replay | Does not refresh the idle timer |
| Uses `RENAME`, not `posix-rename`, by default, and no `limits@openssh.com` | `:3352`, `:3884` | Source | This server advertises neither |

**Not checked, and only a recorded run can settle them:** what it shows for a file with no time attribute (it gets the long name, which always says "Jan  1 00:00", so sorting and "synchronize by time" are suspect); how the listing's long name parses; the exact order of its pipelined requests; and all of its dialogs.

### A file name with a space (F4)

Accepted decision F4 keeps the name rule, so WinSCP cannot upload `my file.txt`. From its source, with each request refused as shown in `winscp_name_with_a_space_is_refused_at_every_step`:

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
- The way round it is a rename by the user, or a file mask in the transfer settings. Nothing about the dialog says that the space is the cause.
- The session goes on, so a batch with Skip continues with the next file.
- Each refused request records `file.name_rejected` with the area and no text, and the `OPEN` records it twice, because wolfSSH's `stat` before it counts too.
- A name with a space that is already on the medium is foreign: it is not listed and cannot be fetched.

## What is faked and what is untested

| Item | State |
| --- | --- |
| The provider | Faked on Host. On Linux, the proof server has its own loop, which is not #192's |
| Authentication | The proof server accepts every key, and takes a session from the user name. No credential is checked. `crux_access` runs with fake crypto |
| Medium | Fake File in `Ssh.sftp`, and the Linux File provider in the OpenSSH cases. Not FatFs, and no card |
| `native_sim`, Zephyr's sockets and threads | **Not run** |
| The target | Compiled, never linked with the service, never run. No board was touched |
| Concurrency | Two sessions at once were held open in the cases, but nothing stresses a race |
| Stack and heap | Not measured |
| Throughput, power loss | Not measured. Direct writes promise nothing across power loss, as the file service says |
| Leaks at server exit | The server is killed, so a leak check does not run at its end. ASan and UBSan did run during the cases |
| The `crux_ssh_callbacks` table on the real operations | Not linked on Host, as it needs a composition. `Ssh.CallbackTable` links the refusing ones |

## Limits

- **A listing error looks like the end of a listing.** wolfSSH ends the batch on any result but success and sends `EOF` for what it has (`wolfsftp.c:4043`, `:4080`), so a storage error or an expired session gives a shorter or empty list, with no error to the client. The port can do nothing about it. A sync tool could take that as files missing, and an event records a storage failure.
- **A listing is one NAME packet.** wolfSSH sets its end flag after the first reply, so the port cannot page. Heap and packet size grow with the entries, which `max_entries` bounds. By reading the code, and not measured, an entry with the longest name takes about 450 bytes of heap at the peak. A failed allocation for an entry also ends the list early.
- **A listing holds the service's only listing handle from `OPENDIR` until read to the end.** One is the default. A second client's `OPENDIR` gets `NO_SUCH_FILE`, for as short a time as a client takes to read, so a client's retry is the cure. A client that opens one and never reads holds it until it closes, disconnects or goes idle. A session with read permission can therefore block listing for others. See [decision 5](#decisions-for-review).
- **A path of 256 bytes or more ends the connection**, in wolfSSH, with no status. Under that, a long name gets the normal refusal. WinSCP can send one, for a long local name.
- **A write-only session records a denial for each upload**, because wolfSSH's `stat` before `OPEN` needs the read permission. A refused `OPEN` by a session with neither permission records two, and a refused name two. The port cannot tell this `stat` from a client's. Security event counts run higher than the operations.
- **No times.** F6 leaves them out. `SETSTAT` with a time reports success and stores nothing, so a client that preserves timestamps will not find them. The listing's long name always says "Jan  1 00:00".
- **`declared_size` stays zero.** wolfSSH parses the size that WinSCP sends with `OPEN`, and hands it to no port. So an upload that cannot fit fails when it crosses the limit, not before the data.
- **Direct writes only.** A managed-only area refuses every SFTP write, as the mode table says.

## Decisions for review

Each was taken without asking, as the narrow and safer choice.

| # | Decision | Why | Alternative |
| --- | --- | --- | --- |
| 1 | A refused `OPEN` is a general failure | No other path without a patch, and it hides denial from absence | Ask the owner to approve a wolfSSH patch |
| 2 | The port is selected, not the mount fallback | It works | None needed |
| 3 | A composition opts in with `CRUX_SSH_SFTP_FILE_SERVICE`, and without it SFTP is refused | So the settings probe and #192 keep linking with no file system, and an image that forgets is closed. No shared file changes | Default on, and the probe opts out, one line in a shared file |
| 4 | 2 files and 2 listings per session, 4 sessions | Each tested client holds one at a time | Larger, at the cost of one session starving others |
| 5 | A listing holds its service handle from `OPENDIR`, not from the first read | wolfSSH turns a read failure into an empty list, so a busy service would look like an empty area, and a sync tool could act on that. A wrong listing is worse than a stalled one | Take it at the first read, with a stall-free server and a chance of false empty lists. A composition can also raise `CRUX_FILE_SERVICE_DIR_MAX` |
| 6 | `wolfSSH_SFTP_SetConfinePath` is not used | At `/` it has no effect, and the second defence is `resolve` | Put the areas under a prefix, which changes what clients see |
| 7 | Flag sets the service states no meaning for are refused | Narrow | Follow POSIX for each |
| 8 | `SETSTAT` is accepted with no effect for an area the session can see, with no file check | A write-only client must finish an upload, and it costs no storage | Check the file, which breaks write-only uploads |
| 9 | Size and owner in `SETSTAT` are `OP_UNSUPPORTED` | Silently ignoring a truncate would be a false success | Accept them |
| 10 | `WOLFSSH_MAX_FILENAME` is 256, up from the refusing port's 64 | 64 could not hold the 80 bytes the service accepts, and more room means more long names get a refusal and not a disconnect | Lower, for the stack |
| 11 | A refused name reaches the service by cutting the first component, only to record the event | `resolve` is the only parser and emits nothing, and the contract wants the event. The text goes only to the rule that refuses it, and the path is refused whatever the answer | Skip the event |
| 12 | Every request that reaches the port counts as activity, refused ones included | It is a client that is there, and the absolute limit still applies | Count only those that succeed |
| 13 | Rights: file `0644`, `0444` or `0200`, area `0755`, `0555` or `0300`, root `0555` | The design derives them from the session, and these are never all set, which WinSCP would read as a link | Other values |
| 14 | A Linux proof server and its cases live in `tests/ssh/sftp/openssh` | The issue asks for proof first, and #192 did not exist. It builds only from its script, and #192 can reuse the cases | Wait for #192 |

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
| Prove the port first | **Met** on Linux with the unedited wolfSSH handlers |
| Attributes, directory reads and permission errors | **Met** on Linux. `WOLFSSH_SFTP_ERRNO` does not exist, and permission errors are as above |
| Virtual root and direct write mode | **Met** on Linux |
| Confinement as a second defence | **Not as written**: [decision 6](#decisions-for-review) |
| Traversal, permission, capacity and disconnect with OpenSSH on `native_sim` | **Not met.** All pass with OpenSSH on Linux. `native_sim` needs #192 |
| Rename and partial files follow the file service | **Met** on Host and Linux. To be repeated on `native_sim` |
| A recorded decision if the port cannot do this | **Recorded**: it can, and the fallback is not selected |
| WinSCP | **Open.** Source and replay only |

## How to run

| Command | Needs | Result here |
| --- | --- | --- |
| `ctest -R '^Ssh\.sftp'` in the Host build | Nothing more | In the PR |
| `scripts/test-ssh-sftp-openssh.sh` | `wolfssl` and `wolfssh` at their pins, `ssh`, `sftp`, `openssl` | In the PR |
| `scripts/test-ssh-wolf-settings.sh --target --native`, then `scripts/test-ssh-sftp-compile.sh` | The `crux-secure-management` west group | In the PR |

Once #192 has a working provider, the same `cases.py` needs only `--port`, a key the server accepts, and the areas of `proof_server.c`, with `--directory` on a host directory that the image reaches. It can then run on `native_sim`.
