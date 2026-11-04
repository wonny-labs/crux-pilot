// The real client against the real mock, over HTTP, with a cookie jar in place of a browser. Both sides
// follow one specification, so a difference between them shows here, and every answer is also checked
// against the specification.
import assert from 'node:assert/strict';
import { afterEach, beforeEach, test } from 'node:test';
import { checkResponse } from '../../mock/conformance.ts';
import { startMock } from '../../mock/server.ts';
import type { MockHandle } from '../../mock/server.ts';
import { loadSpec } from '../../mock/spec.ts';
import { createClient } from '../src/index.ts';
import type { ClientTiming, CruxClient } from '../src/index.ts';

const spec = loadSpec();
const PASSWORD = 'example-password-not-real';
const fast: Partial<ClientTiming> = {
  attemptPollStartMs: 1,
  attemptPollMaxMs: 1,
  busyBaseDelayMs: 1,
  busyMaxDelayMs: 1,
  rebootPollMs: 1,
  rebootExpectedMs: 50,
  rebootLimitMs: 100,
  savePollMs: 1,
  saveLimitMs: 100,
};

let mock: MockHandle;
let jar: Map<string, string>;
let client: CruxClient;
let sessionLost: number;
const problems: string[] = [];

beforeEach(async () => {
  problems.length = 0;
  mock = await startMock({
    onApiResponse: (record) => problems.push(...checkResponse(spec, record)),
  });
  jar = new Map();
  sessionLost = 0;
  client = createClient({
    baseUrl: mock.url,
    timing: fast,
    readCookies: () => [...jar].map(([name, value]) => `${name}=${value}`).join('; '),
    onSessionLost: () => {
      sessionLost += 1;
    },
    fetch: async (input, init = {}) => {
      const headers = new Headers(init.headers);
      if (jar.size > 0) {
        headers.set('Cookie', [...jar].map(([name, value]) => `${name}=${value}`).join('; '));
      }
      if (init.method !== 'GET') {
        headers.set('Origin', mock.url);
      }
      const response = await fetch(input, { ...init, headers });
      for (const line of response.headers.getSetCookie()) {
        const first = line.split(';')[0] as string;
        const name = first.slice(0, first.indexOf('='));
        const value = first.slice(first.indexOf('=') + 1);
        if (value === '') {
          jar.delete(name);
        } else {
          jar.set(name, value);
        }
      }
      return response;
    },
  });
});

afterEach(async () => {
  await mock.close();
  assert.deepEqual(problems, []);
});

test('sign in, read the user and status, then log out', async () => {
  assert.deepEqual(await client.login('operator', PASSWORD), { kind: 'success' });
  const me = await client.getCurrentUser();
  assert.ok(me.ok && me.value.user === 'operator' && !me.value.must_change_password);
  const status = await client.getStatus();
  assert.ok(status.ok && status.value.items.length > 0);
  assert.deepEqual(await client.logout(), { ok: true, value: undefined });
  assert.equal(jar.size, 0);
  const after = await client.getCurrentUser({ quiet: true });
  assert.ok(!after.ok && after.error.kind === 'unauthorized');
});

test('every poll carries the required header, so none is refused', async () => {
  await client.login('operator', PASSWORD);
  const polls = mock.control.requests.filter((request) => request.path.includes('/attempt/'));
  assert.ok(polls.length >= 2);
  assert.ok(polls.every((request) => request.headers['x-crux-request'] === '1'));
});

test('a wrong password is rejected with no detail', async () => {
  assert.deepEqual(await client.login('operator', 'wrong'), { kind: 'rejected' });
  assert.equal(jar.size, 0);
});

test('delay and busy reach the caller with their waits', async () => {
  mock.control.set({ delaySeconds: 9 });
  assert.deepEqual(await client.login('operator', PASSWORD), { kind: 'delayed', retryAfterSeconds: 9 });
  mock.control.set({ delaySeconds: null, attemptBusySeconds: 8 });
  assert.deepEqual(await client.login('operator', PASSWORD), { kind: 'busy', retryAfterSeconds: 8 });
});

