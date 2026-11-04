import assert from 'node:assert/strict';
import { test } from 'node:test';
import { empty, harness, json } from './support.ts';

const user = { user: 'operator', role: 'administrator', must_change_password: false };

test('getCurrentUser returns the session user', async () => {
  const { client } = harness([json(200, user)]);
  assert.deepEqual(await client.getCurrentUser(), { ok: true, value: user });
});

test('a 401 on any session route fires the session callback', async () => {
  const { client, sessionLost } = harness([
    json(401, { code: 'unauthorized' }),
    json(401, { code: 'unauthorized' }),
    json(401, { code: 'unauthorized' }),
  ]);
  await client.getStatus();
  await client.getNetwork();
  await client.getCurrentUser();
  assert.equal(sessionLost.count, 3);
});

test('a quiet probe of the current user does not fire the session callback', async () => {
  const { client, sessionLost } = harness([json(401, { code: 'unauthorized' })]);
  const result = await client.getCurrentUser({ quiet: true });
  assert.equal(!result.ok && result.error.kind, 'unauthorized');
  assert.equal(sessionLost.count, 0);
});

test('a 403 does not fire the session callback', async () => {
  const { client, sessionLost } = harness([json(403, { code: 'permission_denied' })]);
  await client.getNetwork();
  assert.equal(sessionLost.count, 0);
});

test('logout accepts a 200 with an empty body and does not parse it', async () => {
  const { client, requests } = harness([new Response('', { status: 200 })]);
  assert.deepEqual(await client.logout(), { ok: true, value: undefined });
  assert.equal(requests[0]?.method, 'DELETE');
  assert.equal(requests[0]?.url, '/api/v1/session');
});

test('logout with no valid session counts as signed out, and does not report a lost session', async () => {
  const { client, sessionLost } = harness([json(401, { code: 'unauthorized' })]);
  assert.deepEqual(await client.logout(), { ok: true, value: undefined });
  assert.equal(sessionLost.count, 0);
});

test('a refused logout is reported, because the session is still alive', async () => {
  const { client } = harness([json(403, { code: 'csrf_rejected' })]);
  const result = await client.logout();
  assert.equal(!result.ok && result.error.kind, 'csrf_rejected');
});

test('logout retries on a busy route', async () => {
  const { client, requests } = harness([empty(409), empty(200)]);
  const result = await client.logout();
  assert.equal(result.ok, true);
  assert.equal(requests.length, 2);
});

test('the status report is returned as sent', async () => {
  const report = { items: [{ name: 'net.link', status: 'ok', type: 'bool', value: true }] };
  const { client } = harness([json(200, report)]);
  assert.deepEqual(await client.getStatus(), { ok: true, value: report });
});

test('a base URL prefixes each path, so a project may mount elsewhere', async () => {
  const { client, requests } = harness([json(200, { items: [] })], { baseUrl: 'https://device.example' });
  await client.getStatus();
  assert.equal(requests[0]?.url, 'https://device.example/api/v1/status');
});
