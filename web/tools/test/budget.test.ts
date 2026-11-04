import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { gzipSync } from 'node:zlib';
import { checkBudget, measureFolder } from '../budget.ts';

function folder(): string {
  const directory = mkdtempSync(join(tmpdir(), 'crux-budget-'));
  mkdirSync(join(directory, 'assets'));
  writeFileSync(join(directory, 'index.html'), '<!doctype html>');
  writeFileSync(join(directory, 'assets', 'app.js'), 'x'.repeat(5000));
  return directory;
}

test('a folder is measured file by file, recursively, with compressed size at the highest level', () => {
  const measure = measureFolder(folder());
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

test('a folder within its budget passes', () => {
  const measure = measureFolder(folder());
  assert.deepEqual(checkBudget(measure, { maxFiles: 2, maxCompressedBytes: measure.compressedBytes }), []);
});

test('too many files fail the budget', () => {
  const problems = checkBudget(measureFolder(folder()), { maxFiles: 1, maxCompressedBytes: 1_000_000 });
  assert.equal(problems.length, 1);
  assert.match(problems[0] ?? '', /2 files.*1/);
});

test('too many compressed bytes fail the budget', () => {
  const problems = checkBudget(measureFolder(folder()), { maxFiles: 10, maxCompressedBytes: 10 });
  assert.equal(problems.length, 1);
  assert.match(problems[0] ?? '', /compressed/);
});

test('an empty or missing folder fails, because configuration must not quietly pass', () => {
  const empty = mkdtempSync(join(tmpdir(), 'crux-budget-'));
  assert.throws(() => measureFolder(empty), /no files/);
  assert.throws(() => measureFolder(join(empty, 'absent')));
});
