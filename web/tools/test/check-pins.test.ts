import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { checkPins } from '../check-pins.ts';

function tree(files: Record<string, unknown>): string {
  const root = mkdtempSync(join(tmpdir(), 'crux-pins-'));
  for (const [name, content] of Object.entries(files)) {
    const file = join(root, name);
    mkdirSync(join(file, '..'), { recursive: true });
    writeFileSync(file, typeof content === 'string' ? content : JSON.stringify(content));
  }
  return root;
}

const good = {
  engines: { node: '24.22.2' },
  dependencies: { preact: '11.1.1' },
  devDependencies: { esbuild: '0.28.2' },
};
const lock = { lockfileVersion: 3, packages: {} };

test('matching exact pins pass', () => {
  const root = tree({ 'a/package.json': good, 'a/package-lock.json': lock, 'b/package.json': good, 'b/package-lock.json': lock });
  assert.deepEqual(checkPins(root, ['a', 'b'], '24.22.2'), []);
});

test('a different Node.js version in one package fails', () => {
  const root = tree({
    'a/package.json': good,
    'a/package-lock.json': lock,
    'b/package.json': { ...good, engines: { node: '20.18.0' } },
    'b/package-lock.json': lock,
  });
  assert.match(checkPins(root, ['a', 'b'], '24.22.2').join('\n'), /b.*20\.18\.0/);
});

test('a range in place of a Node.js version fails', () => {
  const root = tree({ 'a/package.json': { ...good, engines: { node: '>=24' } }, 'a/package-lock.json': lock });
  assert.notDeepEqual(checkPins(root, ['a'], '24.22.2'), []);
});

test('a range on a dependency fails', () => {
  const root = tree({
    'a/package.json': { ...good, dependencies: { preact: '^11.1.1' } },
    'a/package-lock.json': lock,
  });
  assert.match(checkPins(root, ['a'], '24.22.2').join('\n'), /preact/);
});

test('a missing lockfile fails', () => {
  const root = tree({ 'a/package.json': good });
  assert.match(checkPins(root, ['a'], '24.22.2').join('\n'), /lockfile/);
});

test('running on another Node.js version fails', () => {
  const root = tree({ 'a/package.json': good, 'a/package-lock.json': lock });
  assert.match(checkPins(root, ['a'], '24.0.0').join('\n'), /running/);
});
