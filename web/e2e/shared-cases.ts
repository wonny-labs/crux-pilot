// The browser cases that the Preact reference and the Angular sample both pass, for sign-in and home.
// They find elements by role and label only, so they hold for any framework.
import assert from 'node:assert/strict';
import { after, before, test } from 'node:test';
import type { Browser } from 'playwright-core';
import { launchBrowser, requireBuilt, runCase, signIn, waitForHeading } from './harness.ts';
import type { AppConfig, OpenApp } from './harness.ts';

export function sharedCases(config: AppConfig): void {
  let browser: Browser;
  before(async () => {
    requireBuilt(config);
    browser = await launchBrowser();
  });
  after(async () => {
    await browser.close();
  });

  const run = (name: string, body: (app: OpenApp) => Promise<void>) =>
    test(`${config.name}: ${name}`, () => runCase(browser, config, name, {}, body));

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
    assert.ok(mock.control.requests.every((request) => request.path.startsWith('/api/')));
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

  run('a project item that the interface does not know is shown by its name', async ({ page }) => {
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    await page.getByText('app.cycle_count').waitFor();
    await page.getByText('42', { exact: true }).waitFor();
  });
}
