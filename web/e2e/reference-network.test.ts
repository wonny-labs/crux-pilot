// Network settings cases for the Preact reference. Values take effect at the next boot, and the server
// remains the authority.
import assert from 'node:assert/strict';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import type { Browser, Page } from 'playwright-core';
import { launchBrowser, requireBuilt, runCase, signIn, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig, OpenApp } from './harness.ts';

const config: AppConfig = { name: 'reference', dist: join(webRoot, 'reference', 'dist'), stylePolicy: 'strict' };

let browser: Browser;
before(async () => {
  requireBuilt(config);
  browser = await launchBrowser();
});
after(async () => {
  await browser.close();
});

function run(name: string, body: (app: OpenApp) => Promise<void>) {
  test(name, () => runCase(browser, config, name, {}, body));
}

async function openNetwork(page: Page, user = 'operator'): Promise<void> {
  await page.goto('/#/network');
  await signIn(page, user);
  await waitForHeading(page, 'Network settings');
}

async function fill(page: Page, address: string, prefix: string, gateway = ''): Promise<void> {
  await page.getByLabel('IPv4 address').fill(address);
  await page.getByLabel('Prefix length').fill(prefix);
  await page.getByLabel('Gateway').fill(gateway);
}

function puts(app: OpenApp): number {
  return app.mock.control.requests.filter((request) => request.method === 'PUT').length;
}

async function saveThrough(page: Page, address: string, prefix: string, gateway = ''): Promise<void> {
  await fill(page, address, prefix, gateway);
  await page.getByRole('button', { name: 'Review and save' }).click();
  await page.getByRole('button', { name: 'Save the settings' }).click();
}

run('a deep link survives sign-in, and running values are shown', async ({ page }) => {
  await openNetwork(page);
  const running = page.getByRole('region', { name: 'Running now' });
  await running.getByText('192.0.2.10/24').waitFor();
  await page.getByText(/defaults of this firmware/i).waitFor();
  await page.getByText(/nothing is saved/i).waitFor();
});

run('the form starts from the running values when nothing is saved', async ({ page }) => {
  await openNetwork(page);
  assert.equal(await page.getByLabel('IPv4 address').inputValue(), '192.0.2.10');
  assert.equal(await page.getByLabel('Prefix length').inputValue(), '24');
  assert.equal(await page.getByLabel('Gateway').inputValue(), '');
});

run('saved values differ from the running ones, so the screen says a reboot is needed', async ({ page, mock }) => {
  mock.control.force('getNetwork', { status: 200, example: 'reboot_needed' });
  await openNetwork(page);
  await page.getByRole('region', { name: 'Saved for the next boot' }).getByText('192.0.2.20/24').waitFor();
  await page.getByRole('region', { name: 'Saved for the next boot' }).getByText('192.0.2.1').waitFor();
  await page.getByText(/take effect at the next boot/i).first().waitFor();
  await page.getByRole('link', { name: 'Restart the device' }).click();
  await waitForHeading(page, 'Restart the device');
});

run('the form starts from the saved values when there are some', async ({ page, mock }) => {
  mock.control.force('getNetwork', { status: 200, example: 'reboot_needed' });
  await openNetwork(page);
  assert.equal(await page.getByLabel('IPv4 address').inputValue(), '192.0.2.20');
  assert.equal(await page.getByLabel('Gateway').inputValue(), '192.0.2.1');
});

run('an interface with no address says so, and promises nothing about a reboot', async ({ page, mock }) => {
  mock.control.force('getNetwork', { status: 200, example: 'no_address' });
  await openNetwork(page);
  await page.getByText(/no ipv4 address at the moment/i).waitFor();
  await page.getByText(/not promised to restore/i).waitFor();
});

run('a save running on arrival is shown', async ({ page, mock }) => {
  mock.control.force('getNetwork', { status: 200, example: 'save_pending' });
  await openNetwork(page);
  await page.getByText(/a save is running/i).waitFor();
});

