// The shared browser cases on the Angular sample, under the inline style setting. The sample needs its own
// build first: see samples/web/angular/README.md. CI sets CRUX_REQUIRE_ANGULAR, so that a missing
// build fails there and does not pass as skipped.
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import { webRoot } from './harness.ts';
import { sharedCases } from './shared-cases.ts';

const dist = join(webRoot, '..', 'samples', 'web', 'angular', 'dist');
if (existsSync(join(dist, 'index.html'))) {
  sharedCases({ name: 'angular', dist, stylePolicy: 'inline' });
} else if (process.env['CRUX_REQUIRE_ANGULAR']) {
  throw new Error(`${dist} has not been built`);
} else {
  test('the Angular sample has not been built', { skip: 'run its build first' }, () => undefined);
}
