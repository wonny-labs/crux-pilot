// The build's own promises: one script, one style, hashed names, no source map, and a build that fails
// when the budget is exceeded.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

const web = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const tool = join(web, 'tools', 'build.ts');

function build(...args: string[]) {
  const out = mkdtempSync(join(tmpdir(), 'crux-build-'));
  const result = spawnSync(process.execPath, [tool, '--out', out, ...args], { cwd: web, encoding: 'utf8' });
  return { out, result };
}

test('the reference builds to one hashed script, one hashed style, an index and an icon', () => {
  const { out, result } = build();
  assert.equal(result.status, 0, result.stderr);
  const assets = readdirSync(join(out, 'assets'));
  assert.equal(assets.length, 2);
  const style = assets.find((name) => name.endsWith('.css'));
  const script = assets.find((name) => name.endsWith('.js'));
  assert.match(style ?? '', /^app\.[A-Z0-9]{8}\.css$/);
  assert.match(script ?? '', /^app\.[A-Z0-9]{8}\.js$/);
  assert.deepEqual(readdirSync(out).sort(), ['assets', 'favicon.svg', 'index.html']);
  const index = readFileSync(join(out, 'index.html'), 'utf8');
  assert.ok(index.includes(`./assets/${script}`));
  assert.ok(index.includes(`./assets/${style}`));
  assert.equal(index.includes('./assets/app.js'), false);
});

test('a build over its budget fails and says so', () => {
  const budget = join(mkdtempSync(join(tmpdir(), 'crux-budget-')), 'budget.json');
  writeFileSync(budget, JSON.stringify({ maxFiles: 1, maxCompressedBytes: 10 }));
  const { result } = build('--budget', budget);
  assert.equal(result.status, 1);
  assert.match(result.stderr, /budget/);
});
