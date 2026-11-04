// Home cases for the Preact reference, beyond the shared ones.
import assert from 'node:assert/strict';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import type { Browser } from 'playwright-core';
import { launchBrowser, requireBuilt, runCase, signIn, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig, OpenApp, OpenOptions } from './harness.ts';

const config: AppConfig = { name: 'reference', dist: join(webRoot, 'reference', 'dist'), stylePolicy: 'strict' };

let browser: Browser;
before(async () => {
  requireBuilt(config);
  browser = await launchBrowser();
});
after(async () => {
  await browser.close();
});

function run(name: string, body: (app: OpenApp) => Promise<void>, options: OpenOptions = {}) {
  test(name, () => runCase(browser, config, name, options, body));
}

function statusReads(app: OpenApp): number {
  return app.mock.control.requests.filter((request) => request.path === '/api/v1/status').length;
}

run('versions, link, address, reset causes and time are shown under their labels', async ({ page }) => {
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  for (const heading of ['Versions', 'Network', 'Last reset', 'Time']) {
    await page.getByRole('heading', { name: heading, level: 2 }).waitFor();
  }
  const item = (label: string) => page.locator('.item', { hasText: label }).locator('dd');
  assert.equal(await item('Platform version').textContent(), '0.1.0-mock');
  assert.equal(await item('Link').first().textContent(), 'Up');
  assert.equal(await item('IPv4 address').textContent(), '192.0.2.10');
  assert.equal(await item('Reset causes').first().textContent(), 'power_on');
  assert.equal(await item('Time since boot').textContent(), '1 d 2 h 3 min');
  assert.equal(await item('Clock').textContent(), 'unset');
  assert.match((await item('Watchdog').textContent()) ?? '', /not available \(busy\)/i);
});

run('a u64 above 2^53 keeps its precision in arithmetic', async ({ page, mock }) => {
  mock.control.set({
    statusItems: [
      { item: { name: 'system.uptime_ms', status: 'ok', type: 'u64', value: '18446744073709551615' }, permission: 'status.read' },
      { item: { name: 'app.big', status: 'ok', type: 'u64', value: '9007199254740993' }, permission: 'status.read' },
    ],
  });
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  await page.getByText('9007199254740993').waitFor();
  await page.getByText('213503982334 d 14 h 25 min').waitFor();
});

run(
  'an uptime that is not a number is shown as sent, and does not break the page',
  async ({ page, mock }) => {
    mock.control.set({
      statusItems: [{ item: { name: 'system.uptime_ms', status: 'ok', type: 'u64', value: 'soon' }, permission: 'status.read' }],
    });
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    await page.getByText('soon').waitFor();
  },
  { allowNonConforming: true },
);

run('status text from the device is shown as text, never as markup', async ({ page, mock }) => {
  const hostile = '<img src=x onerror=alert(1)>';
  mock.control.set({
    statusItems: [{ item: { name: 'app.note', status: 'ok', type: 'text', value: hostile }, permission: 'status.read' }],
  });
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  await page.getByText(hostile).waitFor();
  assert.equal(await page.locator('main img').count(), 0);
});

run('status is read about every five seconds, not faster', async (app) => {
  const { page } = app;
  await page.clock.install();
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  await page.getByText('192.0.2.10').waitFor();
  const first = statusReads(app);
  await page.clock.fastForward(4000);
  await page.waitForTimeout(200);
  assert.equal(statusReads(app), first);
  await Promise.all([
    page.waitForResponse((response) => response.url().endsWith('/api/v1/status')),
    page.clock.fastForward(1100),
  ]);
  assert.equal(statusReads(app), first + 1);
});

run('a hidden page does not poll', async (app) => {
  const { page } = app;
  await page.clock.install();
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  await page.getByText('192.0.2.10').waitFor();
  await page.evaluate(() => {
    Object.defineProperty(document, 'hidden', { value: true, configurable: true });
  });
  const first = statusReads(app);
  await page.clock.fastForward(6000);
  await page.waitForTimeout(200);
  assert.equal(statusReads(app), first);
});

run('a read that fails keeps the last values, says how old they are and recovers', async ({ page, mock }) => {
  await page.clock.install();
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  await page.getByText('192.0.2.10').waitFor();
  mock.control.force('getStatus', { status: 503, example: 'not_ready', retryAfter: 1 });
  await page.getByRole('button', { name: 'Refresh now' }).click();
  await page.getByRole('alert').getByText(/not ready.*showing values from/i).waitFor();
  await page.getByText('192.0.2.10').waitFor();
  await page.getByRole('button', { name: 'Refresh now' }).click();
  await page.getByRole('alert').waitFor({ state: 'detached' });
});

run('leaving home stops its polling', async (app) => {
  const { page } = app;
  await page.clock.install();
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  await page.getByText('192.0.2.10').waitFor();
  await page.getByRole('link', { name: 'Restart' }).click();
  await waitForHeading(page, 'Restart the device');
  const first = statusReads(app);
  await page.clock.fastForward(11_000);
  await page.waitForTimeout(200);
  assert.equal(statusReads(app), first);
});

run('the navigation marks the current page and moves focus to the heading', async ({ page }) => {
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  assert.equal(await page.getByRole('link', { name: 'Home' }).getAttribute('aria-current'), 'page');
  await page.getByRole('link', { name: 'Restart' }).focus();
  await page.keyboard.press('Enter');
  await waitForHeading(page, 'Restart the device');
  assert.equal(await page.getByRole('link', { name: 'Restart' }).getAttribute('aria-current'), 'page');
  assert.equal(await page.evaluate(() => document.activeElement?.tagName), 'H1');
  assert.match(await page.title(), /^Restart the device/);
});

run('an unknown route says so and links home', async ({ page }) => {
  await page.goto('/#/nothing-here');
  await signIn(page);
  await waitForHeading(page, 'Page not found');
  await page.getByRole('main').getByRole('link').click();
  await waitForHeading(page, 'Home');
});
