// The mock as a static server for a built folder, with the two policy settings.
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { startMock } from '../server.ts';

function folder(): string {
  const directory = mkdtempSync(join(tmpdir(), 'crux-static-'));
  mkdirSync(join(directory, 'assets'));
  writeFileSync(join(directory, 'index.html'), '<!doctype html><title>x</title>');
  writeFileSync(join(directory, 'assets', 'app.abc123.js'), 'console.log(1)');
  writeFileSync(join(directory, 'favicon.svg'), '<svg xmlns="http://www.w3.org/2000/svg"/>');
  return directory;
}

test('the root serves index.html with the strict policy and no long cache', async () => {
  const mock = await startMock({ staticDir: folder() });
  const response = await fetch(`${mock.url}/`);
  assert.equal(response.status, 200);
  assert.match(response.headers.get('content-type') ?? '', /text\/html/);
  assert.equal(response.headers.get('content-security-policy'), "default-src 'self'; frame-ancestors 'none'");
  assert.equal(response.headers.get('cache-control'), 'no-cache');
  assert.equal(response.headers.get('x-content-type-options'), 'nosniff');
  await mock.close();
});

test('a hashed asset gets a long cache lifetime and its type', async () => {
  const mock = await startMock({ staticDir: folder() });
  const response = await fetch(`${mock.url}/assets/app.abc123.js`);
  assert.equal(response.status, 200);
  assert.match(response.headers.get('content-type') ?? '', /javascript/);
  assert.match(response.headers.get('cache-control') ?? '', /immutable/);
  await mock.close();
});

test('the inline style setting relaxes styles only', async () => {
  const mock = await startMock({ staticDir: folder(), stylePolicy: 'inline' });
  const policy = (await fetch(`${mock.url}/`)).headers.get('content-security-policy');
  assert.equal(policy, "default-src 'self'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'");
  await mock.close();
});

test('the Trusted Types attempt adds its directive', async () => {
  const mock = await startMock({ staticDir: folder(), trustedTypes: true });
  const policy = (await fetch(`${mock.url}/`)).headers.get('content-security-policy');
  assert.equal(policy, "default-src 'self'; frame-ancestors 'none'; require-trusted-types-for 'script'");
  await mock.close();
});

test('a missing asset gets the plain 404', async () => {
  const mock = await startMock({ staticDir: folder() });
  const response = await fetch(`${mock.url}/nothing.js`);
  assert.equal(response.status, 404);
  assert.equal(await response.text(), 'Not Found');
  await mock.close();
});

test('a path cannot leave the folder', async () => {
  const parent = mkdtempSync(join(tmpdir(), 'crux-static-'));
  const directory = join(parent, 'site');
  mkdirSync(join(directory, 'assets'), { recursive: true });
  writeFileSync(join(directory, 'index.html'), '<!doctype html>');
  writeFileSync(join(parent, 'crux-secret-outside.txt'), 'secret');
  const mock = await startMock({ staticDir: directory });
  for (const path of ['/../crux-secret-outside.txt', '/%2e%2e/crux-secret-outside.txt', '/assets/..%2f..%2fcrux-secret-outside.txt']) {
    const response = await fetch(`${mock.url}${path}`);
    assert.notEqual(await response.text(), 'secret', path);
  }
  await mock.close();
});

test('a method other than GET on a static path gets the plain 405', async () => {
  const mock = await startMock({ staticDir: folder() });
  const response = await fetch(`${mock.url}/`, { method: 'POST' });
  assert.equal(response.status, 405);
  await mock.close();
});

test('with no static folder the root is the plain 404', async () => {
  const mock = await startMock();
  assert.equal((await fetch(`${mock.url}/`)).status, 404);
  await mock.close();
});

test('the control route exists only when asked for', async () => {
  const closed = await startMock();
  assert.equal((await fetch(`${closed.url}/__mock/control`, { method: 'POST', body: '{}' })).status, 404);
  await closed.close();
  const open = await startMock({ httpControl: true });
  const reply = await fetch(`${open.url}/__mock/control`, {
    method: 'POST',
    body: JSON.stringify({ action: 'force', operation: 'getStatus', responses: [{ status: 409 }] }),
  });
  assert.equal(reply.status, 200);
  assert.equal((await fetch(`${open.url}/api/v1/status`)).status, 409);
  await open.close();
});
