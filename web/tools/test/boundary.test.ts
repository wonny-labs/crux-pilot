// No C build needs Node.js: nothing in a CMake, Kconfig or Zephyr file names the web tools.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

const repo = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');

function matches(pattern: string, pathspecs: string[]): string[] {
  // git grep exits 1 for no match, which is the passing case here.
  try {
    return execFileSync('git', ['grep', '-n', '-E', pattern, '--', ...pathspecs], { cwd: repo, encoding: 'utf8' })
      .split('\n')
      .filter((line) => line !== '');
  } catch (cause) {
    if ((cause as { status?: number }).status === 1) {
      return [];
    }
    throw cause;
  }
}

test('no C build definition names Node.js, npm or the web folder', () => {
  const buildFiles = ['CMakeLists.txt', '**/CMakeLists.txt', '*.cmake', '**/*.cmake', '**/Kconfig*', '**/*.conf', 'west.yml'];
  assert.deepEqual(matches(String.raw`\b(npm|npx|nodejs)\b|[^a-z_]node [a-z-]|\bweb/(client|reference|mock|tools|api)`, buildFiles), []);
});

test('no component names the web folder', () => {
  assert.deepEqual(matches(String.raw`web/(client|reference|mock|tools|api)`, ['components/**']), []);
});

test('the frontend adds nothing under components', () => {
  const tracked = execFileSync('git', ['ls-files', 'components/**/*.ts', 'components/**/*.tsx', 'components/**/package.json'], {
    cwd: repo,
    encoding: 'utf8',
  });
  assert.equal(tracked, '');
});
