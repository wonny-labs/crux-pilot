// The mock's stateful behaviour, as the specification describes it. Every answer passes through the
// conformance check, and each test ends by asserting that the check found nothing.
import assert from 'node:assert/strict';
import { afterEach, beforeEach, test } from 'node:test';
import { caller } from './support.ts';
import type { Caller } from './support.ts';

const PASSWORD = 'example-password-not-real';
const NEW_PASSWORD = 'example-new-password-not-real';
let h: Caller;

beforeEach(async () => {
  h = await caller();
});
afterEach(async () => {
  await h.mock.close();
  assert.deepEqual(h.mock.conformanceProblems, []);
});

test('a login goes pending, then success, and sets both cookies from the specification', async () => {
  const id = await h.start('operator', PASSWORD);
  const first = await h.call('GET', `/api/v1/session/attempt/${id}`);
  assert.deepEqual(first.body, { state: 'pending' });
  const second = await h.call('GET', `/api/v1/session/attempt/${id}`);
  assert.deepEqual(second.body, { state: 'success' });
  assert.equal(second.setCookie.length, 2);
  assert.match(second.setCookie[0] ?? '', /^crux_session=.*HttpOnly/);
  assert.match(second.setCookie[1] ?? '', /^XSRF-TOKEN=/);
  assert.equal((await h.call('GET', '/api/v1/session/me')).status, 200);
});

test('a finished attempt then answers 404', async () => {
  const id = await h.start('operator', 'wrong');
  await h.call('GET', `/api/v1/session/attempt/${id}`);
  const done = await h.call('GET', `/api/v1/session/attempt/${id}`);
  assert.deepEqual(done.body, { state: 'rejected' });
  assert.equal((await h.call('GET', `/api/v1/session/attempt/${id}`)).status, 404);
});

test('a poll without the request header gets 403 and leaves the attempt as it was', async () => {
  const id = await h.start('operator', PASSWORD);
  const refused = await h.call('GET', `/api/v1/session/attempt/${id}`, { noPollHeader: true });
  assert.equal(refused.status, 403);
  assert.deepEqual(refused.body, { code: 'csrf_rejected' });
  assert.deepEqual(refused.setCookie, []);
  await h.call('GET', `/api/v1/session/attempt/${id}`);
  const collected = await h.call('GET', `/api/v1/session/attempt/${id}`);
  assert.deepEqual(collected.body, { state: 'success' });
});

test('a poll with a wrong request header value also gets 403', async () => {
  const id = await h.start('operator', PASSWORD);
  const refused = await h.call('GET', `/api/v1/session/attempt/${id}`, { headers: { 'X-Crux-Request': '2' } });
  assert.equal(refused.status, 403);
});

test('a forced example cannot bypass the poll header', async () => {
  h.mock.control.force('getSessionAttempt', { status: 200, example: 'success' });
  const reply = await h.call('GET', '/api/v1/session/attempt/k3Jq9sZp0vXw2LmN7bYt1A', { noPollHeader: true });
  assert.equal(reply.status, 403);
});

test('wrong, unknown and disabled accounts look the same', async () => {
  for (const [user, password] of [
    ['operator', 'wrong'],
    ['nobody-here', PASSWORD],
    ['disabled', PASSWORD],
  ] as const) {
    const reply = await h.login(user, password);
    assert.deepEqual(reply.body, { state: 'rejected' }, user);
  }
});

test('a login with a missing or wrong Origin is refused', async () => {
  const missing = await h.call('POST', '/api/v1/session', { body: { user: 'a', password: 'b' }, noOrigin: true });
  assert.equal(missing.status, 403);
  assert.deepEqual(missing.body, { code: 'origin_rejected' });
  const wrong = await h.call('POST', '/api/v1/session', {
    body: { user: 'a', password: 'b' },
    noOrigin: true,
    headers: { Origin: 'https://elsewhere.example' },
  });
  assert.equal(wrong.status, 403);
});

