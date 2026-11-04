// What the Angular sample needs from the content policy, measured on sign-in and home, with the reference
// measured the same way. Scripts must stay strict in every case.
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import type { Browser } from 'playwright-core';
import { launchBrowser, openApp, signIn, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig, OpenApp } from './harness.ts';
import type { StylePolicy } from '../mock/server.ts';

const angular = join(webRoot, '..', 'samples', 'web', 'angular-minimal', 'dist');
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
  await app.page.getByRole('button', { name: 'Sign out' }).click();
  await waitForHeading(app.page, 'Sign in');
  return app;
}

function measured(name: string, dist: string, stylePolicy: StylePolicy, trustedTypes: boolean) {
  return { name, config: { name, dist, stylePolicy }, trustedTypes };
}

const skip = built ? false : 'the Angular sample has not been built';

test('the Angular sample has no violation under the inline style setting', { skip }, async (t) => {
  const { config, trustedTypes } = measured('angular', angular, 'inline', false);
  const app = await walk(config, trustedTypes);
  try {
    t.diagnostic(`angular, inline styles: ${app.violations.length} violations`);
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
    t.diagnostic(`angular, strict: ${app.violations.length} violations: ${app.violations.join(' | ')}`);
    assert.ok(app.violations.length > 0, 'the component style is what the setting exists for');
    assert.ok(
      app.violations.filter((violation) => /CSP-VIOLATION/.test(violation)).every((violation) => /style/.test(violation)),
      app.violations.join('\n'),
    );
    assert.equal(
      app.violations.some((violation) => /script-src/.test(violation)),
      false,
    );
  } finally {
    await app.close();
  }
});

test('the Angular sample under Trusted Types, as a record', { skip }, async (t) => {
  const { config, trustedTypes } = measured('angular', angular, 'inline', true);
  const app = await walk(config, trustedTypes);
  try {
    t.diagnostic(`angular, inline styles and Trusted Types: ${app.violations.length} violations: ${app.violations.join(' | ')}`);
  } finally {
    await app.close();
  }
});

test('the reference has no violation under the strict setting with Trusted Types', async (t) => {
  const { config, trustedTypes } = measured('reference', join(webRoot, 'reference', 'dist'), 'strict', true);
  const app = await walk(config, trustedTypes);
  try {
    t.diagnostic(`reference, strict with Trusted Types: ${app.violations.length} violations`);
    assert.deepEqual(app.violations, []);
  } finally {
    await app.close();
  }
});
