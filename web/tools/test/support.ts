// Test support: a temporary folder that goes away with its test, so that a run leaves nothing behind.
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import type { TestContext } from 'node:test';

export function tempFolder(t: TestContext, prefix = 'crux-test-'): string {
  const folder = mkdtempSync(join(tmpdir(), prefix));
  t.after(() => rmSync(folder, { recursive: true, force: true }));
  return folder;
}