test('a malformed or mistyped login body gets 400', async () => {
  for (const options of [{ rawBody: '{' }, { body: { user: 'a' } }, { body: { user: 1, password: 'b' } }]) {
    const reply = await h.call('POST', '/api/v1/session', options);
    assert.equal(reply.status, 400);
    assert.deepEqual(reply.body, { code: 'bad_request' });
  }
});

test('a body over the limit gets 413', async () => {
  const reply = await h.call('POST', '/api/v1/session', { rawBody: `"${'x'.repeat(5000)}"` });
  assert.equal(reply.status, 413);
  assert.deepEqual(reply.body, { code: 'payload_too_large' });
});

test('a delay window gives 429 with Retry-After on the poll, and ends the attempt', async () => {
  h.mock.control.set({ delaySeconds: 7 });
  const id = await h.start('operator', PASSWORD);
  const reply = await h.call('GET', `/api/v1/session/attempt/${id}`);
  assert.equal(reply.status, 429);
  assert.equal(reply.headers.get('retry-after'), '7');
  assert.deepEqual(reply.body, { code: 'retry_later' });
  assert.equal((await h.call('GET', `/api/v1/session/attempt/${id}`)).status, 404);
});

test('a busy verifier gives 503 on the login route, and nothing is queued', async () => {
  h.mock.control.set({ loginBusySeconds: 4 });
  const reply = await h.call('POST', '/api/v1/session', { body: { user: 'operator', password: PASSWORD } });
  assert.equal(reply.status, 503);
  assert.equal(reply.headers.get('retry-after'), '4');
  assert.deepEqual(reply.body, { code: 'busy' });
});

test('a verifier busy after queueing gives 503 on the poll, and ends the attempt', async () => {
  h.mock.control.set({ attemptBusySeconds: 3 });
  const id = await h.start('operator', PASSWORD);
  const reply = await h.call('GET', `/api/v1/session/attempt/${id}`);
  assert.equal(reply.status, 503);
  assert.equal((await h.call('GET', `/api/v1/session/attempt/${id}`)).status, 404);
});

test('a session route with no session gets 401', async () => {
  for (const [method, path] of [
    ['GET', '/api/v1/session/me'],
    ['DELETE', '/api/v1/session'],
    ['POST', '/api/v1/session/me/password'],
    ['GET', '/api/v1/status'],
    ['GET', '/api/v1/network'],
    ['PUT', '/api/v1/network'],
    ['GET', '/api/v1/system/reboot'],
    ['POST', '/api/v1/system/reboot'],
  ] as const) {
    const reply = await h.call(method, path);
    assert.equal(reply.status, 401, `${method} ${path}`);
    assert.deepEqual(reply.body, { code: 'unauthorized' });
  }
});

test('an ended session gets 401, as an expired one would', async () => {
  await h.login();
  assert.equal((await h.call('GET', '/api/v1/status')).status, 200);
  h.mock.control.expireSessions();
  assert.equal((await h.call('GET', '/api/v1/status')).status, 401);
});

test('a state-changing request needs the CSRF token and the Origin', async () => {
  await h.login();
  const noToken = await h.call('POST', '/api/v1/system/reboot', { noToken: true });
  assert.equal(noToken.status, 403);
  assert.deepEqual(noToken.body, { code: 'csrf_rejected' });
  const wrongToken = await h.call('POST', '/api/v1/system/reboot', {
    noToken: true,
    headers: { 'X-XSRF-TOKEN': 'wrong' },
  });
  assert.deepEqual(wrongToken.body, { code: 'csrf_rejected' });
  const noOrigin = await h.call('POST', '/api/v1/system/reboot', { noOrigin: true });
  assert.deepEqual(noOrigin.body, { code: 'origin_rejected' });
  const logoutNoToken = await h.call('DELETE', '/api/v1/session', { noToken: true });
  assert.deepEqual(logoutNoToken.body, { code: 'csrf_rejected' });
});

