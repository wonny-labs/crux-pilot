// Sign-in and forced password change cases for the Preact example.
import assert from 'node:assert/strict';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import type { Browser, Page } from 'playwright-core';
import { launchBrowser, PASSWORD, requireBuilt, runCase, signIn, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig, OpenApp, OpenOptions } from './harness.ts';

const config: AppConfig = { name: 'preact', dist: join(webRoot, '..', 'samples', 'web', 'preact', 'dist'), stylePolicy: 'strict' };
const NEW_PASSWORD = 'example-new-password-not-real';

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

async function reachForcedChange(page: Page): Promise<void> {
  await page.goto('/');
  await signIn(page, 'newuser');
  await waitForHeading(page, 'Change your password');
}

async function fillPasswords(page: Page, current: string, next: string, repeat = next): Promise<void> {
  await page.getByLabel('Current password').fill(current);
  await page.getByLabel('New password', { exact: true }).fill(next);
  await page.getByLabel('Repeat the new password').fill(repeat);
  await page.getByRole('button', { name: 'Change password' }).click();
}

run('the verifying state is shown while the attempt is pending', async ({ page, mock }) => {
  mock.control.set({ verifyPolls: 3 });
  await page.goto('/');
  await signIn(page);
  await page.getByRole('status').getByText(/checking/i).waitFor();
  await waitForHeading(page, 'Home');
});

run('a wait is announced once, and the running count stays out of the live region', async ({ page, mock }) => {
  mock.control.set({ delaySeconds: 5 });
  await page.goto('/');
  await signIn(page);
  const alert = page.getByRole('alert');
  await alert.getByText(/try again in 5 seconds/i).waitFor();
  const count = page.getByText(/time left: \d+ second/i);
  const first = await count.textContent();
  await page.waitForFunction(
    (text) => !document.body.textContent?.includes(text as string),
    first,
    { timeout: 5000 },
  );
  await alert.getByText(/try again in 5 seconds/i).waitFor();
  assert.equal(await page.locator('[role="alert"] .hint, [role="status"] .hint').count(), 0);
});

run('a sign-in whose session is gone on the next request says so, and does not just start again', async ({ page, mock }) => {
  await page.goto('/');
  await waitForHeading(page, 'Sign in');
  mock.control.force('getCurrentUser', { status: 401 });
  await signIn(page);
  await page.getByRole('alert').getByText(/the session was not there on the next request/i).waitFor();
  await page.getByRole('alert').getByText(/device may also have ended/i).waitFor();
  await page.getByRole('alert').getByText(/https/i).waitFor();
  await waitForHeading(page, 'Sign in');
});

run('empty fields are refused before any request', async ({ page, mock }) => {
  await page.goto('/');
  await waitForHeading(page, 'Sign in');
  await page.getByRole('button', { name: 'Sign in' }).click();
  await page.getByText(/enter your user name/i).waitFor();
  await page.getByText(/enter your password/i).waitFor();
  const user = page.getByLabel('User name');
  assert.equal(await user.getAttribute('aria-invalid'), 'true');
  assert.equal(await page.evaluate(() => document.activeElement?.getAttribute('autocomplete')), 'username');
  assert.equal(mock.control.requests.filter((request) => request.method === 'POST').length, 0);
});

run('sign-in can be completed with the keyboard alone', async ({ page }) => {
  await page.goto('/');
  await waitForHeading(page, 'Sign in');
  assert.equal(await page.evaluate(() => document.activeElement?.getAttribute('autocomplete')), 'username');
  await page.keyboard.type('operator');
  await page.keyboard.press('Tab');
  await page.keyboard.type(PASSWORD);
  await page.keyboard.press('Tab');
  assert.equal(await page.evaluate(() => document.activeElement?.textContent), 'Sign in');
  await page.keyboard.press('Enter');
  await waitForHeading(page, 'Home');
  assert.equal(await page.evaluate(() => document.activeElement?.tagName), 'H1');
});

run('the page title and the landmarks follow the screen', async ({ page }) => {
  await page.goto('/');
  await waitForHeading(page, 'Sign in');
  assert.match(await page.title(), /^Sign in/);
  assert.equal(await page.locator('html').getAttribute('lang'), 'en');
  assert.equal(await page.getByRole('main').count(), 1);
  await signIn(page);
  await waitForHeading(page, 'Home');
  assert.match(await page.title(), /^Home/);
  assert.equal(await page.getByRole('navigation').count(), 1);
});

