// Browser test harness on playwright-core and a browser already on the machine, or named by CRUX_CHROME.
// No browser is downloaded. Each test gets a fresh context and mock, and it fails on a content policy
// violation, a page error, a request to another origin or a mock answer outside the specification.
import { existsSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';
import type { Browser, BrowserContext, Page } from 'playwright-core';
import { checkResponse } from '../mock/conformance.ts';
import { startMock } from '../mock/server.ts';
import type { MockHandle, StylePolicy } from '../mock/server.ts';
import { loadSpec } from '../mock/spec.ts';

export const webRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
export const PASSWORD = 'example-password-not-real';
const spec = loadSpec();

export interface AppConfig {
  name: string;
  dist: string;
  stylePolicy: StylePolicy;
}

export interface OpenOptions {
  trustedTypes?: boolean;
  /** Violations are expected, so they are counted and do not fail the test. */
  recordViolationsOnly?: boolean;
  /** The test sends an answer outside the specification on purpose. */
  allowNonConforming?: boolean;
  /** Console errors that the test expects. They are kept in `loggedErrors` and do not fail the test. */
  allowConsoleError?: RegExp;
}

export interface OpenApp {
  page: Page;
  context: BrowserContext;
  mock: MockHandle;
  violations: string[];
  pageErrors: string[];
  consoleErrors: string[];
  /** The console errors that `allowConsoleError` matched. */
  loggedErrors: string[];
  foreignRequests: string[];
  finish(): void;
  close(): Promise<void>;
}

export async function launchBrowser(): Promise<Browser> {
  const executablePath = process.env['CRUX_CHROME'];
  return executablePath
    ? chromium.launch({ headless: true, executablePath })
    : chromium.launch({ headless: true, channel: 'chrome' });
}

/**
 * A browser that starts with the first test that needs it. Nothing launches when a name pattern matches no test
 * of a file, so no hook is left waiting to close a browser that would idle the process.
 */
export function lazyBrowser(): { get(): Promise<Browser>; close(): Promise<void> } {
  let started: Promise<Browser> | undefined;
  return {
    get: () => (started ??= launchBrowser()),
    async close() {
      if (started !== undefined) {
        await (await started).close();
      }
    },
  };
}

export function requireBuilt(config: AppConfig): void {
  if (!existsSync(join(config.dist, 'index.html'))) {
    throw new Error(`${config.dist} has not been built`);
  }
}

export async function openApp(browser: Browser, config: AppConfig, options: OpenOptions = {}): Promise<OpenApp> {
  const problems: string[] = [];
  const mock = await startMock({
    staticDir: config.dist,
    stylePolicy: config.stylePolicy,
    trustedTypes: options.trustedTypes ?? false,
    onApiResponse: (record) => {
      for (const problem of checkResponse(spec, record)) {
        problems.push(`${record.method} ${record.path} ${record.status}: ${problem}`);
      }
    },
  });
  const context = await browser.newContext({ baseURL: mock.url, viewport: { width: 1000, height: 800 } });
  const page = await context.newPage();
  const violations: string[] = [];
  const pageErrors: string[] = [];
  const consoleErrors: string[] = [];
  const loggedErrors: string[] = [];
  const foreignRequests: string[] = [];
  await page.addInitScript(() => {
    document.addEventListener('securitypolicyviolation', (event) => {
      console.error(`CSP-VIOLATION ${event.violatedDirective} ${event.blockedURI}`);
    });
  });
  page.on('console', (message) => {
    const text = message.text();
    if (text.startsWith('CSP-VIOLATION') || /Content Security Policy|Trusted Type/i.test(text)) {
      violations.push(text);
    } else if (message.type() === 'error' && !text.startsWith('Failed to load resource')) {
      (options.allowConsoleError?.test(text) ? loggedErrors : consoleErrors).push(text);
    }
  });
  page.on('pageerror', (error) => pageErrors.push(String(error)));
  page.on('request', (request) => {
    const url = request.url();
    if (!url.startsWith(`${mock.url}/`) && !url.startsWith('data:') && !url.startsWith('about:')) {
      foreignRequests.push(url);
    }
  });
  return {
    page,
    context,
    mock,
    violations,
    pageErrors,
    consoleErrors,
    loggedErrors,
    foreignRequests,
    finish() {
      const all = [
        ...(options.allowNonConforming ? [] : problems.map((problem) => `not in the specification: ${problem}`)),
        ...(options.recordViolationsOnly ? [] : violations.map((violation) => `policy violation: ${violation}`)),
        ...pageErrors.map((error) => `page error: ${error}`),
        ...consoleErrors.map((error) => `console error: ${error}`),
        ...foreignRequests.map((url) => `request to another origin: ${url}`),
      ];
      if (all.length > 0) {
        throw new Error(`the page was not clean:\n${all.join('\n')}`);
      }
    },
    async close() {
      await context.close();
      await mock.close();
    },
  };
}

/** Run one case in a fresh context, saving a screenshot if it fails. */
export async function runCase(
  browser: Browser,
  config: AppConfig,
  caseName: string,
  options: OpenOptions,
  body: (app: OpenApp) => Promise<void>,
): Promise<void> {
  const app = await openApp(browser, config, options);
  try {
    await body(app);
    app.finish();
  } catch (cause) {
    const directory = join(webRoot, 'build', 'e2e-failures');
    mkdirSync(directory, { recursive: true });
    const file = join(directory, `${config.name}-${caseName.replace(/[^a-z0-9]+/gi, '-')}.png`);
    await app.page.screenshot({ path: file }).catch(() => undefined);
    throw cause;
  } finally {
    await app.close();
  }
}

export async function signIn(page: Page, user = 'operator', password = PASSWORD): Promise<void> {
  await page.getByLabel('User name').fill(user);
  await page.getByLabel('Password', { exact: true }).fill(password);
  await page.getByRole('button', { name: 'Sign in' }).click();
}

export async function waitForHeading(page: Page, name: string | RegExp): Promise<void> {
  await page.getByRole('heading', { name, level: 1 }).waitFor();
}