test('logout answers 200 with an empty body, clears both cookies and ends the session', async () => {
  await h.login();
  const reply = await h.call('DELETE', '/api/v1/session');
  assert.equal(reply.status, 200);
  assert.equal(reply.text, '');
  assert.equal(reply.setCookie.length, 2);
  assert.match(reply.setCookie[0] ?? '', /^crux_session=;.*Max-Age=0/);
  assert.equal((await h.call('GET', '/api/v1/session/me')).status, 401);
});

test('the status list is filtered per item, and a session with nothing to see gets an empty list', async () => {
  await h.login('operator');
  const all = (await h.call('GET', '/api/v1/status')).body as { items: { name: string }[] };
  assert.ok(all.items.some((item) => item.name === 'net.ipv4'));
  assert.ok(all.items.some((item) => item.name === 'app.cycle_count'));
  await h.login('viewer');
  const some = (await h.call('GET', '/api/v1/status')).body as { items: { name: string }[] };
  assert.ok(some.items.some((item) => item.name === 'net.ipv4'));
  assert.equal(
    some.items.some((item) => item.name === 'app.cycle_count'),
    false,
  );
  await h.login('guest');
  const reply = await h.call('GET', '/api/v1/status');
  assert.equal(reply.status, 200);
  assert.deepEqual(reply.body, { items: [] });
});

test('during a forced change the session may read itself, poll, change and log out, and nothing else', async () => {
  await h.login('newuser');
  const me = await h.call('GET', '/api/v1/session/me');
  assert.equal((me.body as { must_change_password: boolean }).must_change_password, true);
  assert.deepEqual((await h.call('GET', '/api/v1/status')).body, { items: [] });
  assert.equal((await h.call('GET', '/api/v1/network')).status, 403);
  assert.equal((await h.call('POST', '/api/v1/system/reboot')).status, 403);
  assert.deepEqual((await h.call('GET', '/api/v1/system/reboot')).body, { code: 'permission_denied' });
});

test('a password change runs as an attempt, and the session stays valid without new cookies', async () => {
  await h.login('newuser');
  const queued = await h.call('POST', '/api/v1/session/me/password', {
    body: { current_password: PASSWORD, new_password: NEW_PASSWORD },
  });
  assert.equal(queued.status, 202);
  const id = (queued.body as { id: string }).id;
  await h.call('GET', `/api/v1/session/attempt/${id}`);
  const done = await h.call('GET', `/api/v1/session/attempt/${id}`);
  assert.deepEqual(done.body, { state: 'success' });
  assert.deepEqual(done.setCookie, []);
  const me = await h.call('GET', '/api/v1/session/me');
  assert.equal((me.body as { must_change_password: boolean }).must_change_password, false);
  assert.ok(((await h.call('GET', '/api/v1/status')).body as { items: unknown[] }).items.length > 0);
});

test('a password change reports a wrong current password and a refused new one in the fields', async () => {
  await h.login('newuser');
  const results: unknown[] = [];
  for (const body of [
    { current_password: 'wrong', new_password: NEW_PASSWORD },
    { current_password: PASSWORD, new_password: 'short' },
  ]) {
    const queued = await h.call('POST', '/api/v1/session/me/password', { body });
    const id = (queued.body as { id: string }).id;
    await h.call('GET', `/api/v1/session/attempt/${id}`);
    results.push((await h.call('GET', `/api/v1/session/attempt/${id}`)).body);
  }
  assert.deepEqual(results, [
    { state: 'rejected', code: 'invalid_field', fields: [{ name: 'current_password', code: 'incorrect' }] },
    { state: 'rejected', code: 'invalid_field', fields: [{ name: 'new_password', code: 'rule_not_met' }] },
  ]);
});

test('network reads need network.read and saves need network.write', async () => {
  await h.login('guest');
  assert.deepEqual((await h.call('GET', '/api/v1/network')).body, { code: 'permission_denied' });
  const refused = await h.call('PUT', '/api/v1/network', { body: { address: '192.0.2.20', prefix_length: 24 } });
  assert.deepEqual(refused.body, { code: 'permission_denied' });
  await h.login('writer');
  assert.equal((await h.call('GET', '/api/v1/network')).status, 403);
  const saved = await h.call('PUT', '/api/v1/network', { body: { address: '192.0.2.20', prefix_length: 24 } });
  assert.equal(saved.status, 202);
});

