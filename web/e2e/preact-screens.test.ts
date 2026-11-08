// Screenshots of each screen, for review. It runs only when CRUX_SCREENSHOTS names a folder, and that
// folder must be outside the repository or ignored by it, because images are not committed.
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import type { Browser, Page } from 'playwright-core';
import { launchBrowser, openApp, signIn, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig, OpenApp } from './harness.ts';

const folder = process.env['CRUX_SCREENSHOTS'];
const config: AppConfig = { name: 'preact', dist: join(webRoot, '..', 'samples', 'web', 'preact', 'dist'), stylePolicy: 'strict' };

let browser: Browser;
before(async () => {
  if (folder !== undefined) {
    mkdirSync(folder, { recursive: true });
    browser = await launchBrowser();
  }
});
after(async () => {
  await browser?.close();
});

async function shot(page: Page, name: string): Promise<void> {
  await page.screenshot({ path: join(folder as string, `${name}.png`), fullPage: true });
}

async function capture(name: string, body: (app: OpenApp) => Promise<void>): Promise<void> {
  const app = await openApp(browser, config);
  try {
    await body(app);
    await shot(app.page, name);
  } finally {
    await app.close();
  }
}

test('screenshots of each screen', { skip: folder === undefined }, async () => {
  await capture('sign-in', async ({ page }) => {
    await page.goto('/');
    await waitForHeading(page, 'Sign in');
  });
  await capture('sign-in-rejected', async ({ page }) => {
    await page.goto('/');
    await signIn(page, 'operator', 'wrong');
    await page.getByRole('alert').waitFor();
  });
  await capture('sign-in-delay', async ({ page, mock }) => {
    mock.control.set({ delaySeconds: 30 });
    await page.goto('/');
    await signIn(page);
    await page.getByRole('alert').waitFor();
  });
  await capture('forced-change', async ({ page }) => {
    await page.goto('/');
    await signIn(page, 'newuser');
    await waitForHeading(page, 'Change your password');
  });
  await capture('home', async ({ page }) => {
    await page.goto('/');
    await signIn(page);
    await page.getByText('192.0.2.10').waitFor();
  });
  await capture('home-no-permission', async ({ page }) => {
    await page.goto('/');
    await signIn(page, 'guest');
    await page.getByText(/nothing to show/i).waitFor();
  });
  await capture('network', async ({ page, mock }) => {
    mock.control.force('getNetwork', { status: 200, example: 'reboot_needed' });
    await page.goto('/#/network');
    await signIn(page);
    await page.getByRole('region', { name: 'Saved for the next boot' }).waitFor();
  });
  await capture('network-warning', async ({ page }) => {
    await page.goto('/#/network');
    await signIn(page);
    await page.getByLabel('IPv4 address').fill('192.0.2.20');
    await page.getByRole('button', { name: 'Review and save' }).click();
    await page.getByRole('heading', { name: 'Before you save' }).waitFor();
  });
  await capture('network-not-supported', async ({ page, mock }) => {
    await page.goto('/#/network');
    await signIn(page);
    mock.control.set({ networkProvider: false });
    await page.getByLabel('IPv4 address').fill('192.0.2.20');
    await page.getByRole('button', { name: 'Review and save' }).click();
    await page.getByRole('button', { name: 'Save the settings' }).click();
    await page.getByRole('alert').waitFor();
  });
  await capture('reboot-confirm', async ({ page }) => {
    await page.goto('/#/reboot');
    await signIn(page);
    await page.getByRole('button', { name: 'Restart the device…' }).click();
  });
  await capture('reboot-wait', async ({ page, mock }) => {
    mock.control.set({ rebootOutcome: 'never' });
    await page.goto('/#/reboot');
    await signIn(page);
    await page.getByRole('button', { name: 'Restart the device…' }).click();
    await page.getByRole('button', { name: 'Restart now' }).click();
    await page.getByText(/waiting for the device/i).waitFor();
  });
  await capture('reboot-refused', async ({ page, mock }) => {
    await page.goto('/#/reboot');
    await signIn(page);
    mock.control.set({ rebootPolicy: { refuse: 'Machine cycle running' } });
    await page.getByRole('button', { name: 'Restart the device…' }).click();
    await page.getByRole('button', { name: 'Restart now' }).click();
    await page.getByRole('alert').waitFor();
  });
  await capture('reboot-not-confirmed', async ({ page, mock }) => {
    mock.control.set({ rebootOutcome: 'away_forever', rebootPendingPolls: 0 });
    await page.clock.install();
    await page.goto('/#/reboot');
    await signIn(page);
    await page.getByRole('button', { name: 'Restart the device…' }).click();
    await page.getByRole('button', { name: 'Restart now' }).click();
    await page.getByText(/not answering/i).waitFor();
    await page.clock.fastForward(181_000);
    await page.getByRole('alert').waitFor();
  });
  await capture('phone-home', async ({ page }) => {
    await page.setViewportSize({ width: 360, height: 740 });
    await page.goto('/');
    await signIn(page);
    await page.getByText('192.0.2.10').waitFor();
  });
});
