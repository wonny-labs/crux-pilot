// No C build needs Node.js: nothing in a CMake, Kconfig or Zephyr file names the web tools.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { nodeUseIn } from '../c-boundary.ts';

const repo = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');

function tracked(...pathspecs: string[]): string[] {
  return execFileSync('git', ['ls-files', '--', ...pathspecs], { cwd: repo, encoding: 'utf8' })
    .split('\n')
    .filter((line) => line !== '');
}

for (const [name, text] of Object.entries({
  'find_program for node': 'find_program(NODE_EXECUTABLE node REQUIRED)',
  'find_program across lines': 'find_program(\n  NODE_EXECUTABLE\n  NAMES node nodejs\n  REQUIRED\n)',
  'a custom target using the found program': 'add_custom_target(web ALL COMMAND ${NODE_EXECUTABLE} tools/build.ts)',
  'a custom command running node': 'add_custom_command(OUTPUT x COMMAND node tools/build.ts)',
  'node by path': 'add_custom_command(OUTPUT x COMMAND /usr/bin/node build.js)',
  npm: 'execute_process(COMMAND npm ci)',
  npx: 'execute_process(COMMAND npx esbuild)',
  yarn: 'execute_process(COMMAND yarn build)',
  pnpm: 'execute_process(COMMAND pnpm build)',
  'find_package for node': 'find_package(Nodejs REQUIRED)',
  'the web tools by path': 'add_subdirectory(web/reference)',
  'the Angular sample by path': 'add_subdirectory(samples/web/angular-minimal)',
})) {
  test(`${name} counts as a C build using Node.js`, () => {
    assert.notDeepEqual(nodeUseIn(text), [], text);
  });
}

for (const text of [
  'A device tree node holds the settings.',
  'DT_NODELABEL(flexspi)',
  'zephyr_library_sources(src/node.c)',
  'config CRUX_WEB\n  bool "Web service"',
  'add_subdirectory(components/web)',
]) {
  test(`${JSON.stringify(text)} does not count`, () => {
    assert.deepEqual(nodeUseIn(text), []);
  });
}

test('no C build definition names Node.js, npm or the web tools', () => {
  const files = tracked(
    ':(glob)**/CMakeLists.txt',
    ':(glob)**/*.cmake',
    ':(glob)**/Kconfig*',
    ':(glob)**/*.conf',
    ':(glob)**/module.yml',
    'west.yml',
    'CMakePresets.json',
  );
  assert.ok(files.length > 10, 'the file list must not be empty');
  const found = files.flatMap((file) => nodeUseIn(readFileSync(join(repo, file), 'utf8')).map((use) => `${file}: ${use}`));
  assert.deepEqual(found, []);
});

test('no component names the web tools', () => {
  const found = tracked(':(glob)components/**').flatMap((file) =>
    nodeUseIn(readFileSync(join(repo, file), 'latin1')).map((use) => `${file}: ${use}`),
  );
  assert.deepEqual(found, []);
});

test('the frontend adds nothing under components', () => {
  assert.deepEqual(tracked(':(glob)components/**/*.ts', ':(glob)components/**/*.tsx', ':(glob)components/**/package.json'), []);
});
