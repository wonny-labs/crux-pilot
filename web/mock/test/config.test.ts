// The mock's configuration routes, as web/api/openapi.yaml describes them. Every answer passes through the
// conformance check, and each test ends by asserting that the check found nothing.
import assert from 'node:assert/strict';
import { afterEach, beforeEach, test } from 'node:test';
import { caller } from './support.ts';
import type { Caller } from './support.ts';

let h: Caller;

beforeEach(async () => {
  h = await caller();
});
afterEach(async () => {
  await h.mock.close();
  assert.deepEqual(h.mock.conformanceProblems, []);
});

interface Item {
  name: string;
  type: string;
  activation: string;
  sensitive: boolean;
  readable: boolean;
  writable: boolean;
  unsaved: boolean;
}

async function list(): Promise<{ items: Item[]; save?: unknown }> {
  const reply = await h.call('GET', '/api/v1/config');
  assert.equal(reply.status, 200);
  return reply.body as { items: Item[]; save?: unknown };
}

function itemNamed(items: Item[], name: string): Item {
  const found = items.find((item) => item.name === name);
  assert.ok(found, `${name} is listed`);
  return found;
}

test('the list shows every parameter to a session that may read and write, with no value', async () => {
  await h.login();
  const { items, save } = await list();
  assert.deepEqual(
    items.map((item) => item.name),
    ['verbose', 'report_interval', 'device_label', 'link_secret', 'build_limit'],
  );
  assert.equal(save, undefined);
  for (const item of items) {
    assert.equal('value' in item, false);
  }
  assert.deepEqual(itemNamed(items, 'link_secret'), {
    name: 'link_secret',
    type: 'string',
    activation: 'restart',
    sensitive: true,
    readable: false,
    writable: true,
    unsaved: false,
  });
  assert.equal(itemNamed(items, 'build_limit').writable, false);
});

test('a session that may only read sees nothing writable or unsaved, and the sensitive parameter as unreadable', async () => {
  await h.login('viewer');
  const { items } = await list();
  assert.deepEqual(
    items.map((item) => item.name),
    ['verbose', 'report_interval', 'device_label', 'link_secret', 'build_limit'],
  );
  assert.ok(items.every((item) => !item.writable && !item.unsaved));
  assert.deepEqual(
    items.filter((item) => !item.readable).map((item) => item.name),
    ['link_secret'],
  );
});

test('a parameter under a permission that no account holds is in nobody\'s list', async () => {
  await h.login();
  assert.equal((await list()).items.some((item) => item.name === 'private_key'), false);
});

test('a session that may only write lists what it may write, none of it readable', async () => {
  await h.login('writer');
  const { items } = await list();
  assert.deepEqual(
    items.map((item) => item.name),
    ['verbose', 'report_interval', 'device_label', 'link_secret'],
  );
  assert.ok(items.every((item) => !item.readable && item.writable));
});

test('a session with no configuration permission, and one in a forced change, get an empty list, never 403', async () => {
  await h.login('guest');
  assert.deepEqual((await h.call('GET', '/api/v1/config')).body, { items: [] });
  h.jar.clear();
  await h.login('newuser');
  assert.deepEqual((await h.call('GET', '/api/v1/config')).body, { items: [] });
});

test('every configuration route needs a session', async () => {
  for (const [method, path] of [
    ['GET', '/api/v1/config'],
    ['GET', '/api/v1/config/verbose'],
    ['PUT', '/api/v1/config/verbose'],
    ['POST', '/api/v1/config/save'],
  ] as const) {
    const reply = await h.call(method, path, method === 'PUT' ? { body: { value: true } } : {});
    assert.equal(reply.status, 401, `${method} ${path}`);
    assert.deepEqual(reply.body, { code: 'unauthorized' });
  }
});

test('a value is read in the JSON form of its type', async () => {
  await h.login();
  assert.deepEqual((await h.call('GET', '/api/v1/config/verbose')).body, { name: 'verbose', type: 'bool', value: false });
  assert.deepEqual((await h.call('GET', '/api/v1/config/report_interval')).body, {
    name: 'report_interval',
    type: 'u32',
    value: 5,
  });
  assert.deepEqual((await h.call('GET', '/api/v1/config/device_label')).body, {
    name: 'device_label',
    type: 'string',
    value: 'pilot',
  });
});

test('a missing parameter, and one this session cannot see, both get the same 404', async () => {
  await h.login('viewer');
  const missing = await h.call('GET', '/api/v1/config/nothing_here');
  const hidden = await h.call('GET', '/api/v1/config/private_key');
  assert.equal(missing.status, 404);
  assert.deepEqual(missing.body, { code: 'not_found' });
  assert.equal(hidden.status, 404);
  assert.equal(hidden.text, missing.text);
});

