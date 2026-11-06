// Browser test harness on playwright-core and a browser already on the machine, or named by CRUX_CHROME.
// No browser is downloaded. Each test gets a fresh context and mock, and it fails on a content policy
// violation, a page error, a request to another origin or a mock answer outside the specification.
//
// A target other than the mock (Issue #220): an AppConfig with a `target` runs the same case against a
// native_sim image over HTTPS, a fresh device for each case. Each result is recorded with its target, so
// that mock and native_sim results stay apart. A case that needs the mock's control says so, and is skipped
// on native_sim with that reason, never passed.
import { appendFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';
import type { Browser, BrowserContext, LaunchOptions, Page, Route } from 'playwright-core';
import { checkResponse } from '../mock/conformance.ts';
import type { ResponseRecord } from '../mock/conformance.ts';
import { startMock } from '../mock/server.ts';
import type { MockHandle, RecordedRequest, StylePolicy } from '../mock/server.ts';
import { loadSpec } from '../mock/spec.ts';
import type { Spec } from '../mock/spec.ts';
import type { Device, DeviceFactory } from './device.ts';
import { request as rawRequest } from './native-raw.ts';

export const webRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
export const PASSWORD = 'example-password-not-real';
const spec = loadSpec();

export type TargetKind = 'mock' | 'native_sim';

/** What a case may expect to see, which differs between the mock's data and a device's. */
export interface Fixture {
  /** Texts that home shows to the operator. */
  homeTexts: string[];
  /** A text that home shows to the viewer, who may read status. */
  viewerText: string;
  /** Some status item reports itself not available. */
  unavailableItem: boolean;
  /** The least number of polls a sign-in makes: the mock answers pending once, a device may not. */
  minPolls: number;
  /** The token cookie's value, where it is fixed. */
  fixedXsrfToken?: string;
  /** A save can succeed: the mock saves, a device with no Configuration provider cannot (decision C6). */
  configurationProvider: boolean;
}

export const MOCK_FIXTURE: Fixture = {
  homeTexts: ['192.0.2.10', '0.1.0-mock'],
  viewerText: '192.0.2.10',
  unavailableItem: true,
  minPolls: 2,
  fixedXsrfToken: 'example-xsrf-token-not-real',
  configurationProvider: true,
};

export interface NativeTarget {
  kind: 'native_sim';
  devices: DeviceFactory;
  /** The image's declared idle limit, which the real session clock applies. */
  idleSeconds: number;
  fixture: Fixture;
  /** Chrome's --ignore-certificate-errors-spki-list value: the test certificate's key, and no other. */
  spki: string;
  /** Where each case's result is appended, one JSON line each. */
  results?: string;
}

export interface AppConfig {
  name: string;
  dist: string;
  stylePolicy: StylePolicy;
  /** Absent: the mock. */
  target?: NativeTarget;
}

/** What a case needs that a target may not have. A case is skipped where its need is missing, with the reason. */
export type Need = 'mock-control' | 'configuration-provider' | 'no-configuration-provider' | 'unavailable-item' | 'reboot-service';

export function missingNeed(config: AppConfig, needs: Need[]): string | undefined {
  const fixture = config.target?.fixture ?? MOCK_FIXTURE;
  for (const need of needs) {
    if (need === 'mock-control' && config.target) {
      return 'needs the mock control to force an answer; the real path is a case of its own where one exists';
    }
    if (need === 'configuration-provider' && !fixture.configurationProvider) {
      return 'needs a save that succeeds; this image has no Configuration provider (decision C6)';
    }
    if (need === 'no-configuration-provider' && fixture.configurationProvider && config.target) {
      return 'needs a device with no Configuration provider, and this one has one';
    }
    if (need === 'reboot-service' && config.target) {
      return 'the reboot service of #214 is not merged and not in this image, so the reboot flow stays mock-only';
    }
    if (need === 'unavailable-item' && !fixture.unavailableItem) {
      return 'needs a status item that reports itself not available, which this image does not have';
    }
  }
  return undefined;
}

/** Appends one case's result, with its target, where the target names a results file. */
export function recordResult(config: AppConfig, entry: Record<string, unknown>): void {
  const file = config.target?.results ?? process.env['CRUX_E2E_RESULTS'];
  if (file) {
    appendFileSync(file, `${JSON.stringify({ suite: config.name, target: config.target?.kind ?? 'mock', ...entry })}\n`);
  }
}

export interface OpenOptions {
  trustedTypes?: boolean;
  /** Violations are expected, so they are counted and do not fail the test. */
  recordViolationsOnly?: boolean;
  /** The test sends an answer outside the specification on purpose. */
  allowNonConforming?: boolean;
}

export interface OpenApp {
  page: Page;
  context: BrowserContext;
  /** The mock. On native_sim any use of it throws, so a case that needs it and is not marked fails. */
  mock: MockHandle;
  target: TargetKind;
  fixture: Fixture;
  /** The requests the server saw: the mock's own record, or on native_sim the browser's same-origin requests. */
  requests: RecordedRequest[];
  /** Ends every session: the mock's control, or on native_sim the real session clock's idle limit. */
  expireSessions(): Promise<void>;
  /**
   * Sends an intercepted request on to the server and returns its answer, for a case that changes an answer.
   * Playwright's own route.fetch() runs outside Chrome, so on native_sim it would meet the test certificate
   * untrusted; there the request goes through native-raw.ts, which trusts that certificate alone.
   */
  fetchThrough(route: Route): Promise<{ status: number; headers: Record<string, string>; body: string }>;
  device?: Device;
  violations: string[];
  pageErrors: string[];
  consoleErrors: string[];
  foreignRequests: string[];
  /** Waits for the checks of answers still being read. */
  settle(): Promise<void>;
  finish(): void;
  close(): Promise<void>;
}

export async function launchBrowser(args: string[] = []): Promise<Browser> {
  const executablePath = process.env['CRUX_CHROME'];
  const options: LaunchOptions = { headless: true, args };
  return executablePath
    ? chromium.launch({ ...options, executablePath })
    : chromium.launch({ ...options, channel: 'chrome' });
}

/** Chrome's arguments for a target: on native_sim, accept the test certificate's key and no other. */
export function browserArgs(config: AppConfig): string[] {
  return config.target ? [`--ignore-certificate-errors-spki-list=${config.target.spki}`] : [];
}

/**
 * A browser that starts with the first test that needs it. Nothing launches when a name pattern matches no test
 * of a file, so no hook is left waiting to close a browser that would idle the process.
 */
export function lazyBrowser(args: string[] = []): { get(): Promise<Browser>; close(): Promise<void> } {
  let started: Promise<Browser> | undefined;
  return {
    // A browser that has gone away, as one killed for memory in a small container, is started again for the
    // next case. The case that met the loss has already failed.
    async get() {
      if (started !== undefined && !(await started).isConnected()) {
        started = undefined;
      }
      return (started ??= launchBrowser(args));
    },
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

/** The operation a real answer belongs to, as the mock matches it. */
function operationOf(spec: Spec, method: string, path: string): string | null {
  for (const operation of spec.operations.values()) {
    if (operation.method.toUpperCase() === method.toUpperCase() && operation.pattern.test(path)) {
      return operation.id;
    }
  }
  return null;
}

const THROWING_MOCK = new Proxy({} as MockHandle, {
  get(_target, property) {
    throw new Error(`the mock's ${String(property)} is not available on native_sim: mark the case with its need`);
  },
});

async function openDevice(browser: Browser, config: AppConfig, target: NativeTarget, options: OpenOptions): Promise<OpenApp> {
  const device = await target.devices.start();
  const problems: string[] = [];
  const checks: Promise<void>[] = [];
  const context = await browser.newContext({ baseURL: device.url, viewport: { width: 1000, height: 800 } });
  const page = await context.newPage();
  const violations: string[] = [];
  const pageErrors: string[] = [];
  const consoleErrors: string[] = [];
  const foreignRequests: string[] = [];
  const requests: RecordedRequest[] = [];
  const unread: string[] = [];
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
      consoleErrors.push(text);
    }
  });
  page.on('pageerror', (error) => pageErrors.push(String(error)));
  page.on('request', (request) => {
    const url = request.url();
    if (url.startsWith(`${device.url}/`)) {
      requests.push({ method: request.method(), path: new URL(url).pathname, headers: request.headers() });
    } else if (!url.startsWith('data:') && !url.startsWith('about:')) {
      foreignRequests.push(url);
    }
  });
  // Every real API answer is checked against the specification, as the mock's are. An answer that a case
  // made up with page.route has no server address and is left out.
  page.on('response', (response) => {
    const url = new URL(response.url());
    if (!response.url().startsWith(`${device.url}/`) || !url.pathname.startsWith('/api/')) {
      return;
    }
    checks.push(
      (async () => {
        const address = await Promise.race([
          response.serverAddr(),
          new Promise<'unknown'>((resolve) => setTimeout(() => resolve('unknown'), 5000)),
        ]);
        if (address === null) {
          return;
        }
        const headers: Record<string, string> = {};
        const setCookie: string[] = [];
        for (const { name, value } of await response.headersArray()) {
          if (name.toLowerCase() === 'set-cookie') {
            setCookie.push(value);
          } else {
            headers[name.toLowerCase()] = value;
          }
        }
        const method = response.request().method();
        // A body that Chrome no longer holds, as for a connection the server closed after its answer, never
        // resolves: such an answer is left unchecked after a while, and listed.
        const bodyText = await Promise.race([
          response.text().catch(() => ''),
          new Promise<undefined>((resolve) => setTimeout(() => resolve(undefined), 5000)),
        ]);
        if (bodyText === undefined) {
          unread.push(`${method} ${url.pathname} ${response.status()}`);
          return;
        }
        const record: ResponseRecord = {
          path: url.pathname,
          method,
          operationId: operationOf(spec, method, url.pathname),
          status: response.status(),
          contentType: headers['content-type'] ?? null,
          headers,
          setCookie,
          bodyText,
        };
        for (const problem of checkResponse(spec, record)) {
          problems.push(`${method} ${url.pathname} ${record.status}: ${problem}`);
        }
      })().catch(() => undefined),
    );
  });
  return {
    page,
    context,
    mock: THROWING_MOCK,
    target: 'native_sim',
    fixture: target.fixture,
    requests,
    device,
    async fetchThrough(route) {
      const sent = route.request();
      const headers = await sent.allHeaders();
      const forwarded: Record<string, string> = {};
      for (const name of ['cookie', 'origin', 'x-xsrf-token', 'x-crux-request']) {
        if (headers[name] !== undefined) {
          forwarded[name] = headers[name];
        }
      }
      const reply = await rawRequest(sent.method(), new URL(sent.url()).pathname, forwarded, sent.postData() ?? undefined);
      return { status: reply.status, headers: reply.headers, body: reply.body };
    },
    async expireSessions() {
      // The real clock: GET does not refresh the idle timer, so a session left this long has ended.
      await page.waitForTimeout((target.idleSeconds + 2) * 1000);
    },
    violations,
    pageErrors,
    consoleErrors,
    foreignRequests,
    async settle() {
      await Promise.all(checks);
      if (unread.length > 0) {
        console.log(`answers left unchecked, whose body Chrome no longer held: ${unread.join(', ')}`);
      }
    },
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
      await Promise.all(checks);
      await context.close();
      await device.close();
    },
  };
}

export async function openApp(browser: Browser, config: AppConfig, options: OpenOptions = {}): Promise<OpenApp> {
  if (config.target) {
    return openDevice(browser, config, config.target, options);
  }
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
      consoleErrors.push(text);
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
    target: 'mock',
    fixture: MOCK_FIXTURE,
    requests: mock.control.requests,
    async expireSessions() {
      mock.control.expireSessions();
    },
    async fetchThrough(route) {
      const response = await route.fetch();
      return { status: response.status(), headers: response.headers(), body: await response.text() };
    },
    violations,
    pageErrors,
    consoleErrors,
    foreignRequests,
    async settle() {},
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
    await app.settle();
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
