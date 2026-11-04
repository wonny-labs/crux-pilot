import assert from 'node:assert/strict';
import { test } from 'node:test';
import { empty, harness, json, plain } from './support.ts';

function failure<T>(result: { ok: true; value: T } | { ok: false; error: unknown }) {
  assert.equal(result.ok, false);
  return (result as { ok: false; error: Record<string, unknown> }).error;
}

const errorCases: [number, string, string][] = [
  [400, 'bad_request', 'bad_request'],
  [401, 'unauthorized', 'unauthorized'],
  [403, 'permission_denied', 'permission_denied'],
  [403, 'csrf_rejected', 'csrf_rejected'],
  [403, 'origin_rejected', 'origin_rejected'],
  [404, 'not_found', 'not_found'],
  [413, 'payload_too_large', 'payload_too_large'],
  [501, 'not_supported', 'not_supported'],
];

for (const [status, code, kind] of errorCases) {
  test(`a JSON ${status} with code ${code} maps to the ${kind} error`, async () => {
    const { client } = harness([json(status, { code })]);
    const error = failure(await client.getStatus());
    assert.equal(error['kind'], kind);
    assert.equal(error['status'], status);
  });
}

test('field errors survive the mapping', async () => {
  const fields = [{ name: 'address', code: 'not_unicast' }];
  const { client } = harness([json(400, { code: 'invalid_field', fields })]);
  const error = failure(await client.putNetwork({ address: '224.0.0.1', prefix_length: 24 }));
  assert.equal(error['kind'], 'invalid_field');
  assert.deepEqual(error['fields'], fields);
});

test('a refused reboot keeps its reason', async () => {
  const { client } = harness([json(423, { code: 'reboot_refused', reason: 'Machine cycle running' })]);
  const error = failure(await client.requestReboot());
  assert.equal(error['kind'], 'reboot_refused');
  assert.equal(error['reason'], 'Machine cycle running');
});

test("the server's plain 404 is told apart from the JSON one and is not parsed", async () => {
  const { client } = harness([plain(404, 'Not Found')]);
  const error = failure(await client.getStatus());
  assert.equal(error['kind'], 'no_route');
  assert.equal(error['status'], 404);
});

test("the server's plain 405 maps to method_not_allowed and is not parsed", async () => {
  const { client } = harness([plain(405, 'Method Not Allowed')]);
  const error = failure(await client.getStatus());
  assert.equal(error['kind'], 'method_not_allowed');
});

test('a success body that is not JSON is unexpected, not a crash', async () => {
  const { client } = harness([new Response('<html>', { status: 200, headers: { 'Content-Type': 'text/html' } })]);
  assert.equal(failure(await client.getStatus())['kind'], 'unexpected');
});

test('malformed JSON with a JSON content type is unexpected', async () => {
  const { client } = harness([
    new Response('{', { status: 200, headers: { 'Content-Type': 'application/json' } }),
  ]);
  assert.equal(failure(await client.getStatus())['kind'], 'unexpected');
});

test('an error code outside the specification is unexpected', async () => {
  const { client } = harness([json(400, { code: 'made_up' })]);
  assert.equal(failure(await client.getStatus())['kind'], 'unexpected');
});

test('a thrown fetch maps to the network error', async () => {
  const { client } = harness([new TypeError('Failed to fetch')]);
  const error = failure(await client.getStatus());
  assert.equal(error['kind'], 'network');
  assert.equal(error['status'], undefined);
});

test('a 409 with no body is retried with growing delays, then succeeds', async () => {
  const { client, requests, sleeps } = harness([empty(409), empty(409), json(200, { items: [] })]);
  const result = await client.getStatus();
  assert.equal(result.ok, true);
  assert.equal(requests.length, 3);
  assert.deepEqual(sleeps, [300, 600]);
});

test('the busy retries stop at their bound and report busy', async () => {
  const { client, requests, sleeps } = harness([empty(409), empty(409), empty(409), empty(409)]);
  const error = failure(await client.getStatus());
  assert.equal(error['kind'], 'busy');
  assert.equal(error['status'], 409);
  assert.equal(requests.length, 4);
  assert.deepEqual(sleeps, [300, 600, 1200]);
});

