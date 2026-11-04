import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import type { TestContext } from 'node:test';
import { checkPins } from '../check-pins.ts';
import { tempFolder } from './support.ts';

function tree(t: TestContext, files: Record<string, unknown>): string {
  const root = tempFolder(t);
  for (const [name, content] of Object.entries(files)) {
    const file = join(root, name);
    mkdirSync(join(file, '..'), { recursive: true });
    writeFileSync(file, typeof content === 'string' ? content : JSON.stringify(content));
  }
  return root;
}

const manifest = {
  engines: { node: '24.22.2' },
  dependencies: { preact: '11.1.1' },
  devDependencies: { esbuild: '0.28.2' },
};

function lockFor(root: unknown = manifest): unknown {
  return { lockfileVersion: 3, packages: { '': root } };
}

test('matching exact pins pass', (t) => {
  const root = tree(t, {
    'a/package.json': manifest,
    'a/package-lock.json': lockFor(),
    'b/package.json': manifest,
    'b/package-lock.json': lockFor(),
  });
  assert.deepEqual(checkPins(root, ['a', 'b'], '24.22.2'), []);
});

test('a different Node.js version in one package fails', (t) => {
  const other = { ...manifest, engines: { node: '20.18.0' } };
  const root = tree(t, {
    'a/package.json': manifest,
    'a/package-lock.json': lockFor(),
    'b/package.json': other,
    'b/package-lock.json': lockFor(other),
  });
  assert.match(checkPins(root, ['a', 'b'], '24.22.2').join('\n'), /b.*20\.18\.0/);
});

test('a range in place of a Node.js version fails', (t) => {
  const ranged = { ...manifest, engines: { node: '>=24' } };
  const root = tree(t, { 'a/package.json': ranged, 'a/package-lock.json': lockFor(ranged) });
  assert.notDeepEqual(checkPins(root, ['a'], '24.22.2'), []);
});

test('a range on a dependency fails', (t) => {
  const ranged = { ...manifest, dependencies: { preact: '^11.1.1' } };
  const root = tree(t, { 'a/package.json': ranged, 'a/package-lock.json': lockFor(ranged) });
  assert.match(checkPins(root, ['a'], '24.22.2').join('\n'), /preact/);
});

test('a missing lockfile fails', (t) => {
  const root = tree(t, { 'a/package.json': manifest });
  assert.match(checkPins(root, ['a'], '24.22.2').join('\n'), /lockfile/);
});

test('running on another Node.js version fails', (t) => {
  const root = tree(t, { 'a/package.json': manifest, 'a/package-lock.json': lockFor() });
  assert.match(checkPins(root, ['a'], '24.0.0').join('\n'), /running/);
});

test('a lockfile whose root still pins the old Node.js fails', (t) => {
  const stale = { ...manifest, engines: { node: '20.18.0' } };
  const root = tree(t, { 'a/package.json': manifest, 'a/package-lock.json': lockFor(stale) });
  assert.match(checkPins(root, ['a'], '24.22.2').join('\n'), /a.*lockfile.*20\.18\.0/);
});

test('a lockfile whose root names another dependency version fails', (t) => {
  const stale = { ...manifest, dependencies: { preact: '11.0.0' } };
  const root = tree(t, { 'a/package.json': manifest, 'a/package-lock.json': lockFor(stale) });
  assert.match(checkPins(root, ['a'], '24.22.2').join('\n'), /a.*lockfile.*preact/);
});

test('a dependency missing from the lockfile root fails', (t) => {
  const stale = { engines: manifest.engines, dependencies: manifest.dependencies };
  const root = tree(t, { 'a/package.json': manifest, 'a/package-lock.json': lockFor(stale) });
  assert.match(checkPins(root, ['a'], '24.22.2').join('\n'), /a.*lockfile.*esbuild/);
});

test('a lockfile with no root entry fails', (t) => {
  const root = tree(t, { 'a/package.json': manifest, 'a/package-lock.json': { lockfileVersion: 1 } });
  assert.match(checkPins(root, ['a'], '24.22.2').join('\n'), /a.*lockfile/);
});

test('a package that must keep lifecycle scripts off needs the setting', (t) => {
  const root = tree(t, { 'a/package.json': manifest, 'a/package-lock.json': lockFor() });
  assert.match(checkPins(root, ['a'], '24.22.2', ['a']).join('\n'), /a.*ignore-scripts/);
});

test('the setting passes, and a setting that is off does not', (t) => {
  const on = tree(t, { 'a/package.json': manifest, 'a/package-lock.json': lockFor(), 'a/.npmrc': 'ignore-scripts=true\n' });
  assert.deepEqual(checkPins(on, ['a'], '24.22.2', ['a']), []);
  const off = tree(t, { 'a/package.json': manifest, 'a/package-lock.json': lockFor(), 'a/.npmrc': 'ignore-scripts=false\n' });
  assert.notDeepEqual(checkPins(off, ['a'], '24.22.2', ['a']), []);
});
