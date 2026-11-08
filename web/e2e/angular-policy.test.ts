// What the Angular sample needs from the content policy, measured on sign-in, home, configuration and
// sign-out, with the Preact example measured the same way. Scripts must stay strict in every case.
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import type { Browser } from 'playwright-core';
import { launchBrowser, openApp, signIn, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig, OpenApp } from './harness.ts';
import type { StylePolicy } from '../mock/server.ts';

const angular = join(webRoot, '..', 'samples', 'web', 'angular', 'dist');
const built = existsSync(join(angular, 'index.html'));
if (!built && process.env['CRUX_REQUIRE_ANGULAR']) {
  throw new Error(`${angular} has not been built`);
}

let browser: Browser;
before(async () => {
  browser = await launchBrowser();
});
after(async () => {
  await browser.close();
});

async function walk(config: AppConfig, trustedTypes: boolean): Promise<OpenApp> {
  const app = await openApp(browser, config, { trustedTypes, recordViolationsOnly: true });
  await app.page.goto('/');
  await waitForHeading(app.page, 'Sign in');
  await signIn(app.page);
  await waitForHeading(app.page, 'Home');
  await app.page.getByText('192.0.2.10').waitFor();
  // The configuration screen too: an update and a save, with their notices and field errors.
  await app.page.getByRole('link', { name: 'Configuration' }).click();
  await waitForHeading(app.page, 'Configuration');
  // The last value is read last, so that no read lands while the walk types.
  await app.page.getByRole('group', { name: 'build_limit', exact: true }).getByText('Current value: 64', { exact: true }).waitFor();
  const block = app.page.getByRole('group', { name: 'report_interval', exact: true });
  await block.getByLabel('New value').fill('0');
  await block.getByRole('button', { name: 'Update' }).click();
  await block.getByText('The device does not accept this value.').waitFor();
  await block.getByLabel('New value').fill('9');
  await block.getByRole('button', { name: 'Update' }).click();
  await block.getByText('Changed, not yet saved').waitFor();
  await app.page.getByRole('button', { name: 'Save changes' }).click();
  await app.page.getByText('The changes are saved.').waitFor();
  await app.page.getByRole('button', { name: 'Sign out' }).click();
  await waitForHeading(app.page, 'Sign in');
  return app;
}

function measured(name: string, dist: string, stylePolicy: StylePolicy, trustedTypes: boolean) {
  return { name, config: { name, dist, stylePolicy }, trustedTypes };
}

/** A blocked style also logs a console message, so only the violation events are counted. */
function events(app: OpenApp): string[] {
  return app.violations.filter((violation) => violation.startsWith('CSP-VIOLATION'));
}

const skip = built ? false : 'the Angular sample has not been built';

test('the Angular sample has no violation under the inline style setting', { skip }, async (t) => {
  const { config, trustedTypes } = measured('angular', angular, 'inline', false);
  const app = await walk(config, trustedTypes);
  try {
    t.diagnostic(`angular, inline styles: ${events(app).length} violations`);
    assert.deepEqual(app.violations, []);
    app.finish();
  } finally {
    await app.close();
  }
});

test('under the strict setting the Angular sample breaks only on styles, never on scripts', { skip }, async (t) => {
  const { config, trustedTypes } = measured('angular', angular, 'strict', false);
  const app = await walk(config, trustedTypes);
  try {
    t.diagnostic(`angular, strict: ${events(app).length} violations: ${events(app).join(' | ')}`);
    assert.ok(events(app).length > 0, 'the component style is what the setting exists for');
    assert.ok(
      events(app).every((violation) => /style/.test(violation)),
      events(app).join('\n'),
    );
    assert.equal(
      events(app).some((violation) => /script-src/.test(violation)),
      false,
    );
  } finally {
    await app.close();
  }
});

test('the Angular sample has no violation with Trusted Types added', { skip }, async (t) => {
  const { config, trustedTypes } = measured('angular', angular, 'inline', true);
  const app = await walk(config, trustedTypes);
  try {
    t.diagnostic(`angular, inline styles and Trusted Types: ${events(app).length} violations: ${events(app).join(' | ')}`);
    assert.deepEqual(app.violations, []);
  } finally {
    await app.close();
  }
});

test('the Preact example has no violation under the strict setting with Trusted Types', async (t) => {
  const { config, trustedTypes } = measured('preact', join(webRoot, '..', 'samples', 'web', 'preact', 'dist'), 'strict', true);
  const app = await walk(config, trustedTypes);
  try {
    t.diagnostic(`preact, strict with Trusted Types: ${events(app).length} violations`);
    assert.deepEqual(app.violations, []);
  } finally {
    await app.close();
  }
});
