// The mock against the specification. Every named example is served as written, every stateful answer
// matches its schema, and the check itself is shown to reject answers that are wrong.
import assert from 'node:assert/strict';
import { after, afterEach, before, test } from 'node:test';
import { checkResponse } from '../conformance.ts';
import { caller, spec } from './support.ts';
import type { Caller } from './support.ts';

const CRUX_HEADERS = ['content-security-policy', 'x-content-type-options', 'referrer-policy', 'cache-control'];
const ATTEMPT_ID = 'k3Jq9sZp0vXw2LmN7bYt1A';

function pathOf(path: string): string {
  return path.replace('{id}', ATTEMPT_ID);
}

let harness: Caller;
before(async () => {
  harness = await caller();
});
after(async () => {
  await harness.mock.close();
});
// Every answer in this file also went through the check, and none may have failed it.
afterEach(() => {
  assert.deepEqual(harness.mock.conformanceProblems, []);
});

for (const operation of spec.operations.values()) {
  for (const response of operation.responses.values()) {
    const names = response.examples.size > 0 ? [...response.examples.keys()] : [undefined];
    for (const name of names) {
      test(`${operation.id} ${response.status} ${name ?? '(no body)'} is served as the specification writes it`, async () => {
        harness.mock.control.reset();
        harness.jar.clear();
        const forced = name === undefined ? { status: response.status } : { status: response.status, example: name };
        harness.mock.control.force(operation.id, forced);
        const requestBody = [...operation.requestExamples.values()][0];
        const reply = await harness.call(
          operation.method,
          pathOf(operation.path),
          requestBody === undefined ? {} : { body: requestBody },
        );
        assert.equal(reply.status, response.status);
        const example = name === undefined ? undefined : response.examples.get(name);
        if (example !== undefined) {
          assert.deepEqual(reply.body, example.value);
          assert.deepEqual(reply.setCookie, example.setCookie ?? response.setCookie ?? []);
          assert.match(reply.headers.get('content-type') ?? '', /application\/json/);
        } else {
          assert.equal(reply.text, '');
          assert.deepEqual(reply.setCookie, response.setCookie ?? []);
        }
        for (const header of response.requiredHeaders) {
          assert.ok(reply.headers.has(header), `${header} is required`);
        }
        if (response.status === 409) {
          for (const header of CRUX_HEADERS) {
            assert.equal(reply.headers.has(header), false, `${header} is absent on the server's own 409`);
          }
          assert.equal(reply.headers.get('connection'), 'close');
        } else {
          for (const header of CRUX_HEADERS) {
            assert.ok(reply.headers.has(header), `${header} is on each API response`);
          }
        }
      });
    }
  }
}

test('every operation in the specification has a route in the mock, with no forced answer', async () => {
  assert.equal(spec.operations.size, 10);
  for (const operation of spec.operations.values()) {
    harness.mock.control.reset();
    harness.jar.clear();
    await harness.login();
    const requestBody = [...operation.requestExamples.values()][0];
    const reply = await harness.call(
      operation.method,
      pathOf(operation.path),
      requestBody === undefined ? {} : { body: requestBody },
    );
    assert.ok(operation.responses.has(reply.status), `${operation.id} answered ${reply.status}`);
    assert.ok(reply.headers.has('content-security-policy'), `${operation.id} got the server's plain answer`);
  }
});

test('the server headers have the values that the design states', async () => {
  harness.mock.control.reset();
  const reply = await harness.call('GET', '/api/v1/session/me');
  assert.equal(reply.headers.get('content-security-policy'), "default-src 'self'; frame-ancestors 'none'");
  assert.equal(reply.headers.get('x-content-type-options'), 'nosniff');
  assert.equal(reply.headers.get('referrer-policy'), 'no-referrer');
  assert.equal(reply.headers.get('cache-control'), 'no-store');
});

test('a path with no registered resource gets the plain 404, with no JSON and no Crux headers', async () => {
  const reply = await harness.call('GET', '/api/v1/nothing-here');
  assert.equal(reply.status, 404);
  assert.equal(reply.text, 'Not Found');
  assert.equal(reply.body, undefined);
  assert.equal(reply.headers.has('content-security-policy'), false);
});

test('a method the path does not support gets the plain 405', async () => {
  const reply = await harness.call('PATCH', '/api/v1/status');
  assert.equal(reply.status, 405);
  assert.equal(reply.text, 'Method Not Allowed');
  assert.equal(reply.body, undefined);
  assert.equal(reply.headers.has('content-security-policy'), false);
});

