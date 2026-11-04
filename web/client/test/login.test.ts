import assert from 'node:assert/strict';
import { test } from 'node:test';
import { empty, harness, json, plain } from './support.ts';

const id = 'k3Jq9sZp0vXw2LmN7bYt1A';
const accepted = () => json(202, { id });

test('login posts JSON credentials and polls until success', async () => {
  const { client, requests } = harness([
    accepted(),
    json(200, { state: 'pending' }),
    json(200, { state: 'pending' }),
    json(200, { state: 'success' }),
  ]);
  const outcome = await client.login('operator', 'secret value');
  assert.deepEqual(outcome, { kind: 'success' });
  assert.equal(requests[0]?.method, 'POST');
  assert.equal(requests[0]?.url, '/api/v1/session');
  assert.equal(requests[0]?.headers.get('Content-Type'), 'application/json');
  assert.deepEqual(JSON.parse(requests[0]?.body ?? ''), { user: 'operator', password: 'secret value' });
  assert.equal(requests[1]?.url, `/api/v1/session/attempt/${id}`);
  assert.equal(requests.length, 4);
});

test('the first poll comes quickly and later ones back off to the limit', async () => {
  const pending = () => json(200, { state: 'pending' });
  const { client, sleeps } = harness([accepted(), pending(), pending(), pending(), pending(), pending(), json(200, { state: 'success' })]);
  await client.login('operator', 'password');
  assert.deepEqual(sleeps, [250, 375, 562, 843, 1000, 1000]);
});

test('a rejected login reports no detail', async () => {
  const { client } = harness([accepted(), json(200, { state: 'rejected' })]);
  assert.deepEqual(await client.login('operator', 'wrong'), { kind: 'rejected' });
});

test('a poll inside a delay window reports the wait and polls no further', async () => {
  const { client, requests } = harness([accepted(), json(429, { code: 'retry_later' }, { 'Retry-After': '7' })]);
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'delayed', retryAfterSeconds: 7 });
  assert.equal(requests.length, 2);
});

test('a poll that could not run reports busy with the wait and starts no new attempt itself', async () => {
  const { client, requests } = harness([accepted(), json(503, { code: 'busy' }, { 'Retry-After': '3' })]);
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'busy', retryAfterSeconds: 3 });
  assert.equal(requests.length, 2);
});

test('a busy login route reports busy at once, and the password is not sent again', async () => {
  const { client, requests, sleeps } = harness([json(503, { code: 'busy' }, { 'Retry-After': '1' })]);
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'busy', retryAfterSeconds: 1 });
  assert.equal(requests.length, 1);
  assert.deepEqual(sleeps, []);
});

test('a busy password change route reports busy at once, and the passwords are not sent again', async () => {
  const { client, requests, sleeps } = harness([json(503, { code: 'busy' }, { 'Retry-After': '1' })]);
  assert.deepEqual(await client.changePassword('old', 'new'), { kind: 'busy', retryAfterSeconds: 1 });
  assert.equal(requests.length, 1);
  assert.deepEqual(sleeps, []);
});

test('a login held off by a 409 is the only resend, because the server did not process it', async () => {
  const { client, requests } = harness([empty(409), json(202, { id }), json(200, { state: 'success' })]);
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'success' });
  assert.equal(requests.filter((request) => request.method === 'POST').length, 2);
});

test('a login route held by another client reports busy after its retries', async () => {
  const { client, requests } = harness([empty(409), empty(409), empty(409), empty(409)]);
  const outcome = await client.login('operator', 'password');
  assert.equal(outcome.kind, 'busy');
  assert.equal(requests.length, 4);
});

test('a 404 while polling means the result is lost, and credentials are not sent again', async () => {
  const { client, requests } = harness([accepted(), json(404, { code: 'not_found' })]);
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'lost' });
  assert.equal(requests.filter((request) => request.method === 'POST').length, 1);
});

test('a 409 on a poll keeps the attempt and polls again', async () => {
  const { client, requests } = harness([accepted(), empty(409), json(200, { state: 'success' })]);
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'success' });
  assert.equal(requests.length, 3);
  assert.equal(requests[2]?.url, `/api/v1/session/attempt/${id}`);
});

test('a poll 409 that outlasts its retries leaves the attempt alive, and polling goes on', async () => {
  const { client, requests } = harness([
    accepted(),
    empty(409),
    empty(409),
    empty(409),
    empty(409),
    json(200, { state: 'success' }),
  ]);
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'success' });
  assert.equal(requests.length, 6);
  assert.ok(requests.slice(1).every((request) => request.url === `/api/v1/session/attempt/${id}`));
});

test('a poll 409 that never ends stops at the attempt limit', async () => {
  const { client } = harness((request) => (request.method === 'POST' ? json(202, { id }) : empty(409)));
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'timed_out' });
});

test('a short network failure while polling is tolerated', async () => {
  const { client } = harness([accepted(), new TypeError('reset'), new TypeError('reset'), json(200, { state: 'success' })]);
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'success' });
});

test('only consecutive failed polls count, so a good answer in between starts the count again', async () => {
  const { client, requests } = harness([
    accepted(),
    new TypeError('a'),
    new TypeError('b'),
    json(200, { state: 'pending' }),
    new TypeError('c'),
    new TypeError('d'),
    json(200, { state: 'success' }),
  ]);
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'success' });
  assert.equal(requests.length, 7);
});

