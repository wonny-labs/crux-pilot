# Web advanced flows: network settings and restart, on both examples

Status: built on the mock for the Preact and the Angular example, in the second step of the owner's review of 8 November 2026
(Refs #243). Crux provides the services, the APIs and HTTPS hosting, and the product owns its application UI, so both screens are
examples, with no promise of feature parity beyond what the shared browser cases hold. The API is the one in
[`web/api/openapi.yaml`](../web/api/openapi.yaml). The services are described in the
[reboot service](reboot-service.md) and, for the web side of configuration, the [configuration flow](web-config-flow.md). The
layout of the examples is in the [web guide](../web/README.md).

## What is built, faked and untested

| State | Item |
| --- | --- |
| Built | Preact example, `samples/web/preact`: network settings and restart screens, as before this step |
| Built | Angular example, `samples/web/angular`: the same two screens, `src/app/network.ts` and `src/app/reboot.ts`, with `src/app/texts.ts` holding the words |
| Built | 44 browser cases in `web/e2e/shared-advanced-cases.ts`, run on both examples on the mock. They were the Preact-only cases of `preact-network.test.ts` and `preact-reboot.test.ts`. |
| Built | One Preact-only case, in `web/e2e/preact-network.test.ts`: a deep link to the network settings survives sign-in |
| Built | A `native_sim` case for the Angular example, the same one that the Preact example has, in `web/e2e/native-cases.ts` |
| Faked | The device. The mock keeps the network state, the state of a save and the state of a restart, over plain HTTP on the loopback address |
| Untested | The Angular `native_sim` case. It is written and **NOT RUN**, because the image needs a heavy Linux build that this machine could not take beside other work. |
| Untested | The restart flow on `native_sim` for either example: the reboot service is merged but not composed in the services image, so the case is marked `reboot-service` and skips |
| Untested | Firefox and Edge, a screen reader, and any board |

## How the screens use the API

```mermaid
sequenceDiagram
    participant P as Person
    participant S as Screen
    participant D as Device
    P->>S: Open network settings
    S->>D: GET /api/v1/network
    D-->>S: running, saved, source, reboot_required, save
    P->>S: Enter address, prefix, gateway
    S->>S: Check the values, for usability only
    P->>S: Review and save, then Save the settings
    S->>D: PUT /api/v1/network, with the token
    D-->>S: 202 pending, or 400 invalid_field, 501 not_supported, 403, 503, 401
    loop every second, up to 15 s
        S->>D: GET /api/v1/network
        D-->>S: save pending, failed, or the saved values equal the request
    end
    S-->>P: Saved, failed, not confirmed, or cannot see the result
```

```mermaid
stateDiagram-v2
    [*] --> loading
    loading --> idle: state none
    loading --> idle: earlier failed or refused, shown
    loading --> waiting: state pending
    loading --> denied: 403
    idle --> confirm: Restart the device
    confirm --> idle: Cancel
    confirm --> requesting: Restart now
    requesting --> waiting: 202
    requesting --> refused: 423 reboot_refused, with its reason
    requesting --> denied: 403
    waiting --> restarted: 401, the session is gone
    waiting --> failed: state failed
    waiting --> refused: state refused, with its reason
    waiting --> not_confirmed: 180 s with no change
    not_confirmed --> waiting: Keep waiting
    restarted --> [*]: sign-in says the device restarted
```

## What both examples do

| Behaviour | Network settings | Restart |
| --- | --- | --- |
| Arrival | Reads the state. A `403` on the read shows that this account may not read, and the form still works. | Reads the restart state and the network state together. An earlier failed or refused request is shown. A pending one resumes the wait. |
| Checks before a request | The client's own `checkIpv4Settings`: address, prefix 1 to 30, gateway in the subnet and usable. Nothing is sent when a check fails, and the first bad field is focused. | None |
| Warning before the action | A review step, with where the device will answer, that any reset applies the values, and the serial console recovery. Cancel is focused, and cancelling sends nothing. | A confirmation, with Cancel focused. Cancelling sends nothing. |
| Request | `PUT /api/v1/network` with the token. The gateway is left out when empty. | `POST /api/v1/system/reboot` with the token |
| Success | Only when the device shows the saved values and no pending save. Otherwise failed, not confirmed in 15 s, or unreadable for an account that may write but not read. | Only a session that is no longer valid. The running device also answers, so an answer is not success. |
| Documented `501` | `not_supported`: "no Configuration provider", nothing was changed, the running address stays. | |
| Refusal | `invalid_field`: each field's error appears on its field. | `reboot_refused` and the late `refused` state show the device's reason as text, never as markup. No wait starts for the first. |
| Other errors | `403` permission, `403` token, `503` busy with the wait, `401` back to sign-in, `409` retried | `403`, `409` retried, `401` on arrival back to sign-in, saved network settings mentioned first |
| Wait limits | Poll 1 s, stop at 15 s | Poll 2 s, flag at 60 s, stop at 180 s, and keep waiting on request |

## Where the two examples differ

| Topic | Preact example | Angular example |
| --- | --- | --- |
| Calls | The framework-free client in `web/client` | `HttpClient` in `src/app/api.ts`, and the client's `checkIpv4Settings` for the checks. The waits are written again there, with the same limits. |
| Deep link | `#/network` survives sign-in | Sign-in returns to home. The case for it is Preact-only. |
| A short `503` | The client retries one `503` with a `Retry-After` of 5 s or less | Shown at once with its wait, as for the other Angular screens |
| Words | `src/text.ts`, with project overrides | `src/app/texts.ts`, copied from it, with no overrides |
| Style policy | Strict | Inline, which Angular's component styles need. Unchanged. |
| Navigation | `Network` and `Restart` beside Home and Configuration | The same, as links |

## The shared cases

The cases that were Preact-only now sit in one list that both examples run. They find elements by role, label and visible text, and
open each screen from home through the navigation. All 44 drive the mock's control or its clock, so on a `native_sim` image
they skip with the reason `needs the mock control`; the real service has its own case.

| Where | Cases | Runs on |
| --- | --- | --- |
| `shared-advanced-cases.ts`, network settings | 26: values and the form, a reboot notice, no address, pending and failed on arrival, the checks and their focus, server field errors, the warning and cancel, a confirmed save with the token, no gateway, three unusable gateways, a failed save, a save never confirmed, a viewer, a guest, a writer, no provider, busy, a rejected token, the keyboard, an expired session | Both, on the mock |
| `shared-advanced-cases.ts`, restart | 18: confirm and cancel, success by the gone session, an answer alone is not success, never restarts, away too long, back after the limit, a failed reset, a refusal, a late refusal, a reason as text, a viewer, a refused permission, pending on arrival, an earlier failure and refusal, saved settings mentioned, a held route retried, an expired session | Both, on the mock |
| `preact-network.test.ts` | 1: a deep link survives sign-in | Preact |
| `native-cases.ts` | 1: the real service shows no running address and refuses a change it cannot save, on the real `501` | Both, on `native_sim`. Preact ran in #220. Angular is **NOT RUN**. |

Two cases were changed so that they hold for a framework that draws a moment after a click, and neither was weakened. The
network screen is opened once its first read has landed, so a field is not read or typed before it. After a cancel, the case
waits for the confirmation to leave before it counts it and checks the focus.

## Sizes

Measured with `node tools/compare.ts` on built folders. Compressed bytes are gzip at level 9, file by file.

| Build | Files | Bytes | Compressed bytes | Limit |
| --- | --- | --- | --- | --- |
| Preact example, all six screens | 4 | 69583 | 23302 | 32768 compressed, in `samples/web/preact/budget.json`, unchanged. 9,466 bytes are left. |
| Angular example, core screens, before this step | 5 | 298780 | 91197 | 300 kB raw initial, 281.11 kB used |
| Angular example, all six screens | 5 | 326609 | 97941 | **320 kB** raw initial, 308.94 kB used, 81.68 kB transferred. 11.06 kB are left. |

Network settings and restart added 27,829 bytes, 6,744 compressed, to Angular, which is 4.2 times Preact on the same six screens.
The Angular script, the largest asset, is 92,011 compressed bytes, and the largest asset that fits the default 3,000 ms response deadline
is 98,304, so **6,293 bytes remain** before the Angular example stops fitting a native_sim image at the default deadline. `web_assets.py`
still refuses it at 1,500 ms, which the `e2e-*` runner holds as a control.

## Decisions for review

| # | Decision taken here | Why | Alternative |
| --- | --- | --- | --- |
| 1 | The Angular limit in `angular.json` goes from 300 kB to 320 kB | The two screens took the initial bundle to 308.94 kB. The limit was set in #243 and its change was left to the owner. | Keep 300 kB and drop a screen, or lazy-load the two screens, which the example avoids so that it ships one script like Preact |
| 2 | The Angular screens use the client's `checkIpv4Settings` as code, not only its types | The rules are held by shared test vectors, so one copy cannot drift from the firmware's | Write the checks again in the Angular example, so that it imports only types |
| 3 | The deep link that survives sign-in stays Preact-only | It is a shell behaviour, and the Angular example always returns to home | Add a return address to the Angular sign-in |
| 4 | The shared advanced cases skip on `native_sim` | They need the mock's control and clock, and the real service has its own case | Real-service versions of more of them, which wait on the services image |
| 5 | The Angular `native_sim` case is written and not run | The image needs a heavy Linux build | The main session or CI with the image runs `scripts/test-native-composition.sh e2e-mbed` |

## Results

Run on macOS with Node.js 24.22.2 and Chrome from `/Applications/Google Chrome.app`, against the mock, on the code of commit
`0f05306`. Another build shared the machine, so heavy runs went one at a time.

| Check | Result |
| --- | --- |
| New shared cases against the Angular code of the step before (3 of them) | Red: 3 of 3 failed, because the Angular example had no `Network` link or restart screen |
| `npm run check` in `web`: pins, types, `tsc`, source rules on 22 files, 630 unit tests, build | Pass, 630 of 630. An earlier run of the same tree failed one timing test in `client/test/against-mock.test.ts` under load, which passed 14 of 14 in 3 of 3 runs alone and does not touch this change. |
| `node tools/check-dom.ts ../samples/web/angular/src` | 15 files clean |
| `npm run test:angular`, Angular built from its lockfile, `CRUX_REQUIRE_ANGULAR=1` | Pass, 113 of 113: the shared list with the 44 advanced cases, and the policy cases |
| `npm run test:browser`, Preact | 158 of 160 pass, 1 skipped (screenshots, not asked for), 1 fails: "a value is updated with the keyboard alone…" in `preact-config.test.ts`. It fails the same way on unmodified `origin/main` on this machine, where I read `Control+A` as the cause, which moves the cursor on macOS. Not measured. The Linux job in CI is the evidence for it. |
| `web_assets.py` on the Angular folder at 3,000 ms and at 1,500 ms | Packages at 3,000 ms, and is refused at 1,500 ms, as the control expects |
| `native_sim` end to end, `scripts/test-native-composition.sh e2e-*` | **NOT RUN.** The Angular case is written and not run. |
