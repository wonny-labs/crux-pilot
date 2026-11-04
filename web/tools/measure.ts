// Print the file count and sizes of a built folder: node tools/measure.ts <folder> [--max-files n --max-bytes n]
import { parseArgs } from 'node:util';
import { checkBudget, measureFolder } from './budget.ts';

const { values, positionals } = parseArgs({
  allowPositionals: true,
  options: { 'max-files': { type: 'string' }, 'max-bytes': { type: 'string' } },
});
const folder = positionals[0];
if (folder === undefined) {
  process.stderr.write('usage: measure <folder> [--max-files n --max-bytes n]\n');
  process.exit(2);
}
const measure = measureFolder(folder);
process.stdout.write(`${JSON.stringify(measure, null, 2)}\n`);
if (values['max-files'] !== undefined && values['max-bytes'] !== undefined) {
  const problems = checkBudget(measure, {
    maxFiles: Number(values['max-files']),
    maxCompressedBytes: Number(values['max-bytes']),
  });
  for (const problem of problems) {
    process.stderr.write(`${problem}\n`);
  }
  process.exitCode = problems.length === 0 ? 0 : 1;
}
