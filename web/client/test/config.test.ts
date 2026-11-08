// The configuration calls: the list, one value, one update, the save and the wait for its outcome.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { harness, json } from './support.ts';

const item = {
  name: 'report_interval',
  type: 'u32',
  activation: 'application',
  sensitive: false,
  readable: true,
  writable: true,
  unsaved: false,
};

function list(extra: Record<string, unknown> = {}) {
  return json(200, { items: [item], ...extra });
}

test('listConfig reads the list, with no token and no body', async () => {
  const { client, requests } = harness([list()]);
  const result = await client.listConfig();
  assert.deepEqual(result, { ok: true, value: { items: [item] } });
  assert.equal(requests[0]?.method, 'GET');
  assert.equal(requests[0]?.url, '/api/v1/config');
  assert.equal(requests[0]?.headers.has('X-XSRF-TOKEN'), false);
  assert.equal(requests[0]?.body, null);
});

test('getConfigItem reads one value by its name', async () => {
  const { client, requests } = harness([json(200, { name: 'report_interval', type: 'u32', value: 5 })]);
  const result = await client.getConfigItem('report_interval');
  assert.deepEqual(result, { ok: true, value: { name: 'report_interval', type: 'u32', value: 5 } });
  assert.equal(requests[0]?.url, '/api/v1/config/report_interval');
});

test('a name that cannot be a parameter is refused before any request', async () => {
  const { client, requests } = harness([]);
  for (const name of ['', 'save', '.', '..', 'a/b', 'a b', 'a?b', 'x'.repeat(31), '%2e%2e', 'ä']) {
    const result = await client.getConfigItem(name);
    assert.equal(result.ok, false, name);
    assert.equal(!result.ok && result.error.kind, 'bad_request', name);
    const update = await client.putConfigItem(name, 1);
    assert.equal(update.ok, false, name);
  }
  assert.equal(requests.length, 0);
});

test('a name with every allowed character is sent as it is', async () => {
  const { client, requests } = harness([json(200, { name: 'A-z_0.9', type: 'bool', value: true })]);
  await client.getConfigItem('A-z_0.9');
  assert.equal(requests[0]?.url, '/api/v1/config/A-z_0.9');
});

test('putConfigItem sends one value member with the token and the JSON type', async () => {
  const { client, requests } = harness([json(200, { ...item, unsaved: true })], { cookies: 'XSRF-TOKEN=t0k' });
  const result = await client.putConfigItem('report_interval', 10);
  assert.deepEqual(result, { ok: true, value: { ...item, unsaved: true } });
  assert.equal(requests[0]?.method, 'PUT');
  assert.equal(requests[0]?.url, '/api/v1/config/report_interval');
  assert.equal(requests[0]?.headers.get('X-XSRF-TOKEN'), 't0k');
  assert.equal(requests[0]?.headers.get('Content-Type'), 'application/json');
  assert.equal(requests[0]?.body, '{"value":10}');
});

test('a boolean and a string are sent in their own JSON types', async () => {
  const { client, requests } = harness([json(200, item), json(200, item)]);
  await client.putConfigItem('verbose', true);
  await client.putConfigItem('device_label', 'Pump "3"');
  assert.equal(requests[0]?.body, '{"value":true}');
  assert.equal(requests[1]?.body, '{"value":"Pump \\"3\\""}');
});

test('a refused value comes back with the field codes of the device', async () => {
  const { client } = harness([
    json(400, { code: 'invalid_field', fields: [{ name: 'value', code: 'out_of_range' }] }),
    json(400, { code: 'invalid_field', fields: [{ name: 'value', code: 'rule_not_met' }] }),
    json(400, { code: 'invalid_field', fields: [{ name: 'value', code: 'wrong_type' }] }),
  ]);
  for (const code of ['out_of_range', 'rule_not_met', 'wrong_type']) {
    const result = await client.putConfigItem('report_interval', 0);
    assert.equal(!result.ok && result.error.kind, 'invalid_field');
    assert.deepEqual(!result.ok && result.error.fields, [{ name: 'value', code }]);
  }
});

test('a session without permission and an unknown parameter are told apart', async () => {
  const { client } = harness([json(403, { code: 'permission_denied' }), json(404, { code: 'not_found' })]);
  const denied = await client.putConfigItem('report_interval', 1);
  assert.equal(!denied.ok && denied.error.kind, 'permission_denied');
  const missing = await client.putConfigItem('nothing', 1);
  assert.equal(!missing.ok && missing.error.kind, 'not_found');
});

test('a 401 on an update reports the lost session once', async () => {
  const { client, sessionLost } = harness([json(401, { code: 'unauthorized' })]);
  const result = await client.putConfigItem('report_interval', 1);
  assert.equal(!result.ok && result.error.kind, 'unauthorized');
  assert.equal(sessionLost.count, 1);
});

test('a held route is retried on an update, because the server did not process it', async () => {
  const { client, requests, sleeps } = harness([new Response(null, { status: 409 }), json(200, item)]);
  const result = await client.putConfigItem('report_interval', 1);
  assert.equal(result.ok, true);
  assert.equal(requests.length, 2);
  assert.deepEqual(sleeps, [300]);
});

test('a busy answer with a short wait is retried once, and a longer one is reported with its wait', async () => {
  const { client, requests, sleeps } = harness([
    json(503, { code: 'busy' }, { 'Retry-After': '2' }),
    json(200, item),
    json(503, { code: 'busy' }, { 'Retry-After': '30' }),
  ]);
  assert.equal((await client.putConfigItem('report_interval', 1)).ok, true);
  assert.deepEqual(sleeps, [2000]);
  const long = await client.putConfigItem('report_interval', 1);
  assert.equal(!long.ok && long.error.kind, 'busy');
  assert.equal(!long.ok && long.error.retryAfterSeconds, 30);
  assert.equal(requests.length, 3);
});

