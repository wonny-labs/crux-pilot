# SSH provider contract

Status: proposal for review, Issue #205, part of gate #183. It fixes what the SSH, CLI and SFTP sessions share. Nothing
here is adopted until it is reviewed, and it authorises no hardware operation.

The evidence is of three different kinds. Host tests run the fake provider and the conformance cases. Build results come
from compiling and linking probe images, which were never run. Statements about wolfSSH behaviour come from reading its
source at v1.6.0 (`8221e4e`), and no real provider exists yet. See [Evidence and limits](#evidence-and-limits).

## The interface

[`components/ssh/src/crux_ssh_provider.h`](../components/ssh/src/crux_ssh_provider.h) is the private interface. A
provider offers `start`, `stop` and channel `read`, `write` and `close`. Crux supplies credential, `shell`, `exec` and SFTP
callbacks, and every other request is refused with no callback. The header documents each result.

It is private: there is no `components/ssh/include`, no `crux::ssh` target, and the `crux_ssh_provider` target is linked
PRIVATE by Crux adapters only. It names no SSH library type, Zephyr header or access control type, so the CLI and SFTP
pieces can build and test on the Host with no SSH library. `scripts/check-ssh-boundaries.py` checks all of this, and runs
in the Host CTest as `Ssh.Boundaries`.

SSH user keys are Ed25519 and ECDSA P-256 only, by owner decision. The key type enum has no RSA value, the provider refuses
any other key before a callback, and `crux_wolfssh_settings.h` removes RSA from wolfSSH. wolfSSL keeps RSA for TLS.

## Ownership

### Threads and the wolfSSH loop

The provider owns every thread and all of the wolfSSH state. No other code calls wolfSSH.

| Item | Owner | Rule |
| --- | --- | --- |
| Listening socket and accept | Provider | A connection past `max_connections` is closed without a callback. |
| One worker thread per connection | Provider | At most `max_connections` workers and one listener. Stack sizes come from measurement in #192. |
| `WOLFSSH_CTX`, `WOLFSSH`, socket callbacks | Provider | Each `WOLFSSH` is used by its own worker only. |
| `wolfSSH_accept`, `wolfSSH_worker`, stream calls, the SFTP loop | Provider | They run on the worker. |
| Attempt and unauthenticated time limits | Provider | From `crux_ssh_config_t`. |
| Algorithm lists, strict key exchange, host key loading | Provider, #192 | The host key bytes arrive through the configuration, borrowed until `stop` returns. |
| Password verification and sessions | Access adapter calling `crux_access`, #192 | Runs on the worker of its connection, so it stalls no other connection. |
| Line handling, command execution, exit status | CLI, #193 | Runs inside the `shell` or `exec` callback, on the worker. It creates no thread. |
| File operations | SFTP port, #194 | Run on the worker, called by wolfSSH with the session's file handle. |

Every Crux callback runs on the thread of its connection, with no provider lock held, and can block that thread only. So a
slow client or a slow verification holds one connection and no other. A callback must not call `stop`, and a channel must
not be used after its session callback returns.

Because the typed wolfSSH channel callbacks run while a packet is being parsed, and `wolfssh/ssh.h` says that nothing may
re-enter the receive side there, the provider's wolfSSH callbacks only record a request. The worker calls the Crux `shell`,
`exec` or `sftp_open` callback after `wolfSSH_accept` has returned. `wolfSSH_SetUserAuth` and `wolfSSH_SetUserAuthResult`
do call Crux, because the login decision belongs there, and they have no channel to use. wolfSSH's success value is zero,
so #192 maps `CRUX_SSH_AUTH_ACCEPTED` explicitly and every other value to failure.

### One session per connection

A connection carries one session channel, and the first `shell`, `exec` or SFTP request starts its session. The provider
refuses a second channel and ends the connection when the session ends. This matches the flow of `wolfSSH_accept` and is
the simplest bound on memory. It rules out OpenSSH connection sharing (`ControlMaster`): each `ssh` or `sftp` client process
then needs its own connection and slot. A later change could relax this, and it would be a change to this contract.

### From a connection to Crux

The header names two opaque types and defines neither. Each is an incomplete type that its owner defines in its own source,
so nothing in the interface depends on `crux_access`, which is not merged yet.

| Type | Defined by | Provider's part | Life |
| --- | --- | --- | --- |
| `crux_ssh_attachment_t` | Access adapter, #192 | Stores the pointer and passes it to each session callback and to `connection_closed` | From an accepted credential to `connection_closed`, which runs exactly once and last |
| `crux_ssh_file_handle_t` | SFTP port, #194 | Passes the pointer to `wolfSSH_SetFilesystemHandle` | From `sftp_open` to `sftp_close` |
| `crux_ssh_channel_t` | Each provider | Its own | During one session callback |

A session attaches to a file handle like this:

1. A credential callback accepts, and sets an attachment that holds the access session.
2. The client asks for the SFTP subsystem. The provider calls `sftp_open` with the attachment.
3. #194 returns a file handle that holds the access session or the attachment.
4. The provider calls `wolfSSH_SetFilesystemHandle`. wolfSSH passes that handle as the `fs` argument of each port macro, so
   each file operation knows its session with no thread-local state.
5. When SFTP ends, `sftp_close` runs, then `connection_closed`.

### How #192 connects this to `crux_access`

The adapter defines `struct crux_ssh_attachment` to hold a `crux_access_session_t` and a `crux_access_origin_t`, in a
static pool of `max_connections`. It is a small mapping, to be checked against the merged access header:

| Provider callback | Adapter call | Accepted when |
| --- | --- | --- |
| `password` | `crux_access_login_password` | It returns `CRUX_ACCESS_OK`. A slot is taken, and the session goes into the attachment. |
| `key_check` | `crux_access_key_is_authorised`, passing `signed_request` | It returns `CRUX_ACCESS_OK`. It creates nothing. |
| `key_verified` | `crux_access_login_verified` | It returns `CRUX_ACCESS_OK`, as for `password`. |
| `key_failed` | `crux_access_report_key_failure` | Not applicable |
| `connection_closed` | `crux_access_logout`, then the slot is freed | Not applicable |

The origin uses `CRUX_ACCESS_CHANNEL_SSH`, with the peer address as its source. For IPv6 the adapter passes the /64 prefix,
as the access contract says. Every access result other than success is a denial on the wire, including
`CRUX_ACCESS_ERR_RETRY_LATER` and `CRUX_ACCESS_ERR_BUSY`. The user name reaches `crux_access` already checked for length and
embedded NUL, and `crux_access` checks its character set. The CLI and SFTP pieces get the session from the attachment
through one accessor that #192 publishes in its adapter header.

The session goes to an SSH connection, and `crux_access_touch` is the caller's job. #193 and #194 call it on user activity.

### Which files each session changes

| Session | May change | Notes |
| --- | --- | --- |
| Provider and authentication, #192 | `components/ssh/ports/zephyr/` other than the shared files below, `components/ssh/CMakeLists.txt`, `tests/ssh/` other than the shared files below, new `tests/zephyr/ssh_*`, new `scripts/test-ssh*.sh` | The provider source and the access adapter. It also owns the callback table, and until #193 and #194 land their entries refuse. |
| CLI over SSH, #193 | New `components/ssh/cli/` and `tests/ssh/cli/`, plus one line each in the callback table for `shell` and `exec` | It exports functions with the exact callback signatures. It uses only `read`, `write` and `close`, with the fake provider on the Host. |
| SFTP, #194 | New `components/ssh/sftp/` and `tests/ssh/sftp/`, plus one line each in the callback table for `sftp_open` and `sftp_close` | It owns the real `myFilesystem.h`, on the include path of the wolfSSH sources. The one in `tests/zephyr/ssh_wolf_probe/port/` is a stand-in for build evidence. |
| Shared, by one reviewed PR that all sessions rebase onto | `components/ssh/src/crux_ssh_provider.h`, `components/ssh/ports/zephyr/crux_wolfssh_settings.h`, `wolf-ssh.conf`, `wolf_ssh_settings.cmake`, and `tests/ssh/{ssh_test_client.h,fake_provider.h,fake_provider.c,conformance.h,conformance.c}` | This is the rule that gate #183 records. |

The three sessions can run separately after this issue and the gate. #193 and #194 do not need a working provider: they
test against the fake. Their end-to-end acceptance with OpenSSH needs #192.

## wolfSSL and wolfSSH settings

### Decision

Keep wolfSSL on its Kconfig-driven module settings, and give wolfSSH its own settings through the module's own hook. No
vendor file is edited, and the include path plays no part.

| Setting | Where | Reaches |
| --- | --- | --- |
| `#undef NO_FILESYSTEM`, `WOLFSSH_USER_FILESYSTEM`, `WOLFSSH_SFTP`, `#undef WOLFSSH_ZEPHYR`, `WOLFSSH_USER_IO`, `WOLFSSH_NO_RSA`, `WOLFSSH_NO_SHA1`, `WOLFSSH_NO_AES_CBC` | [`crux_wolfssh_settings.h`](../components/ssh/ports/zephyr/crux_wolfssh_settings.h), through `CONFIG_WOLFSSH_SETTINGS_FILE` | wolfSSH only |
| `CONFIG_WOLFSSH`, `CONFIG_WOLFSSL_CURVE25519` | [`wolf-ssh.conf`](../components/ssh/ports/zephyr/wolf-ssh.conf) | The whole image |
| `WOLFSSL_WOLFSSH`, `WOLFSSL_AES_COUNTER`, `WOLFSSL_KEY_GEN`, `WOLFSSL_ED25519_STREAMING_VERIFY` | [`wolf_ssh_settings.cmake`](../components/ssh/ports/zephyr/wolf_ssh_settings.cmake), on the module's `wolfSSL` interface target | Every translation unit |

`wolfssh/settings.h` includes `CONFIG_WOLFSSH_SETTINGS_FILE` after the wolfSSL `user_settings.h`, so a `#undef` there holds
for the rest of a wolfSSH translation unit. That module hook is why no wrapper is needed.

### Why

- **wolfSSL keeps `NO_FILESYSTEM`.** Removing it for the whole image, as the first probe and the wrapper below do, gives the
  TLS layer its own direct file code and pulls Zephyr's file system into the image. Here TLS sees exactly the settings it
  sees in an HTTPS-only image, and wolfSSH reaches files only through the port.
- **`WOLFSSH_ZEPHYR` has to go as well.** The module always defines it. With `NO_FILESYSTEM` undefined, it compiles wolfSSH's
  direct Zephyr file code in `port.c` and `ssh.c`, even with `WOLFSSH_USER_FILESYSTEM`, and it compiles the `SETSTAT` and
  `FSETSTAT` handlers out of `wolfsftp.c`. Removing it resolves the finding in the gate record that "accepted without effect"
  could not hold. Its other effects are the socket code, which `WOLFSSH_USER_IO` replaces with provider callbacks using
  `zsock_recv` and `zsock_send`, the Zephyr default directory, which the port supplies through `WGETCWD`, and a `/RAM:` path
  rule for the FAT layout, which the virtual root does not use.
- **`WOLFSSL_ED25519_STREAMING_VERIFY` matters.** wolfSSH defines `WOLFSSH_NO_ED25519` without it. The first probe did not set
  it, or `CONFIG_WOLFSSL_CURVE25519`, so `ssh-ed25519` was missing from its figures. The probe now fails to compile if a
  required algorithm is missing, or if RSA, SHA-1 or CBC is present.

A wolfSSH translation unit and wolfSSL now disagree about `NO_FILESYSTEM`, which is safe only if nothing they share depends
on it. The 21 wolfCrypt headers that wolfSSH uses were preprocessed for the target with the same flags, as wolfSSH includes them,
with and without the `#undef`: the outputs were identical. wolfSSH also includes no `wolfssl/ssl.h` here, because
`WOLFSSH_CERTS` is off. A later release could change this, so this check needs repeating on a wolfSSL upgrade.

### Rules for Crux code that includes wolfSSH headers

The module adds the wolfSSH sources and its definitions to the `app` target, and `-Wall -Wextra -Werror` must not reach them.
So the provider goes in its own library, and calls `crux_ssh_wolfssh_client(<target>)` from `wolf_ssh_settings.cmake`. That
gives it the same definitions and settings as wolfSSH. A translation unit without `WOLFSSH_ZEPHYR` defined would skip
`crux_wolfssh_settings.h` and see different settings, without any error.

### Alternatives tested or considered

| Option | Result | Decision |
| --- | --- | --- |
| `CONFIG_WOLFSSH_SETTINGS_FILE`, with wolfSSL unchanged | RT1060 combined image builds: 418,016 B Flash, 219,924 B RAM | **Chosen** |
| A Crux `user_settings.h` earlier on the include path, with `#include_next` and `#undef NO_FILESYSTEM` | RT1060 combined image builds: 418,040 B Flash, 219,924 B RAM | Not chosen. See below. |
| `CONFIG_WOLFSSL_SETTINGS_FILE` | Rejected at configuration with the socket backend: a control in the recipe | Not possible |
| Deleting the line in a copy of the module | Built in the first probe | An edited vendor file. Rejected. |

The wrapper was the candidate to test first, and it works. It costs only 24 B of Flash after linking, because the linker drops
what no one calls. It was not chosen for four reasons. The wolfSSL library then compiles its file functions (5 of the 5 checked
names, against none here), so the TLS layer holds direct file code that a later call could reach past the file service. It has
to change the whole image, TLS included, to serve wolfSSH. It depends on include order, and took `BEFORE` on `zephyr_interface` to
take effect. And it removes no step, as `WOLFSSH_ZEPHYR` still needs `CONFIG_WOLFSSH_SETTINGS_FILE` to undefine it.

Flash is higher than the 369,152 B of the first probe. That probe had no Ed25519 or Curve25519, so it could not offer the design's
preferred algorithms, and it also lacked the SFTP `SETSTAT` handlers. RAM includes the same configured 32 KB main stack and 128 KB
allocation arena, so it is not measured use.

### If SFTP needs the mount fallback

If #194 records a decision for the file system mount fallback, wolfSSH uses its own Zephyr file port instead of `myFilesystem.h`. That
port calls `z_fs_open` and `z_fs_close` from wolfSSL, which do not exist while wolfSSL keeps `NO_FILESYSTEM`: the build fails on them.
So that path needs the wrapper above, with `WOLFSSH_ZEPHYR` left defined. It was built once for the target in a scratch copy, with no
vendor file edited: 415,384 B Flash, 219,988 B RAM. It leaves `SETSTAT` and `FSETSTAT` compiled out, and that is a change to the shared
files, so it would be one reviewed PR.

### Recipe

`scripts/test-ssh-wolf-settings.sh` builds all of this, and never flashes or runs an image. It needs a workspace with the
optional stacks: `west config manifest.group-filter -- +crux-secure-management`, then `west update`. It fetches the wolfSSL
socket patch by revision, checks its SHA-256, merges it into a separate sparse copy of Zephyr, and checks the merged tree
against a recorded Git tree ID. No default workflow runs it. How a real composition applies the patch stays with #185.

The script fails unless the image holds the wolfSSH SFTP path and the required algorithms, holds none of wolfSSL's file
functions, wolfSSH's Zephyr file code or a Zephyr file system, and the two modules are unedited at their pins. Two controls
must fail for the expected reason: `CONFIG_WOLFSSL_SETTINGS_FILE` with the socket backend, and the probe with no wolfSSH
settings file.

## Testing on native_sim

### Decision

SSH is tested on `native_sim` without the TLS socket backend, on an unpatched Zephyr. [`native_sim.conf`](../tests/zephyr/ssh_wolf_probe/native_sim.conf)
holds the settings: `CONFIG_POSIX_API=n`, `CONFIG_POSIX_SYSTEM_INTERFACES=y`, no TLS sockets, and the native socket offload, which is
how OpenSSH on the host is meant to reach the image. It uses Zephyr's minimal C library, and has no file system.

This builds for `native_sim/native/64` on Zephyr v4.4.2. It was not run. An SSH test has no use for TLS, and this way #192, #193 and #194
do not depend on a patch that is not stored in this repository.

### The combined image also builds

wolfSSH and the socket backend together do build on `native_sim`, with the settings in
[`native_sim_combined.conf`](../tests/zephyr/ssh_wolf_probe/native_sim_combined.conf), on a patched Zephyr. The fix is
`CONFIG_POSIX_REALTIME_SIGNALS=y`, which supplies the `union sigval` that `CONFIG_POSIX_TIMERS` needs. This confirms the Kconfig gap
suspected in #176. It was not run, and this configuration has no network path to the host, so #197 has that still to settle.

`CONFIG_POSIX_API=y` was not made to work. An attempt with `CONFIG_PICOLIBC=y`, the libc the networking tests use, was stopped before
it finished because the settings above made it unnecessary, so that attempt has no result.

### What is still open

Whether wolfSSH's socket callbacks work over the native socket offload is unverified, as nothing has run. #192 proves that first, with
the design's OpenSSH cases. If it does not work, the fallback is the `native_sim` Ethernet driver with a TAP interface, which needs host
privileges that CI may not have. The settings arrangement does not change in that case.

## Host tests

`Ssh.ProviderConformance` runs the cases in [`tests/ssh/conformance.c`](../tests/ssh/conformance.c) against the fake provider.
They go through a scripted client (`ssh_test_client.h`), so a real provider can run the same cases with its own client.
`Ssh.ConformanceCatchesDefects` gives the fake each of its deliberate defects and requires the named case to fail, so a
case cannot pass by testing nothing.

The fake runs every callback on the calling thread, and a session callback runs inside `channel_request`. It models a signature
check by being told the outcome. So these cases fix the order and content of the calls, and cannot show concurrency, timing,
a real signature check or any real protocol behaviour. Those belong to #192 on `native_sim`.

## Findings for the SFTP session

From reading wolfSSH v1.6.0 source. None of this has run.

- `WOLFSSH_SFTP_ERRNO`, named in the design and in #194, does not occur anywhere in this revision. A failed `WOPEN` is sent
  as `WOLFSSH_FTP_FAILURE`. `WS_PERMISSIONS` becomes a permission status from a few places, such as path confinement and the
  results of `WMKDIR` and `WRMDIR`. So #194 must find out how a refused open reaches the client, or accept a general failure
  there. This does not change any file listed above, and it is a question for the first proof of the port.
- The port is `myFilesystem.h`, included by that name from `wolfssh/port.h`. Beyond the file macros it must supply functions
  `SFTP_GetAttributes` and `SFTP_GetAttributes_Handle`, and with `WOLFSSH_SFTP_NAME_READDIR` the hook `SFTP_Name_readdir`. The
  stand-in in `tests/zephyr/ssh_wolf_probe/port/` lists what compiled.
- With `WOLFSSH_ZEPHYR` removed, `SETSTAT` and `FSETSTAT` are compiled in, and call `WSETTIME`, `WFSETTIME`, `WCHMOD` and `WFCHMOD`.

## Evidence and limits

@@EVIDENCE@@

Not shown by any of this: that wolfSSH works with these settings at run time, a handshake, a login, a transfer, stack use,
timing, or any behaviour on a board. No real provider exists until #192, and the file port is a stand-in that always fails.