run('a failed save on arrival is shown', async ({ page, mock }) => {
  mock.control.force('getNetwork', { status: 200, example: 'save_failed' });
  await openNetwork(page);
  await page.getByText(/last save failed/i).waitFor();
});

run('client-side checks report each field, focus the first and send nothing', async (app) => {
  const { page } = app;
  await openNetwork(page);
  await fill(page, '224.0.0.1', '31', 'x');
  await page.getByRole('button', { name: 'Review and save' }).click();
  await page.getByText(/not a usable device address/i).waitFor();
  await page.getByText(/prefix length from 1 to 30/i).waitFor();
  await page.getByText(/four numbers from 0 to 255/i).waitFor();
  assert.equal(await page.getByLabel('IPv4 address').getAttribute('aria-invalid'), 'true');
  assert.equal(await page.evaluate(() => document.activeElement?.id), 'network-address');
  assert.equal(await page.getByRole('button', { name: 'Save the settings' }).count(), 0);
  assert.equal(puts(app), 0);
});

run('a gateway outside the subnet is refused before any request', async (app) => {
  const { page } = app;
  await openNetwork(page);
  await fill(page, '192.0.2.20', '24', '198.51.100.1');
  await page.getByRole('button', { name: 'Review and save' }).click();
  await page.getByText(/inside the subnet/i).waitFor();
  assert.equal(puts(app), 0);
});

run('field errors from the server appear on their fields', async ({ page, mock }) => {
  await openNetwork(page);
  mock.control.force('putNetwork', { status: 400, example: 'invalid_field' });
  await saveThrough(page, '192.0.2.20', '24', '192.0.2.1');
  await page.getByText(/not a usable device address/i).waitFor();
  await page.getByText(/prefix length from 1 to 30/i).waitFor();
  await page.getByText(/inside the subnet/i).waitFor();
  assert.equal(await page.getByLabel('Gateway').getAttribute('aria-invalid'), 'true');
  assert.equal(await page.evaluate(() => document.activeElement?.id), 'network-address');
});

run('a warning comes before any save, and cancelling sends nothing', async (app) => {
  const { page } = app;
  await openNetwork(page);
  await fill(page, '192.0.2.20', '24', '192.0.2.1');
  await page.getByRole('button', { name: 'Review and save' }).click();
  await page.getByRole('heading', { name: 'Before you save' }).waitFor();
  await page.getByText(/at the next boot the device answers at 192\.0\.2\.20/i).waitFor();
  await page.getByText(/any later reset, including a power cut/i).waitFor();
  await page.getByText(/serial console/i).waitFor();
  assert.equal(await page.evaluate(() => document.activeElement?.textContent), 'Cancel');
  assert.equal(puts(app), 0);
  await page.getByRole('button', { name: 'Cancel' }).click();
  assert.equal(await page.getByRole('heading', { name: 'Before you save' }).count(), 0);
  assert.equal(await page.evaluate(() => document.activeElement?.textContent), 'Review and save');
  assert.equal(puts(app), 0);
});

run('a save is confirmed only when the device shows it, and sends the token', async (app) => {
  const { page, mock } = app;
  await openNetwork(page);
  await saveThrough(page, '192.0.2.20', '24', '192.0.2.1');
  await page.getByRole('status').getByText(/settings are saved/i).waitFor();
  await page.getByRole('region', { name: 'Saved for the next boot' }).getByText('192.0.2.20/24').waitFor();
  await page.getByRole('link', { name: 'Restart the device' }).waitFor();
  const put = mock.control.requests.find((request) => request.method === 'PUT');
  assert.equal(put?.headers['x-xsrf-token'], 'example-xsrf-token-not-real');
  assert.equal(puts(app), 1);
});

run('a request with no gateway leaves it out', async ({ page, mock }) => {
  await openNetwork(page);
  await saveThrough(page, '192.0.2.20', '24');
  await page.getByRole('status').getByText(/settings are saved/i).waitFor();
  assert.equal(await page.getByRole('region', { name: 'Saved for the next boot' }).getByText('None').count(), 1);
  assert.equal(mock.conformanceProblems.length, 0);
});

