// A call that rejects must release the configuration screen. The client never throws for an HTTP or a network
// failure, so a project's own reader is made to throw, in a build of the Preact example with that one change.
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
  // The screen logs what it could not make sense of, and these cases expect that.
  const options = { allowConsoleError: /configuration (update|save|read-back) failed/ };
  test(name, async () => runCase(await shared.get(), config(), name, options, body));
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

run('an update whose call rejects says so, logs it, and releases the screen', async ({ page, loggedErrors }) => {
  await open(page);
  await failNextRead(page);
  await block(page).getByLabel('New value').fill('10');
  await block(page).getByRole('button', { name: 'Update' }).click();
  await page.getByRole('alert').getByText(/does not understand/i).waitFor();
  assert.equal(loggedErrors.length, 1);
  assert.match(loggedErrors[0] ?? '', /configuration update failed.*the cookie reader failed/);
  assert.equal(await page.getByText(/sending the value/i).count(), 0);
  // Nothing is stuck: the same update works when asked again, and so does a save.
  await block(page).getByRole('button', { name: 'Update' }).click();
  await block(page).getByText('Current value: 10', { exact: true }).waitFor();
  await page.getByRole('button', { name: 'Save changes' }).click();
  await page.getByText('The changes are saved.').waitFor();
});

run('a save whose call rejects says so, logs it, and releases the screen', async ({ page, loggedErrors }) => {
  await open(page);
  await failNextRead(page);
  await page.getByRole('button', { name: 'Save changes' }).click();
  await page.getByRole('alert').getByText(/does not understand/i).waitFor();
  assert.equal(loggedErrors.length, 1);
  assert.match(loggedErrors[0] ?? '', /configuration save failed.*the cookie reader failed/);
  assert.equal(await page.getByText(/saving the changes/i).count(), 0);
  await page.getByRole('button', { name: 'Save changes' }).click();
  await page.getByText('The changes are saved.').waitFor();
});

run('a read-back that rejects after an accepted update says that the write succeeded', async ({ page, loggedErrors }) => {
  await open(page);
  await page.evaluate(() => {
    (globalThis as unknown as { cruxTestFetch: string }).cruxTestFetch = 'break-get';
  });
  await block(page).getByLabel('New value').fill('10');
  await block(page).getByRole('button', { name: 'Update' }).click();
  await page.getByRole('status').getByText(/updated report_interval, but.*could not be read back/i).waitFor();
  assert.equal(await page.getByRole('alert').count(), 0, 'this is not a failed update');
  await block(page).getByText('Changed, not yet saved').waitFor();
  assert.equal(await block(page).getByText('Current value: 5', { exact: true }).count(), 0, 'the old value is not kept on show');
  assert.equal(loggedErrors.length, 1);
  assert.match(loggedErrors[0] ?? '', /configuration read-back failed/);
  // The screen is released, and a later read shows the device's value.
  await block(page).getByRole('button', { name: 'Update' }).click();
  await block(page).getByText('Current value: 10', { exact: true }).waitFor();
});

run('a call that rejects after the screen was left changes nothing and logs nothing', async ({ page, loggedErrors }) => {
  await open(page);
  await page.evaluate(() => {
    (globalThis as unknown as { cruxTestFetch: string }).cruxTestFetch = 'hold-put';
  });
  await block(page).getByLabel('New value').fill('10');
  await block(page).getByRole('button', { name: 'Update' }).click();
  await page.waitForFunction(() => (globalThis as unknown as { cruxTestRelease?: unknown }).cruxTestRelease !== undefined);
  await page.getByRole('link', { name: 'Home' }).click();
  await waitForHeading(page, 'Home');
  await page.evaluate(() => (globalThis as unknown as { cruxTestRelease: () => void }).cruxTestRelease());
  await page.waitForTimeout(500);
  assert.deepEqual(loggedErrors, []);
});
