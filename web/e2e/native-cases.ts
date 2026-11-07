// Browser cases that only a real server can show (Issue #220): TLS, the policy and cache headers that the
// packager gave each asset, hash routes with no deep-link fallback, the load time against its declared budget,
// what a page sees when a stalled handshake or idle connections hold the server, and the real 409 and 503 that
// the mock can only force. They run on a native_sim image over HTTPS, and nothing here is device timing.
import assert from 'node:assert/strict';
import { after } from 'node:test';
import type { Browser, Response } from 'playwright-core';
import { browserArgs, lazyBrowser, PASSWORD, recordResult, signIn, waitForHeading } from './harness.ts';
import type { AppConfig } from './harness.ts';
import { DEVICE_URL } from './device.ts';
import { holdRoute, holdSlot, login, openSilent, request } from './native-raw.ts';
import { caseRunner } from './shared-cases.ts';

/** The two policies of docs/web-assets.md, and no other. */
export const ASSET_POLICY = {
  strict:
    "default-src 'self'; script-src 'self'; style-src 'self'; base-uri 'self'; form-action 'self'; frame-ancestors 'none'",
  inline:
    "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; base-uri 'self'; form-action 'self'; frame-ancestors 'none'",
} as const;

/** The packager's rule: the last `-` or `.` part before the extension is 8 to 32 word characters with a capital or digit. */
export function fingerprinted(path: string): boolean {
  const name = path.split('/').pop() ?? '';
  const stem = name.slice(0, name.lastIndexOf('.'));
  const part = stem.split(/[-.]/).pop() ?? '';
  return stem !== part && /^[A-Za-z0-9_]{8,32}$/.test(part) && /[A-Z0-9]/.test(part);
}

/**
 * Declared before the first measurement, from the server's own bounds:
 * - every asset arrives within one response deadline, CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS, 3,000 ms in this
 *   image, so none is cut;
 * - the page is usable, the sign-in form shown, within 10,000 ms of the navigation, the default
 *   HTTP_SERVER_CLIENT_INACTIVITY_TIMEOUT, after which a browser's waiting connection would be closed.
 */
export const LOAD_BUDGET = { assetMs: 3000, pageMs: 10_000 } as const;
/** TLS connect timeout of the image, NET_SOCKETS_TLS_CONNECT_TIMEOUT, and its inactivity timer. */
const HANDSHAKE_TIMEOUT_MS = 2000;
const INACTIVITY_MS = 3000;
/** Slack for the emulated container, as the HTTPS precedent declares: 1.5 s, and 2.5 s on Wolf for W1 (#280). */
function slackMs(): number {
  return process.env['CRUX_E2E_PROFILE'] === 'wolf' ? 2500 : 1500;
}

interface Load {
  pageMs: number;
  navigation: Record<string, number>;
  assets: { path: string; status: number; ms: number; newConnection: boolean }[];
}

/** A cold load in a fresh context, to the sign-in form, with the browser's own timing of each asset. */
async function coldLoad(browser: Browser, baseURL: string): Promise<Load> {
  const context = await browser.newContext({ baseURL });
  const page = await context.newPage();
  try {
    const started = Date.now();
    await page.goto('/');
    await waitForHeading(page, 'Sign in');
    const pageMs = Date.now() - started;
    const timing = await page.evaluate(() => {
      const navigation = performance.getEntriesByType('navigation')[0] as PerformanceNavigationTiming;
      return {
        navigation: {
          connect: navigation.connectEnd - navigation.connectStart,
          tls: navigation.secureConnectionStart > 0 ? navigation.connectEnd - navigation.secureConnectionStart : 0,
          firstByte: navigation.responseStart - navigation.requestStart,
          document: navigation.responseEnd - navigation.startTime,
          domContentLoaded: navigation.domContentLoadedEventEnd - navigation.startTime,
          load: navigation.loadEventEnd - navigation.startTime,
        },
        // The packaged files only. The page's own question whether a session exists, a 401 here, is an API call.
        assets: (performance.getEntriesByType('resource') as PerformanceResourceTiming[])
          .filter((entry) => !new URL(entry.name).pathname.startsWith('/api/'))
          .map((entry) => ({
            path: new URL(entry.name).pathname,
            status: entry.responseStatus,
            ms: entry.responseEnd - entry.startTime,
            newConnection: entry.connectEnd > entry.connectStart,
          })),
      };
    });
    return { pageMs, ...timing };
  } finally {
    await context.close();
  }
}