test('a parameter this session may write but not read, and a sensitive one, get 403', async () => {
  await h.login('writer');
  assert.deepEqual((await h.call('GET', '/api/v1/config/verbose')).body, { code: 'permission_denied' });
  h.jar.clear();
  await h.login('viewer');
  assert.equal((await h.call('GET', '/api/v1/config/link_secret')).status, 403);
  h.jar.clear();
  await h.login();
  const sensitive = await h.call('GET', '/api/v1/config/link_secret');
  assert.equal(sensitive.status, 403);
  assert.deepEqual(sensitive.body, { code: 'permission_denied' });
});

test('an update is accepted in memory, marks the parameter unsaved and returns no value', async () => {
  await h.login();
  const reply = await h.call('PUT', '/api/v1/config/report_interval', { body: { value: 10 } });
  assert.equal(reply.status, 200);
  assert.deepEqual(reply.body, {
    name: 'report_interval',
    type: 'u32',
    activation: 'application',
    sensitive: false,
    readable: true,
    writable: true,
    unsaved: true,
  });
  assert.equal(((await h.call('GET', '/api/v1/config/report_interval')).body as { value: number }).value, 10);
  assert.equal(itemNamed((await list()).items, 'report_interval').unsaved, true);
});

test('a bool and a string are updated in their own forms', async () => {
  await h.login();
  assert.equal((await h.call('PUT', '/api/v1/config/verbose', { body: { value: true } })).status, 200);
  assert.equal((await h.call('PUT', '/api/v1/config/device_label', { body: { value: 'Pump station 4' } })).status, 200);
  assert.equal(((await h.call('GET', '/api/v1/config/verbose')).body as { value: boolean }).value, true);
  assert.equal(((await h.call('GET', '/api/v1/config/device_label')).body as { value: string }).value, 'Pump station 4');
});

test('a sensitive parameter is written and never read back', async () => {
  await h.login();
  const reply = await h.call('PUT', '/api/v1/config/link_secret', { body: { value: 'a new secret' } });
  assert.equal(reply.status, 200);
  assert.equal(reply.text.includes('a new secret'), false);
  assert.equal((await h.call('GET', '/api/v1/config/link_secret')).status, 403);
});

test('the wrong JSON type, a number that is not a u32, and a refused value get field errors', async () => {
  await h.login();
  const cases: [string, unknown, string][] = [
    ['verbose', 'true', 'wrong_type'],
    ['report_interval', '10', 'wrong_type'],
    ['device_label', 7, 'wrong_type'],
    ['report_interval', -1, 'out_of_range'],
    ['report_interval', 4294967296, 'out_of_range'],
    ['report_interval', 1.5, 'out_of_range'],
    ['report_interval', 0, 'rule_not_met'],
    ['report_interval', 3601, 'rule_not_met'],
    ['device_label', 'a bad label', 'rule_not_met'],
    ['device_label', 'x'.repeat(129), 'rule_not_met'],
    ['device_label', 'tab\tinside', 'rule_not_met'],
  ];
  for (const [name, value, code] of cases) {
    const reply = await h.call('PUT', `/api/v1/config/${name}`, { body: { value } });
    assert.equal(reply.status, 400, `${name} ${JSON.stringify(value)}`);
    assert.deepEqual(reply.body, { code: 'invalid_field', fields: [{ name: 'value', code }] });
  }
  assert.equal(((await h.call('GET', '/api/v1/config/report_interval')).body as { value: number }).value, 5);
  assert.equal(itemNamed((await list()).items, 'report_interval').unsaved, false);
});

test('a malformed body gets bad_request', async () => {
  await h.login();
  for (const options of [
    { rawBody: '{' },
    { rawBody: '' },
    { rawBody: 'true' },
    { body: {} },
    { body: { value: null } },
    { body: { value: [1] } },
    { body: { value: { a: 1 } } },
    { body: { value: 1, extra: 2 } },
    { body: { other: 1 } },
  ]) {
    const reply = await h.call('PUT', '/api/v1/config/report_interval', options);
    assert.equal(reply.status, 400, JSON.stringify(options));
    assert.deepEqual(reply.body, { code: 'bad_request' });
  }
});

test('a body over the limit gets 413', async () => {
  await h.login();
  const reply = await h.call('PUT', '/api/v1/config/device_label', { rawBody: `{"value":"${'x'.repeat(5000)}"}` });
  assert.equal(reply.status, 413);
  assert.deepEqual(reply.body, { code: 'payload_too_large' });
});

