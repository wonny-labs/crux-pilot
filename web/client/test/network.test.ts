import assert from 'node:assert/strict';
import { test } from 'node:test';
import { harness, json } from './support.ts';

const wanted = { address: '192.0.2.20', prefix_length: 24, gateway: '192.0.2.1' };
const running = { address: '192.0.2.10', prefix_length: 24 };

function state(extra: Record<string, unknown>) {
  return json(200, { running, source: 'default', reboot_required: true, ...extra });
}

test('putNetwork sends the settings and reports the queued state', async () => {
  const { client, requests } = harness([json(202, { state: 'pending' })], { cookies: 'XSRF-TOKEN=t' });
  assert.deepEqual(await client.putNetwork(wanted), { ok: true, value: { state: 'pending' } });
  assert.equal(requests[0]?.method, 'PUT');
  assert.equal(requests[0]?.url, '/api/v1/network');
  assert.deepEqual(JSON.parse(requests[0]?.body ?? ''), wanted);
});

test('a save is confirmed only when no save is pending and the saved values match', async () => {
  const { client } = harness([state({ save: { state: 'pending' } }), state({ saved: wanted })]);
  const outcome = await client.watchSave(wanted);
  assert.equal(outcome.kind, 'saved');
  assert.deepEqual(outcome.kind === 'saved' && outcome.state.saved, wanted);
});

test('a failed save is reported with the state the device holds', async () => {
  const { client } = harness([state({ save: { state: 'failed' } })]);
  const outcome = await client.watchSave(wanted);
  assert.equal(outcome.kind, 'failed');
  assert.deepEqual(outcome.kind === 'failed' && outcome.state.save, { state: 'failed' });
});

test('a session that may only write learns that it cannot read the result', async () => {
  const { client } = harness([json(403, { code: 'permission_denied' })]);
  assert.deepEqual(await client.watchSave(wanted), { kind: 'unreadable' });
});

test('a save that stays pending stops at the limit as not confirmed', async () => {
  const { client, clock } = harness((_request) => state({ save: { state: 'pending' } }));
  const start = clock.now;
  assert.deepEqual(await client.watchSave(wanted), { kind: 'not_confirmed' });
  assert.ok(clock.now - start >= 15_000);
  assert.ok(clock.now - start < 17_000);
});

test('saved values that differ from the request are not reported as this save', async () => {
  const { client } = harness((_request) => state({ saved: { ...wanted, address: '192.0.2.99' } }));
  assert.deepEqual(await client.watchSave(wanted), { kind: 'not_confirmed' });
});

test('a settings set with no gateway matches saved values with no gateway', async () => {
  const noGateway = { address: '192.0.2.20', prefix_length: 24 };
  const { client } = harness([state({ saved: noGateway })]);
  assert.equal((await client.watchSave(noGateway)).kind, 'saved');
});

test('a gateway the device kept does not match a request without one', async () => {
  const noGateway = { address: '192.0.2.20', prefix_length: 24 };
  const { client } = harness((_request) => state({ saved: wanted }));
  assert.equal((await client.watchSave(noGateway)).kind, 'not_confirmed');
});

test('a read failure during the wait does not end it', async () => {
  const { client } = harness([new TypeError('reset'), state({ saved: wanted })]);
  assert.equal((await client.watchSave(wanted)).kind, 'saved');
});

test('an expired session during the wait reports it', async () => {
  const { client, sessionLost } = harness([json(401, { code: 'unauthorized' })]);
  const outcome = await client.watchSave(wanted);
  assert.equal(outcome.kind === 'failed_request' && outcome.error.kind, 'unauthorized');
  assert.equal(sessionLost.count, 1);
});