test('jitter stretches a busy delay by at most a quarter', async () => {
  const { client, sleeps } = harness([empty(409), json(200, { items: [] })], { random: () => 1 });
  await client.getStatus();
  assert.deepEqual(sleeps, [375]);
});

test('a state-changing request is retried on 409, because it was not processed', async () => {
  const { client, requests } = harness([empty(409), json(202, { state: 'pending' })], {
    cookies: 'XSRF-TOKEN=t',
  });
  const result = await client.requestReboot();
  assert.equal(result.ok, true);
  assert.equal(requests.length, 2);
  assert.equal(requests[1]?.headers.get('X-XSRF-TOKEN'), 't');
});

test('a 503 busy with a short Retry-After waits that long and retries once', async () => {
  const { client, requests, sleeps } = harness([
    json(503, { code: 'busy' }, { 'Retry-After': '2' }),
    json(200, { items: [] }),
  ]);
  const result = await client.getStatus();
  assert.equal(result.ok, true);
  assert.equal(requests.length, 2);
  assert.deepEqual(sleeps, [2000]);
});

test('a 503 busy with a long Retry-After is reported at once', async () => {
  const { client, requests, sleeps } = harness([json(503, { code: 'busy' }, { 'Retry-After': '30' })]);
  const error = failure(await client.getStatus());
  assert.equal(error['kind'], 'busy');
  assert.equal(error['retryAfterSeconds'], 30);
  assert.equal(requests.length, 1);
  assert.deepEqual(sleeps, []);
});

test('a second 503 busy after the automatic retry is reported', async () => {
  const { client, requests } = harness([
    json(503, { code: 'busy' }, { 'Retry-After': '1' }),
    json(503, { code: 'busy' }, { 'Retry-After': '1' }),
  ]);
  const error = failure(await client.getStatus());
  assert.equal(error['kind'], 'busy');
  assert.equal(requests.length, 2);
});

test('a 503 not_ready is reported without a retry', async () => {
  const { client, requests } = harness([json(503, { code: 'not_ready' }, { 'Retry-After': '1' })]);
  const error = failure(await client.getStatus());
  assert.equal(error['kind'], 'not_ready');
  assert.equal(error['retryAfterSeconds'], 1);
  assert.equal(requests.length, 1);
});

test('a missing or invalid Retry-After falls back to the default wait', async () => {
  for (const header of [undefined, 'soon', '0', '-3', '1.5']) {
    const headers: Record<string, string> = header === undefined ? {} : { 'Retry-After': header };
    const { client } = harness([json(503, { code: 'not_ready' }, headers)]);
    const error = failure(await client.getStatus());
    assert.equal(error['retryAfterSeconds'], 5, String(header));
  }
});

test('a wait is reported to the caller as it starts', async () => {
  const waits: unknown[] = [];
  const { client } = harness([empty(409), json(200, { items: [] })]);
  await client.getStatus({ onWait: (wait) => waits.push(wait) });
  assert.deepEqual(waits, [{ reason: 'busy', delayMs: 300 }]);
});

test('requests stay same-origin, refuse redirects and are never cached', async () => {
  const { client, requests } = harness([json(200, { items: [] })]);
  await client.getStatus();
  const init = requests[0]?.init;
  assert.equal(init?.credentials, 'same-origin');
  assert.equal(init?.redirect, 'error');
  assert.equal(init?.cache, 'no-store');
  assert.equal(requests[0]?.url, '/api/v1/status');
});

test('an aborted request reports aborted and does not retry', async () => {
  const controller = new AbortController();
  controller.abort();
  const { client, requests } = harness([new DOMException('aborted', 'AbortError')]);
  const error = failure(await client.getStatus({ signal: controller.signal }));
  assert.equal(error['kind'], 'aborted');
  assert.equal(requests.length, 1);
});
