// Reboot cases for the Preact example. Success is a session that is no longer valid, because the running
// device also answers. The clock is moved by hand, so the wait limits run in no real time.
import assert from 'node:assert/strict';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import type { Browser, Page } from 'playwright-core';
import { launchBrowser, requireBuilt, runCase, signIn, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig, OpenApp } from './harness.ts';

const config: AppConfig = { name: 'preact', dist: join(webRoot, '..', 'samples', 'web', 'preact', 'dist'), stylePolicy: 'strict' };

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

async function openReboot(page: Page, user = 'operator'): Promise<void> {
  await page.goto('/#/reboot');
  await signIn(page, user);
  await waitForHeading(page, 'Restart the device');
}

async function confirmReboot(page: Page): Promise<void> {
  await page.getByRole('button', { name: 'Restart the device…' }).click();
  await page.getByRole('button', { name: 'Restart now' }).click();
}

function posts(app: OpenApp): number {
  return app.mock.control.requests.filter(
    (request) => request.method === 'POST' && request.path === '/api/v1/system/reboot',
  ).length;
}

run('a restart asks for confirmation, and cancelling sends nothing', async (app) => {
  const { page } = app;
  await openReboot(page);
  await page.getByRole('button', { name: 'Restart the device…' }).click();
  await page.getByRole('heading', { name: 'Restart the device now?' }).waitFor();
  assert.equal(await page.evaluate(() => document.activeElement?.textContent), 'Cancel');
  await page.getByRole('button', { name: 'Cancel' }).click();
  assert.equal(await page.getByRole('heading', { name: 'Restart the device now?' }).count(), 0);
  assert.equal(await page.evaluate(() => document.activeElement?.textContent), 'Restart the device…');
  assert.equal(posts(app), 0);
});

run('a restart is success once the session is gone, and sign-in says so', async (app) => {
  const { page, mock } = app;
  // A browser may repeat a request on a dropped connection, so the device stays away for several.
  mock.control.set({ rebootPendingPolls: 1, rebootAwayPolls: 4 });
  await page.clock.install();
  await openReboot(page);
  await confirmReboot(page);
  await page.getByText(/waiting for the device to restart/i).waitFor();
  assert.equal(posts(app), 1);
  const post = mock.control.requests.find((request) => request.method === 'POST' && request.path.endsWith('/reboot'));
  assert.equal(post?.headers['x-xsrf-token'], 'example-xsrf-token-not-real');
  await page.clock.fastForward(2100);
  await page.getByText(/not answering, which is expected/i).waitFor();
  const heading = page.getByRole('heading', { name: 'Sign in', level: 1 });
  for (let tries = 0; tries < 8 && (await heading.count()) === 0; tries += 1) {
    await page.clock.fastForward(2100);
    await page.waitForTimeout(150);
  }
  await waitForHeading(page, 'Sign in');
  await page.getByText(/device has restarted/i).waitFor();
  assert.equal(await page.getByText(/session has ended/i).count(), 0);
});

run('an answer from the running device alone does not count as success', async ({ page, mock }) => {
  mock.control.set({ rebootOutcome: 'never' });
  await page.clock.install();
  await openReboot(page);
  await confirmReboot(page);
  await page.getByText(/waiting for the device to restart/i).waitFor();
  await page.clock.fastForward(2100);
  await page.waitForTimeout(100);
  assert.equal(await page.getByText(/device has restarted/i).count(), 0);
  await waitForHeading(page, 'Restart the device');
});

run('a device that never restarts is reported as not confirmed', async ({ page, mock }) => {
  mock.control.set({ rebootOutcome: 'never' });
  await page.clock.install();
  await openReboot(page);
  await confirmReboot(page);
  await page.getByText(/waiting for the device to restart/i).waitFor();
  await page.clock.fastForward(181_000);
  await page.getByRole('alert').getByText(/not confirmed.*still answering/i).waitFor();
  await waitForHeading(page, 'Restart the device');
  await page.getByRole('button', { name: 'Keep waiting' }).waitFor();
});

run('a device that stays away longer than expected is flagged, then reported as not confirmed', async ({ page, mock }) => {
  mock.control.set({ rebootOutcome: 'away_forever', rebootPendingPolls: 0 });
  await page.clock.install();
  await openReboot(page);
  await confirmReboot(page);
  await page.getByText(/not answering, which is expected/i).waitFor();
  await page.clock.fastForward(61_000);
  await page.getByText(/longer than expected/i).waitFor();
  await page.clock.fastForward(121_000);
  await page.getByRole('alert').getByText(/not confirmed.*has not answered.*serial console/i).waitFor();
});

