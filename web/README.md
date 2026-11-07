# Crux web frontend

Examples and tooling for a browser interface on a controller. Crux provides management services, APIs and HTTPS hosting,
and the product owns its application UI, in any framework. Nothing here is a product-ready interface, a common page set or
a design system. The firmware build needs none of it: it takes a built folder, and no C build runs Node.js.

| Folder | What it holds |
| --- | --- |
| [`api`](api/README.md) | The management API specification, shared with the firmware track |
| `client` | An optional TypeScript client with no framework: login polling, CSRF, error mapping and retry timing |
| `mock` | A small mock server that follows the specification, and its conformance check |
| `e2e` | Browser tests on the mock, and the same cases on a `native_sim` image over HTTPS |
| `tools` | Type generation, the build, the packaging budget and the source rules |

The two examples are outside this folder, in `samples/web`: the Preact example in [`preact`](../samples/web/preact) (sign-in, forced
password change, home, configuration, and network settings and reboot as advanced examples) and the
[Angular example](../samples/web/angular/README.md). This folder holds what both use: the specification, the client, the mock
server and the browser tests. The Preact example takes its one dependency, `preact`, from this folder's install. The
[implementation design](../docs/plans/web-frontend-implementation.md) has the diagrams and decisions, and it says how
a product builds its own interface or starts from an example.

## Core and Advanced flows

Core and advanced flows are in both examples and in the shared browser cases, with service tests underneath. The advanced flows
are described in [the advanced flows](../docs/web-advanced-flows.md).

| Flow | Kind | Preact example | Angular example |
| --- | --- | --- | --- |
| Sign-in and sign-out | Core | Built | Built |
| Session expiry | Core | Built | Built |
| Forced password change | Core | Built | Built |
| Status | Core | Built | Built |
| Simple configuration update | Core | Built, on the mock | Built, on the mock |
| Network settings | Advanced | Built | Built |
| Reboot | Advanced | Built | Built |

## Evidence

Each state is recorded on its own, and none stands in for another. The flow-by-flow table is in the
[implementation design](../docs/plans/web-frontend-implementation.md#core-and-advanced-evidence).

| State | What it shows | Today |
| --- | --- | --- |
| Mock | The examples and the client against the specification, over plain HTTP on the loopback address | Run in CI |
| Host | The management services in C and their web routes, through a fake transport | Run in the Host CI job |
| `native_sim` | Real TLS, sessions and limits with a browser | Both examples packaged and run over HTTPS in Chrome, #220: [end-to-end design](../docs/e2e-native-220.md) |
| Physical | RT1060-EVKB, with separate approval | Not run |

Host and `native_sim` functional completion is an interim result. Physical evidence stays tracked separately and is not
waived.

## Set up

Use the Node.js version in `package.json`. It is pinned exactly, and the same version pins the specification lint and
the Angular example. Lifecycle scripts stay off through `.npmrc`, and esbuild works without its install step.

```sh
cd web
npm ci
```

## Commands

| Command | What it does |
| --- | --- |
| `npm run check` | Pins, generated types, `tsc`, source rules, unit tests and the build, with its budget |
| `npm run test:browser` | Browser tests of the Preact example on the mock |
| `npm run test:angular` | The shared browser cases on the Angular example, once it is built |
| `npm run test:native` | The shared and real-server cases on `native_sim` images named by `CRUX_E2E_IMAGE_PREACT` and `CRUX_E2E_IMAGE_ANGULAR`. `scripts/test-native-composition.sh e2e-mbed` builds the images and runs them. |
| `npm run gen:types` | Regenerate `client/src/api-types.generated.ts` after a specification change |
| `npm run build` | Build `../samples/web/preact/dist`. It fails over the budget in `../samples/web/preact/budget.json`. |
| `npm run dev` | Build, then serve the Preact example from the mock on <http://127.0.0.1:8080> |
| `npm run sbom` | Print a CycloneDX record of the production packages |
| `npm run audit:prod` | Audit the production packages |

The browser tests use playwright-core, which downloads nothing, with Chrome from the machine. Set `CRUX_CHROME` to the
path of another Chromium-based browser. They do not run on Firefox, because playwright-core cannot drive a stock Firefox.

## Using the client

The client has no framework dependency and holds no secret. It returns a result and does not throw for an HTTP or
network failure.

```ts
import { createClient } from './client/src/index.ts';

const client = createClient({ onSessionLost: () => showSignIn() });

const outcome = await client.login(user, password);
if (outcome.kind === 'delayed') {
  showWait(outcome.retryAfterSeconds);
}

const status = await client.getStatus();
if (status.ok) {
  render(status.value.items);
} else {
  showError(status.error.kind);
}
```

The configuration calls follow the same rule. `listConfig` and `getConfigItem` read, `putConfigItem` changes a value in the
device's memory, and `saveConfig` then `watchConfigSave` write it, with the outcome as a result and never a throw. The
[configuration flow](../docs/web-config-flow.md) says how the examples use them.

## The mock server

`npm run mock` listens on the loopback address only, over plain HTTP. The configuration routes keep their parameters and the state of a save in the mock, and `writer` may write them while `viewer` may read them. The accounts are `operator`, `viewer`, `writer`,
`guest`, `newuser` and `disabled`, each with the password `example-password-not-real`. They differ in permissions, and
`newuser` has to change its password.

To give a failure by hand, post to the control route. Any operation, status and example in the specification can be
named:

```sh
curl -X POST http://127.0.0.1:8080/__mock/control \
  -d '{"action":"force","operation":"requestReboot","responses":[{"status":423,"example":"refused"}]}'
curl -X POST http://127.0.0.1:8080/__mock/control -d '{"action":"set","config":{"delaySeconds":30}}'
curl -X POST http://127.0.0.1:8080/__mock/control -d '{"action":"expire"}'
curl -X POST http://127.0.0.1:8080/__mock/control -d '{"action":"reset"}'
```

The mock shows nothing about TLS, sessions, connection limits or timing on a real device. Those need the firmware, and
results from the mock do not stand in for them. The [end-to-end design](../docs/e2e-native-220.md) runs the same cases on
`native_sim`, and records each target's results apart.
