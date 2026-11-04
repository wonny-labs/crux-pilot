# Crux web frontend

Everything a controller's browser interface needs on the frontend side. The firmware build needs none of it: it takes
a built folder, and no C build runs Node.js.

| Folder | What it holds |
| --- | --- |
| [`api`](api/README.md) | The management API specification, shared with the firmware track |
| `client` | A TypeScript client with no framework: login polling, CSRF, error mapping and retry timing |
| `reference` | The reference interface in Preact: sign-in, forced password change, home, network settings and reboot |
| `mock` | A small mock server that follows the specification, and its conformance check |
| `e2e` | Browser tests on the mock |
| `tools` | Type generation, the build, the packaging budget and the source rules |

The [implementation design](../docs/plans/web-frontend-implementation.md) has the diagrams and decisions, and it says how
a project replaces the reference interface.

## Set up

Use the Node.js version in `package.json`. It is pinned exactly, and the same version pins the specification lint and
the Angular sample. Lifecycle scripts stay off through `.npmrc`, and esbuild works without its install step.

```sh
cd web
npm ci
```

## Commands

| Command | What it does |
| --- | --- |
| `npm run check` | Pins, generated types, `tsc`, source rules, unit tests and the build, with its budget |
| `npm run test:browser` | Browser tests of the reference on the mock |
| `npm run test:angular` | The same browser cases on the Angular sample, once it is built |
| `npm run gen:types` | Regenerate `client/src/api-types.generated.ts` after a specification change |
| `npm run build` | Build `reference/dist`. It fails over the budget in `reference/budget.json`. |
| `npm run dev` | Build, then serve the reference from the mock on <http://127.0.0.1:8080> |
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

## The mock server

`npm run mock` listens on the loopback address only, over plain HTTP. The accounts are `operator`, `viewer`, `writer`,
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
results from the mock do not stand in for them.