test('a lasting network failure while polling ends the attempt with the error', async () => {
  const { client, requests } = harness([accepted(), new TypeError('a'), new TypeError('b'), new TypeError('c')]);
  const outcome = await client.login('operator', 'password');
  assert.equal(outcome.kind, 'failed');
  assert.equal(outcome.kind === 'failed' && outcome.error.kind, 'network');
  assert.equal(requests.length, 4);
});

test('an attempt that never ends stops at the client limit', async () => {
  const { client, clock } = harness((request) =>
    request.method === 'POST' ? json(202, { id }) : json(200, { state: 'pending' }),
  );
  const start = clock.now;
  assert.deepEqual(await client.login('operator', 'password'), { kind: 'timed_out' });
  assert.ok(clock.now - start >= 30_000);
  assert.ok(clock.now - start < 32_000);
});

test('a poll without the header would be refused, and that is reported, not hidden', async () => {
  const { client } = harness([accepted(), json(403, { code: 'csrf_rejected' })]);
  const outcome = await client.login('operator', 'password');
  assert.equal(outcome.kind === 'failed' && outcome.error.kind, 'csrf_rejected');
});

test('a login with a wrong Origin is reported', async () => {
  const { client } = harness([json(403, { code: 'origin_rejected' })]);
  const outcome = await client.login('operator', 'password');
  assert.equal(outcome.kind === 'failed' && outcome.error.kind, 'origin_rejected');
});

test("a poll route missing on the server is reported as the server's plain 404", async () => {
  const { client } = harness([accepted(), plain(404, 'Not Found')]);
  const outcome = await client.login('operator', 'password');
  assert.equal(outcome.kind === 'failed' && outcome.error.kind, 'no_route');
});

test('a response with no known attempt state is reported', async () => {
  const { client } = harness([accepted(), json(200, { state: 'maybe' })]);
  const outcome = await client.login('operator', 'password');
  assert.equal(outcome.kind === 'failed' && outcome.error.kind, 'unexpected');
});

test('a malformed attempt identifier is refused before it reaches a URL', async () => {
  const { client, requests } = harness([json(202, { id: '../../evil' })]);
  const outcome = await client.login('operator', 'password');
  assert.equal(outcome.kind === 'failed' && outcome.error.kind, 'unexpected');
  assert.equal(requests.length, 1);
});

test('an abort stops the polling', async () => {
  const controller = new AbortController();
  const { client, requests } = harness((request) => {
    if (request.method === 'POST') {
      return json(202, { id });
    }
    controller.abort();
    return json(200, { state: 'pending' });
  });
  const outcome = await client.login('operator', 'password', { signal: controller.signal });
  assert.deepEqual(outcome, { kind: 'aborted' });
  assert.equal(requests.length, 2);
});

test('progress is reported while waiting', async () => {
  const seen: string[] = [];
  const { client } = harness([accepted(), json(200, { state: 'pending' }), json(200, { state: 'success' })]);
  await client.login('operator', 'password', { onProgress: (progress) => seen.push(progress.phase) });
  assert.deepEqual(seen, ['submitting', 'verifying']);
});

test('a password change queues an attempt and returns field errors from the poll', async () => {
  const fields = [{ name: 'current_password', code: 'incorrect' }];
  const { client, requests } = harness(
    [json(202, { id }), json(200, { state: 'rejected', code: 'invalid_field', fields })],
    { cookies: 'XSRF-TOKEN=t' },
  );
  const outcome = await client.changePassword('old', 'new-password-long');
  assert.deepEqual(outcome, { kind: 'rejected', code: 'invalid_field', fields });
  assert.deepEqual(JSON.parse(requests[0]?.body ?? ''), {
    current_password: 'old',
    new_password: 'new-password-long',
  });
  assert.equal(requests[0]?.url, '/api/v1/session/me/password');
});

for (const code of ['bad_request', 'storage_failed', 'not_ready']) {
  test(`a password change rejected with ${code} keeps the code`, async () => {
    const { client } = harness([json(202, { id }), json(200, { state: 'rejected', code })]);
    assert.deepEqual(await client.changePassword('old', 'new'), { kind: 'rejected', code });
  });
}

test('a password change whose session ended reports it and fires the session callback', async () => {
  const { client, sessionLost } = harness([json(202, { id }), json(200, { state: 'rejected', code: 'unauthorized' })]);
  assert.deepEqual(await client.changePassword('old', 'new'), { kind: 'rejected', code: 'unauthorized' });
  assert.equal(sessionLost.count, 1);
});

test('a password change with a lost result reports lost, because success cannot be assumed', async () => {
  const { client } = harness([json(202, { id }), json(404, { code: 'not_found' })]);
  assert.deepEqual(await client.changePassword('old', 'new'), { kind: 'lost' });
});

test('a password change inside a delay window reports the wait', async () => {
  const { client } = harness([json(202, { id }), json(429, { code: 'retry_later' }, { 'Retry-After': '9' })]);
  assert.deepEqual(await client.changePassword('old', 'new'), { kind: 'delayed', retryAfterSeconds: 9 });
});

test('a password change refused at the route reports the error', async () => {
  const { client, sessionLost } = harness([json(401, { code: 'unauthorized' })]);
  const outcome = await client.changePassword('old', 'new');
  assert.equal(outcome.kind === 'failed' && outcome.error.kind, 'unauthorized');
  assert.equal(sessionLost.count, 1);
});
