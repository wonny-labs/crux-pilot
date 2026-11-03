# Web frontend foundation design

Status: proposal for review, candidate milestone M13. It refines the
[web design](secure-management-web-design.md), and it replaces that document's page set and reference
technology where they differ. Nothing here authorises implementation, dependency adoption or hardware
operations.

## Outcome

A project can give a controller a browser interface with little effort. Crux supplies a working reference
interface and everything underneath it, and the project adds its own screens and APIs in a framework that it
chooses.

## Decisions

| ID | Decision | State |
| --- | --- | --- |
| W1 | The reference interface starts with Preact and TypeScript | Owner, 3 November 2026 |
| W2 | Angular should also be supported where that is possible | Owner, 3 November 2026 |
| W3 | The first screens are a home screen, network settings and reboot | Owner, 3 November 2026 |
| W4 | Sign-in and forced password change are added to W3, because the other screens cannot be reached without them | Proposal |
| W5 | Frontend work has its own milestone, M13, and the existing M11 and M12 keep their numbers | Proposal |
| W6 | Angular support means the API, packaging and security settings work with it, shown by a minimal sample in CI. Crux ships no Angular components. | Proposal |
| W7 | Network settings are saved and take effect at the next boot. There is no live apply. | Proposal |
| W8 | Static IPv4 comes first, and DHCP is a separate later item | Proposal |
| W9 | A composition must state its reboot policy explicitly | Proposal |
| W10 | Crux's own frontend tooling is `tsc`, esbuild and a small mock server, and it does not use Vite | Proposal |

W1 changes the plain HTML reference in the web design. A framework is justified here because hand-written DOM code
is where cross-site scripting mistakes occur as a screen grows, while Preact escapes output by default. It adds one
package with no runtime dependencies, in metadata read on 3 November 2026.

## Responsibilities

| Item | Crux | Project |
| --- | --- | --- |
| HTTPS, sessions, permissions, response headers | Provides, in M11 | Sets policy and limits |
| Management API and its specification | Provides | Adds its own routes with `CRUX_WEB_ROUTE` |
| TypeScript API client | Provides, with no framework dependency | Uses it, or generates its own from the specification |
| Asset packaging into firmware | Provides | Supplies a built folder from any tool |
| Reference interface: sign-in, home, network, reboot | Provides, as source to reuse or replace | Extends, restyles or replaces |
| Controller screens, branding, languages, roles | | Owns |
| Files, accounts, certificates and firmware screens | Later, each after its API | May build its own sooner |
| Angular components, or a UI component library | Not provided | Owns |

The firmware build needs no Node.js. It takes a built folder. A project that uses no web interface pays for none of this.

## Structure

```text
Project UI                 Preact or Angular screens
Reference interface        web/reference, Preact and TypeScript            M13
TypeScript client          web/client, session, login polling, CSRF        M13
API specification          OpenAPI file, shared by both sides              M11
Asset packaging            folder to firmware data, headers, budgets       M11
crux_web and services      routes, sessions, status, network, reboot       M11
```

A change to the specification is one reviewed PR that both tracks rebase onto, like any other shared contract.

## First screens

Permission names are proposals. The status response follows the status service proposal, in which that route is
session-only and the service filters items by permission.

| Screen | API | Permission | Backend state |
| --- | --- | --- | --- |
| Sign-in | `POST /api/v1/session`, attempt polling, `DELETE`, `GET /api/v1/session/me` | Public for login | #186 |
| Forced password change | Change own password | `access.self` | #186, #187 |
| Home | `GET /api/v1/status` | Session-only, filtered per item | #187, with the status service from #180 |
| Network settings | `GET`, `PUT /api/v1/network` | `network.read`, `network.write` | New service |
| Reboot | `POST /api/v1/system/reboot` | `system.reboot` | New service |

Account, file, certificate, firmware and console screens are outside this first set. Account management stays on
the CLI until then. The first credential stays on the serial console, as the access contract requires.

## Network settings

The `crux_netif` contract is read-only, and that stays. Samples set their IPv4 address in native code, with DHCP
disabled, so writing settings is a new capability.

| Topic | Proposal |
| --- | --- |
| Values | IPv4 address, prefix length and gateway, stored with the existing `U32` Configuration type |
| Validation | Unicast address, a usable prefix, and a gateway inside the subnet. Reject with field errors. |
| Apply | Saved only. The composition reads them at boot and applies them in private native code. |
| Display | Both running and saved values, with a notice that a reboot is needed |
| Bad or missing values | The composition's default values are used, and status reports the source |
| Recovery from a wrong address | A serial command to clear the saved values, added under #181 |
| Events | A change is a security event, with no secret involved |

W7 avoids a live change that would cut off the user's own session and leave the device unreachable with no way to
confirm. A confirm-or-revert apply can be added later.

**Dependencies and limits.**

- Saving on the target needs a Configuration provider, which Issue #16 has not decided. Until then, this works only on
  the Host and `native_sim`, and the target returns not supported.
- An address change can invalidate a certificate naming the old address, so the certificate work in #190 must state how
  this behaves.
- DHCP is disabled in the Crux network samples today and has no evidence, which is why W8 defers it.

## Reboot

`crux_system_reset` exists, and its contract says that it does not flush storage, drain logs or perform safe-state
actions. A web request therefore goes through a service.

1. Check `system.reboot` and the composition's reboot policy.
2. Record the event and reply `202`, so that the browser gets an answer.
3. After a short delay on a worker, give events and pending saves a bounded time to finish, then call `crux_system_reset`.
4. The browser polls until the device answers, and then shows sign-in, because sessions do not survive a reset.

