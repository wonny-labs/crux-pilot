# Minimal Angular sample

This sample shows that a project can use Angular with Crux. It holds sign-in and home, uses the types generated from
the [management API specification](../../../web/api/openapi.yaml), and passes the same browser cases as the Preact
reference on those screens. Crux ships no Angular components, and this is not a starting template for a product.

| Topic | How it is set here |
| --- | --- |
| Routing | `withHashLocation`, so the server sees only the root path and the assets |
| CSRF | `HttpClient`'s default names, `XSRF-TOKEN` and `X-XSRF-TOKEN`, which the API uses. Nothing is configured. |
| Attempt poll | `X-Crux-Request: 1`, sent by `src/app/api.ts` |
| Logout | `responseType: 'text'`, because its `200` has an empty body |
| Change detection | Zoneless, so `zone.js` is not a dependency |
| Build | `@angular/build`, with critical CSS inlining and subresource integrity off, because both would need more from the policy |

## Inline style setting

**This sample needs the inline style setting.** Angular inserts component styles into the page at run time, so a policy
without `'unsafe-inline'` in `style-src` blocks them. The composition that serves this sample must choose:

```text
default-src 'self'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'
```

Scripts stay `'self'`, and `frame-ancestors 'none'` stays. The setting weakens only style injection, which can still restyle
a page to mislead a user, and a project should record that it chose this. The Preact reference runs under the strict
setting. The measured effect is in the
[implementation design](../../../docs/plans/web-frontend-implementation.md#angular-comparison).

## Build and test

`node_modules` is about 240 MB, so delete it when finished. Use the Node.js version in `package.json`.

```sh
npm ci
npm run build
cd ../../../web
npm ci
npm run test:angular
```

The build writes `dist`, which the mock serves. Run `node tools/compare.ts` in `web` for the size table, after
`npm run build` and a build of `e2e/fixtures/two-screens.tsx` there.

## Limits

- It has no forced password change screen. A session that must change its password sees a notice saying so.
- It has no network or reboot screen.
- It has not been packaged into firmware. Issue #220 covers that, on `native_sim`.
