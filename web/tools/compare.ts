// Print the size comparison between the Preact example and the Angular sample, as a Markdown table.
// Build all three folders first. See samples/web/angular/README.md for the commands.
//   node tools/compare.ts
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { measureFolder } from './budget.ts';

const web = join(dirname(fileURLToPath(import.meta.url)), '..');
const sample = join(web, '..', 'samples', 'web', 'angular');

interface LockEntry {
  dev?: boolean;
  devOptional?: boolean;
  optional?: boolean;
}

/** Packages that ship, and all packages in the lockfile. */
export function packageCounts(lockfile: string): { production: number; locked: number } {
  const lock = JSON.parse(readFileSync(lockfile, 'utf8')) as { packages: Record<string, LockEntry> };
  const entries = Object.entries(lock.packages).filter(([path]) => path !== '');
  return {
    production: entries.filter(([, entry]) => !entry.dev && !entry.devOptional).length,
    locked: entries.length,
  };
}

const rows = [
  { name: 'Preact example, all six screens', dist: join(web, '..', 'samples', 'web', 'preact', 'dist'), lock: join(web, 'package-lock.json') },
  { name: 'Preact example, core screens', dist: join(web, 'build', 'two-screens'), lock: join(web, 'package-lock.json') },
  { name: 'Angular sample, core screens', dist: join(sample, 'dist'), lock: join(sample, 'package-lock.json') },
];

process.stdout.write('| Build | Files | Bytes | Compressed bytes | Production packages | Locked packages |\n');
process.stdout.write('| --- | --- | --- | --- | --- | --- |\n');
for (const row of rows) {
  if (!existsSync(row.dist)) {
    process.stdout.write(`| ${row.name} | not built | | | | |\n`);
    continue;
  }
  const measure = measureFolder(row.dist);
  const packages = packageCounts(row.lock);
  process.stdout.write(
    `| ${row.name} | ${measure.fileCount} | ${measure.rawBytes} | ${measure.compressedBytes} | ${packages.production} | ${packages.locked} |\n`,
  );
}
