// The mock as a static server for a built folder, with the two policy settings.
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import type { TestContext } from 'node:test';
import { tempFolder } from '../../tools/test/support.ts';
import { startMock } from '../server.ts';
import type { MockHandle, MockOptions } from '../server.ts';

/** A mock that closes with its test, also when an assertion fails first. */
async function started(t: TestContext, options: MockOptions = {}): Promise<MockHandle> {
  const mock = await startMock(options);
  t.after(() => mock.close());
  return mock;
}

function folder(t: TestContext): string {
  const directory = tempFolder(t);
  mkdirSync(join(directory, 'assets'));
  writeFileSync(join(directory, 'index.html'), '<!doctype html><title>x</title>');
  writeFileSync(join(directory, 'assets', 'app.abc123.js'), 'console.log(1)');
  writeFileSync(join(directory, 'favicon.svg'), '<svg xmlns="http://www.w3.org/2000/svg"/>');
  return directory;
}

test('the root serves index.html with the strict policy and no long cache', async (t) => {
  const mock = await started(t, { staticDir: folder(t) });
  const response = await fetch(`${mock.url}/`);
  assert.equal(response.status, 200);
  assert.match(response.headers.get('content-type') ?? '', /text\/html/);
  assert.equal(response.headers.get('content-security-policy'), "default-src 'self'; frame-ancestors 'none'");
  assert.equal(response.headers.get('cache-control'), 'no-cache');
  assert.equal(response.headers.get('x-content-type-options'), 'nosniff');
});

test('a hashed asset gets a long cache lifetime and its type', async (t) => {
  const mock = await started(t, { staticDir: folder(t) });
  const response = await fetch(`${mock.url}/assets/app.abc123.js`);
  assert.equal(response.status, 200);
  assert.match(response.headers.get('content-type') ?? '', /javascript/);
  assert.match(response.headers.get('cache-control') ?? '', /immutable/);
});

test('the inline style setting relaxes styles only', async (t) => {
  const mock = await started(t, { staticDir: folder(t), stylePolicy: 'inline' });
  const policy = (await fetch(`${mock.url}/`)).headers.get('content-security-policy');
  assert.equal(policy, "default-src 'self'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'");
});

test('the Trusted Types attempt adds its directive', async (t) => {
  const mock = await started(t, { staticDir: folder(t), trustedTypes: true });
  const policy = (await fetch(`${mock.url}/`)).headers.get('content-security-policy');
  assert.equal(policy, "default-src 'self'; frame-ancestors 'none'; require-trusted-types-for 'script'");
});

test('every request is recorded, so that a test can say which paths the server saw', async (t) => {
  const mock = await started(t, { staticDir: folder(t) });
  await fetch(`${mock.url}/`);
  await fetch(`${mock.url}/assets/app.abc123.js`);
  await fetch(`${mock.url}/nothing.js`);
  await fetch(`${mock.url}/api/v1/status`);
  assert.deepEqual(
    mock.control.requests.map((request) => request.path),
    ['/', '/assets/app.abc123.js', '/nothing.js', '/api/v1/status'],
  );
});

test('a missing asset gets the plain 404', async (t) => {
  const mock = await started(t, { staticDir: folder(t) });
  const response = await fetch(`${mock.url}/nothing.js`);
  assert.equal(response.status, 404);
  assert.equal(await response.text(), 'Not Found');
});

test('a path cannot leave the folder', async (t) => {
  const parent = tempFolder(t);
  const directory = join(parent, 'site');
  mkdirSync(join(directory, 'assets'), { recursive: true });
  writeFileSync(join(directory, 'index.html'), '<!doctype html>');
  writeFileSync(join(parent, 'crux-secret-outside.txt'), 'secret');
  const mock = await started(t, { staticDir: directory });
  for (const path of ['/../crux-secret-outside.txt', '/%2e%2e/crux-secret-outside.txt', '/assets/..%2f..%2fcrux-secret-outside.txt']) {
    const response = await fetch(`${mock.url}${path}`);
    assert.notEqual(await response.text(), 'secret', path);
  }
});

test('a method other than GET on a static path gets the plain 405', async (t) => {
  const mock = await started(t, { staticDir: folder(t) });
  const response = await fetch(`${mock.url}/`, { method: 'POST' });
  assert.equal(response.status, 405);
});

test('with no static folder the root is the plain 404', async (t) => {
  const mock = await started(t);
  assert.equal((await fetch(`${mock.url}/`)).status, 404);
});

test('the control route exists only when asked for', async (t) => {
  const closed = await started(t);
  assert.equal((await fetch(`${closed.url}/__mock/control`, { method: 'POST', body: '{}' })).status, 404);
  const open = await started(t, { httpControl: true });
  const reply = await fetch(`${open.url}/__mock/control`, {
    method: 'POST',
    body: JSON.stringify({ action: 'force', operation: 'getStatus', responses: [{ status: 409 }] }),
  });
  assert.equal(reply.status, 200);
  assert.equal((await fetch(`${open.url}/api/v1/status`)).status, 409);
});
