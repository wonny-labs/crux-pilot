# Secure CLI and file transfer design

Status: proposal for review, track M12. It follows the
[secure management design](secure-management-design.md) and does not authorise implementation,
hardware operations, dependency adoption, merging or issue closure.

## Outcome

An authenticated user reaches a permission-aware command line with a standard SSH client and exchanges
files with a standard SFTP client. OpenSSH and WinSCP are the acceptance clients.

## Scope

| Included | Excluded |
| --- | --- |
| SSH server with password and public-key login | Port forwarding, agent forwarding, X11, SSH client role |
| Interactive CLI and single-command `exec` | Zephyr shell over the network |
| SFTP over declared file areas | SCP, FTP, FTPS, Telnet |
| Host key generation at first boot | Central authentication such as RADIUS or LDAP |
| Firmware upload path after M9 | Certificate-based SSH authentication |

SCP stays disabled even though wolfSSH can provide it. Removing it reduces the code exposed to the
network, and SFTP covers the need. Compatibility with the `scp` command remains to be tested against
OpenSSH, whose newer clients use the SFTP protocol for `scp` by default.

## Provider

wolfSSH is the first provider, pinned to a reviewed commit or release. It sits behind a small private
interface:

```c
/* Private to Crux. Never included by a project. */
typedef struct {
    int (*start)(const crux_ssh_config_t *config, const crux_ssh_callbacks_t *callbacks);
    int (*stop)(void);
    int (*read)(crux_ssh_channel_t *channel, void *buffer, size_t length);
    int (*write)(crux_ssh_channel_t *channel, const void *buffer, size_t length);
    int (*close)(crux_ssh_channel_t *channel);
} crux_ssh_provider_t;
```

The callbacks cover credential checks and shell, `exec` and SFTP subsystem requests, and every other
request is refused. This interface exists to keep SSH library types out of the rest of Crux. It will
be revised once a second provider is actually attempted.

## Authentication

`wolfSSH_SetUserAuth` connects provider authentication to `crux_access`. Passwords go to
`crux_access_login_password`. For a public key, this callback only checks that the key belongs to the
account, and wolfSSH calls it before verifying the signature, so it must not create a session. The
session is created with `crux_access_login_verified` only from the success branch of the
`wolfSSH_SetUserAuthResult` callback, which runs after the signature check. A session belongs to one
SSH connection.

Attempts per connection and the unauthenticated time limit are bounded. Failures produce security
events and use the shared delay, which rejects attempts without sleeping. Password verification runs on
the connection's own session thread, so it does not stall other sessions. Concurrent login tests
verify this.

Web and SSH share accounts. Public keys are added through a management API or the CLI, under
`access.manage` or by a user for their own account.

## Algorithms

The server offers a short explicit list, set with the wolfSSH algorithm list APIs:

| Purpose | Offered |
| --- | --- |
| Key exchange | `mlkem768x25519-sha256` if its resource cost is acceptable, `curve25519-sha256`, `ecdh-sha2-nistp256` |
| Host key | `ssh-ed25519`, or `ecdsa-sha2-nistp256` where Ed25519 is unavailable |
| User public key | `ssh-ed25519`, `ecdsa-sha2-nistp256` |
| Cipher | `aes128-gcm@openssh.com`, `aes256-gcm@openssh.com`, `aes128-ctr` |
| MAC for CTR | `hmac-sha2-256` |
| Compression | `none` |

The user key row is the first SSH provider's scope, and #192 compiles in only what the table names. `crux_access` is wider: it also accepts `ecdsa-sha2-nistp384` and `ecdsa-sha2-nistp521` keys.

Strict key exchange stays on. SHA-1, CBC, DES and RC4 are not offered. The WinSCP algorithm
documentation lists every name above. A client from before the Terrapin fix (WinSCP before 6.2.2) can
still connect but lacks the mitigation, so user documentation states a minimum version.

OpenSSH 10.1 warns when key exchange is not post-quantum. Offering `mlkem768x25519-sha256` avoids that
warning, so its Flash, RAM and handshake cost is measured. If it is too expensive the warning is
declared as a known limitation.

## Host key

The device generates its key from the qualified entropy source at first boot and stores it through the
credential and key store. The fingerprint is available on the serial console and to a principal with
`access.manage`, so a user can verify it at first connection. A host key change is a deliberate, recorded
operation, because clients will warn about it.

## Command line

The SSH shell channel connects to `crux_cli`. The Zephyr shell is not used, for three reasons: its
commands have no permission field, `shell_set_root_cmd` applies to all instances, and enabling it
would bring its built-in commands within reach of the network.

The line handler supports backspace, a bounded line length and optional short history that excludes
secret input. A terminal request is accepted and its size ignored unless a command needs it. Output
stays bounded and handles slow clients without blocking other sessions. An `exec` request runs one
command through the same parser and returns its exit status, for scripted use.

Commands use the same service operations as the web interface, so they cannot differ in permission.
The registry enforces each command's access class, and services check the rest:

```text
status                 status.read
config get <name>      session-only, service checks the parameter's read permission
config set <name> ...  session-only, service checks the parameter's write permission
config save            config.save
account list           session-only, service checks access.manage
passwd                 session-only, service checks ownership
logout                 session-only
```

The same registry runs on the serial console. A serial session starts with a login prompt, and a
development build can keep the Zephyr shell only where the composition marks it development-only.

## SFTP

### File system integration

wolfSSH implements the SFTP request handling and calls file operations through macros in its port
layer. On Zephyr it calls the file system directly, which would skip the Crux file service and its
permission checks. It also has no per-operation permission callback. Three options exist:

| Option | Description | Assessment |
| --- | --- | --- |
| A | `WOLFSSH_USER_FILESYSTEM` port that sends each operation to the Crux file service | **Recommended.** Keeps one enforcement point and needs no upstream patch if the port macros are enough. |
| B | A Zephyr virtual file system mount that checks permissions and forwards to the real volume | Fallback. Also needs no upstream patch, but it adds a native layer. |
| C | Patch the SFTP request handlers | Rejected. It adds a security-relevant fork to maintain. |

Each port macro receives an `fs` argument, which is the per-session handle set with
`wolfSSH_SetFilesystemHandle`. That handle carries the Crux session, so no thread-local state is
needed. The port header must also supply `SFTP_GetAttributes`, `SFTP_GetAttributes_Handle` and
directory reads in the shape the handlers expect, and define `WOLFSSH_SFTP_ERRNO` with `errno` set so
that a refusal reaches the client as a permission error. Track M12 proves this port before anything
else. `wolfSSH_SFTP_SetConfinePath` is added as a second defence, never as the permission check.

### Client view and operations

Each file area declared by the composition appears as a top-level directory. Files inside are flat. A
user sees only the areas they can read or write, and creating top-level directories is refused.

| SFTP request | Behaviour |
| --- | --- |
| `INIT`, `REALPATH` | Normalised paths inside the virtual root |
| `OPENDIR`, `READDIR`, `CLOSE` | Areas at the root, files inside areas |
| `STAT`, `LSTAT`, `FSTAT` | Type, size, time when known, permissions derived from the session |
| `OPEN`, `READ`, `WRITE`, `CLOSE` | Service operations, with the area limit enforced |
| `REMOVE`, `RENAME` | Service operations, rename within one area only |
| `SETSTAT`, `FSETSTAT` | Accepted without effect for permission bits. Modification time applied only if the medium supports it. |
| `MKDIR`, `RMDIR`, `SYMLINK`, `READLINK`, unsupported requests | Permission or unsupported status |

### WinSCP behaviour

WinSCP documents that a transfer above its resume threshold (100 KiB by default) goes to a temporary
name and is renamed when finished. A server that cannot rename makes such a transfer fail. Rename is
therefore required, and it is the main reason that the File contract must grow. The temporary suffix
must also fit the 63-character name limit, and `.filepart` is within the allowed character set.

The other behaviours are not yet confirmed, including the exact requests sent at connect time and how
WinSCP reacts to a timestamp update it cannot apply. Track M12 records a real client trace against a
host build and turns it into conformance cases. If an upload or download option in WinSCP needs a
non-default setting, that setting is documented as a known limitation.

## Firmware upload

After M9, a dedicated `/firmware` entry accepts an image for a principal with `firmware.upgrade` and
passes it to the M9 staging path, instead of writing to a file area. Upgrade state is read through a
CLI command. Signature checks stay with the M9 path. This entry does not exist in builds without M9.

## Failure handling

| Failure | Result |
| --- | --- |
| Session or channel limit reached | Connection closed after the SSH disconnect message, control work unaffected |
| Transfer abort or disconnect | Handle closed. A partial file stays in place, because the client's resume depends on it, and counts against the area limit. Removing stale partial files is a declared service rule. |
| Slow client | Window and timeouts hold the transfer, session ended at the idle limit |
| Rejected file operation | Permission-denied status through `WOLFSSH_SFTP_ERRNO`, and a security event |
| Storage missing or full | General failure status, because SFTP version 3 in wolfSSH has no out-of-space status. An event records the cause. |
| Host key unavailable | Server does not start, event recorded, no key is silently regenerated |

## Testing

- **Host:** `crux_cli` parser and permission tests, file port against a fake file service, path
  normalisation, traversal cases
- **`native_sim`:** OpenSSH `ssh` and `sftp` scripted through a host-reachable link, with success,
  wrong password, wrong key, wrong role, traversal, oversized write and disabled-feature cases
- **Protocol audit:** `ssh-audit` or an equivalent check confirming the offered algorithms and strict
  key exchange
- **WinSCP:** connect, list, upload above and below the threshold, download, rename, delete and resume
  an interrupted upload. It needs a Windows host, so this is one recorded run per release candidate
  unless a Windows CI path to a test target is found
- **Target, with separate approval:** handshake time, transfer throughput, RAM and stack use, jitter
  under transfer load, disconnect during transfer