run('no web path sets a first credential, on a device with no account every login is rejected', async ({ page, mock }) => {
  await page.goto('/');
  await signIn(page, 'first-user', 'any-password-at-all');
  await page.getByRole('alert').getByText(/sign-in failed/i).waitFor();
  await page.getByText(/serial console/i).waitFor();
  assert.equal(await page.getByRole('link').count(), 0);
  assert.equal(await page.locator('input').count(), 2);
  const api = mock.control.requests.filter((request) => request.path.startsWith('/api/'));
  const paths = new Set(api.map((request) => `${request.method} ${request.path.replace(/attempt\/.*/, 'attempt')}`));
  for (const path of paths) {
    assert.ok(
      ['GET /api/v1/session/me', 'POST /api/v1/session', 'GET /api/v1/session/attempt'].includes(path),
      `unexpected request ${path}`,
    );
  }
});

run('no violation under the strict policy with Trusted Types added', async ({ page, violations }) => {
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  await page.getByRole('button', { name: 'Sign out' }).click();
  await waitForHeading(page, 'Sign in');
  assert.deepEqual(violations, []);
}, { trustedTypes: true });

run('the served policy is the strict one', async ({ page, mock }) => {
  const response = await page.goto('/');
  assert.equal(response?.headers()['content-security-policy'], "default-src 'self'; frame-ancestors 'none'");
  assert.ok(mock.url.startsWith('http://127.0.0.1'));
});

run('a refused sign-out is reported, because the session is still alive', async ({ page, mock }) => {
  await page.goto('/');
  await signIn(page);
  await waitForHeading(page, 'Home');
  mock.control.force('deleteSession', { status: 403, example: 'csrf' });
  await page.getByRole('button', { name: 'Sign out' }).click();
  await page.getByRole('alert').getByText(/could not sign out/i).waitFor();
  await waitForHeading(page, 'Home');
});

run('the interface reports a device it cannot reach, and offers another try', async ({ page, mock }) => {
  mock.control.force('getCurrentUser', { status: 503, example: 'not_ready', retryAfter: 1 });
  await page.goto('/');
  await page.getByRole('alert').getByText(/not ready/i).waitFor();
  await page.getByRole('button', { name: 'Try again' }).click();
  await waitForHeading(page, 'Sign in');
});

run('a forced change holds the session on its screen, whatever the route', async ({ page, mock }) => {
  await reachForcedChange(page);
  assert.equal(mock.control.requests.some((request) => request.path === '/api/v1/status'), false);
  assert.equal(await page.getByRole('navigation').count(), 0);
  await page.evaluate(() => {
    window.location.hash = '#/network';
  });
  await waitForHeading(page, 'Change your password');
  assert.equal(mock.control.requests.some((request) => request.path === '/api/v1/network'), false);
});

run('a forced change completes, and the session goes to home with its permissions', async ({ page, mock }) => {
  await reachForcedChange(page);
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await waitForHeading(page, 'Home');
  await page.getByText('192.0.2.10').waitFor();
  const change = mock.control.requests.find((request) => request.path === '/api/v1/session/me/password');
  assert.equal(change?.headers['x-xsrf-token'], 'example-xsrf-token-not-real');
});

run('a wrong current password is a field error, and focus goes to the field', async ({ page }) => {
  await reachForcedChange(page);
  await fillPasswords(page, 'not-the-password', NEW_PASSWORD);
  await page.getByText(/current password is not correct/i).waitFor();
  const field = page.getByLabel('Current password');
  assert.equal(await field.getAttribute('aria-invalid'), 'true');
  assert.equal(await field.inputValue(), '');
  assert.equal(await page.evaluate(() => document.activeElement?.getAttribute('autocomplete')), 'current-password');
  await waitForHeading(page, 'Change your password');
});