test('a save is pending, then shows the saved values and that a reboot is needed', async () => {
  await h.login();
  const before = (await h.call('GET', '/api/v1/network')).body;
  assert.deepEqual(before, {
    running: { address: '192.0.2.10', prefix_length: 24 },
    source: 'default',
    reboot_required: false,
  });
  const wanted = { address: '192.0.2.20', prefix_length: 24, gateway: '192.0.2.1' };
  assert.deepEqual((await h.call('PUT', '/api/v1/network', { body: wanted })).body, { state: 'pending' });
  const during = (await h.call('GET', '/api/v1/network')).body as { save?: unknown };
  assert.deepEqual(during.save, { state: 'pending' });
  const after_ = (await h.call('GET', '/api/v1/network')).body as Record<string, unknown>;
  assert.deepEqual(after_['saved'], wanted);
  assert.equal(after_['reboot_required'], true);
  assert.equal(after_['save'], undefined);
});

test('a second save while one runs is told to wait', async () => {
  await h.login();
  const wanted = { address: '192.0.2.20', prefix_length: 24 };
  await h.call('PUT', '/api/v1/network', { body: wanted });
  const second = await h.call('PUT', '/api/v1/network', { body: wanted });
  assert.equal(second.status, 503);
  assert.deepEqual(second.body, { code: 'busy' });
});

test('invalid settings get field errors, and a mistyped body gets bad_request', async () => {
  await h.login();
  const bad = await h.call('PUT', '/api/v1/network', {
    body: { address: '224.0.0.1', prefix_length: 31, gateway: '198.51.100.1' },
  });
  assert.equal(bad.status, 400);
  assert.deepEqual(bad.body, {
    code: 'invalid_field',
    fields: [
      { name: 'address', code: 'not_unicast' },
      { name: 'prefix_length', code: 'prefix_not_usable' },
    ],
  });
  const gateway = await h.call('PUT', '/api/v1/network', {
    body: { address: '192.0.2.20', prefix_length: 24, gateway: '198.51.100.1' },
  });
  assert.deepEqual(gateway.body, {
    code: 'invalid_field',
    fields: [{ name: 'gateway', code: 'gateway_outside_subnet' }],
  });
  assert.equal((await h.call('PUT', '/api/v1/network', { body: { address: 7 } })).status, 400);
  assert.equal((await h.call('PUT', '/api/v1/network', { body: { address: 'nope', prefix_length: 24 } })).status, 400);
});

test('a failed save shows as failed until the next accepted one', async () => {
  await h.login();
  h.mock.control.set({ saveResult: 'fail', savePolls: 0 });
  await h.call('PUT', '/api/v1/network', { body: { address: '192.0.2.20', prefix_length: 24 } });
  const state = (await h.call('GET', '/api/v1/network')).body as { save?: unknown };
  assert.deepEqual(state.save, { state: 'failed' });
});

test('a device with no Configuration provider answers 501 and keeps its values', async () => {
  await h.login();
  h.mock.control.set({ networkProvider: false });
  const reply = await h.call('PUT', '/api/v1/network', { body: { address: '192.0.2.20', prefix_length: 24 } });
  assert.equal(reply.status, 501);
  assert.deepEqual(reply.body, { code: 'not_supported' });
  assert.equal(((await h.call('GET', '/api/v1/network')).body as { saved?: unknown }).saved, undefined);
});

test('a reboot needs system.reboot', async () => {
  await h.login('viewer');
  assert.deepEqual((await h.call('POST', '/api/v1/system/reboot')).body, { code: 'permission_denied' });
  assert.deepEqual((await h.call('GET', '/api/v1/system/reboot')).body, { code: 'permission_denied' });
});

