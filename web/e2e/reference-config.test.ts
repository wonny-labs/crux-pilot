// Configuration cases that belong to the Preact example alone: the page's focus and title, the keyboard, the
// policy with Trusted Types, and a project that names a screen of its own. The cases both examples pass are
// in shared-cases.ts.
import assert from 'node:assert/strict';
import { join } from 'node:path';
import { after, test } from 'node:test';
import type { Page } from 'playwright-core';
import { lazyBrowser, requireBuilt, runCase, signIn, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig, OpenApp, OpenOptions } from './harness.ts';

const config: AppConfig = { name: 'reference', dist: join(webRoot, 'reference', 'dist'), stylePolicy: 'strict' };

const shared = lazyBrowser();
after(() => shared.close());

function run(name: string, body: (app: OpenApp) => Promise<void>, options: OpenOptions = {}) {
  test(name, async () => {
    requireBuilt(config);
    return runCase(await shared.get(), config, name, options, body);
  });
}

/** Open the screen and wait for the last value, so that no read lands while a case is typing. */
async function openSettled(page: Page, user = 'operator'): Promise<void> {
  await page.goto('/#/config');
  await signIn(page, user);
  await waitForHeading(page, 'Configuration');
  await page.getByRole('group', { name: 'build_limit', exact: true }).getByText('Current value: 64', { exact: true }).waitFor();
}

run('the configuration link marks the page, and focus and the title follow it', async ({ page }) => {
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  await page.getByRole('link', { name: 'Configuration' }).focus();
  await page.keyboard.press('Enter');
  await waitForHeading(page, 'Configuration');
  assert.equal(await page.getByRole('link', { name: 'Configuration' }).getAttribute('aria-current'), 'page');
  assert.equal(await page.evaluate(() => document.activeElement?.tagName), 'H1');
  assert.match(await page.title(), /^Configuration/);
});

run('a value is updated with the keyboard alone, and the field error is tied to its field', async ({ page }) => {
  await openSettled(page);
  const field = page.getByRole('group', { name: 'report_interval', exact: true }).getByLabel('New value');
  await field.focus();
  await page.keyboard.press('Control+A');
  await page.keyboard.type('0');
  await page.keyboard.press('Enter');
  await page.getByText('The device does not accept this value.').waitFor();
  const describedBy = await field.getAttribute('aria-describedby');
  assert.ok(describedBy);
  assert.match((await page.locator(`[id="${describedBy}"]`).textContent()) ?? '', /does not accept/);
  assert.equal(await page.evaluate(() => document.activeElement?.id), 'config-report_interval');
  await page.keyboard.press('Control+A');
  await page.keyboard.type('11');
  await page.keyboard.press('Enter');
  await page.getByRole('status').getByText(/updated report_interval/i).waitFor();
});

run('a switch is reached and changed with the keyboard', async ({ page }) => {
  await openSettled(page);
  const box = page.getByRole('group', { name: 'verbose', exact: true }).getByRole('checkbox');
  await box.focus();
  await page.keyboard.press('Space');
  assert.equal(await box.isChecked(), true);
  await page.keyboard.press('Enter');
  await page.getByText('Current value: On', { exact: true }).waitFor();
});

run('the save is announced as it runs, and the page stays quiet otherwise', async ({ page }) => {
  await openSettled(page);
  assert.equal(await page.getByRole('alert').count(), 0);
  await page.getByRole('button', { name: 'Save changes' }).click();
  await page.getByRole('status').getByText('The changes are saved.').waitFor();
  assert.equal(await page.getByRole('alert').count(), 0);
});

run('configuration passes the strict policy with Trusted Types added', async ({ page, violations }) => {
  await openSettled(page);
  await page.getByRole('group', { name: 'report_interval', exact: true }).getByLabel('New value').fill('9');
  await page.getByRole('group', { name: 'report_interval', exact: true }).getByRole('button', { name: 'Update' }).click();
  await page.getByText('Changed, not yet saved').waitFor();
  await page.getByRole('button', { name: 'Save changes' }).click();
  await page.getByText('The changes are saved.').waitFor();
  assert.deepEqual(violations, []);
}, { trustedTypes: true });