test('a path under a registered route gets the JSON 404', async () => {
  const reply = await harness.call('GET', '/api/v1/status/x');
  assert.equal(reply.status, 404);
  assert.deepEqual(reply.body, { code: 'not_found' });
  assert.ok(reply.headers.has('content-security-policy'));
});

test('a malformed attempt identifier gets the JSON 404', async () => {
  const reply = await harness.call('GET', '/api/v1/session/attempt/!!');
  assert.equal(reply.status, 404);
  assert.deepEqual(reply.body, { code: 'not_found' });
});

test('the mock listens on the loopback address only', () => {
  assert.match(harness.mock.url, /^http:\/\/127\.0\.0\.1:\d+$/);
});

// The check itself has to fail on wrong answers, or passing it means nothing.
const baseRecord = {
  path: '/api/v1/status',
  method: 'GET',
  operationId: 'getStatus',
  status: 200,
  contentType: 'application/json',
  headers: {
    'content-security-policy': 'x',
    'x-content-type-options': 'nosniff',
    'referrer-policy': 'no-referrer',
    'cache-control': 'no-store',
  } as Record<string, string>,
  setCookie: [] as string[],
  bodyText: JSON.stringify({ items: [] }),
};

test('the check accepts a correct answer', () => {
  assert.deepEqual(checkResponse(spec, baseRecord), []);
});

const wrong: [string, Partial<typeof baseRecord>][] = [
  ['an undeclared status', { status: 418 }],
  ['a body missing a required member', { bodyText: '{}' }],
  ['a body with an extra member', { bodyText: '{"items":[],"more":1}' }],
  ['an item with a wrong type', { bodyText: '{"items":[{"name":"a","status":"ok","type":"u32","value":"7"}]}' }],
  ['a u64 sent as a number', { bodyText: '{"items":[{"name":"a","status":"ok","type":"u64","value":7}]}' }],
  ['a body that is not JSON', { bodyText: 'nope' }],
  ['a JSON body with the wrong content type', { contentType: 'text/plain' }],
  ['a missing security header', { headers: { 'x-content-type-options': 'nosniff' } }],
];
for (const [name, change] of wrong) {
  test(`the check rejects ${name}`, () => {
    assert.notDeepEqual(checkResponse(spec, { ...baseRecord, ...change }), []);
  });
}

test('the check rejects a 409 with a body', () => {
  const record = { ...baseRecord, status: 409, bodyText: '{"code":"busy"}', headers: {} };
  assert.notDeepEqual(checkResponse(spec, record), []);
});

test('the check rejects a refused reboot without its reason', () => {
  const record = {
    ...baseRecord,
    operationId: 'requestReboot',
    status: 423,
    bodyText: '{"code":"reboot_refused"}',
  };
  assert.notDeepEqual(checkResponse(spec, record), []);
});

test('the check rejects a 429 without Retry-After', () => {
  const record = { ...baseRecord, operationId: 'getSessionAttempt', status: 429, bodyText: '{"code":"retry_later"}' };
  assert.notDeepEqual(checkResponse(spec, record), []);
});

test('the check rejects a logout whose 200 has a body', () => {
  const record = { ...baseRecord, operationId: 'deleteSession', status: 200, bodyText: '{}' };
  assert.notDeepEqual(checkResponse(spec, record), []);
});

const SESSION = 'crux_session=session-token; Secure; HttpOnly; SameSite=Strict; Path=/';
const TOKEN = 'XSRF-TOKEN=csrf-token; Secure; SameSite=Strict; Path=/';
const pollRecord = {
  ...baseRecord,
  operationId: 'getSessionAttempt',
  bodyText: '{"state":"success"}',
  setCookie: [SESSION, TOKEN],
};
const logoutRecord = {
  ...baseRecord,
  operationId: 'deleteSession',
  bodyText: '',
  contentType: null,
  setCookie: [
    'crux_session=; Secure; HttpOnly; SameSite=Strict; Path=/; Max-Age=0',
    'XSRF-TOKEN=; Secure; SameSite=Strict; Path=/; Max-Age=0',
  ],
};

test('the check accepts the cookies of a login and of a logout', () => {
  assert.deepEqual(checkResponse(spec, pollRecord), []);
  assert.deepEqual(checkResponse(spec, logoutRecord), []);
});

test('the check accepts a password change that sets no cookie', () => {
  assert.deepEqual(checkResponse(spec, { ...pollRecord, setCookie: [] }), []);
});

