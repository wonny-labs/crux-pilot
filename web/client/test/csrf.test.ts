import assert from 'node:assert/strict';
import { test } from 'node:test';
import { empty, harness, json } from './support.ts';

const accepted = { id: 'k3Jq9sZp0vXw2LmN7bYt1A' };

test('a state-changing request sends the XSRF-TOKEN cookie in the header', async () => {
  const { client, requests } = harness([json(202, { state: 'pending' })], {
    cookies: 'other=1; XSRF-TOKEN=abc123; more=2',
  });
  await client.requestReboot();
  assert.equal(requests[0]?.headers.get('X-XSRF-TOKEN'), 'abc123');
});

test('the token is opaque, so it is sent byte for byte', async () => {
  const { client, requests } = harness([json(202, { state: 'pending' })], {
    cookies: 'XSRF-TOKEN=a%2Fb=c+d',
  });
  await client.requestReboot();
  assert.equal(requests[0]?.headers.get('X-XSRF-TOKEN'), 'a%2Fb=c+d');
});

test('a cookie whose name only ends the same way is not the token', async () => {
  const { client, requests } = harness([json(202, { state: 'pending' })], { cookies: 'NOT-XSRF-TOKEN=wrong' });
  await client.requestReboot();
  assert.equal(requests[0]?.headers.has('X-XSRF-TOKEN'), false);
});

test('logout, password change and network save carry the token', async () => {
  const { client, requests } = harness(
    [empty(200), json(202, accepted), json(200, { state: 'success' }), json(202, { state: 'pending' })],
    { cookies: 'XSRF-TOKEN=t' },
  );
  await client.logout();
  await client.changePassword('current-password', 'new-password-long');
  await client.putNetwork({ address: '192.0.2.20', prefix_length: 24 });
  const stateChanging = requests.filter((request) => request.method !== 'GET');
  assert.deepEqual(
    stateChanging.map((request) => request.headers.get('X-XSRF-TOKEN')),
    ['t', 't', 't'],
  );
});

test('the token is read again for each try, so a newer session is used on the retry', async () => {
  const cookies = ['XSRF-TOKEN=first', 'XSRF-TOKEN=second'];
  const { client, requests } = harness([empty(409), json(202, { state: 'pending' })], {
    readCookies: () => cookies.shift() ?? '',
  });
  await client.requestReboot();
  assert.equal(requests[0]?.headers.get('X-XSRF-TOKEN'), 'first');
  assert.equal(requests[1]?.headers.get('X-XSRF-TOKEN'), 'second');
});

test('a token that has gone by the retry is not sent again from memory', async () => {
  const cookies = ['XSRF-TOKEN=first', ''];
  const { client, requests } = harness([empty(409), json(401, { code: 'unauthorized' })], {
    readCookies: () => cookies.shift() ?? '',
  });
  await client.requestReboot();
  assert.equal(requests[1]?.headers.has('X-XSRF-TOKEN'), false);
});

test('a read sends no token', async () => {
  const { client, requests } = harness([json(200, { items: [] })], { cookies: 'XSRF-TOKEN=t' });
  await client.getStatus();
  assert.equal(requests[0]?.headers.has('X-XSRF-TOKEN'), false);
});

test('login sends no token, even with a stale cookie', async () => {
  const { client, requests } = harness([json(202, accepted), json(200, { state: 'rejected' })], {
    cookies: 'XSRF-TOKEN=stale',
  });
  await client.login('operator', 'password');
  assert.equal(requests[0]?.headers.has('X-XSRF-TOKEN'), false);
});

test('only the attempt poll sends X-Crux-Request', async () => {
  const { client, requests } = harness([json(202, accepted), json(200, { state: 'success' })]);
  await client.login('operator', 'password');
  assert.equal(requests[0]?.headers.has('X-Crux-Request'), false);
  assert.equal(requests[1]?.headers.get('X-Crux-Request'), '1');
});

test('a rejected token is reported as csrf_rejected', async () => {
  const { client } = harness([json(403, { code: 'csrf_rejected' })], { cookies: 'XSRF-TOKEN=t' });
  const result = await client.requestReboot();
  assert.equal(result.ok, false);
  assert.equal(!result.ok && result.error.kind, 'csrf_rejected');
});
