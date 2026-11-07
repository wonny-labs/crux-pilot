# Secure management glossary

The terms of the HTTPS web, SSH command line and SFTP documents, with the one meaning each document uses. When a document and this page differ, the document is wrong: fix it. The code and its tests keep some older names, and the table says so. Evidence records under `docs/evidence/` are frozen and keep the names they were written with.

"Must" in these documents means that the build or the program enforces the requirement. A sentence that only describes behaviour does not use it.

## Sessions and channels

| Term | Means | Not |
| --- | --- | --- |
| **Connection** | A TCP connection with its SSH transport, or a TCP connection with its TLS session | A session |
| **Session channel** | An SSH channel of type `session` (RFC 4254 §6): a shell, an `exec` or an SFTP subsystem request rides on it | An access session |
| **Access session** | `crux_access_session_t`: what `crux_access` holds after a login, on any channel. The web identifies it by the `crux_session` cookie. | A session channel, or a browser tab |
| **Pseudo-terminal** | What `pty-req` asks for (RFC 4254 §6.2). The `terminal` flag of the shell callback is true when it was granted. | "terminal" alone |
| **Account**, **administrator** | An account in the credential store. The administrator is an account that manages other accounts: see the [Access contract](access-contract.md). | "user" for the account, except in field names such as `user` of an event |
| **Log in** (verb), **sign-in** | Log in is the action. Sign-in names a screen of a frontend. | |

## Layers

| Term | Means | Not |
| --- | --- | --- |
| **Provider** | The library or implementation behind a private interface or a port: the SSH library behind `crux_ssh_provider.h`, the PSA provider, a port provider such as `crux_cli_security_events` | The adapter |
| **Adapter** | Crux's own private layer between a provider or a server and Crux code: the web adapter over the Zephyr HTTP server, the SSH adapter that a provider callback calls (access, command line, SFTP) | A provider |
| **Entry point** | A callback of the SSH adapter, such as `crux_ssh_cli_shell`. A **refusing entry point** is the one that an image without a command line or SFTP links, and that refuses every request. | "Stand-in", except for a test double |
| **Server** | The Zephyr HTTP server thread | "Web server" |
| **Listener** | A listening socket: the TLS socket of the web, the TCP socket of SSH | The server |
| **SFTP file port** | The macros and functions of `myFilesystem.h` that wolfSSH calls | "Port" alone |
| **Event port**, **listen port** | The private function a component calls to record an event. The TCP port a listener binds. | "Port" alone |

## Refusing and denying

| Word | Used for |
| --- | --- |
| **Refuse** | A component said no on the wire or at an interface: a refused login, a refusing entry point, a refused request |
| **Deny** | An access decision: `crux_access` denied a permission, and the event is `access.permission_denied` |
| **Reject** | Only inside names that exist: `csrf_rejected`, `origin_rejected`, `config.rejected`, `cli.line_rejected`, and the `rejected` state of a login attempt |

## Web

| Term | Means | Not |
| --- | --- | --- |
| **Route** | A path with its methods and its access class, defined by `CRUX_WEB_ROUTE()` | "Endpoint" |
| **Resource** | The generated Zephyr HTTP resource behind a route | A route |
| **Access class** | `public`, `session`, or a permission name | |
| **Permission**, **role** | A permission is a string. A role is a set of permissions. | |
| **Server-generated response** | An answer the Zephyr HTTP server sends before any Crux code runs: `404`, `405`, `409`, `500`, `426`. It has no JSON and no security header. | The code and its tests call it "plain" (`PLAIN_404`). The documents may say "plain" for the same thing. |
| **Synchronizer token** | The CSRF token: cookie `XSRF-TOKEN`, header `X-XSRF-TOKEN` | |
| **`Origin` verification**, **custom request header**, **`SameSite`** | The other three controls of [web login and sessions](web-session.md): `Origin` must equal the scheme and the request's own `Host`; `X-Crux-Request: 1` on the attempt poll; `SameSite=Strict` on both cookies | "CSRF protection" for any one of them |
| **Trickled headers** (Slowloris), **slow reader** (slow-read attack) | A client that sends headers a byte at a time, or reads a response slowly | |

## TLS

| Term | Means |
| --- | --- |
| **Mbed profile**, **Wolf profile** | The two sets of TLS and PSA libraries a composition can select: Mbed TLS with TF-PSA-Crypto, or wolfSSL with wolfPSA. SSH is available on the Wolf profile only. |
| **Declared TLS configuration** | TLS 1.3 only, with `TLS_AES_128_GCM_SHA256` |
| **Device certificate**, **test certificate** | The certificate the TLS certificate model makes for a device, and the declared certificate of the `native_sim` images, whose key is public and which must never ship |
| **Trust on first use** | The procedure of comparing a fingerprint by hand at the first connection: [TLS certificate](tls-certificate.md#the-first-connection) |
| **#280 late first read** | On the Wolf profile a new connection's first request is read up to about 1 s late: [Issue #280](https://github.com/wonny-labs/crux-pilot/issues/280). Earlier records call it W1. The decisions W1 to W18 of the [frontend foundation design](plans/web-frontend-foundation-design.md) are a different list. |

## SSH and SFTP

| Term | Means |
| --- | --- |
| **Pre-auth connection exhaustion** | Silent clients that hold every connection slot until `auth_timeout_ms`: CWE-400, which OpenSSH's `MaxStartups` addresses. Earlier records call it the slot lockout. |
| **`SSH_DISCONNECT_TOO_MANY_CONNECTIONS`** | Disconnect reason 12 of RFC 4253 §11.1, sent to a connection past `max_connections` |
| **`kex-strict-s-v00@openssh.com`** | The strict key exchange marker that the server offers |
| **`SSH_FXP_INIT`**, **`SSH_FX_FAILURE`** (4), **`SSH_FX_NO_SUCH_FILE`** (2) | The SFTP request and status names. The SFTP design writes `INIT`, `FAILURE` and `NO_SUCH_FILE` for them. |

## Platform

| Term | Means | Not |
| --- | --- | --- |
| **Linux Host build** | The Linux build of Crux for development and tests ("Host" in the evidence words) | The development machine, or the SSH host key |
| **Development machine** | The computer that runs the container and the clients | "Host" or "host side" |
| **Image** | A built firmware or `native_sim` program. The images of this work are the **base composition** (`tests/native/management_composition`), the **HTTPS test image** (`tests/native/web_https`), the **services image** (`tests/native/management_services`), the **browser image** (its `e2e` variant) and the **integrated image** (`tests/native/management_ssh`). | A product image |
| **Real**, **faked**, **untested**, **Host** | The evidence words of [the product guide](management-in-a-product.md#what-you-get-and-what-is-not-built) | |
