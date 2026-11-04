// The mock against the specification. Every named example is served as written, every stateful answer
// matches its schema, and the check itself is shown to reject answers that are wrong.
import assert from 'node:assert/strict';
import { after, before, test } from 'node:test';
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

test('every operation in the specification has a route in the mock', () => {
  assert.equal(spec.operations.size, 10);
  for (const operation of spec.operations.values()) {
    assert.ok(operation.responses.size > 0, operation.id);
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

test('the check accepts the plain server answers and nothing else without headers', () => {
  const plain = { ...baseRecord, operationId: null, contentType: 'text/plain', headers: {} };
  assert.deepEqual(checkResponse(spec, { ...plain, status: 404, bodyText: 'Not Found' }), []);
  assert.deepEqual(checkResponse(spec, { ...plain, status: 405, bodyText: 'Method Not Allowed' }), []);
  assert.notDeepEqual(checkResponse(spec, { ...plain, status: 404, bodyText: 'Gone away' }), []);
  assert.notDeepEqual(checkResponse(spec, { ...plain, status: 500, bodyText: 'Oops' }), []);
});
