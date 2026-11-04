// One Node.js version, pinned exactly, across the specification lint, the web tools and the Angular
// sample, and exact dependency versions with a committed lockfile in each.
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const EXACT = /^\d+\.\d+\.\d+$/;

export function checkPins(root: string, packages: string[], running: string): string[] {
  const problems: string[] = [];
  const versions = new Set<string>();
  for (const name of packages) {
    const manifestPath = join(root, name, 'package.json');
    if (!existsSync(manifestPath)) {
      problems.push(`${name}: no package.json`);
      continue;
    }
    const manifest = JSON.parse(readFileSync(manifestPath, 'utf8')) as {
      engines?: { node?: string };
      dependencies?: Record<string, string>;
      devDependencies?: Record<string, string>;
    };
    const node = manifest.engines?.node ?? '(none)';
    versions.add(node);
    if (!EXACT.test(node)) {
      problems.push(`${name}: engines.node is ${node}, not an exact version`);
    }
    for (const [dependency, version] of Object.entries({ ...manifest.dependencies, ...manifest.devDependencies })) {
      if (!EXACT.test(version)) {
        problems.push(`${name}: ${dependency} is ${version}, not an exact version`);
      }
    }
    if (!existsSync(join(root, name, 'package-lock.json'))) {
      problems.push(`${name}: no lockfile`);
    }
  }
  if (versions.size > 1) {
    const list = packages.map((name) => {
      const manifest = JSON.parse(readFileSync(join(root, name, 'package.json'), 'utf8')) as {
        engines?: { node?: string };
      };
      return `${name}=${manifest.engines?.node ?? '(none)'}`;
    });
    problems.push(`different Node.js pins: ${list.join(', ')}`);
  }
  for (const version of versions) {
    if (version !== running && EXACT.test(version)) {
      problems.push(`running Node.js ${running}, but ${version} is pinned`);
    }
  }
  return problems;
}

function main(): number {
  const web = join(dirname(fileURLToPath(import.meta.url)), '..');
  const root = join(web, '..');
  const packages = ['web', 'web/api/lint'];
  if (existsSync(join(root, 'samples/web/angular-minimal/package.json'))) {
    packages.push('samples/web/angular-minimal');
  }
  const problems = checkPins(root, packages, process.versions.node);
  for (const problem of problems) {
    process.stderr.write(`check-pins: ${problem}\n`);
  }
  if (problems.length === 0) {
    process.stdout.write(`check-pins: ${packages.join(', ')} pin Node.js ${process.versions.node}\n`);
  }
  return problems.length === 0 ? 0 : 1;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  process.exitCode = main();
}