run('a refused new password is a field error on the new password', async ({ page }) => {
  await reachForcedChange(page);
  await fillPasswords(page, PASSWORD, 'short');
  await page.getByText(/does not meet the device's password rule/i).waitFor();
  assert.equal(await page.getByLabel('New password', { exact: true }).getAttribute('aria-invalid'), 'true');
});

run('passwords that differ are refused before any request', async ({ page, mock }) => {
  await reachForcedChange(page);
  await fillPasswords(page, PASSWORD, NEW_PASSWORD, 'something-else-entirely');
  await page.getByText(/passwords do not match/i).waitFor();
  assert.equal(mock.control.requests.some((request) => request.path === '/api/v1/session/me/password'), false);
});

run('a failed save says what is true of the running password', async ({ page, mock }) => {
  await reachForcedChange(page);
  mock.control.force('getSessionAttempt', { status: 200, example: 'save_failed' });
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await page.getByRole('alert').getByText(/could not be saved.*running password is unchanged/i).waitFor();
  await waitForHeading(page, 'Change your password');
});

run('a delay during a change shows the wait', async ({ page, mock }) => {
  await reachForcedChange(page);
  mock.control.set({ delaySeconds: 1 });
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await page.getByRole('alert').getByText(/too many attempts.*try again in \d+ second/i).waitFor();
});

run('a lost result where the device shows the change succeeded counts as done', async ({ page, mock }) => {
  await reachForcedChange(page);
  mock.control.force('getSessionAttempt', { status: 404 });
  mock.control.force('getCurrentUser', { status: 200, example: 'normal' });
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await waitForHeading(page, 'Home');
});

run('a lost result with the change still required asks to submit again, and never claims success', async ({ page, mock }) => {
  await reachForcedChange(page);
  mock.control.force('getSessionAttempt', { status: 404 });
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await page.getByRole('alert').getByText(/result was lost/i).waitFor();
  await waitForHeading(page, 'Change your password');
  assert.equal(await page.getByLabel('Current password').inputValue(), '');
  // The second try says incorrect, which may mean that the earlier change took effect.
  await fillPasswords(page, 'not-the-password', NEW_PASSWORD);
  await page.getByText(/earlier attempt may have taken effect/i).waitFor();
  await waitForHeading(page, 'Change your password');
  assert.equal(await page.getByText(/password changed/i).count(), 0);
});

run('after a password poll that failed, a later incorrect is reported as uncertain', async ({ page, mock }) => {
  await reachForcedChange(page);
  mock.control.force('getSessionAttempt', { status: 403, example: 'header_missing' });
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await page.getByRole('alert').getByText(/security token/i).waitFor();
  await fillPasswords(page, 'not-the-password', NEW_PASSWORD);
  await page.getByText(/earlier attempt may have taken effect/i).waitFor();
  assert.equal(await page.getByText(/current password is not correct/i).count(), 0);
});

run('after a password poll that timed out, a later incorrect is reported as uncertain', async ({ page, mock }) => {
  await page.clock.install();
  await reachForcedChange(page);
  mock.control.set({ verifyPolls: 1000 });
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await page.getByRole('status').getByText(/checking and saving/i).waitFor();
  await page.clock.fastForward(31_000);
  await page.getByRole('alert').getByText(/did not finish in time/i).waitFor();
  mock.control.set({ verifyPolls: 0 });
  await fillPasswords(page, 'not-the-password', NEW_PASSWORD);
  await page.getByText(/earlier attempt may have taken effect/i).waitFor();
});

run('after a password POST with no answer, a later incorrect is reported as uncertain', async ({ page }) => {
  await reachForcedChange(page);
  await page.route('**/api/v1/session/me/password', (route) => route.abort('connectionreset'));
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await page.getByRole('alert').getByText(/did not answer/i).waitFor();
  await page.unroute('**/api/v1/session/me/password');
  await fillPasswords(page, 'not-the-password', NEW_PASSWORD);
  await page.getByText(/earlier attempt may have taken effect/i).waitFor();
});

run('a change refused before it was queued leaves a later incorrect as it is', async ({ page, mock }) => {
  await reachForcedChange(page);
  mock.control.force('changeOwnPassword', { status: 403, example: 'csrf' });
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await page.getByRole('alert').getByText(/security token/i).waitFor();
  await fillPasswords(page, 'not-the-password', NEW_PASSWORD);
  await page.getByText(/current password is not correct/i).waitFor();
  assert.equal(await page.getByText(/earlier attempt may have taken effect/i).count(), 0);
});

run('neither password reaches browser storage or a readable cookie during a change', async ({ page }) => {
  await reachForcedChange(page);
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await waitForHeading(page, 'Home');
  const seen = await page.evaluate(async () => ({
    local: { ...localStorage },
    session: { ...sessionStorage },
    databases: (await indexedDB.databases()).length,
    cookies: document.cookie,
    address: window.location.href,
  }));
  assert.deepEqual(seen.local, {});
  assert.deepEqual(seen.session, {});
  assert.equal(seen.databases, 0);
  for (const text of [PASSWORD, NEW_PASSWORD]) {
    assert.equal(seen.cookies.includes(text), false);
    assert.equal(seen.address.includes(text), false);
  }
});

run('a session that ended during the change returns to sign-in', async ({ page, mock }) => {
  await reachForcedChange(page);
  mock.control.force('getSessionAttempt', { status: 200, example: 'password_change_session_ended' });
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await page.getByText(/session has ended/i).waitFor();
  await waitForHeading(page, 'Sign in');
});

run('sign out works during a forced change', async ({ page }) => {
  await reachForcedChange(page);
  await page.getByRole('button', { name: 'Sign out' }).click();
  await waitForHeading(page, 'Sign in');
});

run('an expired session during a forced change returns to sign-in', async ({ page, mock }) => {
  await reachForcedChange(page);
  mock.control.expireSessions();
  await fillPasswords(page, PASSWORD, NEW_PASSWORD);
  await page.getByText(/session has ended/i).waitFor();
  await waitForHeading(page, 'Sign in');
});
