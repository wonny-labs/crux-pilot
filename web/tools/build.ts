// Build a frontend folder with esbuild: one hashed script, one hashed style, an index and an icon. The
// firmware build takes the resulting folder and needs no Node.js.
//   node tools/build.ts [--entry file] [--out folder] [--budget file]
// A build over its budget fails.
import { copyFileSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { build } from 'esbuild';
import { checkBudget, measureFolder } from './budget.ts';
import type { Budget } from './budget.ts';

const web = join(dirname(fileURLToPath(import.meta.url)), '..');
const { values } = parseArgs({
  options: {
    entry: { type: 'string', default: join(web, 'reference', 'src', 'main.tsx') },
    out: { type: 'string', default: join(web, 'reference', 'dist') },
    budget: { type: 'string', default: join(web, 'reference', 'budget.json') },
  },
});

const out = values.out;
rmSync(out, { recursive: true, force: true });
mkdirSync(join(out, 'assets'), { recursive: true });

const result = await build({
  entryPoints: { app: values.entry },
  bundle: true,
  format: 'esm',
  splitting: false,
  minify: true,
  target: ['chrome120', 'edge120', 'firefox120'],
  jsx: 'automatic',
  jsxImportSource: 'preact',
  outdir: join(out, 'assets'),
  entryNames: '[name].[hash]',
  metafile: true,
  legalComments: 'none',
  sourcemap: false,
  logLevel: 'warning',
});

let script: string | undefined;
let style: string | undefined;
for (const output of Object.keys(result.metafile.outputs)) {
  if (output.endsWith('.js')) {
    script = basename(output);
  } else if (output.endsWith('.css')) {
    style = basename(output);
  }
}
if (script === undefined || style === undefined) {
  process.stderr.write('build: the entry must produce one script and one style\n');
  process.exit(1);
}

const index = readFileSync(join(web, 'reference', 'index.html'), 'utf8')
  .replace('./assets/app.js', `./assets/${script}`)
  .replace('./assets/app.css', `./assets/${style}`);
writeFileSync(join(out, 'index.html'), index);
copyFileSync(join(web, 'reference', 'favicon.svg'), join(out, 'favicon.svg'));

const measure = measureFolder(out);
const budget = JSON.parse(readFileSync(values.budget, 'utf8')) as Budget;
const problems = checkBudget(measure, budget);
process.stdout.write(
  `build: ${out}: ${measure.fileCount} files, ${measure.rawBytes} bytes, ${measure.compressedBytes} compressed\n`,
);
for (const problem of problems) {
  process.stderr.write(`build: ${problem}\n`);
}
if (problems.length > 0) {
  process.exitCode = 1;
}
