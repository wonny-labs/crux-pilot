// A project adds a home item, text and timing without changing any reference file, and can drop screens.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import type { Browser } from 'playwright-core';
import { launchBrowser, PASSWORD, runCase, waitForHeading, webRoot } from './harness.ts';
import type { AppConfig } from './harness.ts';

function buildEntry(entry: string, out: string): AppConfig {
  const dist = join(webRoot, 'build', out);
  execFileSync(process.execPath, [
    join(webRoot, 'tools', 'build.ts'),
    '--entry',
    join(webRoot, 'e2e', 'fixtures', entry),
    '--out',
    dist,
  ]);
  return { name: out, dist, stylePolicy: 'strict' };
}

let browser: Browser;
let project: AppConfig;
let twoScreens: AppConfig;
before(async () => {
  project = buildEntry('project-entry.tsx', 'project');
  twoScreens = buildEntry('two-screens.tsx', 'two-screens');
  browser = await launchBrowser();
});
after(async () => {
  await browser.close();
});

test('a project labels its own status item through the options alone', () =>
  runCase(browser, project, 'project item', {}, async ({ page }) => {
    await page.goto('/');
    await waitForHeading(page, 'Operator sign-in');
    assert.match(await page.title(), /Example Press/);
    await page.getByLabel('User name').fill('operator');
    await page.getByLabel('Password', { exact: true }).fill(PASSWORD);
    await page.getByRole('button', { name: 'Sign in' }).click();
    await waitForHeading(page, 'Home');
    await page.getByRole('heading', { name: 'Machine', level: 2 }).waitFor();
    await page.getByText('Machine cycles').waitFor();
    await page.getByText('42 cycles').waitFor();
    assert.equal(await page.getByText('app.cycle_count').count(), 0);
    await page.getByText('Example Press').first().waitFor();
  }));

test('a build can drop screens, and the navigation follows', () =>
  runCase(browser, twoScreens, 'two screens', {}, async ({ page }) => {
    await page.goto('/');
    await page.getByLabel('User name').fill('operator');
    await page.getByLabel('Password', { exact: true }).fill(PASSWORD);
    await page.getByRole('button', { name: 'Sign in' }).click();
    await waitForHeading(page, 'Home');
    assert.equal(await page.getByRole('link', { name: 'Restart' }).count(), 0);
    assert.equal(await page.getByRole('link', { name: 'Network' }).count(), 0);
  }));