export function nativeCases(config: AppConfig, options: { advanced: boolean }): void {
  const target = config.target;
  if (!target) {
    throw new Error('native cases need a native_sim target');
  }
  const shared = lazyBrowser(browserArgs(config));
  after(() => shared.close());
  const run = caseRunner(config, shared);
  const record = (name: string, measured: Record<string, unknown>): void =>
    recordResult(config, { case: name, outcome: 'measured', measured });

  run('the page comes over TLS 1.3 with the declared certificate, and every asset carries its policy and cache header', async (app) => {
    const { page } = app;
    const responses: Response[] = [];
    page.on('response', (response) => responses.push(response));
    const document = await page.goto('/');
    await waitForHeading(page, 'Sign in');
    const security = await document?.securityDetails();
    assert.equal(security?.protocol, 'TLS 1.3');
    const assets = responses.filter((response) => !new URL(response.url()).pathname.startsWith('/api/'));
    assert.ok(assets.length >= 3, 'the index and its assets');
    const seen = [];
    for (const response of assets) {
      const path = new URL(response.url()).pathname;
      const headers = await response.allHeaders();
      assert.equal(response.status(), 200, path);
      assert.equal(headers['content-security-policy'], ASSET_POLICY[config.stylePolicy], `${path}: policy`);
      assert.equal(headers['x-content-type-options'], 'nosniff', `${path}: nosniff`);
      assert.equal(headers['referrer-policy'], 'no-referrer', `${path}: referrer`);
      const cache = fingerprinted(path) ? 'max-age=31536000, immutable' : 'no-cache';
      assert.equal(headers['cache-control'], cache, `${path}: cache`);
      seen.push({ path, cache: headers['cache-control'], encoding: headers['content-encoding'] ?? null });
    }
    assert.ok(
      seen.some((entry) => entry.cache === 'no-cache') && seen.some((entry) => entry.cache !== 'no-cache'),
      'both cache classes were met',
    );
    record('assets as the browser received them', { protocol: security?.protocol, subject: security?.subjectName, assets: seen });
  });

  run('a path the interface routes is not served: the route lives in the fragment', async ({ page }) => {
    for (const path of ['/config', '/home', '/network', '/signin', '/index.html/config']) {
      const response = await page.goto(path);
      assert.equal(response?.status(), 404, `${path} has no fallback to the index`);
      assert.equal((await response?.text())?.includes('<script'), false);
    }
    await page.goto('/#/config');
    await waitForHeading(page, 'Sign in');
  });

  run('a cold load fits its declared budget', async (app) => {
    const browser = await shared.get();
    const loads: Load[] = [];
    for (let index = 0; index < 3; ++index) {
      loads.push(await coldLoad(browser, app.device?.url ?? ''));
    }
    record('cold load', { budget: LOAD_BUDGET, loads });
    for (const load of loads) {
      assert.ok(load.pageMs <= LOAD_BUDGET.pageMs, `page usable in ${load.pageMs} ms`);
      for (const asset of load.assets) {
        assert.equal(asset.status, 200, asset.path);
        assert.ok(asset.ms <= LOAD_BUDGET.assetMs, `${asset.path} in ${asset.ms} ms`);
      }
    }
  });

  run('a stalled handshake delays a load by at most the TLS connect timeout', async (app) => {
    const browser = await shared.get();
    const before = await coldLoad(browser, app.device?.url ?? '');
    const silent = await openSilent();
    try {
      const during = await coldLoad(browser, app.device?.url ?? '');
      const bound = before.pageMs + HANDSHAKE_TIMEOUT_MS + slackMs();
      record('load beside a stalled handshake', { beforeMs: before.pageMs, duringMs: during.pageMs, boundMs: bound, assets: during.assets });
      assert.ok(during.pageMs <= bound, `${during.pageMs} ms against ${bound} ms`);
      assert.ok(during.assets.every((asset) => asset.status === 200));
    } finally {
      silent.destroy();
    }
  });

  run('with every client slot held by an idle connection, a load waits and is served', async (app) => {
    const browser = await shared.get();
    const before = await coldLoad(browser, app.device?.url ?? '');
    const slots = [await holdSlot(), await holdSlot(), await holdSlot()];
    try {
      assert.deepEqual(slots.map((slot) => slot.status), [200, 200, 200]);
      const during = await coldLoad(browser, app.device?.url ?? '');
      const bound = before.pageMs + INACTIVITY_MS + slackMs();
      record('load while three idle connections hold the slots', { beforeMs: before.pageMs, duringMs: during.pageMs, boundMs: bound, assets: during.assets });
      assert.ok(during.pageMs <= bound, `${during.pageMs} ms against ${bound} ms`);
      assert.ok(during.assets.every((asset) => asset.status === 200));
    } finally {
      for (const slot of slots) {
        slot.socket.destroy();
      }
    }
  });

  run('a route another client holds answers the real 409, and the example retries it', async ({ page }) => {
    const statuses: number[] = [];
    page.on('response', (response) => {
      if (response.request().method() === 'PUT') {
        statuses.push(response.status());
      }
    });
    await page.goto('/');
    await signIn(page);
    await waitForHeading(page, 'Home');
    await page.getByRole('link', { name: 'Configuration' }).click();
    await waitForHeading(page, 'Configuration');
    const block = page.getByRole('group', { name: 'report_interval', exact: true });
    await block.getByText('Current value: 5', { exact: true }).waitFor();
    const holder = await holdRoute(await login('writer', PASSWORD), '/api/v1/config/report_interval');
    try {
      await block.getByLabel('New value').fill('10');
      await block.getByRole('button', { name: 'Update' }).click();
      const deadline = Date.now() + 5000;
      while (!statuses.includes(409) && Date.now() < deadline) {
        await page.waitForTimeout(50);
      }
    } finally {
      holder.release();
    }
    await block.getByText('Current value: 10', { exact: true }).waitFor();
    record('held route', { putStatuses: statuses });
    assert.equal(statuses[0], 409, 'the first update met the real hold');
    assert.equal(statuses.at(-1), 200, 'a retry went through once the holder left');
  });

  run('a full login table answers the real 503 busy, which the example shows, and a later sign-in works', async ({ page }) => {
    const answers: { status: number; retryAfter: string | null }[] = [];
    page.on('response', (response) => {
      if (response.request().method() === 'POST' && new URL(response.url()).pathname === '/api/v1/session') {
        answers.push({ status: response.status(), retryAfter: response.headers()['retry-after'] ?? null });
      }
    });
    await page.goto('/');
    await waitForHeading(page, 'Sign in');
    // Four results that nobody collects hold every attempt slot until they expire, 20 s after they finish.
    const held: number[] = [];
    for (let index = 0; index < 4; ++index) {
      const reply = await request('POST', '/api/v1/session', { Origin: DEVICE_URL }, JSON.stringify({ user: 'viewer', password: PASSWORD }));
      held.push(reply.status);
      await page.waitForTimeout(800);
    }
    await signIn(page);
    await page.getByText(/busy.*try again in \d+ second/i).waitFor({ timeout: 20_000 });
    assert.ok(answers.some((answer) => answer.status === 503), 'the browser met the real 503');
    // The table frees when the held results expire. Until then each try meets 503 again, and the person
    // tries again once the wait the page shows has passed.
    const started = Date.now();
    let tries = 0;
    while (Date.now() - started < 45_000) {
      await page.waitForFunction(() => !document.body.textContent?.match(/try again in \d/i), undefined, { timeout: 15_000 });
      await page.getByRole('button', { name: 'Sign in' }).click();
      tries += 1;
      const home = page.getByRole('heading', { name: 'Home', level: 1 });
      const busy = page.getByText(/busy.*try again in \d+ second/i);
      await Promise.race([home.waitFor({ timeout: 20_000 }), busy.waitFor({ timeout: 20_000 })]).catch(() => undefined);
      if (await home.count()) {
        break;
      }
    }
    record('full login table', { heldStatuses: held, browserAnswers: answers, laterTries: tries, freedAfterMs: Date.now() - started });
    await waitForHeading(page, 'Home');
  });

  if (options.advanced) {
    run('network settings: the real service shows no running address and refuses a change it cannot save', async ({ page }) => {
      await page.goto('/#/network');
      await signIn(page);
      await waitForHeading(page, 'Network settings');
      await page.getByText(/no ipv4 address at the moment/i).waitFor();
      await page.getByLabel('IPv4 address').fill('192.0.2.20');
      await page.getByLabel('Prefix length').fill('24');
      await page.getByLabel('Gateway').fill('');
      await page.getByRole('button', { name: 'Review and save' }).click();
      await page.getByRole('button', { name: 'Save the settings' }).click();
      await page.getByRole('alert').getByText(/no configuration provider.*nothing was changed/i).waitFor();
    });
    run('reboot and a restarted session', async () => undefined, ['reboot-service']);
  }
}

