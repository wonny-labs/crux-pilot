// One Node.js version, pinned exactly, across the specification lint, the web tools and the Angular sample.
// Exact dependency versions, and a committed lockfile whose root agrees with its manifest, in each. Lifecycle
// scripts stay off through .npmrc where the tools are installed by hand.
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const EXACT = /^\d+\.\d+\.\d+$/;

interface Manifest {
  engines?: { node?: string };
  dependencies?: Record<string, string>;
  devDependencies?: Record<string, string>;
}

function checkLockfile(name: string, path: string, manifest: Manifest, problems: string[]): void {
  if (!existsSync(path)) {
    problems.push(`${name}: no lockfile`);
    return;
  }
  const lock = JSON.parse(readFileSync(path, 'utf8')) as { packages?: Record<string, Manifest> };
  const root = lock.packages?.[''];
  if (root === undefined) {
    problems.push(`${name}: the lockfile has no root entry`);
    return;
  }
  if (root.engines?.node !== manifest.engines?.node) {
    problems.push(`${name}: the lockfile pins Node.js ${root.engines?.node ?? '(none)'}`);
  }
  const locked = { ...root.dependencies, ...root.devDependencies };
  const wanted = { ...manifest.dependencies, ...manifest.devDependencies };
  for (const dependency of new Set([...Object.keys(locked), ...Object.keys(wanted)])) {
    if (locked[dependency] !== wanted[dependency]) {
      problems.push(`${name}: the lockfile has ${dependency} at ${locked[dependency] ?? '(none)'}, not ${wanted[dependency] ?? '(none)'}`);
    }
  }
}

/** Every action in a workflow is pinned to a full commit, because a tag can be moved. */
export function checkWorkflowPins(workflow: string): string[] {
  const problems: string[] = [];
  for (const match of workflow.matchAll(/^\s*(?:-\s*)?uses:\s*(\S+)/gm)) {
    const action = match[1] as string;
    if (!/@[0-9a-f]{40}$/.test(action)) {
      problems.push(`${action} is not pinned to a full commit`);
    }
  }
  return problems;
}

export function checkPins(root: string, packages: string[], running: string, scriptsOff: string[] = []): string[] {
  const problems: string[] = [];
  const pins = new Map<string, string>();
  for (const name of packages) {
    const manifestPath = join(root, name, 'package.json');
    if (!existsSync(manifestPath)) {
      problems.push(`${name}: no package.json`);
      continue;
    }
    const manifest = JSON.parse(readFileSync(manifestPath, 'utf8')) as Manifest;
    const node = manifest.engines?.node ?? '(none)';
    pins.set(name, node);
    if (!EXACT.test(node)) {
      problems.push(`${name}: engines.node is ${node}, not an exact version`);
    }
    for (const [dependency, version] of Object.entries({ ...manifest.dependencies, ...manifest.devDependencies })) {
      if (!EXACT.test(version)) {
        problems.push(`${name}: ${dependency} is ${version}, not an exact version`);
      }
    }
    checkLockfile(name, join(root, name, 'package-lock.json'), manifest, problems);
  }
  const versions = new Set(pins.values());
  if (versions.size > 1) {
    problems.push(`different Node.js pins: ${[...pins].map(([name, node]) => `${name}=${node}`).join(', ')}`);
  }
  for (const version of versions) {
    if (version !== running && EXACT.test(version)) {
      problems.push(`running Node.js ${running}, but ${version} is pinned`);
    }
  }
  for (const name of scriptsOff) {
    const path = join(root, name, '.npmrc');
    if (!existsSync(path) || !/^ignore-scripts=true$/m.test(readFileSync(path, 'utf8'))) {
      problems.push(`${name}: .npmrc must set ignore-scripts=true`);
    }
  }
  return problems;
}

function main(): number {
  const web = join(dirname(fileURLToPath(import.meta.url)), '..');
  const root = join(web, '..');
  const packages = ['web', 'web/api/lint'];
  const scriptsOff = ['web'];
  if (existsSync(join(root, 'samples/web/angular-minimal/package.json'))) {
    packages.push('samples/web/angular-minimal');
    scriptsOff.push('samples/web/angular-minimal');
  }
  const problems = checkPins(root, packages, process.versions.node, scriptsOff);
  problems.push(...checkWorkflowPins(readFileSync(join(root, '.github/workflows/web.yml'), 'utf8')));
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