const wrongLogin: [string, string[]][] = [
  ['only the session cookie', [SESSION]],
  ['only the token cookie', [TOKEN]],
  ['a session cookie without HttpOnly', ['crux_session=a; Secure; SameSite=Strict; Path=/', TOKEN]],
  ['a session cookie without Secure', ['crux_session=a; HttpOnly; SameSite=Strict; Path=/', TOKEN]],
  ['a session cookie without SameSite=Strict', ['crux_session=a; Secure; HttpOnly; SameSite=Lax; Path=/', TOKEN]],
  ['a session cookie on a narrower path', ['crux_session=a; Secure; HttpOnly; SameSite=Strict; Path=/api', TOKEN]],
  ['a persistent session cookie', [`${SESSION}; Max-Age=3600`, TOKEN]],
  ['a session cookie with an expiry date', [`${SESSION}; Expires=Wed, 04 Nov 2026 10:00:00 GMT`, TOKEN]],
  ['an empty session cookie', ['crux_session=; Secure; HttpOnly; SameSite=Strict; Path=/', TOKEN]],
  ['a token cookie that scripts cannot read', [SESSION, 'XSRF-TOKEN=b; Secure; HttpOnly; SameSite=Strict; Path=/']],
  ['a token cookie without Secure', [SESSION, 'XSRF-TOKEN=b; SameSite=Strict; Path=/']],
  ['a persistent token cookie', [SESSION, `${TOKEN}; Max-Age=3600`]],
  ['a token equal to the session', ['crux_session=same; Secure; HttpOnly; SameSite=Strict; Path=/', 'XSRF-TOKEN=same; Secure; SameSite=Strict; Path=/']],
  ['a token holding a percent sign', [SESSION, 'XSRF-TOKEN=a%2Fb; Secure; SameSite=Strict; Path=/']],
  ['a token in surrounding quotes', [SESSION, 'XSRF-TOKEN="a"; Secure; SameSite=Strict; Path=/']],
  ['a cookie of another name', [SESSION, TOKEN, 'other=1; Secure; Path=/']],
];
for (const [name, setCookie] of wrongLogin) {
  test(`the check rejects a login with ${name}`, () => {
    assert.notDeepEqual(checkResponse(spec, { ...pollRecord, setCookie }), []);
  });
}

test('the check rejects cookies on a poll that is not a success', () => {
  assert.notDeepEqual(checkResponse(spec, { ...pollRecord, bodyText: '{"state":"pending"}' }), []);
  assert.notDeepEqual(checkResponse(spec, { ...pollRecord, bodyText: '{"state":"rejected"}' }), []);
});

test('the check rejects cookies on a response that declares none', () => {
  assert.notDeepEqual(checkResponse(spec, { ...baseRecord, setCookie: [SESSION, TOKEN] }), []);
  const plain = { ...baseRecord, operationId: null, contentType: 'text/plain', headers: {}, status: 404, bodyText: 'Not Found' };
  assert.notDeepEqual(checkResponse(spec, { ...plain, setCookie: [SESSION] }), []);
});

test('the check rejects a logout that does not clear both cookies', () => {
  const [session, token] = logoutRecord.setCookie as [string, string];
  assert.notDeepEqual(checkResponse(spec, { ...logoutRecord, setCookie: [session] }), []);
  assert.notDeepEqual(checkResponse(spec, { ...logoutRecord, setCookie: [] }), []);
  assert.notDeepEqual(checkResponse(spec, { ...logoutRecord, setCookie: [session.replace('; Max-Age=0', ''), token] }), []);
  assert.notDeepEqual(checkResponse(spec, { ...logoutRecord, setCookie: [session.replace('crux_session=', 'crux_session=a'), token] }), []);
  assert.notDeepEqual(checkResponse(spec, { ...logoutRecord, setCookie: [session.replace(' HttpOnly;', ''), token] }), []);
});

test('the check accepts the plain server answers and nothing else without headers', () => {
  const plain = { ...baseRecord, operationId: null, contentType: 'text/plain', headers: {} };
  assert.deepEqual(checkResponse(spec, { ...plain, status: 404, bodyText: 'Not Found' }), []);
  assert.deepEqual(checkResponse(spec, { ...plain, status: 405, bodyText: 'Method Not Allowed' }), []);
  assert.notDeepEqual(checkResponse(spec, { ...plain, status: 404, bodyText: 'Gone away' }), []);
  assert.notDeepEqual(checkResponse(spec, { ...plain, status: 500, bodyText: 'Oops' }), []);
});
