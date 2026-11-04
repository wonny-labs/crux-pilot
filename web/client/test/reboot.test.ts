import assert from 'node:assert/strict';
import { test } from 'node:test';
import { empty, harness, json, plain } from './support.ts';

const pending = () => json(200, { state: 'pending' });

test('requestReboot posts and reports the accepted state', async () => {
  const { client, requests } = harness([json(202, { state: 'pending' })]);
  assert.deepEqual(await client.requestReboot(), { ok: true, value: { state: 'pending' } });
  assert.equal(requests[0]?.method, 'POST');
  assert.equal(requests[0]?.url, '/api/v1/system/reboot');
});

test('a session that stops being valid is the restart, after the running device also answered', async () => {
  const { client, sessionLost } = harness([pending(), pending(), json(401, { code: 'unauthorized' })]);
  assert.deepEqual(await client.watchReboot(), { kind: 'restarted' });
  assert.equal(sessionLost.count, 0);
});

test('an answer from the running device alone is never success', async () => {
  const { client } = harness((_request) => pending());
  const outcome = await client.watchReboot();
  assert.deepEqual(outcome, { kind: 'not_confirmed', lastSeen: 'running' });
});

test('a device that goes away and returns with no session has restarted', async () => {
  const phases: string[] = [];
  const { client } = harness([
    pending(),
    new TypeError('connection refused'),
    new TypeError('connection refused'),
    json(401, { code: 'unauthorized' }),
  ]);
  const outcome = await client.watchReboot({ onUpdate: (update) => phases.push(update.lastSeen) });
  assert.deepEqual(outcome, { kind: 'restarted' });
  assert.deepEqual(phases, ['running', 'away', 'away']);
});

test('a device that stays away reaches the limit as not confirmed', async () => {
  const { client, clock } = harness((_request) => new TypeError('connection refused'));
  const start = clock.now;
  assert.deepEqual(await client.watchReboot(), { kind: 'not_confirmed', lastSeen: 'away' });
  assert.ok(clock.now - start >= 180_000);
  assert.ok(clock.now - start < 183_000);
});

test('a wait longer than expected is flagged before the limit', async () => {
  const overdue: boolean[] = [];
  const { client } = harness((_request) => new TypeError('connection refused'));
  await client.watchReboot({ onUpdate: (update) => overdue.push(update.overdue) });
  assert.equal(overdue[0], false);
  assert.equal(overdue.at(-1), true);
  assert.equal(overdue.indexOf(true), Math.ceil(60_000 / 2000));
});

test('a failed reset is reported', async () => {
  const { client } = harness([pending(), json(200, { state: 'failed' })]);
  assert.deepEqual(await client.watchReboot(), { kind: 'failed' });
});

test('a refusal at the second policy check keeps its reason', async () => {
  const { client } = harness([pending(), json(200, { state: 'refused', reason: 'Machine cycle running' })]);
  assert.deepEqual(await client.watchReboot(), { kind: 'refused', reason: 'Machine cycle running' });
});

test('state none with a live session is not success', async () => {
  const { client } = harness((_request) => json(200, { state: 'none' }));
  assert.deepEqual(await client.watchReboot(), { kind: 'not_confirmed', lastSeen: 'running' });
});

test('busy answers during the wait mean the device still runs, and are not retried at once', async () => {
  const { client, sleeps } = harness([
    empty(409),
    json(503, { code: 'busy' }, { 'Retry-After': '4' }),
    json(401, { code: 'unauthorized' }),
  ]);
  assert.deepEqual(await client.watchReboot(), { kind: 'restarted' });
  assert.deepEqual(sleeps, [2000, 4000]);
});

test('a lost permission ends the wait with the error', async () => {
  const { client } = harness([json(403, { code: 'permission_denied' })]);
  const outcome = await client.watchReboot();
  assert.equal(outcome.kind === 'failed_request' && outcome.error.kind, 'permission_denied');
});

test('a plain server answer during the wait does not end it', async () => {
  const { client } = harness([plain(404, 'Not Found'), json(401, { code: 'unauthorized' })]);
  assert.deepEqual(await client.watchReboot(), { kind: 'restarted' });
});

test('an abort ends the wait', async () => {
  const controller = new AbortController();
  const { client } = harness((_request) => {
    controller.abort();
    return pending();
  });
  assert.deepEqual(await client.watchReboot({ signal: controller.signal }), { kind: 'aborted' });
});

test('the polling interval stays low', async () => {
  const { client, sleeps } = harness([pending(), pending(), json(401, { code: 'unauthorized' })]);
  await client.watchReboot();
  assert.deepEqual(sleeps, [2000, 2000]);
});