test('a service that is not ready is reported with its wait', async () => {
  const { client } = harness([json(503, { code: 'not_ready' }, { 'Retry-After': '1' })]);
  const result = await client.putConfigItem('report_interval', 1);
  assert.equal(!result.ok && result.error.kind, 'not_ready');
  assert.equal(!result.ok && result.error.retryAfterSeconds, 1);
});

test('saveConfig posts with the token and no body, and reports the queued state', async () => {
  const { client, requests } = harness([json(202, { state: 'pending' })], { cookies: 'XSRF-TOKEN=t0k' });
  assert.deepEqual(await client.saveConfig(), { ok: true, value: { state: 'pending' } });
  assert.equal(requests[0]?.method, 'POST');
  assert.equal(requests[0]?.url, '/api/v1/config/save');
  assert.equal(requests[0]?.headers.get('X-XSRF-TOKEN'), 't0k');
  assert.equal(requests[0]?.body, null);
});

test('a save by a session without config.save is refused, and a running save is busy', async () => {
  const { client } = harness([json(403, { code: 'permission_denied' }), json(503, { code: 'busy' }, { 'Retry-After': '9' })]);
  const denied = await client.saveConfig();
  assert.equal(!denied.ok && denied.error.kind, 'permission_denied');
  const busy = await client.saveConfig();
  assert.equal(!busy.ok && busy.error.kind, 'busy');
});

test('a save is saved only when the list says so, after it was pending', async () => {
  const { client, requests } = harness([
    list({ save: { state: 'pending' } }),
    list({ save: { state: 'pending' } }),
    list({ save: { state: 'saved' } }),
  ]);
  const outcome = await client.watchConfigSave();
  assert.equal(outcome.kind, 'saved');
  assert.equal(requests.length, 3);
  assert.deepEqual(outcome.kind === 'saved' && outcome.list.items.length, 1);
});

test('a failed save keeps its code and names no parameter', async () => {
  const { client } = harness([list({ save: { state: 'failed', code: 'not_supported' } })]);
  const outcome = await client.watchConfigSave();
  assert.deepEqual(outcome.kind === 'failed' && outcome.code, 'not_supported');
});

test('a list with no save member cannot show this session the outcome', async () => {
  const { client } = harness([list()]);
  assert.deepEqual(await client.watchConfigSave(), { kind: 'not_visible' });
});

test('busy and not ready during the wait mean that the save is still running', async () => {
  const { client, sleeps } = harness([
    json(503, { code: 'busy' }, { 'Retry-After': '3' }),
    json(503, { code: 'not_ready' }, { 'Retry-After': '1' }),
    list({ save: { state: 'saved' } }),
  ]);
  const outcome = await client.watchConfigSave();
  assert.equal(outcome.kind, 'saved');
  // The server's own wait is not shortened by the poll interval.
  assert.ok(sleeps.some((ms) => ms >= 3000));
});

test('a held route during the wait is retried by the client, and a dropped connection is tried again', async () => {
  const { client } = harness([new TypeError('reset'), new Response(null, { status: 409 }), list({ save: { state: 'saved' } })]);
  assert.equal((await client.watchConfigSave()).kind, 'saved');
});

test('a save that stays pending stops at the limit as not confirmed', async () => {
  const { client, clock } = harness(() => list({ save: { state: 'pending' } }));
  const start = clock.now;
  assert.deepEqual(await client.watchConfigSave(), { kind: 'not_confirmed' });
  assert.ok(clock.now - start >= 15_000);
  assert.ok(clock.now - start < 17_000);
});

test('an expired session during the wait is reported', async () => {
  const { client, sessionLost } = harness([json(401, { code: 'unauthorized' })]);
  const outcome = await client.watchConfigSave();
  assert.equal(outcome.kind === 'failed_request' && outcome.error.kind, 'unauthorized');
  assert.equal(sessionLost.count, 1);
});

test('an aborted wait ends as aborted', async () => {
  const controller = new AbortController();
  const { client } = harness([list({ save: { state: 'pending' } })]);
  controller.abort();
  assert.deepEqual(await client.watchConfigSave({ signal: controller.signal }), { kind: 'aborted' });
});

test('a long Retry-After does not make the wait for a save sleep that long', async () => {
  const { client, sleeps } = harness([json(503, { code: 'busy' }, { 'Retry-After': '3600' }), list({ save: { state: 'saved' } })]);
  assert.equal((await client.watchConfigSave()).kind, 'saved');
  assert.deepEqual(sleeps, [5000]);
});

test('a wait for a save never sleeps past its own limit', async () => {
  const { client, sleeps, clock } = harness(() => json(503, { code: 'busy' }, { 'Retry-After': '3600' }), {
    timing: { saveLimitMs: 2000, savePollMs: 1000 },
  });
  const start = clock.now;
  assert.deepEqual(await client.watchConfigSave(), { kind: 'not_confirmed' });
  assert.ok(sleeps.every((ms) => ms <= 2000), sleeps.join());
  assert.ok(clock.now - start <= 2000);
});

test('an abort during the sleep ends the wait as aborted', async () => {
  const controller = new AbortController();
  const { client } = harness(() => {
    controller.abort();
    return list({ save: { state: 'pending' } });
  });
  assert.deepEqual(await client.watchConfigSave({ signal: controller.signal }), { kind: 'aborted' });
});