test('a reboot refused by policy answers 423 with the reason, and the state shows it', async () => {
  await h.login();
  h.mock.control.set({ rebootPolicy: { refuse: 'Machine cycle running' } });
  const reply = await h.call('POST', '/api/v1/system/reboot');
  assert.equal(reply.status, 423);
  assert.deepEqual(reply.body, { code: 'reboot_refused', reason: 'Machine cycle running' });
  assert.deepEqual((await h.call('GET', '/api/v1/system/reboot')).body, {
    state: 'refused',
    reason: 'Machine cycle running',
  });
});

test('a restart goes pending, then away, then the session is gone', async () => {
  await h.login();
  h.mock.control.set({ rebootOutcome: 'restart', rebootPendingPolls: 1, rebootAwayPolls: 1 });
  assert.deepEqual((await h.call('GET', '/api/v1/system/reboot')).body, { state: 'none' });
  assert.equal((await h.call('POST', '/api/v1/system/reboot')).status, 202);
  assert.deepEqual((await h.call('GET', '/api/v1/system/reboot')).body, { state: 'pending' });
  await assert.rejects(h.call('GET', '/api/v1/system/reboot'));
  assert.equal((await h.call('GET', '/api/v1/system/reboot')).status, 401);
});

test('a request while one is pending gets 202 and runs no new policy check', async () => {
  await h.login();
  h.mock.control.set({ rebootOutcome: 'never' });
  assert.equal((await h.call('POST', '/api/v1/system/reboot')).status, 202);
  h.mock.control.set({ rebootPolicy: { refuse: 'Machine cycle running' } });
  assert.equal((await h.call('POST', '/api/v1/system/reboot')).status, 202);
});

test('a failed reset shows as failed and the session stays valid', async () => {
  await h.login();
  h.mock.control.set({ rebootOutcome: 'fail', rebootPendingPolls: 0 });
  await h.call('POST', '/api/v1/system/reboot');
  assert.deepEqual((await h.call('GET', '/api/v1/system/reboot')).body, { state: 'failed' });
  assert.equal((await h.call('GET', '/api/v1/session/me')).status, 200);
});

test('a refusal at the second check shows with its reason', async () => {
  await h.login();
  h.mock.control.set({ rebootOutcome: { refuseLate: 'Machine started' }, rebootPendingPolls: 0 });
  assert.equal((await h.call('POST', '/api/v1/system/reboot')).status, 202);
  assert.deepEqual((await h.call('GET', '/api/v1/system/reboot')).body, {
    state: 'refused',
    reason: 'Machine started',
  });
});

test('a device that never comes back stays away', async () => {
  await h.login();
  h.mock.control.set({ rebootOutcome: 'away_forever', rebootPendingPolls: 0 });
  await h.call('POST', '/api/v1/system/reboot');
  await assert.rejects(h.call('GET', '/api/v1/system/reboot'));
  await assert.rejects(h.call('GET', '/api/v1/system/reboot'));
  h.mock.control.reset();
});

test('forced responses are used in order and then the state takes over', async () => {
  await h.login();
  h.mock.control.force('getStatus', { status: 409 }, { status: 503, example: 'busy' });
  assert.equal((await h.call('GET', '/api/v1/status')).status, 409);
  assert.equal((await h.call('GET', '/api/v1/status')).status, 503);
  assert.equal((await h.call('GET', '/api/v1/status')).status, 200);
});

test('a forced example with a session cookie starts a usable session', async () => {
  h.mock.control.force('getSessionAttempt', { status: 200, example: 'success' });
  await h.call('GET', '/api/v1/session/attempt/k3Jq9sZp0vXw2LmN7bYt1A');
  assert.equal((await h.call('GET', '/api/v1/session/me')).status, 200);
});

test('a request is recorded with its headers, for a test to read', async () => {
  await h.login();
  const polls = h.mock.control.requests.filter((request) => request.path.includes('/attempt/'));
  assert.ok(polls.length > 0);
  assert.ok(polls.every((request) => request.headers['x-crux-request'] === '1'));
});
