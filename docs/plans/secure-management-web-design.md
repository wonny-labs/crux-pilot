# HTTPS web management design

Status: proposal for review, track M11. It follows the
[secure management design](secure-management-design.md) and does not authorise implementation,
hardware operations, merging or issue closure.

## Outcome

A project serves its own web interface from a controller over HTTPS, without Zephyr or TLS
dependencies. Users can monitor status, change configuration, exchange files and, after M9, upgrade
firmware, each only when authenticated and permitted.

## Scope

| Crux provides | Project provides | Excluded |
| --- | --- | --- |
| HTTPS service and bounded connections | Controller pages and branding | Web framework or server-side templates |
| Route and static asset registration | Project APIs and data | UI generated from a configuration schema |
| Login, session and CSRF handling | Role policy | External identity providers |
| APIs for status, configuration, files, accounts and certificates | Choosing which areas and parameters to expose | Plain HTTP and redirects |
| Replaceable common pages | Replacing common pages if wanted | Multi-tenant or internet-facing hardening claims |

## Engine

Use the Zephyr HTTP server behind a private adapter. It is in the tree, Apache-2.0 licensed and uses
TLS sockets, so it works with both provider profiles. A second web server would add a licence and a
parser without adding a needed capability. A commercial or GPLv2 option such as Mongoose stays
unselected unless the Zephyr server fails its evidence.

The first configuration uses HTTP/1.1 only. HTTP/2 stays disabled until a measured need justifies its
extra parser and memory. Connection limits are therefore a real concern, as the next section explains.

## Public contract sketch

The contract is portable, and this shape is not adopted.

```c
typedef crux_web_result_t (*crux_web_handler_t)(crux_web_request_t *request,
                                                crux_access_session_t *session,
                                                void *context);

/* The access class is mandatory: a permission, CRUX_WEB_SESSION or CRUX_WEB_PUBLIC. */
CRUX_WEB_ROUTE(pump_status, "/api/v1/app/pump", CRUX_WEB_GET, "app.pump.read", on_pump_status, NULL);
CRUX_WEB_ROUTE(pump_item, "/api/v1/app/pump/{id}", CRUX_WEB_GET, CRUX_WEB_SESSION, on_pump_item, NULL);

crux_web_result_t crux_web_read_body(crux_web_request_t *request, void *buffer, size_t length,
                                     size_t *transferred);
crux_web_result_t crux_web_respond(crux_web_request_t *request, int status,
                                   const char *content_type, const void *body, size_t length);
```

A route with no access class fails to compile. The adapter enforces that class before the handler runs.
`CRUX_WEB_SESSION` means any valid session, for routes such as logout, or for handlers and services
that check a per-resource permission themselves. A `CRUX_WEB_SESSION` route that changes or reads
protected data must call a service or `crux_access_check`. `CRUX_WEB_PUBLIC` is reserved for login, its attempt
result and the static sign-in assets, and a check script lists every public and session-only route for review.
Handlers never see the Zephyr client context or a socket.

The macro builds a Crux route table. One private catch-all resource passes requests to the adapter,
which finds the route, checks the session and the access class, and only then runs the handler. The
Zephyr service fallback resource is the first candidate, since the wildcard option pulls in extra
POSIX support. If neither suits, the adapter generates Zephyr resources at build time. This is the
first thing that track M11 proves.

## Threading and large transfers

Zephyr runs one server thread and calls handlers on it, so a slow handler stalls every client. This
design therefore has three rules.

- Small API handlers run on the server thread and have a declared time limit.
- Storage and firmware transfers run on one bounded worker. The adapter passes data through a fixed
  buffer, and the worker applies flow control.
- A single bulk transfer runs at a time. Another returns `503` with `Retry-After`.

Whether the server thread can apply flow control without blocking other clients is unproven, because
its callback model delivers data as it arrives. If it cannot, the fallback is a temporary pause of
other clients during storage writes, with the stall measured and declared.

Browsers open several connections. With a low client limit, assets must be few: one compressed bundle
each of script and style, with a long cache lifetime and a hash in each name. The inactivity timeout,
default limit of three clients and header capture buffer (128 bytes by default) are all set against
real browsers and the session cookie size. The capture must hold Cookie, Origin, the CSRF header and
Host together, and those headers arrive only in the first callback of a request. A request whose
headers were dropped for lack of space is rejected, and is never treated as having no session.

## Authentication and sessions

A response cannot be deferred within one request. In `dynamic_post_put_req`, once request data is final,
the Zephyr v4.4.0 server calls the callback again in a loop until the response is final, without
returning to `poll`. Verifying a password in that callback would stall every client, and rotating the
account or source would avoid the per-account delay. Login therefore uses two requests.

| Topic | Decision |
| --- | --- |
| Login | `POST /api/v1/session` with JSON credentials. HTTP Basic is not used, because it would send and verify the password on every request. |
| Session token | At least 256 bits from the qualified entropy source. Only a hash is stored, in a bounded RAM table. |
| Cookie | `Secure`, `HttpOnly`, `SameSite=Strict`, `Path=/`, with no persistent expiry |
| CSRF | A per-session token in a request header on every state-changing method, compared in constant time, with `Origin` checked too |
| Expiry | Idle and absolute limits. Logout and reboot end sessions. A password or role change ends that account's other sessions. |
| Login processing | Two steps, proposed in D10. `POST /api/v1/session` queues verification on the bounded management worker and returns `202` with an attempt identifier. `GET /api/v1/session/attempt/{id}` returns pending, rejected or success, and success sets the session cookie. One verification runs at a time. Another returns `503` with `Retry-After`, and a login inside its delay window returns `429`. Attempts expire quickly, and the password buffer is cleared after verification. |
| Login limits | The `crux_access` delay applies. Replies do not reveal whether a user exists. `Origin` is checked on login too, which covers login CSRF. |
| Authorisation | The adapter enforces the route's access class. Crux services and per-resource handlers check their own permissions. |

