// A case for the Preact example only: a deep link to the network settings survives sign-in. The Angular
// example always returns to home after sign-in, so this is not a shared case. The other network settings
// cases, and the restart cases, are in shared-advanced-cases.ts and run on both examples.
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import type { Browser } from 'playwright-core';
import { launchBrowser, requireBuilt, runCase, signIn, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig } from './harness.ts';

const config: AppConfig = { name: 'preact', dist: join(webRoot, '..', 'samples', 'web', 'preact', 'dist'), stylePolicy: 'strict' };

let browser: Browser;
before(async () => {
  requireBuilt(config);
  browser = await launchBrowser();
});
after(async () => {
  await browser.close();
});

test('a deep link survives sign-in, and running values are shown', () =>
  runCase(browser, config, 'a deep link survives sign-in, and running values are shown', {}, async ({ page }) => {
    await page.goto('/#/network');
    await signIn(page);
    await waitForHeading(page, 'Network settings');
    const running = page.getByRole('region', { name: 'Running now' });
    await running.getByText('192.0.2.10/24').waitFor();
    await page.getByText(/defaults of this firmware/i).waitFor();
    await page.getByText(/nothing is saved/i).waitFor();
  }));
