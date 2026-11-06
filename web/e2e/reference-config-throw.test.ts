// A call that rejects must release the configuration screen. The client never throws for an HTTP or a network
// failure, so a project's own reader is made to throw, in a build of the reference with that one change.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { after, test } from 'node:test';
import type { Page } from 'playwright-core';
import { lazyBrowser, runCase, signIn, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig, OpenApp } from './harness.ts';

const shared = lazyBrowser();
after(() => shared.close());

let built: AppConfig | undefined;
function config(): AppConfig {
  if (built === undefined) {
    const dist = join(webRoot, 'build', 'throwing');
    execFileSync(process.execPath, [
      join(webRoot, 'tools', 'build.ts'),
      '--entry',
      join(webRoot, 'e2e', 'fixtures', 'throwing-client-entry.tsx'),
      '--out',
      dist,
    ]);
    built = { name: 'throwing', dist, stylePolicy: 'strict' };
  }
  return built;
}

function run(name: string, body: (app: OpenApp) => Promise<void>) {
  test(name, async () => runCase(await shared.get(), config(), name, {}, body));
}

async function open(page: Page): Promise<void> {
  await page.goto('/#/config');
  await signIn(page);
  await waitForHeading(page, 'Configuration');
  await page.getByRole('group', { name: 'build_limit', exact: true }).getByText('Current value: 64', { exact: true }).waitFor();
}

async function failNextRead(page: Page): Promise<void> {
  await page.evaluate(() => {
    (globalThis as unknown as { cruxTestThrows: number }).cruxTestThrows = 1;
  });
}

const block = (page: Page) => page.getByRole('group', { name: 'report_interval', exact: true });

run('an update whose call rejects says so, and the screen is released', async ({ page }) => {
  await open(page);
  await failNextRead(page);
  await block(page).getByLabel('New value').fill('10');
  await block(page).getByRole('button', { name: 'Update' }).click();
  await page.getByRole('alert').getByText(/does not understand/i).waitFor();
  assert.equal(await page.getByText(/sending the value/i).count(), 0);
  // Nothing is stuck: the same update works when asked again, and so does a save.
  await block(page).getByRole('button', { name: 'Update' }).click();
  await block(page).getByText('Current value: 10', { exact: true }).waitFor();
  await page.getByRole('button', { name: 'Save changes' }).click();
  await page.getByText('The changes are saved.').waitFor();
});

run('a save whose call rejects says so, and the screen is released', async ({ page }) => {
  await open(page);
  await failNextRead(page);
  await page.getByRole('button', { name: 'Save changes' }).click();
  await page.getByRole('alert').getByText(/does not understand/i).waitFor();
  assert.equal(await page.getByText(/saving the changes/i).count(), 0);
  await page.getByRole('button', { name: 'Save changes' }).click();
  await page.getByText('The changes are saved.').waitFor();
});
