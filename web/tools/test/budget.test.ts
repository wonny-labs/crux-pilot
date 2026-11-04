import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import type { TestContext } from 'node:test';
import { gzipSync } from 'node:zlib';
import { checkBudget, measureFolder } from '../budget.ts';
import { tempFolder } from './support.ts';

function folder(t: TestContext): string {
  const directory = tempFolder(t);
  mkdirSync(join(directory, 'assets'));
  writeFileSync(join(directory, 'index.html'), '<!doctype html>');
  writeFileSync(join(directory, 'assets', 'app.js'), 'x'.repeat(5000));
  return directory;
}

test('a folder is measured file by file, recursively, with compressed size at the highest level', (t) => {
  const measure = measureFolder(folder(t));
  assert.equal(measure.files.length, 2);
  assert.equal(measure.fileCount, 2);
  assert.equal(measure.rawBytes, 5015);
  const expected = gzipSync('x'.repeat(5000), { level: 9 }).length + gzipSync('<!doctype html>', { level: 9 }).length;
  assert.equal(measure.compressedBytes, expected);
  assert.deepEqual(
    measure.files.map((file) => file.path),
    ['assets/app.js', 'index.html'],
  );
});

test('a folder within its budget passes', (t) => {
  const measure = measureFolder(folder(t));
  assert.deepEqual(checkBudget(measure, { maxFiles: 2, maxCompressedBytes: measure.compressedBytes }), []);
});

test('too many files fail the budget', (t) => {
  const problems = checkBudget(measureFolder(folder(t)), { maxFiles: 1, maxCompressedBytes: 1_000_000 });
  assert.equal(problems.length, 1);
  assert.match(problems[0] ?? '', /2 files.*1/);
});

test('too many compressed bytes fail the budget', (t) => {
  const problems = checkBudget(measureFolder(folder(t)), { maxFiles: 10, maxCompressedBytes: 10 });
  assert.equal(problems.length, 1);
  assert.match(problems[0] ?? '', /compressed/);
});

test('an empty or missing folder fails, because configuration must not quietly pass', (t) => {
  const empty = tempFolder(t);
  assert.throws(() => measureFolder(empty), /no files/);
  assert.throws(() => measureFolder(join(empty, 'absent')));
});
