# Secure management design

Status: proposal for review. The owner decisions below are approved. The public contracts,
component names and provider details are proposals until each is reviewed. This document does not
authorise implementation, hardware operations, dependency adoption, merging or issue closure.

Tracks: milestones M10, M11 and M12. Track designs:
[HTTPS web management](secure-management-web-design.md) and
[secure CLI and file transfer](secure-management-cli-sftp-design.md).

## Outcome

A controller built on Crux can be monitored, configured, updated and exchange files remotely, and
only an authenticated and permitted user can do so, over an encrypted channel. Projects add their
own web pages and functions without depending on Zephyr, a TLS stack or an SSH stack.

Crux provides mechanisms and evidence that help a product meet its security obligations. The
product manufacturer owns those obligations and the conclusions drawn from this evidence.

## Decisions

| ID | Decision | State |
| --- | --- | --- |
| D1 | Crux provides the HTTPS server, management APIs, authentication and authorisation, plus common pages that a project can replace. Projects add their own pages and APIs. | Owner, 3 November 2026 |
| D2 | A device generates its own TLS certificate and SSH host key at first boot. An operator can replace the TLS certificate later. | Owner, 3 November 2026 |
| D3 | Web, CLI, file transfer and serial console share one account and role store. | Owner, 3 November 2026 |
| D4 | The Pilot evaluates wolfSSL and wolfSSH under GPLv3. Products use a commercial licence that they obtain themselves. | Owner, 3 November 2026 |
| D5 | One provider profile is selected at build time. Running two stacks side by side at run time is not a goal. | Owner, 3 November 2026 |
| D6 | Telnet, FTP and plain HTTP are not provided (1 November 2026). WinSCP must work as an SFTP client (3 November 2026). | Owner |
| D7 | Common work comes first. HTTPS and CLI/SFTP then proceed in parallel. | Owner, 3 November 2026 |
| D8 | The Wolf profile is the first complete profile. Mbed TLS validates the HTTPS seam. | Proposal |
| D9 | The production CLI is a Crux command registry. The Zephyr shell stays a development-only facility and is never reachable from the network. | Owner, 3 November 2026, as rule L5 of [the command line contract](../cli-contract.md#zephyr-shell-rule) |
| D10 | Web login uses two requests, so that password verification runs on a worker and never stalls the HTTP server thread. | Owner, 3 November 2026 |

## Requirements

| ID | Requirement | Verified by |
| --- | --- | --- |
| R1 | Web, CLI and file transfer use only authenticated, encrypted channels. | Protocol and negative tests |
| R2 | The reference configuration has no Telnet, FTP or plain HTTP listener, including redirects. | Port scan on the target |
| R3 | A provider profile changes through build configuration, without project source changes. | Same conformance suite and consumer source on each profile |
| R4 | Every management operation declares its access class: public, session-only or a named permission. Public is limited to login and sign-in assets. A missing, invalid or unknown permission, role or session is rejected. | Host conformance and static route checks |
| R5 | Roles and role-to-permission mapping are product data. A level model is expressed as roles whose permission sets include each other. | Host conformance with two policies |
| R6 | File access is limited to areas that the composition declares, with separate read and write permissions and size limits. | Traversal, permission and capacity tests |
| R7 | The reference configuration has no default password. The first credential has a documented setup path. | Build inspection and first-boot test |
| R8 | Failed authentication attempts are delayed and limited. Sessions expire when idle and at an absolute limit. | Negative and timing-bound tests |
| R9 | Logins, failures, rejected operations and security-relevant changes produce security events. Secrets never appear in events, logs or diagnostics. | Log assertions with known secret values |
| R10 | Credentials and private keys have a declared storage path and a declared confidentiality basis. | Provider declaration and evidence |
| R11 | Sessions, memory, transfers and request sizes have explicit bounds. Exhaustion returns an error and leaves control work running. | Resource exhaustion and timing tests |
| R12 | Firmware upgrade requests are available only after M9, and only for an authorised principal. | M9 path evidence and negative tests |

## Evidence

Upstream sources were read on 3 November 2026 at the versions stated. None of this has been built
or run in Crux yet.

- **Pinned baseline.** Crux pins Zephyr `684c9e8` (v4.4.0), which has no SSH library. Zephyr v4.5.0
  adds `subsys/net/lib/ssh`, marked experimental.
- **Zephyr SSH at v4.5.0.** It offers `curve25519-sha256`, RSA host keys and `aes128-ctr` with
  `hmac-sha2-256`. Ed25519 and ChaCha20 are commented out. Its core sources contain no strict key
  exchange code. A server holds one username and password, compared with `strncmp`. It has no SFTP
  server, although it reports subsystem requests to the application.
- **Mbed TLS.** It has no SSH implementation. The project announced that its current maintainers
  stop working on the TLS and X.509 libraries from April 2027, and that security fixes stop then
  unless new maintainers take over. TF-PSA-Crypto stays maintained.
- **Zephyr TLS sockets.** `NET_SOCKETS_SOCKOPT_TLS` selects Mbed TLS and `sockets_tls.c` calls it
  directly. The `wolfSSL/osp` repository maintains patches that replace this backend for Zephyr 3.7,
  4.3 and 4.4.1. The 4.4.1 patch needs a wolfSSL release newer than 5.9.2 and POSIX options. The
  vendor states that the HTTP server and WebSockets run over it. There is no 4.5 patch yet.
- **wolfSSH.** Release v1.6.0 includes server, SFTP, a user authentication callback, per-session
  path confinement, a user file-system port, strict key exchange, Ed25519 and
  `mlkem768x25519-sha256`. Its Zephyr samples are only allowed on `qemu_x86`, so Cortex-M7 evidence
  does not exist yet. It has no Zephyr shell backend.
- **Zephyr HTTP server at v4.4.0.** It has static, dynamic and WebSocket resources and header
  capture. It has no authentication or session handling. One thread runs a `poll` loop and calls
  dynamic resource callbacks directly. Resources are defined with Zephyr macros.
- **Zephyr shell at v4.4.0.** Command entries have no permission field, and `shell_set_root_cmd`
  applies to every shell instance.
- **Crux contracts.** File has flat names with no listing, stat or rename. Configuration supports
  `BOOL` and `U32` only. Networking excludes TCP servers and TLS for portable consumers, which is
  acceptable because listeners stay inside private providers.

## Architecture

```text
Project       web pages, project routes and commands, role policy, file area declarations
Crux public   crux_web, crux_cli, crux_access, management services (status, configuration, file)
Adapters      HTTP adapter | CLI adapter | SFTP adapter
Providers     Zephyr HTTP server over TLS sockets | wolfSSH
Crypto/TLS    wolfSSL and wolfCrypt | Mbed TLS and TF-PSA-Crypto
```

Three rules hold this together.

1. **Services decide permission.** A service operation takes the caller's session and checks the
   permission itself, including per-parameter and per-area permissions. Adapters translate protocols
   and enforce the declared access class of a project route or command before it runs. They can
   reject and cannot grant, so a new protocol cannot widen access. A project handler that varies
   permission per resource calls `crux_access_check` itself.
2. **Providers only authenticate and transport.** A provider reports a verified identity and moves
   bytes. Provider types never appear in public headers.
3. **Portable first.** Public contracts and services build on the Linux Host and are tested there
   with fake providers. Only private provider code touches Zephyr, wolfSSL or Mbed TLS.

## Provider profiles

| Profile | HTTPS | CLI and SFTP | Crypto for Crux code | Status |
| --- | --- | --- | --- | --- |
| Wolf | wolfSSL behind Zephyr TLS sockets | wolfSSH | wolfPSA over wolfCrypt | First complete profile |
| Mbed | Mbed TLS behind Zephyr TLS sockets | Not provided | TF-PSA-Crypto | HTTPS seam validation |

The seams differ on purpose. They follow existing interfaces and avoid general wrappers.

- **TLS:** the Zephyr TLS socket API is the seam. Crux adds no TLS wrapper. The HTTP adapter does not
  know which backend is linked.
- **SSH:** a small private interface inside Crux, used only by the CLI and SFTP adapters. It is not a
  public contract, because projects never see SSH.
- **Crypto:** the PSA Crypto API for Crux-owned code, qualified under Issue #142.

A build that enables CLI/SFTP with the Mbed profile fails at configuration time. Zephyr SSH would be
a future second SSH provider, and adding it should require no change above the private interface.
It is not selected now because it is experimental, lacks SFTP and multiple accounts, and needs Zephyr
v4.5.0, for which no wolfSSL socket patch exists yet.

The Wolf profile carries a maintenance cost: a patch of about 190 KB on Zephyr and a move to 4.4.1.
Issue "Select the provider profile and Zephyr baseline" measures that cost before anything is adopted.

## Common contracts

These sketches show the intended shape. They are not adopted interfaces.

### Access control

```c
/* Opaque, bounded, owned by crux_access. */
typedef struct crux_access_session crux_access_session_t;

crux_access_result_t crux_access_login_password(const char *user, const char *password,
                                                crux_access_channel_t channel,
                                                crux_access_session_t **out);
crux_access_result_t crux_access_login_verified(const char *user, crux_access_channel_t channel,
                                                crux_access_session_t **out);
crux_access_result_t crux_access_check(const crux_access_session_t *session,
                                       const char *permission);
void crux_access_logout(crux_access_session_t **session);
```

`crux_access_login_verified` is for a provider that has already verified a public-key signature. It
is private to providers, so a project cannot create a session without a credential check.

Sessions are referenced through an opaque handle that carries a generation, so an expired or ended
session is rejected and a stale handle never reaches another account's session.

The contract never sleeps. During a delay window, a login returns a retry-after result without
verifying anything. Password verification itself is slow, so an adapter calls the login operation
from a management worker and never from a thread that other clients depend on. A login for an
unknown user performs the same verification work against a fixed dummy verifier and returns the same
result as a wrong password.

Permission identifiers are short strings such as `file.read` or `app.pump.write`. Capabilities and
projects name their own. The product supplies a constant policy table of roles and permissions. A
permission that is not in the table is rejected. Each account has one role, and the model has no
inheritance. A viewer, operator and administrator arrangement is three roles with overlapping sets.

The account store holds a user name, role, password verifier, public keys and state. It has
operations to list, create, disable and change the role of an account, to set a password and to add or
remove a public key. These operations take a session and need `access.manage`, except that a user can
change their own password and keys. Setting the first credential is a separate provisioning operation,
available only to the local serial path and only while no account exists. Password
verification runs only at login, because it is deliberately slow. The verifier algorithm and its cost
are chosen by measurement on the target. Failed attempts add an increasing delay, tracked in a bounded
table by account and source, so that an attacker cannot lock out the administrator permanently. When
the table is full, the oldest expired entry is evicted, and otherwise a new source shares a global
delay entry, so an attacker cannot clear the delay by flooding the table.

### Management services

Each service is a portable component that takes a session on every operation.

- **Accounts:** the account operations above.
- **Status:** system status, reset cause, network interface status and version, plus items that a
  project registers with a permission.
- **Configuration:** parameter listing with name, type, permission and sensitivity, then read, update
  and save over the existing Configuration contract. Sensitive values are write-only. The contract
  gains string support. Applying a change safely stays application policy.
- **File:** list, stat, read, write, remove and rename within declared areas. A composition declares
  each area as a name, storage location, read and write permissions and a maximum file size. The File
  contract gains list, stat and rename. Areas appear to clients as top-level directories, and files
  inside each area remain flat.

The file service has two write modes. A managed upload writes to a temporary name, renames on success
and removes the temporary file on abort. The web interface uses it. A direct write opens the named file
and leaves any partial file in place on abort, because an SFTP client such as WinSCP owns its own
temporary name, rename and resume. Partial files count against the area limit, and a declared service
rule removes stale ones. Neither mode provides atomic replacement across power loss, and results must
say so. Existing single-writer rules stay, so a conflicting open returns a busy result.

Firmware does not pass through the file service. After M9, a firmware service that M10 does not
deliver wraps the M9 update path and enforces `firmware.upgrade` itself, for both tracks.

### CLI command registry

`crux_cli` holds a table of commands, each with a name, argument limits, handler and access class. It parses a bounded line, checks permission and runs the handler with the session. Serial
and SSH use the same registry. Commands that a session cannot run are hidden from help, and typing one
gives the same output as an unknown command (rule L3 of [the command line contract](../cli-contract.md)).

### Credentials and keys

A narrow store holds password verifiers, private keys, certificates and authorised public keys. The
provider declares its confidentiality basis. Candidates are compared on RT1060-EVKB. Without a
qualified device-unique key, results must state that stored secrets are readable by anyone with access
to the storage medium. Keys are generated from a qualified entropy source and are never exported
through management APIs.

### Security events

Events use the existing Logging contract with a stable identifier, user name, channel, source address
and result. They never contain passwords, tokens, private keys or request bodies. Timestamps state
whether the clock was set. Loss of events under load is counted and reported.

## Request flow

1. A client connects. The provider completes TLS or SSH and receives credentials.
2. The adapter calls `crux_access`. Failure produces a security event and a delay.
3. Success creates a bounded session, bound to its channel.
4. The adapter translates a request into a service operation and passes the session.
5. The service calls `crux_access_check`. Rejection produces a security event and changes nothing.
6. The service performs the bounded operation and returns a Crux result. The adapter maps that to a
   protocol status. Internal paths and error text do not reach the client.
7. Logout, idle timeout, the absolute limit, account change or disconnect ends the session.

Changing a password or role ends that account's other sessions. Sessions do not survive a reboot.

## Parallel work

The tracks split when gate G1 passes. It needs these items, and it does not need all of M10:

- The provider profile and Zephyr baseline decision, with wolfSSL, wolfSSH and wolfPSA pinned in the
  manifest
- Reviewed `crux_access` contract, including account operations, with a fake provider and Host
  conformance cases
- Reviewed service operations for accounts, status, configuration and file, with their contract
  extensions
- Reviewed `crux_cli` registry contract, which M12 uses and M11 does not
- Reviewed security event identifiers and the credential and key store interface

The reviewed contracts need a fake provider and Host tests at the gate. Their target implementations
can finish alongside the tracks.

After G1:

| Track | Owns | Must not change without a shared review |
| --- | --- | --- |
| M11 HTTPS | `crux_web`, HTTP adapter, certificate management, web pages | `crux_access`, services, `west.yml` |
| M12 CLI/SFTP | SSH private interface, wolfSSH provider, SFTP adapter, CLI over SSH | `crux_access`, services, `west.yml` |

A change to a shared contract after G1 is one reviewed PR that both tracks rebase onto. Each track
works in its own branch and worktree. Each milestone tests with the other track disabled. A final
integrated image then enables both tracks and measures resources and concurrent use, because the
tracks share RAM, sockets and storage.

## Resources and scheduling

Management threads run below every control priority. A management operation must not block a control
thread. Reference limits are starting values to replace with measurements: three web clients, two SSH
sessions and one bulk transfer per device at a time.

The sample configurations upstream use a 60 KB Mbed TLS heap, and a 512 KB allocation arena with a
32 KB main stack for wolfSSH tests. These are configuration values and not measured needs. M10
records real Flash, RAM and stack use on RT1060-EVKB. Designs must not assume execution in place or a
particular boot medium, and Pilot work validates only RT1060-EVKB.

## Testing

| Level | Covers |
| --- | --- |
| Linux Host with fakes | Contracts, permission decisions, sessions, delays, parsers, area rules, secret absence |
| `native_sim` | HTTP adapter and providers against real clients where the stack allows it |
| Fuzz or malformed input | HTTP adapter inputs, CLI lines, SFTP paths and names, configuration values |
| RT1060-EVKB | Handshake time, resource limits, port scan, entropy, storage and physical acceptance, each with separate approval |
| Integrated image | Both tracks with a control workload, comparing jitter with and without management load |

Each public operation is tested for success, unauthenticated, unpermitted and invalid input cases.
The same conformance suite runs on each supported provider profile.

## Regulatory traceability

This table links Cyber Resilience Act outcomes to Crux evidence for a manufacturer's own assessment.
It paraphrases Annex I Part I, has not been checked against the Official Journal text by compliance,
and states no legal conclusion. The regulation sets outcomes. It does not name SSH or any other
protocol.

| Outcome | Crux mechanism | Evidence |
| --- | --- | --- |
| Secure default configuration | No default password, no plain-text listeners, optional features excluded | R2, R7 |
| Protection from unauthorised access | Authentication, roles, permission checks | R1, R4, R5, R6 |
| Confidentiality and integrity in transit | TLS and SSH with declared algorithms | R1 |
| Reduced attack surface and impact | Bounded services, Zephyr shell absent from the network, least privilege | R2, R6, R11 |
| Availability under attack | Bounds, delays, management below control priority | R8, R11 |
| Recording of security-relevant activity | Security events | R9 |
| Security updates | M8, M9 and the authorised upgrade path | R12 |
| Due diligence on third-party components | Pinned sources, licence review, SBOM inputs, tracked upstream fixes | Provider decision record |

## Client tools

This is an engineering position for compliance to review, and it is not a legal conclusion. The design
does not depend on a particular client. A device controls only what it offers, so the server rejects
weak algorithms and plain-text protocols, whichever client connects. WinSCP also supports FTP, which
has no effect here because the device does not offer it. Whether shipping or recommending a client
brings further duties is a question for each manufacturer's compliance review. User documentation
should state a minimum client version.

## Open questions

Each has a working default, so none blocks gate G1.

| Question | Working default |
| --- | --- |
| Authentication methods | Password and public key for SSH, password for web |
| First credential and recovery | Set over the serial console, recovered by a product-defined local reset that erases secrets. Until an account exists, every network login is rejected. |
| Reference roles | Viewer, operator, administrator, as reference policy only |
| File use, sizes and directories | Declared areas with flat files, sizes set by the project |
| Upgrade timing and control interaction | Application policy, decided with M9 |
| External API automation | Session login only, tokens deferred until a consumer needs them |
| Scheduling against M7 | Planning only, with no commitment before M7 unless the owner decides otherwise |
