// The browser cases that the Preact reference and the Angular sample both pass: sign-in, home, the forced
// password change and configuration.
// They find elements by role, label and visible text only, so they hold for any framework.
import assert from 'node:assert/strict';
import { after, test } from 'node:test';
import type { Locator, Page } from 'playwright-core';
import { lazyBrowser, PASSWORD, requireBuilt, runCase, signIn, waitForHeading } from './harness.ts';
import type { AppConfig, OpenApp } from './harness.ts';

const NEW_PASSWORD = 'example-new-password-not-real';

/** One configuration parameter's block on the configuration screen. */
function parameter(page: Page, name: string): Locator {
  return page.getByRole('group', { name, exact: true });
}

/**
 * Open the screen and, unless told not to, wait until every value is read. A value that lands while a case is
 * typing would change the field under it, so a case that types waits first.
 */
async function openConfiguration(page: Page, user = 'operator', settled = true): Promise<void> {
  await page.goto('/');
  await signIn(page, user);
  await waitForHeading(page, 'Home');
  await page.getByRole('link', { name: 'Configuration' }).click();
  await waitForHeading(page, 'Configuration');
  await parameter(page, 'report_interval').waitFor();
  if (settled && user !== 'writer' && user !== 'guest') {
    await waitForValues(page);
  }
}

/** The operator's last parameter is read last, so its value shows that the reading has ended. */
async function waitForValues(page: Page): Promise<void> {
  await parameter(page, 'build_limit').getByText('Current value: 64', { exact: true }).waitFor();
}