**Policy (W9).** Rebooting a controller while it runs a machine is a safety matter that only the application can judge.
A composition must therefore either register a callback that can refuse, returning `409` with a reason, or select an
explicit always-allow option. It cannot simply leave this unset. The screen asks for confirmation, but that is a usability
step and not a security control, since the server checks everything.

On the Host, `crux_system_reset` returns not supported, so tests observe the service's steps with a fake.

## What the web foundation must do for Angular

These change M11 issues and they also help any other framework. Angular facts are from its security guide, and none has
been tested here yet.

| Topic | Fact | Requirement |
| --- | --- | --- |
| Response headers | A Zephyr static resource has no custom headers, while a dynamic response does | Serve assets through the dynamic path, with headers and `Content-Encoding` set by the adapter |
| Style policy | Angular inserts styles at run time, so it needs a per-request nonce or `'unsafe-inline'` in `style-src` | A composition setting with two values: strict, or inline styles allowed. Scripts stay `'self'` in both, and no free-form policy string is offered. |
| CSRF | `HttpClient` by default reads a JavaScript-readable `XSRF-TOKEN` cookie and sends `X-XSRF-TOKEN` on state-changing requests | Use those names. The session cookie stays `HttpOnly`, and the TypeScript client does the same. |
| Deep links | A browser reload on an application path needs `index.html` | Requests outside `/api/` that match no asset return `index.html`, and `/api/` returns JSON errors |
| Connections | Lazy loading creates many files, and the server default is three clients | A packaging budget for file count and compressed size, failing the build when exceeded |
| Header capture | Two cookies and more headers exceed the 128-byte, three-header default | Size them against real browsers, as #184 already requires |

The strict setting is the default. Choosing the inline style setting weakens only style injection, and a project should
record that choice. A nonce would need server-side templating, which the web design excludes.

**Sample.** `samples/web/angular-minimal` holds sign-in and home using the generated types, and is built in its own CI job
with its own lockfile. It does not reach feature parity, and Angular versions move only when that sample is updated. It
also gives the first measured comparison with Preact: compressed size, file count, policy violations and production
package count.

## Tooling and supply chain

Build tools do not ship in the product, but they do produce what ships. The aim is few packages that are easy to review.

| Rule | Detail |
| --- | --- |
| Pinned | Node.js version and a committed lockfile, installed with `npm ci` |
| Scripts | `--ignore-scripts` if esbuild's platform package works without its install step, which needs checking |
| Build | `tsc` for types and esbuild for the bundle. esbuild has no runtime dependencies, whereas Vite with its Preact preset adds more packages. |
| Runtime | Preact only. Routing and state are small local code at first. |
| Assets | No external script, style or font, enforced by a browser test and the content policy |
| DOM | No `innerHTML` or `dangerouslySetInnerHTML` in the reference, checked by a script |
| SBOM | A CycloneDX record for production packages, plus the packager's list of asset hashes |
| Advisories | `npm audit` for production packages in CI, reviewed and not blindly auto-upgraded |

The bundled assets sit in the firmware image, so they are covered by image signing. Fixing a frontend vulnerability needs a
firmware update, and the product's support plan has to include these packages. No long-term support policy was found for
Preact, so a product should expect to patch a pinned version itself if needed.

Crux keeps no built output in the repository. A firmware composition names a built folder. If it asks for assets and
the folder is missing, configuration fails.

## Testing

| Level | Covers | Needs |
| --- | --- | --- |
| Client unit tests | Login polling, CSRF, error mapping, retry timing | Node.js |
| Specification check | The mock server and the firmware adapter both conform to the OpenAPI file | M11 for the firmware side |
| Browser tests on the mock | Each screen, a denied permission, an expired session and the reboot wait | A browser, in CI |
| Browser tests on `native_sim` | The same tests against real TLS and real sessions, and policy violations | #184, #186, #187 |
| Packaging tests | Headers, encodings, budgets and `index.html` fallback | M11 |
| Angular sample | Sign-in and home under the inline style setting | Separate CI job |
| Target, separately approved | Load time, connections, Flash and RAM on RT1060-EVKB | One board, one session at a time |

## Parallel work

Frontend work needs the specification and not the firmware, so it can begin before gate #183. It uses a mock server until the
real one is available.

| Session | Work | Starts when |
| --- | --- | --- |
| Frontend | M13 | The API specification is reviewed |
| HTTPS | M11, including the specification first | The specification can start now, and the rest after gate #183 |
| SSH | #192, #193 | Gate #183 and #205 |
| SFTP | #194, then #195 | Gate #183, #205 and #179 |

The frontend session owns `web/` and `samples/web/`, and it does not touch `components/`. End-to-end browser tests need M11's
first issues, so they come last. Limited RAM and disk on one development machine mean that the four sessions should not all run
Zephyr builds at once. Frontend builds are light, and Zephyr builds are the expensive ones.

## Why a separate milestone

M12 already holds the CLI and SFTP work, so renumbering it would break existing references. Numbers do not set the order, so
M13 can finish before M12. A separate milestone fits because the toolchain, evidence and likely owner differ, and because M11 can
then exit on the backend alone. Asset packaging stays in M11 because it is firmware work.

## Open questions, with working defaults

| Question | Default |
| --- | --- |
| Languages | English text in one table, ready for translation |
| Browsers | Current Chrome, Edge and Firefox |
| Separate npm packages for the client and reference | Same repository at first, not published |
| Status refresh | Polling every few seconds, kept low because of the single server thread |