run('a device that comes back after the limit is still recognised by keeping on waiting', async ({ page, mock }) => {
  mock.control.set({ rebootOutcome: 'away_forever', rebootPendingPolls: 0 });
  await page.clock.install();
  await openReboot(page);
  await confirmReboot(page);
  await page.getByText(/not answering, which is expected/i).waitFor();
  await page.clock.fastForward(181_000);
  await page.getByRole('button', { name: 'Keep waiting' }).waitFor();
  // The device returns with no session.
  mock.control.reset();
  await page.getByRole('button', { name: 'Keep waiting' }).click();
  await waitForHeading(page, 'Sign in');
  await page.getByText(/device has restarted/i).waitFor();
});

run('a failed reset is shown as failed, and the session stays usable', async ({ page, mock }) => {
  mock.control.set({ rebootOutcome: 'fail', rebootPendingPolls: 0 });
  await openReboot(page);
  await confirmReboot(page);
  await page.getByRole('alert').getByText(/restart failed.*still running/i).waitFor();
  await page.getByRole('link', { name: 'Home' }).click();
  await waitForHeading(page, 'Home');
  await page.getByText('192.0.2.10').waitFor();
});

run('a refusal is shown with its reason, and no wait starts', async ({ page, mock }) => {
  await openReboot(page);
  mock.control.set({ rebootPolicy: { refuse: 'Machine cycle running' } });
  await confirmReboot(page);
  await page.getByRole('alert').getByText('The device refused the restart: Machine cycle running').waitFor();
  assert.equal(await page.getByText(/waiting for the device/i).count(), 0);
});

run('a refusal at the second check is shown with its reason', async ({ page, mock }) => {
  mock.control.set({ rebootOutcome: { refuseLate: 'Machine started' }, rebootPendingPolls: 0 });
  await openReboot(page);
  await confirmReboot(page);
  await page.getByRole('alert').getByText('The device refused the restart: Machine started').waitFor();
});

run('a reason from the device is shown as text, never as markup', async ({ page, mock }) => {
  await openReboot(page);
  mock.control.set({ rebootPolicy: { refuse: '<b>bold</b>' } });
  await confirmReboot(page);
  await page.getByRole('alert').getByText('<b>bold</b>').waitFor();
  assert.equal(await page.locator('main b').count(), 0);
});

run('an account with no permission is told so on arrival, and has nothing to press', async (app) => {
  const { page } = app;
  await openReboot(page, 'viewer');
  await page.getByText(/may not restart/i).waitFor();
  assert.equal(await page.getByRole('button', { name: 'Restart the device…' }).count(), 0);
  assert.equal(posts(app), 0);
});

run('a refused request for permission at the POST is reported', async ({ page, mock }) => {
  await openReboot(page);
  mock.control.force('requestReboot', { status: 403, example: 'denied' });
  await confirmReboot(page);
  await page.getByText(/may not restart/i).waitFor();
});

run('a request that is already pending resumes the wait on arrival', async ({ page, mock }) => {
  await page.clock.install();
  mock.control.force('getRebootState', { status: 200, example: 'pending' }, { status: 200, example: 'pending' });
  await openReboot(page);
  await page.getByText(/waiting for the device to restart/i).waitFor();
});

run('an earlier failed request is shown on arrival', async ({ page, mock }) => {
  mock.control.force('getRebootState', { status: 200, example: 'failed' });
  await openReboot(page);
  await page.getByText(/last restart request failed/i).waitFor();
  await page.getByRole('button', { name: 'Restart the device…' }).waitFor();
});

run('an earlier refused request is shown on arrival with its reason', async ({ page, mock }) => {
  mock.control.force('getRebootState', { status: 200, example: 'refused' });
  await openReboot(page);
  await page.getByText('The last restart request was refused: Machine cycle running').waitFor();
});

run('saved network settings that this restart will apply are mentioned first', async ({ page, mock }) => {
  mock.control.force('getNetwork', { status: 200, example: 'reboot_needed' });
  await openReboot(page);
  await page.getByText(/will answer at 192\.0\.2\.20/).waitFor();
});

run('a held route on the request is retried', async (app) => {
  const { page, mock } = app;
  mock.control.set({ rebootOutcome: 'never' });
  await openReboot(page);
  mock.control.force('requestReboot', { status: 409 });
  await confirmReboot(page);
  await page.getByText(/waiting for the device to restart/i).waitFor();
  assert.equal(posts(app), 2);
});

run('an expired session on arrival returns to sign-in', async ({ page, mock }) => {
  await openReboot(page);
  await page.getByRole('link', { name: 'Home' }).click();
  await waitForHeading(page, 'Home');
  // Home's first read has to finish, or that read would end the session before the click.
  await page.getByText('192.0.2.10').waitFor();
  mock.control.expireSessions();
  await page.getByRole('link', { name: 'Restart' }).click();
  await page.getByText(/session has ended/i).waitFor();
  await waitForHeading(page, 'Sign in');
});
