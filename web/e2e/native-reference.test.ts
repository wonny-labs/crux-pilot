// The shared browser cases and the real-server cases on the Preact example, packaged under the strict
// setting into a native_sim image and served over HTTPS (Issue #220). The advanced flows run here only.
import { join } from 'node:path';
import { after } from 'node:test';
import { webRoot } from './harness.ts';
import { nativeCases } from './native-cases.ts';
import { imageFor, nativeTarget } from './native-target.ts';
import { sharedCases } from './shared-cases.ts';

const image = imageFor('CRUX_E2E_IMAGE_PREACT');
if (image) {
  const target = nativeTarget(image);
  after(() => target.devices.close());
  const config = { name: 'reference', dist: join(webRoot, 'reference', 'dist'), stylePolicy: 'strict' as const, target };
  if (process.env['CRUX_E2E_ONLY'] !== 'native') {
    sharedCases(config);
  }
  nativeCases(config, { advanced: true });
}