## Response rules

Every response that Crux produces carries `Content-Security-Policy: default-src 'self'; frame-ancestors 'none'`,
`X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer` and `Cache-Control: no-store` for
API data. Fixed error responses built into the Zephyr server may lack these headers, and track M11
declares any that it cannot cover. Cross-origin requests are not supported, so no CORS headers are
sent. No page loads an external script, style or font.

The reference configuration does not send HSTS. A browser ignores HSTS on a connection with certificate
errors, so it adds nothing to a self-signed device. Once a browser has accepted it from a trusted
certificate, a later expiry or a reset to self-signed turns the warning into a hard failure and locks
users out. A product with managed certificates may enable it.

Errors use a small JSON form with a stable code. They never include paths, stack traces or native
error text. `401` means no valid session and `403` means a valid session without permission.

## API outline

| Area | Routes | Permission |
| --- | --- | --- |
| Session | `POST`, `DELETE /api/v1/session`, `GET /api/v1/session/attempt/{id}`, `GET /api/v1/session/me` | Public for login and its attempt, session-only otherwise |
| Accounts | `/api/v1/accounts` list, create, change role, disable, reset password, plus change own password | Session-only route, with the account service checking `access.manage` or ownership |
| Status | `GET /api/v1/status`, `GET /api/v1/app/...` | `status.read`, project defined |
| Configuration | `GET /api/v1/config`, `GET`, `PUT /api/v1/config/{name}`, `POST /api/v1/config/save` | Session-only route, with the service checking per parameter and `config.save` |
| Files | `GET /api/v1/files/{area}`, then `GET`, `PUT`, `DELETE /api/v1/files/{area}/{name}` | Session-only route, with the service checking per area |
| Certificates | `GET /api/v1/tls/certificate`, `POST /api/v1/tls/csr`, `PUT /api/v1/tls/certificate` | `tls.manage` |
| Security events | `GET /api/v1/events` when a bounded store exists | `events.read` |
| Firmware | `/api/v1/firmware`, available only after M9 | `firmware.upgrade`, checked by the firmware service |

Updates respond with the applied value or a validation error. A configuration value that needs a
restart or a safe state says so in its response, and the application decides. Uploads send
`Content-Length`, which is checked against the area limit before any write. The upload is written to a
temporary name and renamed on success. Downloads set `Content-Disposition: attachment` and stream from
a bounded buffer, so file content cannot run as a page from the device origin.

## Common pages

Crux provides a small page set: sign-in, status, configuration, files, accounts and certificates.
Firmware appears after M9. The web interface has no first-credential setup. In the reference
configuration the first credential is set over the serial console, and until then every network login
is rejected, so an unprovisioned device has no network path to take over. A product that needs another
setup path designs and reviews it as its own decision. The
pages are static files that call only the APIs above, so a project can replace them without changing
firmware logic.

Projects add pages through a build step that bundles, compresses and hashes assets into read-only
data. The default keeps pages in the firmware image, so they are covered by image signing and updated
with the firmware. Pages on removable storage are excluded, because they would load code from a
medium that image verification does not cover.

The reference is plain HTML, CSS and JavaScript with no runtime dependency, because each dependency
adds size and upstream fixes to track. A project may build its bundle with any tool, as long as the
result satisfies the size limit and the content policy.

## Certificate model

At first boot the device generates an ECDSA P-256 key and a self-signed certificate if none exists.
Browsers show a warning until an operator replaces it, and documentation tells users to compare the
fingerprint, which is also available on the serial console. An operator can create a CSR on the device
and install the signed certificate, or upload a certificate that matches the device key. A bad
certificate is rejected and the previous one stays active.

An uploaded certificate is checked only for format and for a match with the device key. That does not
prove that a client will trust it. The device does not verify peer certificates in this track, so it
has no validity check that depends on its clock. It keeps serving when its own certificate expires and
reports the expiry, and browsers will warn until the certificate is replaced.

## Firmware upgrade

This is available only when M9 delivers its path. The route streams the image to the M9 staging path,
and it does not use the file service. It reports the state from the M9 image-state contract. Image
signature checks stay with the bootloader and the update path, and a valid session never replaces
them. Confirmation of a test image and timing around the control application stay application policy.
Authorisation and rejection have their own tests.

## Failure handling

| Failure | Result |
| --- | --- |
| Client limit reached | Connection refused or `503`, control work unaffected |
| TLS handshake failure | Connection closed, counted, event rate limited |
| Storage missing or full | `503` or `507`, temporary file removed where possible |
| Upload aborted | Temporary file removed, or removed at the next start if power failed |
| Malformed request or oversized header or body | Rejected and connection closed. `400` or `413` where the adapter can respond, otherwise the server's own behaviour, recorded by a test. |
| Credential store unavailable | All logins rejected, event recorded |

## Testing

- **Host:** route table, permission enforcement, session and CSRF rules, JSON parsing limits, secret
  absence, using a fake transport
- **`native_sim`:** real TLS with `curl` where available, both provider profiles running the same
  suite, listener check for no plain HTTP
- **Negative:** missing or expired session, wrong role, missing CSRF token, wrong `Origin`, oversized
  bodies, path traversal in area and file names, slow clients, connection floods
- **Target, with separate approval:** handshake time, concurrent browser load, upload stall, jitter
  under management load, reboot during upload
- **Browser:** the common pages against a real device build, checking the content policy has no
  violations