test('an update needs the write permission, whatever the body holds', async () => {
  await h.login('viewer');
  for (const body of [{ value: 10 }, { value: 'wrong type' }, { value: 99999 }]) {
    const reply = await h.call('PUT', '/api/v1/config/report_interval', { body });
    assert.equal(reply.status, 403);
    assert.deepEqual(reply.body, { code: 'permission_denied' });
  }
  const fixed = await h.call('PUT', '/api/v1/config/build_limit', { body: { value: 1 } });
  assert.equal(fixed.status, 403);
});

test('an update of a missing or hidden parameter gets 404, whatever the body holds', async () => {
  await h.login('viewer');
  for (const body of [{ value: 10 }, { value: 'wrong type' }]) {
    assert.equal((await h.call('PUT', '/api/v1/config/private_key', { body })).status, 404);
    assert.equal((await h.call('PUT', '/api/v1/config/nothing_here', { body })).status, 404);
  }
});

test('an update needs the CSRF token and Origin', async () => {
  await h.login();
  const noToken = await h.call('PUT', '/api/v1/config/verbose', { body: { value: true }, noToken: true });
  assert.deepEqual(noToken.body, { code: 'csrf_rejected' });
  const noOrigin = await h.call('PUT', '/api/v1/config/verbose', { body: { value: true }, noOrigin: true });
  assert.deepEqual(noOrigin.body, { code: 'origin_rejected' });
});

test('save is a fixed route, so a GET or PUT of its path gets the plain 405', async () => {
  await h.login();
  for (const method of ['GET', 'PUT']) {
    const reply = await h.call(method, '/api/v1/config/save', method === 'PUT' ? { body: { value: 1 } } : {});
    assert.equal(reply.status, 405);
    assert.equal(reply.text, 'Method Not Allowed');
  }
});

test('a path under a configuration parameter gets the JSON 404', async () => {
  await h.login();
  const reply = await h.call('GET', '/api/v1/config/verbose/deeper');
  assert.equal(reply.status, 404);
  assert.deepEqual(reply.body, { code: 'not_found' });
});

test('a save needs config.save, and a viewer does not have it', async () => {
  await h.login('viewer');
  const reply = await h.call('POST', '/api/v1/config/save');
  assert.equal(reply.status, 403);
  assert.deepEqual(reply.body, { code: 'permission_denied' });
});

test('a save is pending, then saved, and clears the unsaved marks', async () => {
  await h.login();
  await h.call('PUT', '/api/v1/config/report_interval', { body: { value: 10 } });
  const queued = await h.call('POST', '/api/v1/config/save');
  assert.equal(queued.status, 202);
  assert.deepEqual(queued.body, { state: 'pending' });
  const pending = await list();
  assert.deepEqual(pending.save, { state: 'pending' });
  assert.equal(itemNamed(pending.items, 'report_interval').unsaved, true);
  const done = await list();
  assert.deepEqual(done.save, { state: 'saved' });
  assert.equal(itemNamed(done.items, 'report_interval').unsaved, false);
});

test('a save while one is pending gets 503 busy, and starts nothing new', async () => {
  await h.login();
  assert.equal((await h.call('POST', '/api/v1/config/save')).status, 202);
  const second = await h.call('POST', '/api/v1/config/save');
  assert.equal(second.status, 503);
  assert.equal(second.headers.get('retry-after'), '1');
  assert.deepEqual(second.body, { code: 'busy' });
});

test('a failed save shows failed with a code until the next accepted one, and keeps the marks', async () => {
  await h.login();
  h.mock.control.set({ saveResult: 'fail', savePolls: 0 });
  await h.call('PUT', '/api/v1/config/report_interval', { body: { value: 10 } });
  await h.call('POST', '/api/v1/config/save');
  const failed = await list();
  assert.deepEqual(failed.save, { state: 'failed', code: 'storage_failed' });
  assert.equal(itemNamed(failed.items, 'report_interval').unsaved, true);
  h.mock.control.set({ saveResult: 'ok' });
  await h.call('POST', '/api/v1/config/save');
  assert.deepEqual((await list()).save, { state: 'saved' });
});

test('a device with no Configuration provider fails a save with not_supported', async () => {
  await h.login();
  h.mock.control.set({ networkProvider: false, savePolls: 0 });
  await h.call('PUT', '/api/v1/config/report_interval', { body: { value: 10 } });
  assert.equal((await h.call('POST', '/api/v1/config/save')).status, 202);
  assert.deepEqual((await list()).save, { state: 'failed', code: 'not_supported' });
});

test('the save state is shown only to a session that holds config.save', async () => {
  await h.login();
  await h.call('POST', '/api/v1/config/save');
  h.jar.clear();
  await h.login('viewer');
  assert.equal((await list()).save, undefined);
});