test('a held route is retried on a new request and then answers', async () => {
  await client.login('operator', PASSWORD);
  mock.control.force('getStatus', { status: 409 }, { status: 409 });
  const status = await client.getStatus();
  assert.equal(status.ok, true);
});

test('the forced password change completes, and the session then has its permissions', async () => {
  await client.login('newuser', PASSWORD);
  const before = await client.getCurrentUser();
  assert.ok(before.ok && before.value.must_change_password);
  const denied = await client.getNetwork();
  assert.ok(!denied.ok && denied.error.kind === 'permission_denied');
  assert.deepEqual(await client.changePassword(PASSWORD, 'a-long-enough-password'), { kind: 'success' });
  const after = await client.getCurrentUser();
  assert.ok(after.ok && !after.value.must_change_password);
  const network = await client.getNetwork();
  assert.equal(network.ok, true);
});

test('a wrong current password comes back as a field error', async () => {
  await client.login('newuser', PASSWORD);
  assert.deepEqual(await client.changePassword('wrong', 'a-long-enough-password'), {
    kind: 'rejected',
    code: 'invalid_field',
    fields: [{ name: 'current_password', code: 'incorrect' }],
  });
});

test('a network save is queued and then confirmed', async () => {
  await client.login('operator', PASSWORD);
  const wanted = { address: '192.0.2.20', prefix_length: 24, gateway: '192.0.2.1' };
  assert.deepEqual(await client.putNetwork(wanted), { ok: true, value: { state: 'pending' } });
  const outcome = await client.watchSave(wanted);
  assert.equal(outcome.kind, 'saved');
  assert.equal(outcome.kind === 'saved' && outcome.state.reboot_required, true);
});

test('a session that may only write learns that it cannot read the result', async () => {
  await client.login('writer', PASSWORD);
  const wanted = { address: '192.0.2.20', prefix_length: 24 };
  assert.equal((await client.putNetwork(wanted)).ok, true);
  assert.deepEqual(await client.watchSave(wanted), { kind: 'unreadable' });
});

test('a device with no Configuration provider reports not_supported', async () => {
  await client.login('operator', PASSWORD);
  mock.control.set({ networkProvider: false });
  const result = await client.putNetwork({ address: '192.0.2.20', prefix_length: 24 });
  assert.ok(!result.ok && result.error.kind === 'not_supported');
});

test('a restart is seen through the session ending, with the device away in between', async () => {
  await client.login('operator', PASSWORD);
  mock.control.set({ rebootPendingPolls: 2, rebootAwayPolls: 2 });
  assert.equal((await client.requestReboot()).ok, true);
  const seen: string[] = [];
  const outcome = await client.watchReboot({ onUpdate: (update) => seen.push(update.lastSeen) });
  assert.deepEqual(outcome, { kind: 'restarted' });
  assert.ok(seen.includes('running'));
  assert.ok(seen.includes('away'));
  assert.equal(sessionLost, 0);
});

test('a device that never restarts is reported as not confirmed', async () => {
  await client.login('operator', PASSWORD);
  mock.control.set({ rebootOutcome: 'never' });
  await client.requestReboot();
  assert.deepEqual(await client.watchReboot(), { kind: 'not_confirmed', lastSeen: 'running' });
});

test('an expired session fires the callback on the next read', async () => {
  await client.login('operator', PASSWORD);
  mock.control.expireSessions();
  const status = await client.getStatus();
  assert.ok(!status.ok && status.error.kind === 'unauthorized');
  assert.equal(sessionLost, 1);
});

test("the server's plain answers map without a parse error", async () => {
  const stray = createClient({ baseUrl: `${mock.url}/api/v1/nothing`, timing: fast });
  const result = await stray.getStatus();
  assert.ok(!result.ok && result.error.kind === 'no_route');
});