async function update(page: Page, name: string, value: string): Promise<void> {
  const block = parameter(page, name);
  await block.getByLabel('New value').fill(value);
  await block.getByRole('button', { name: 'Update' }).click();
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

const PROTECTED = ['/api/v1/status', '/api/v1/config', '/api/v1/network', '/api/v1/system/reboot'];

export function sharedCases(config: AppConfig): void {
  const shared = lazyBrowser();
  after(() => shared.close());

  const run = (name: string, body: (app: OpenApp) => Promise<void>) =>
    test(`${config.name}: ${name}`, async () => {
      requireBuilt(config);
      return runCase(await shared.get(), config, name, {}, body);
    });

  run('a visit with no session shows sign-in', async ({ page, mock }) => {
    await page.goto('/');
    await waitForHeading(page, 'Sign in');
    assert.equal(mock.control.requests.some((request) => request.path === '/api/v1/status'), false);
  });

  run('a correct sign-in reaches home and shows the items', async ({ page }) => {
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    await page.getByText('192.0.2.10').waitFor();
    await page.getByText('0.1.0-mock').waitFor();
  });

  run('the session cookie stays out of page scripts, and the token cookie stays readable', async ({ page, context }) => {
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    const seen = await page.evaluate(() => document.cookie);
    assert.equal(seen.includes('crux_session'), false);
    assert.equal(seen.includes('XSRF-TOKEN'), true);
    const session = (await context.cookies()).find((cookie) => cookie.name === 'crux_session');
    assert.equal(session?.httpOnly, true);
    assert.equal(session?.secure, true);
    assert.equal(session?.sameSite, 'Strict');
  });

  run('the password reaches no browser storage, readable cookie or address', async ({ page }) => {
    await page.goto('/');
    await signIn(page);
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
    assert.equal(seen.cookies.includes(PASSWORD), false);
    assert.equal(seen.address.includes(PASSWORD), false);
  });

  run('every poll carries the required header', async ({ page, mock }) => {
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    const polls = mock.control.requests.filter((request) => request.path.includes('/session/attempt/'));
    assert.ok(polls.length >= 2);
    assert.ok(polls.every((request) => request.headers['x-crux-request'] === '1'));
  });

  run('a wrong password shows a rejection with no reason, and allows another try', async ({ page }) => {
    await page.goto('/');
    await signIn(page, 'operator', 'wrong-password');
    await page.getByRole('alert').getByText(/sign-in failed/i).waitFor();
    await waitForHeading(page, 'Sign in');
    assert.equal(await page.getByLabel('Password', { exact: true }).inputValue(), '');
    assert.equal(await page.getByLabel('User name').inputValue(), 'operator');
    await signIn(page);
    await waitForHeading(page, 'Home');
  });

  run('an unknown account looks the same as a wrong password', async ({ page }) => {
    await page.goto('/');
    await signIn(page, 'nobody-here');
    await page.getByRole('alert').getByText(/sign-in failed/i).waitFor();
  });

  run('a delay shows the wait, holds the form during it and allows a later try', async ({ page, mock }) => {
    mock.control.set({ delaySeconds: 2 });
    await page.goto('/');
    await signIn(page);
    await page.getByRole('alert').getByText(/too many attempts.*try again in \d+ second/i).waitFor();
    const posts = () => mock.control.requests.filter((request) => request.method === 'POST').length;
    const before = posts();
    await page.getByRole('button', { name: 'Sign in' }).click({ force: true });
    // Give a request that should not exist the time to reach the mock.
    await page.waitForTimeout(200);
    assert.equal(posts(), before);
    mock.control.set({ delaySeconds: null });
    await page.waitForFunction(() => !document.querySelector('[role="alert"]')?.textContent?.match(/try again in/i), undefined, {
      timeout: 5000,
    });
    await page.getByRole('button', { name: 'Sign in' }).click();
    await waitForHeading(page, 'Home');
  });

  run('a busy verifier is shown with the wait, and a later try works', async ({ page, mock }) => {
    mock.control.set({ attemptBusySeconds: 1 });
    await page.goto('/');
    await signIn(page);
    await page.getByText(/busy.*try again in \d+ second/i).waitFor();
    mock.control.set({ attemptBusySeconds: null });
    await page.waitForFunction(() => !document.body.textContent?.match(/try again in \d/i), undefined, { timeout: 5000 });
    await page.getByRole('button', { name: 'Sign in' }).click();
    await waitForHeading(page, 'Home');
  });

  run('a held login route is retried without the user doing anything', async ({ page, mock }) => {
    mock.control.force('createSession', { status: 409 }, { status: 409 });
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    assert.equal(mock.control.requests.filter((request) => request.method === 'POST').length, 3);
  });

  run('a lost attempt asks the user to sign in again and does not resend the password', async ({ page, mock }) => {
    await page.goto('/');
    mock.control.force('getSessionAttempt', { status: 404 });
    await signIn(page);
    await page.getByRole('alert').getByText(/sign in again/i).waitFor();
    assert.equal(mock.control.requests.filter((request) => request.method === 'POST').length, 1);
  });

  run('an expired session returns to sign-in with a notice, and signing in shows home again', async ({ page, mock }) => {
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    mock.control.expireSessions();
    await page.getByText(/session has ended/i).waitFor({ timeout: 15_000 });
    await waitForHeading(page, 'Sign in');
    await signIn(page);
    await waitForHeading(page, 'Home');
  });

  run('sign out ends the session with the CSRF token and returns to sign-in', async ({ page, mock, context }) => {
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    await page.getByRole('button', { name: 'Sign out' }).click();
    await waitForHeading(page, 'Sign in');
    const request = mock.control.requests.find((entry) => entry.method === 'DELETE');
    assert.equal(request?.headers['x-xsrf-token'], 'example-xsrf-token-not-real');
    assert.deepEqual(
      (await context.cookies()).map((cookie) => cookie.name),
      [],
    );
  });

  run('a reload keeps the session, and the server sees only the root path', async ({ page, mock }) => {
    await page.goto('/#/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    assert.ok(page.url().includes('#'), 'the route is in the fragment');
    await page.reload();
    await waitForHeading(page, 'Home');
    // The route stays in the fragment, so besides the API the server sees only the root and the assets.
    const other = mock.control.requests.map((request) => request.path).filter((path) => !path.startsWith('/api/'));
    assert.ok(other.includes('/'), 'the page itself was loaded');
    assert.deepEqual(
      other.filter((path) => path !== '/' && !/^\/[\w./-]+\.(js|css|svg|ico)$/.test(path)),
      [],
    );
  });

  run('home shows only what the session may read', async ({ page }) => {
    await page.goto('/');
    await signIn(page, 'viewer');
    await waitForHeading(page, 'Home');
    await page.getByText('192.0.2.10').waitFor();
    assert.equal(await page.getByText('app.cycle_count').count(), 0);
  });

  run('a session that may read nothing sees an empty home, and no error', async ({ page }) => {
    await page.goto('/');
    await signIn(page, 'guest');
    await waitForHeading(page, 'Home');
    await page.getByText(/nothing to show/i).waitFor();
    assert.equal(await page.getByRole('alert').count(), 0);
  });

  run('one failed source shows as unavailable without hiding the rest', async ({ page }) => {
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    await page.getByText(/not available/i).first().waitFor();
    await page.getByText('192.0.2.10').waitFor();
  });

  run('an item named like a built-in JavaScript property keeps its name as its label', async ({ page, mock }) => {
    mock.control.set({
      statusItems: ['constructor', 'tostring', '__proto__', 'hasownproperty'].map((name, index) => ({
        item: { name, status: 'ok', type: 'u32', value: 100 + index },
        permission: 'status.read',
      })),
    });
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    for (const name of ['constructor', 'tostring', '__proto__', 'hasownproperty']) {
      await page.getByText(name, { exact: true }).waitFor();
    }
    await page.getByText('100', { exact: true }).waitFor();
  });

  run('a project item that the interface does not know is shown by its name', async ({ page }) => {
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    await page.getByText('app.cycle_count').waitFor();
    await page.getByText('42', { exact: true }).waitFor();
  });

  // Forced password change

  run('a forced change holds the session on its screen whatever the route, and asks for nothing protected', async ({ page, mock }) => {
    await reachForcedChange(page);
    await page.evaluate(() => {
      window.location.hash = '#/config';
    });
    await page.waitForTimeout(300);
    await waitForHeading(page, 'Change your password');
    assert.equal(await page.getByRole('link', { name: 'Configuration' }).count(), 0);
    const asked = mock.control.requests.map((request) => request.path);
    assert.deepEqual(
      asked.filter((path) => PROTECTED.some((protectedPath) => path.startsWith(protectedPath))),
      [],
    );
  });

  run('a protected route cannot be used before the change is made', async ({ page }) => {
    await reachForcedChange(page);
    const seen = await page.evaluate(async () => {
      const token = /(?:^|; )XSRF-TOKEN=([^;]*)/.exec(document.cookie)?.[1] ?? '';
      const list = await (await fetch('/api/v1/config')).json();
      const put = await fetch('/api/v1/config/report_interval', {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json', 'X-XSRF-TOKEN': token },
        body: '{"value":9}',
      });
      const save = await fetch('/api/v1/config/save', { method: 'POST', headers: { 'X-XSRF-TOKEN': token } });
      return { items: list.items.length, put: put.status, save: save.status };
    });
    assert.equal(seen.items, 0);
    assert.notEqual(seen.put, 200);
    assert.notEqual(seen.save, 202);
    // Nothing changed, as the operator would see after the change.
    await fillPasswords(page, PASSWORD, NEW_PASSWORD);
    await waitForHeading(page, 'Home');
    await page.getByRole('link', { name: 'Configuration' }).click();
    await waitForHeading(page, 'Configuration');
    await page.getByText('Current value: 5', { exact: true }).waitFor();
  });

  run('a forced change completes with the token, and the session goes to home with its permissions', async ({ page, mock }) => {
    await reachForcedChange(page);
    await fillPasswords(page, PASSWORD, NEW_PASSWORD);
    await waitForHeading(page, 'Home');
    await page.getByText('192.0.2.10').waitFor();
    const change = mock.control.requests.find((request) => request.path === '/api/v1/session/me/password');
    assert.equal(change?.headers['x-xsrf-token'], 'example-xsrf-token-not-real');
    const polls = mock.control.requests.filter((request) => request.path.includes('/session/attempt/'));
    assert.ok(polls.length >= 1);
    assert.ok(polls.every((request) => request.headers['x-crux-request'] === '1'));
  });

  run('a wrong current password is a field error on that field', async ({ page }) => {
    await reachForcedChange(page);
    await fillPasswords(page, 'not-the-password', NEW_PASSWORD);
    await page.getByText(/current password is not correct/i).waitFor();
    const field = page.getByLabel('Current password');
    assert.equal(await field.getAttribute('aria-invalid'), 'true');
    assert.equal(await field.inputValue(), '');
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

  run('a lost result where the device shows the change succeeded counts as done', async ({ page, mock }) => {
    await reachForcedChange(page);
    mock.control.force('getSessionAttempt', { status: 404 });
    mock.control.force('getCurrentUser', { status: 200, example: 'normal' });
    await fillPasswords(page, PASSWORD, NEW_PASSWORD);
    await waitForHeading(page, 'Home');
  });

  run('a lost result with the change still required never claims success', async ({ page, mock }) => {
    await reachForcedChange(page);
    mock.control.force('getSessionAttempt', { status: 404 });
    await fillPasswords(page, PASSWORD, NEW_PASSWORD);
    await page.getByRole('alert').getByText(/result was lost/i).waitFor();
    await waitForHeading(page, 'Change your password');
    assert.equal(await page.getByLabel('Current password').inputValue(), '');
  });

  run('a session that ended during the change returns to sign-in', async ({ page, mock }) => {
    await reachForcedChange(page);
    mock.control.force('getSessionAttempt', { status: 200, example: 'password_change_session_ended' });
    await fillPasswords(page, PASSWORD, NEW_PASSWORD);
    await page.getByText(/session has ended/i).waitFor();
    await waitForHeading(page, 'Sign in');
  });

  run('an expired session during a forced change returns to sign-in', async ({ page, mock }) => {
    await reachForcedChange(page);
    mock.control.expireSessions();
    await fillPasswords(page, PASSWORD, NEW_PASSWORD);
    await page.getByText(/session has ended/i).waitFor();
    await waitForHeading(page, 'Sign in');
  });

  run('sign out works during a forced change', async ({ page }) => {
    await reachForcedChange(page);
    await page.getByRole('button', { name: 'Sign out' }).click();
    await waitForHeading(page, 'Sign in');
  });

  // Configuration

  run('configuration lists each parameter with its value, and never shows a sensitive one', async ({ page, mock }) => {
    await openConfiguration(page);
    for (const name of ['verbose', 'report_interval', 'device_label', 'link_secret', 'build_limit']) {
      await parameter(page, name).waitFor();
    }
    assert.equal(await page.getByRole('group').count(), 5, 'a parameter the session may not see is not listed');
    await parameter(page, 'verbose').getByText('Current value: Off', { exact: true }).waitFor();
    await parameter(page, 'report_interval').getByText('Current value: 5', { exact: true }).waitFor();
    await parameter(page, 'device_label').getByText('Current value: pilot', { exact: true }).waitFor();
    await parameter(page, 'link_secret').getByText('A sensitive value is never shown.').waitFor();
    await parameter(page, 'build_limit').getByText('Read only for this account.').waitFor();
    await parameter(page, 'build_limit').getByText('Current value: 64', { exact: true }).waitFor();
    assert.equal(await parameter(page, 'link_secret').getByRole('button').count(), 0);
    assert.equal(await parameter(page, 'build_limit').getByRole('button').count(), 0);
    assert.equal((await page.locator('body').innerText()).includes('unset'), false);
    const asked = mock.control.requests.map((request) => request.path);
    assert.equal(asked.includes('/api/v1/config/link_secret'), false);
    assert.equal(asked.includes('/api/v1/config/build_limit'), true);
  });

  run('an update sends the token and the typed value, and shows the parameter as changed and not yet saved', async ({ page, mock }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    await page.getByRole('status').getByText(/updated report_interval/i).waitFor();
    await parameter(page, 'report_interval').getByText('Current value: 10', { exact: true }).waitFor();
    await parameter(page, 'report_interval').getByText('Changed, not yet saved').waitFor();
    const put = mock.control.requests.find((request) => request.method === 'PUT');
    assert.equal(put?.path, '/api/v1/config/report_interval');
    assert.equal(put?.headers['x-xsrf-token'], 'example-xsrf-token-not-real');
    assert.equal(put?.headers['content-type'], 'application/json');
  });

  run('a switch and a text parameter are updated in their own types', async ({ page }) => {
    await openConfiguration(page);
    await parameter(page, 'verbose').getByRole('checkbox').check();
    await parameter(page, 'verbose').getByRole('button', { name: 'Update' }).click();
    await parameter(page, 'verbose').getByText('Current value: On', { exact: true }).waitFor();
    await update(page, 'device_label', 'Pump 3');
    await parameter(page, 'device_label').getByText('Current value: Pump 3', { exact: true }).waitFor();
  });

  run('a value the device refuses shows its field error, keeps the old value and moves focus to the field', async ({ page }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', '0');
    await parameter(page, 'report_interval').getByText('The device does not accept this value.').waitFor();
    const field = parameter(page, 'report_interval').getByLabel('New value');
    assert.equal(await field.getAttribute('aria-invalid'), 'true');
    assert.equal(await page.evaluate(() => document.activeElement?.getAttribute('aria-invalid')), 'true');
    await parameter(page, 'report_interval').getByText('Current value: 5', { exact: true }).waitFor();
    await update(page, 'report_interval', '-1');
    await parameter(page, 'report_interval').getByText('Enter a whole number from 0 to 4294967295.').waitFor();
    await update(page, 'report_interval', '4294967296');
    await parameter(page, 'report_interval').getByText('Enter a whole number from 0 to 4294967295.').waitFor();
    await update(page, 'device_label', 'a bad label');
    await parameter(page, 'device_label').getByText('The device does not accept this value.').waitFor();
    await update(page, 'report_interval', '12');
    await parameter(page, 'report_interval').getByText('Current value: 12', { exact: true }).waitFor();
    assert.equal(await parameter(page, 'report_interval').getByText('The device does not accept this value.').count(), 0);
  });

  run('a field error for the wrong type is shown too', async ({ page, mock }) => {
    await openConfiguration(page);
    mock.control.force('putConfigItem', { status: 400, example: 'wrong_type' });
    await update(page, 'report_interval', '10');
    await parameter(page, 'report_interval').getByText('This is not the kind of value this parameter takes.').waitFor();
  });

  run('text that is no whole number is refused before any request', async ({ page, mock }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', 'ten');
    await parameter(page, 'report_interval').getByText('Enter a whole number.').waitFor();
    await update(page, 'report_interval', '');
    await parameter(page, 'report_interval').getByText('Enter a value.').waitFor();
    assert.equal(mock.control.requests.some((request) => request.method === 'PUT'), false);
  });

  run('a session that may read and not write sees the values and no way to change them', async ({ page }) => {
    await openConfiguration(page, 'viewer');
    await parameter(page, 'report_interval').getByText('Current value: 5', { exact: true }).waitFor();
    assert.equal(await page.getByRole('button', { name: 'Update' }).count(), 0);
    assert.equal(await page.getByRole('button', { name: 'Save changes' }).count(), 0);
    await parameter(page, 'report_interval').getByText('Read only for this account.').waitFor();
  });

  run('a session that may write and not read can update, cannot see the value, and is refused the save', async ({ page, mock }) => {
    await openConfiguration(page, 'writer');
    await parameter(page, 'report_interval').getByText('The value is not shown to this account.').waitFor();
    assert.equal(mock.control.requests.some((request) => request.path.startsWith('/api/v1/config/') && request.method === 'GET'), false);
    await update(page, 'report_interval', '7');
    await parameter(page, 'report_interval').getByText('Changed, not yet saved').waitFor();
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByRole('alert').getByText(/this account may not save changes/i).waitFor();
  });

  run('an update that the device refuses as not permitted says so, and changes nothing on screen', async ({ page, mock }) => {
    await openConfiguration(page);
    mock.control.force('putConfigItem', { status: 403, example: 'denied' });
    await update(page, 'report_interval', '10');
    await page.getByRole('alert').getByText(/this account may not change report_interval/i).waitFor();
    await parameter(page, 'report_interval').getByText('Current value: 5', { exact: true }).waitFor();
  });

  run('an update of a parameter that has gone says so', async ({ page, mock }) => {
    await openConfiguration(page);
    mock.control.force('putConfigItem', { status: 404 });
    await update(page, 'report_interval', '10');
    await page.getByRole('alert').getByText(/no parameter report_interval/i).waitFor();
  });

  run('a held route during an update is retried without the user doing anything', async ({ page, mock }) => {
    await openConfiguration(page);
    mock.control.force('putConfigItem', { status: 409 });
    await update(page, 'report_interval', '10');
    await parameter(page, 'report_interval').getByText('Current value: 10', { exact: true }).waitFor();
    assert.equal(mock.control.requests.filter((request) => request.method === 'PUT').length, 2);
  });

  run('a busy device on an update shows the wait and keeps the typed value', async ({ page, mock }) => {
    await openConfiguration(page);
    mock.control.force('putConfigItem', { status: 503, example: 'busy', retryAfter: 30 });
    await update(page, 'report_interval', '10');
    await page.getByRole('alert').getByText(/busy.*try again in 30 seconds/i).waitFor();
    assert.equal(await parameter(page, 'report_interval').getByLabel('New value').inputValue(), '10');
  });

  run('a service that is not ready on an update is reported', async ({ page, mock }) => {
    await openConfiguration(page);
    mock.control.force('putConfigItem', { status: 503, example: 'not_ready' });
    await update(page, 'report_interval', '10');
    await page.getByRole('alert').getByText(/not ready/i).waitFor();
  });

  run('a save shows pending, then saved, with the token and no body', async ({ page, mock }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    await parameter(page, 'report_interval').getByText('Changed, not yet saved').waitFor();
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByRole('status').getByText(/saving the changes/i).waitFor();
    await page.getByRole('status').getByText('The changes are saved.').waitFor();
    assert.equal(await page.getByText('Changed, not yet saved').count(), 0);
    const post = mock.control.requests.find((request) => request.path === '/api/v1/config/save');
    assert.equal(post?.method, 'POST');
    assert.equal(post?.headers['x-xsrf-token'], 'example-xsrf-token-not-real');
  });

  run('a failed save says what is true, and the parameter stays unsaved', async ({ page, mock }) => {
    mock.control.set({ saveResult: 'fail' });
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByRole('alert').getByText(/the save failed.*could not store the changes/i).waitFor();
    await parameter(page, 'report_interval').getByText('Changed, not yet saved').waitFor();
    assert.equal(await page.getByText('The changes are saved.').count(), 0);
  });

  run('a device with no Configuration provider says that retrying will not help', async ({ page, mock }) => {
    mock.control.set({ networkProvider: false });
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByRole('alert').getByText(/no Configuration provider.*retrying will not help/i).waitFor();
  });

  run('a busy device on a save shows the wait', async ({ page, mock }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    mock.control.force('saveConfig', { status: 503, example: 'busy', retryAfter: 30 });
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByRole('alert').getByText(/busy.*try again in 30 seconds/i).waitFor();
    // The button works again, and a later save succeeds.
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByText('The changes are saved.').waitFor();
  });

  run('a service that is not ready on a save is reported, and a later save works', async ({ page, mock }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    mock.control.force('saveConfig', { status: 503, example: 'not_ready' });
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByRole('alert').getByText(/not ready/i).waitFor();
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByText('The changes are saved.').waitFor();
  });

  run('a device that answers busy while it saves is still waited for', async ({ page, mock }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    // While the worker is inside storage the list answers busy, as the device does.
    mock.control.force('listConfig', { status: 503, example: 'busy', retryAfter: 1 });
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByText('The changes are saved.').waitFor({ timeout: 15_000 });
  });

  run('the configuration list failing shows the error and a way to try again', async ({ page, mock }) => {
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    mock.control.force('listConfig', { status: 503, example: 'not_ready' });
    await page.getByRole('link', { name: 'Configuration' }).click();
    await waitForHeading(page, 'Configuration');
    await page.getByRole('alert').getByText(/not ready/i).waitFor();
    await page.getByRole('button', { name: 'Try again' }).click();
    await parameter(page, 'report_interval').waitFor();
  });

  run('a session that expires during an update returns to sign-in, and signing in works again', async ({ page, mock }) => {
    await openConfiguration(page);
    await waitForValues(page);
    mock.control.expireSessions();
    await update(page, 'report_interval', '10');
    await page.getByText(/session has ended/i).waitFor();
    await waitForHeading(page, 'Sign in');
    await signIn(page);
    // Whether sign-in returns to the page that was open or to home is the example's own choice.
    await waitForHeading(page, /^(Home|Configuration)$/);
  });

  run('a session that expires during a save returns to sign-in', async ({ page, mock }) => {
    await openConfiguration(page);
    await waitForValues(page);
    await update(page, 'report_interval', '10');
    // The read-back after the update has ended, so that the 401 below meets the save and nothing else.
    await parameter(page, 'report_interval').getByText('Current value: 10', { exact: true }).waitFor();
    mock.control.expireSessions();
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByText(/session has ended/i).waitFor();
    await waitForHeading(page, 'Sign in');
  });

  run('configuration with no session shows sign-in and asks for nothing', async ({ page, mock }) => {
    await page.goto('/#/config');
    await waitForHeading(page, 'Sign in');
    assert.equal(mock.control.requests.some((request) => request.path.startsWith('/api/v1/config')), false);
  });

  run('a session that may see no parameter gets an empty list and no error', async ({ page }) => {
    await page.goto('/');
    await signIn(page, 'guest');
    await waitForHeading(page, 'Home');
    await page.getByRole('link', { name: 'Configuration' }).click();
    await waitForHeading(page, 'Configuration');
    await page.getByText(/nothing to configure/i).waitFor();
    assert.equal(await page.getByRole('alert').count(), 0);
  });

  run('a value read after the person started typing does not replace what was typed', async ({ page }) => {
    // Hold the read of this one value until the person has typed.
    let release = (): void => undefined;
    const gate = new Promise<void>((resolve) => {
      release = resolve;
    });
    await page.route('**/api/v1/config/device_label', async (route) => {
      await gate;
      await route.continue();
    });
    await openConfiguration(page, 'operator', false);
    await parameter(page, 'device_label').getByLabel('New value').fill('typed-before-the-read');
    release();
    await waitForValues(page);
    assert.equal(await parameter(page, 'device_label').getByLabel('New value').inputValue(), 'typed-before-the-read');
  });

  run('no value or password reaches browser storage or the address during configuration', async ({ page }) => {
    await openConfiguration(page);
    await update(page, 'device_label', 'marker-value-123');
    await parameter(page, 'device_label').getByText('Current value: marker-value-123', { exact: true }).waitFor();
    const seen = await page.evaluate(async () => ({
      local: { ...localStorage },
      session: { ...sessionStorage },
      databases: (await indexedDB.databases()).length,
      address: window.location.href,
    }));
    assert.deepEqual(seen.local, {});
    assert.deepEqual(seen.session, {});
    assert.equal(seen.databases, 0);
    assert.equal(seen.address.includes('marker-value-123'), false);
  });

  // Overlapping work on one screen

  /** Hold every PUT of one parameter until released, and say when one is waiting. */
  async function holdUpdates(page: Page, name: string): Promise<{ release: () => void }> {
    let release = (): void => undefined;
    const gate = new Promise<void>((resolve) => {
      release = resolve;
    });
    await page.route(`**/api/v1/config/${name}`, async (route) => {
      if (route.request().method() === 'PUT') {
        await gate;
      }
      await route.continue();
    });
    return { release };
  }

  const puts = (mock: OpenApp['mock']): string[] =>
    mock.control.requests.filter((request) => request.method === 'PUT').map((request) => request.path);

  run('a second update while the first is in flight waits for it, and neither screen nor save stays stuck', async ({ page, mock }) => {
    await openConfiguration(page);
    const held = await holdUpdates(page, 'report_interval');
    await update(page, 'report_interval', '10');
    await parameter(page, 'report_interval').getByText(/sending the value/i).waitFor();
    await parameter(page, 'device_label').getByLabel('New value').fill('second');
    await parameter(page, 'device_label').getByLabel('New value').press('Enter');
    await page.waitForTimeout(400);
    assert.deepEqual(puts(mock), [], 'the first request is still held, and the second was not sent');
    held.release();
    await parameter(page, 'report_interval').getByText('Current value: 10', { exact: true }).waitFor();
    assert.equal(await page.getByText(/sending the value/i).count(), 0);
    assert.deepEqual(puts(mock), ['/api/v1/config/report_interval'], 'the first update was not abandoned or doubled');
    // Nothing is stuck: the second update works when asked again, and so does the save.
    await parameter(page, 'device_label').getByRole('button', { name: 'Update' }).click();
    await parameter(page, 'device_label').getByText('Current value: second', { exact: true }).waitFor();
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByText('The changes are saved.').waitFor();
  });

  run('a save asked for while an update is in flight is not sent, and works afterwards', async ({ page, mock }) => {
    await openConfiguration(page);
    const held = await holdUpdates(page, 'report_interval');
    await update(page, 'report_interval', '10');
    await parameter(page, 'report_interval').getByText(/sending the value/i).waitFor();
    // The button is marked disabled, which Playwright will not click, and a person's click can still land.
    await page.getByRole('button', { name: 'Save changes' }).click({ force: true });
    await page.waitForTimeout(400);
    assert.equal(mock.control.requests.some((request) => request.path === '/api/v1/config/save'), false);
    held.release();
    await parameter(page, 'report_interval').getByText('Current value: 10', { exact: true }).waitFor();
    assert.equal(await page.getByText(/sending the value/i).count(), 0);
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByText('The changes are saved.').waitFor();
    assert.equal(mock.control.requests.filter((request) => request.path === '/api/v1/config/save').length, 1);
  });

  run('an update asked for while a save runs is not sent', async ({ page, mock }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    await parameter(page, 'report_interval').getByText('Changed, not yet saved').waitFor();
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByRole('status').getByText(/saving the changes/i).waitFor();
    await parameter(page, 'device_label').getByLabel('New value').fill('during');
    await parameter(page, 'device_label').getByLabel('New value').press('Enter');
    await page.getByText('The changes are saved.').waitFor();
    assert.deepEqual(puts(mock), ['/api/v1/config/report_interval']);
  });

  // Guards that the device's own filtering makes unreachable, and that the screens keep anyway

  run('a parameter flagged sensitive is never read or shown, whatever else its flags say', async ({ page, mock }) => {
    // A device must never send readable and sensitive together, but the screen does not rely on that.
    await page.route('**/api/v1/config', async (route) => {
      const response = await route.fetch();
      const body = (await response.json()) as { items: { name: string; sensitive: boolean; readable: boolean }[] };
      for (const item of body.items) {
        if (item.name === 'device_label') {
          item.sensitive = true;
          item.readable = true;
        }
      }
      await route.fulfill({ response, json: body });
    });
    await openConfiguration(page);
    const block = parameter(page, 'device_label');
    await block.getByText('A sensitive value is never shown.').waitFor();
    assert.equal(await block.getByRole('button').count(), 0);
    assert.equal(await block.getByRole('textbox').count(), 0);
    assert.equal((await page.locator('body').innerText()).includes('pilot'), false);
    assert.equal(mock.control.requests.some((request) => request.path === '/api/v1/config/device_label'), false);
  });

  run('a value from the device is shown as text, never as markup', async ({ page }) => {
    await openConfiguration(page);
    const markup = '<img src=x onerror=alert(1)><b>x</b>';
    await update(page, 'device_label', markup);
    await parameter(page, 'device_label').getByText(`Current value: ${markup}`, { exact: true }).waitFor();
    assert.equal(await page.locator('main img, main b').count(), 0);
  });

  // After a completed save, and while a save waits

  run('after a save has completed, the next update and the next save both work', async ({ page, mock }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    await parameter(page, 'report_interval').getByText('Current value: 10', { exact: true }).waitFor();
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByText('The changes are saved.').waitFor();
    assert.equal(await page.getByText('Changed, not yet saved').count(), 0);
    await update(page, 'report_interval', '11');
    await parameter(page, 'report_interval').getByText('Current value: 11', { exact: true }).waitFor();
    await parameter(page, 'report_interval').getByText('Changed, not yet saved').waitFor();
    assert.equal(await page.getByText('The changes are saved.').count(), 0, 'the old result is gone');
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByText('The changes are saved.').waitFor();
    assert.equal(mock.control.requests.filter((request) => request.path === '/api/v1/config/save').length, 2);
    assert.equal(puts(mock).length, 2);
  });

  run('a long Retry-After while a save runs does not hold the wait past a few seconds', async ({ page, mock }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    // Without a cap this sleeps its whole limit of 15 s and ends as not confirmed.
    mock.control.force('listConfig', { status: 503, example: 'busy', retryAfter: 60 });
    await page.getByRole('button', { name: 'Save changes' }).click();
    await page.getByText('The changes are saved.').waitFor({ timeout: 12_000 });
  });

  run('leaving the screen stops the wait for a save, and nothing reads the list afterwards', async ({ page, mock }) => {
    await openConfiguration(page);
    await update(page, 'report_interval', '10');
    mock.control.force('listConfig', { status: 503, example: 'busy', retryAfter: 60 });
    const reads = (): number =>
      mock.control.requests.filter((request) => request.method === 'GET' && request.path === '/api/v1/config').length;
    const before = reads();
    await page.getByRole('button', { name: 'Save changes' }).click();
    // The wait has begun once its first read, which is the forced busy answer, has reached the mock.
    for (let tries = 0; reads() === before && tries < 100; tries += 1) {
      await page.waitForTimeout(100);
    }
    assert.equal(reads(), before + 1);
    await page.getByRole('link', { name: 'Home' }).click();
    await waitForHeading(page, 'Home');
    const after = reads();
    // Longer than the cap on one sleep, so a wait that kept going would have read again.
    await page.waitForTimeout(7000);
    assert.equal(reads(), after);
  });
}
