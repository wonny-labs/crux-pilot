// The native_sim target of the browser cases (Issue #220), from the environment that
// scripts/test-native-composition.sh e2e-mbed and e2e-wolf set. Without CRUX_E2E_IMAGE the native files skip,
// unless CRUX_REQUIRE_NATIVE is set, so that a missing image fails a run that expects one.
import { test } from 'node:test';
import { DeviceFactory, testCertificateSpki } from './device.ts';
import type { Fixture, NativeTarget } from './harness.ts';

/** What the image of tests/native/management_services/e2e shows, by its src/main.c: net.ipv4 reports no data and
 * system.watchdog_state not supported on native_sim, so home has items that are not available. */
export const NATIVE_FIXTURE: Fixture = {
  homeTexts: ['0.1.0-native_sim', 'e2e-220'],
  viewerText: '0.1.0-native_sim',
  unavailableItem: true,
  minPolls: 1,
  configurationProvider: false,
};

export function nativeTarget(image: string): NativeTarget {
  const results = process.env['CRUX_E2E_RESULTS'];
  return {
    kind: 'native_sim',
    devices: new DeviceFactory(image),
    idleSeconds: Number(process.env['CRUX_E2E_IDLE_S'] ?? '30'),
    fixture: NATIVE_FIXTURE,
    spki: testCertificateSpki(),
    ...(results ? { results } : {}),
  };
}

/** The image named for this example, or undefined after recording a skip. */
export function imageFor(variable: string): string | undefined {
  const image = process.env[variable];
  if (image) {
    return image;
  }
  if (process.env['CRUX_REQUIRE_NATIVE']) {
    throw new Error(`${variable} names no native_sim image`);
  }
  test(`no native_sim image in ${variable}`, { skip: 'scripts/test-native-composition.sh e2e-mbed builds one' }, () => undefined);
  return undefined;
}
