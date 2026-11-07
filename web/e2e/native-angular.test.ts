// The shared browser cases and the real-server cases on the Angular example, packaged under the inline
// style setting into a native_sim image and served over HTTPS (Issue #220). Core flows only.
import { join } from 'node:path';
import { after } from 'node:test';
import { webRoot } from './harness.ts';
import { nativeCases } from './native-cases.ts';
import { imageFor, nativeTarget } from './native-target.ts';
import { sharedCases } from './shared-cases.ts';

const image = imageFor('CRUX_E2E_IMAGE_ANGULAR');
if (image) {
  const target = nativeTarget(image);
  after(() => target.devices.close());
  const dist = join(webRoot, '..', 'samples', 'web', 'angular', 'dist');
  const config = { name: 'angular', dist, stylePolicy: 'inline' as const, target };
  if (process.env['CRUX_E2E_ONLY'] !== 'native') {
    sharedCases(config);
  }
  nativeCases(config, { advanced: false });
}