run('a failed save is reported as failed', async ({ page, mock }) => {
  await openNetwork(page);
  mock.control.set({ saveResult: 'fail', savePolls: 0 });
  await saveThrough(page, '192.0.2.20', '24');
  await page.getByRole('alert').getByText(/save failed/i).waitFor();
  assert.equal(await page.getByText(/settings are saved/i).count(), 0);
});

run('a save that stays pending is reported as not confirmed', async ({ page, mock }) => {
  await page.clock.install();
  await openNetwork(page);
  mock.control.set({ savePolls: 1000 });
  await saveThrough(page, '192.0.2.20', '24');
  await page.getByText(/saving the settings/i).waitFor();
  await page.clock.fastForward(16_000);
  await page.getByText(/not confirmed in time/i).waitFor();
  assert.equal(await page.getByText(/settings are saved/i).count(), 0);
});

run('a read-only account can look, and is told it may not save', async ({ page }) => {
  await openNetwork(page, 'viewer');
  await page.getByRole('region', { name: 'Running now' }).getByText('192.0.2.10/24').waitFor();
  await saveThrough(page, '192.0.2.20', '24');
  await page.getByRole('alert').getByText(/may not change the network settings/i).waitFor();
});

run('an account with no permission is told so, and the server still decides a save', async ({ page }) => {
  await openNetwork(page, 'guest');
  await page.getByText(/may not read the network settings/i).waitFor();
  assert.equal(await page.getByRole('region', { name: 'Running now' }).count(), 0);
  await saveThrough(page, '192.0.2.20', '24');
  await page.getByRole('alert').getByText(/may not change the network settings/i).waitFor();
});

run('an account that may only write learns that it cannot see the result', async ({ page }) => {
  await openNetwork(page, 'writer');
  await page.getByText(/may not read the network settings/i).waitFor();
  await saveThrough(page, '192.0.2.20', '24');
  await page.getByText(/accepted the settings.*cannot see/i).waitFor();
});

run('a device with no Configuration provider says that nothing was saved, and why', async ({ page, mock }) => {
  await openNetwork(page);
  mock.control.set({ networkProvider: false });
  await saveThrough(page, '192.0.2.20', '24');
  await page.getByRole('alert').getByText(/no configuration provider.*nothing was changed/i).waitFor();
  await page.getByRole('region', { name: 'Running now' }).getByText('192.0.2.10/24').waitFor();
});

run('a busy device reports the wait', async ({ page, mock }) => {
  await openNetwork(page);
  mock.control.force('putNetwork', { status: 503, example: 'busy', retryAfter: 30 });
  await saveThrough(page, '192.0.2.20', '24');
  await page.getByRole('alert').getByText(/busy/i).waitFor();
});

run('a rejected token is reported in plain terms', async ({ page, mock }) => {
  await openNetwork(page);
  mock.control.force('putNetwork', { status: 403, example: 'csrf' });
  await saveThrough(page, '192.0.2.20', '24');
  await page.getByRole('alert').getByText(/security token/i).waitFor();
});

run('the form can be completed with the keyboard', async ({ page }) => {
  await openNetwork(page);
  await page.getByLabel('IPv4 address').fill('192.0.2.20');
  await page.keyboard.press('Enter');
  await page.getByRole('heading', { name: 'Before you save' }).waitFor();
  await page.keyboard.press('Tab');
  assert.equal(await page.evaluate(() => document.activeElement?.textContent), 'Save the settings');
  await page.keyboard.press('Enter');
  await page.getByRole('status').getByText(/settings are saved/i).waitFor();
});

run('an expired session during a save returns to sign-in', async ({ page, mock }) => {
  await openNetwork(page);
  mock.control.expireSessions();
  await saveThrough(page, '192.0.2.20', '24');
  await page.getByText(/session has ended/i).waitFor();
  await waitForHeading(page, 'Sign in');
});
