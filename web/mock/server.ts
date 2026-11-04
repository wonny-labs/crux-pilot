// A small mock of the Crux management API, with no framework. It serves the examples of
// web/api/openapi.yaml, keeps enough state to walk the screens through a login, a save and a restart,
// and can be told to give any failure that the specification declares. It listens on the loopback
// address only, over plain HTTP, so it shows nothing about TLS, connection limits or timing.
import { randomBytes } from 'node:crypto';
import { readFile, stat } from 'node:fs/promises';
import { createServer } from 'node:http';
import type { IncomingMessage, Server, ServerResponse } from 'node:http';
import type { AddressInfo } from 'node:net';
import { extname, join, normalize, sep } from 'node:path';
import { checkIpv4Settings, parseIpv4 } from '../client/src/ipv4.ts';
import type { ResponseRecord } from './conformance.ts';
import { loadSpec } from './spec.ts';
import type { SpecExample, SpecOperation, SpecResponse } from './spec.ts';

export type StylePolicy = 'strict' | 'inline';

export interface ForcedResponse {
  status: number;
  /** A named example of that response. The first is used when none is named. */
  example?: string;
  retryAfter?: number;
  /** The account behind a session that a forced example starts. */
  account?: string;
}

export type RebootOutcome = 'restart' | 'never' | 'fail' | 'away_forever' | { refuseLate: string };

export interface MockStatusItem {
  item: Record<string, unknown>;
  permission: string;
}

export interface MockConfig {
  /** Polls answered pending before an attempt ends. */
  verifyPolls: number;
  /** New attempts answer 429 on the poll, with this Retry-After. */
  delaySeconds: number | null;
  /** POST /api/v1/session answers 503 busy, with this Retry-After. */
  loginBusySeconds: number | null;
  /** New attempts answer 503 busy on the poll, with this Retry-After. */
  attemptBusySeconds: number | null;
  savePolls: number;
  saveResult: 'ok' | 'fail';
  /** False is a device with no Configuration provider. */
  networkProvider: boolean;
  rebootPolicy: 'allow' | { refuse: string };
  rebootOutcome: RebootOutcome;
  rebootPendingPolls: number;
  rebootAwayPolls: number;
  statusItems: MockStatusItem[];
}

export interface RecordedRequest {
  method: string;
  path: string;
  headers: Record<string, string>;
}

export interface MockControl {
  reset(): void;
  force(operationId: string, ...responses: ForcedResponse[]): void;
  set(config: Partial<MockConfig>): void;
  expireSessions(): void;
  requests: RecordedRequest[];
}

export interface MockOptions {
  staticDir?: string;
  stylePolicy?: StylePolicy;
  trustedTypes?: boolean;
  /** Offer POST /__mock/control for a person driving the mock by hand. */
  httpControl?: boolean;
  port?: number;
  onApiResponse?: (record: ResponseRecord) => void;
}

export interface MockHandle {
  url: string;
  control: MockControl;
  conformanceProblems: string[];
  close(): Promise<void>;
}

interface Account {
  password: string;
  role: string;
  permissions: string[];
  mustChange?: boolean;
  disabled?: boolean;
}

interface Session {
  xsrf: string;
  accountName: string;
}

interface Attempt {
  run: () => SpecAnswer;
  pollsLeft: number;
  delaySeconds: number | null;
  busySeconds: number | null;
}

interface SpecAnswer {
  example: string;
}

const EXAMPLE_PASSWORD = 'example-password-not-real';
const ALL_PERMISSIONS = ['status.read', 'network.read', 'network.write', 'system.reboot', 'app.cycle.read'];
const BODY_LIMIT = 4096;

function accounts(): Map<string, Account> {
  return new Map<string, Account>([
    ['operator', { password: EXAMPLE_PASSWORD, role: 'administrator', permissions: ALL_PERMISSIONS }],
    ['viewer', { password: EXAMPLE_PASSWORD, role: 'viewer', permissions: ['status.read', 'network.read'] }],
    ['writer', { password: EXAMPLE_PASSWORD, role: 'installer', permissions: ['network.write'] }],
    ['guest', { password: EXAMPLE_PASSWORD, role: 'guest', permissions: [] }],
    ['newuser', { password: EXAMPLE_PASSWORD, role: 'administrator', permissions: ALL_PERMISSIONS, mustChange: true }],
    ['disabled', { password: EXAMPLE_PASSWORD, role: 'guest', permissions: [], disabled: true }],
  ]);
}

function defaultStatusItems(): MockStatusItem[] {
  const builtIn = (item: Record<string, unknown>): MockStatusItem => ({ item, permission: 'status.read' });
  return [
    builtIn({ name: 'version.platform', status: 'ok', type: 'text', value: '0.1.0-mock' }),
    builtIn({ name: 'version.project', status: 'ok', type: 'text', value: 'reference-mock' }),
    builtIn({ name: 'net.link', status: 'ok', type: 'bool', value: true }),
    builtIn({ name: 'net.admin_up', status: 'ok', type: 'bool', value: true }),
    builtIn({ name: 'net.ipv4', status: 'ok', type: 'text', value: '192.0.2.10' }),
    builtIn({ name: 'reset.causes', status: 'ok', type: 'text', value: 'power_on' }),
    builtIn({ name: 'reset.supported', status: 'ok', type: 'bool', value: true }),
    builtIn({ name: 'system.uptime_ms', status: 'ok', type: 'u64', value: '93784000' }),
    builtIn({ name: 'system.clock', status: 'ok', type: 'text', value: 'unset' }),
    builtIn({ name: 'system.watchdog_state', status: 'busy' }),
    { item: { name: 'app.cycle_count', status: 'ok', type: 'u32', value: 42 }, permission: 'app.cycle.read' },
  ];
}

function defaultConfig(): MockConfig {
  return {
    verifyPolls: 1,
    delaySeconds: null,
    loginBusySeconds: null,
    attemptBusySeconds: null,
    savePolls: 1,
    saveResult: 'ok',
    networkProvider: true,
    rebootPolicy: 'allow',
    rebootOutcome: 'restart',
    rebootPendingPolls: 1,
    rebootAwayPolls: 1,
    statusItems: defaultStatusItems(),
  };
}

const CONTENT_TYPES: Record<string, string> = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.json': 'application/json',
  '.txt': 'text/plain; charset=utf-8',
};

function cookieOf(lines: string[], name: string): string | undefined {
  for (const line of lines) {
    const first = line.split(';')[0] ?? '';
    const equals = first.indexOf('=');
    if (first.slice(0, equals) === name) {
      return first.slice(equals + 1);
    }
  }
  return undefined;
}

function requestCookie(request: IncomingMessage, name: string): string | undefined {
  for (const part of (request.headers.cookie ?? '').split(';')) {
    const trimmed = part.trim();
    const equals = trimmed.indexOf('=');
    if (equals > 0 && trimmed.slice(0, equals) === name) {
      return trimmed.slice(equals + 1);
    }
  }
  return undefined;
}

class PayloadTooLarge extends Error {}

async function readBody(request: IncomingMessage): Promise<string> {
  const chunks: Buffer[] = [];
  let length = 0;
  for await (const chunk of request) {
    length += (chunk as Buffer).length;
    if (length <= BODY_LIMIT) {
      chunks.push(chunk as Buffer);
    }
  }
  if (length > BODY_LIMIT) {
    throw new PayloadTooLarge();
  }
  return Buffer.concat(chunks).toString('utf8');
}

function parseJsonObject(text: string): Record<string, unknown> | null {
  try {
    const value: unknown = JSON.parse(text);
    return typeof value === 'object' && value !== null && !Array.isArray(value)
      ? (value as Record<string, unknown>)
      : null;
  } catch {
    return null;
  }
}

function hasOnly(body: Record<string, unknown>, allowed: string[]): boolean {
  return Object.keys(body).every((key) => allowed.includes(key));
}

export async function startMock(options: MockOptions = {}): Promise<MockHandle> {
  const spec = loadSpec();
  const operationList = [...spec.operations.values()];

  let config = defaultConfig();
  let accountTable = accounts();
  const sessions = new Map<string, Session>();
  const attempts = new Map<string, Attempt>();
  const forced = new Map<string, ForcedResponse[]>();
  const requests: RecordedRequest[] = [];
  let network = {
    running: { address: '192.0.2.10', prefix_length: 24 } as Record<string, unknown>,
    saved: undefined as Record<string, unknown> | undefined,
    source: 'default' as 'default' | 'saved',
    saving: null as null | { pollsLeft: number; values: Record<string, unknown>; result: 'ok' | 'fail' },
    failed: false,
  };
  let reboot = {
    state: 'none' as 'none' | 'pending' | 'failed' | 'refused',
    reason: '',
    pendingLeft: 0,
    away: false,
    awayForever: false,
    awayLeft: 0,
  };

  function resetState(): void {
    config = defaultConfig();
    accountTable = accounts();
    sessions.clear();
    attempts.clear();
    forced.clear();
    requests.length = 0;
    network = {
      running: { address: '192.0.2.10', prefix_length: 24 },
      saved: undefined,
      source: 'default',
      saving: null,
      failed: false,
    };
    reboot = { state: 'none', reason: '', pendingLeft: 0, away: false, awayForever: false, awayLeft: 0 };
  }

  function policy(): string {
    const parts = ["default-src 'self'"];
    if (options.stylePolicy === 'inline') {
      parts.push("style-src 'self' 'unsafe-inline'");
    }
    parts.push("frame-ancestors 'none'");
    if (options.trustedTypes) {
      parts.push("require-trusted-types-for 'script'");
    }
    return parts.join('; ');
  }

  function commonHeaders(): Record<string, string> {
    return {
      'Content-Security-Policy': policy(),
      'X-Content-Type-Options': 'nosniff',
      'Referrer-Policy': 'no-referrer',
      'Cache-Control': 'no-store',
    };
  }

  function finish(
    response: ServerResponse,
    request: IncomingMessage,
    operationId: string | null,
    status: number,
    body: string,
    setCookie: string[] = [],
  ): void {
    if (setCookie.length > 0) {
      response.setHeader('Set-Cookie', setCookie);
    }
    response.statusCode = status;
    // A dynamic response on the real server is always chunked, so this one is too.
    response.flushHeaders();
    response.end(body);
    const headers: Record<string, string> = {};
    for (const [name, value] of Object.entries(response.getHeaders())) {
      if (name !== 'set-cookie' && value !== undefined) {
        headers[name] = String(value);
      }
    }
    options.onApiResponse?.({
      path: (request.url ?? '').split('?')[0] ?? '',
      method: request.method ?? '',
      operationId,
      status,
      contentType: headers['content-type'] ?? null,
      headers,
      setCookie,
      bodyText: body,
    });
  }

  /** The server's own plain answers, which hold no JSON and none of the API's headers. */
  function plain(response: ServerResponse, request: IncomingMessage, status: 404 | 405): void {
    response.setHeader('Content-Type', 'text/plain');
    finish(response, request, null, status, status === 404 ? 'Not Found' : 'Method Not Allowed');
  }

  function busy409(response: ServerResponse, request: IncomingMessage, operation: SpecOperation): void {
    response.setHeader('Connection', 'close');
    finish(response, request, operation.id, 409, '');
  }

  function json(
    response: ServerResponse,
    request: IncomingMessage,
    operationId: string | null,
    status: number,
    body: unknown,
    extra: { retryAfter?: number; setCookie?: string[] } = {},
  ): void {
    for (const [name, value] of Object.entries(commonHeaders())) {
      response.setHeader(name, value);
    }
    response.setHeader('Content-Type', 'application/json');
    if (extra.retryAfter !== undefined) {
      response.setHeader('Retry-After', String(extra.retryAfter));
    }
    finish(response, request, operationId, status, JSON.stringify(body), extra.setCookie ?? []);
  }

  function error(
    response: ServerResponse,
    request: IncomingMessage,
    operation: SpecOperation,
    status: number,
    code: string,
    extra: { retryAfter?: number; fields?: unknown[]; reason?: string } = {},
  ): void {
    const body: Record<string, unknown> = { code };
    if (extra.fields) {
      body['fields'] = extra.fields;
    }
    if (extra.reason !== undefined) {
      body['reason'] = extra.reason;
    }
    json(response, request, operation.id, status, body, extra.retryAfter === undefined ? {} : { retryAfter: extra.retryAfter });
  }

  function startSessionFrom(lines: string[], accountName: string): void {
    const token = cookieOf(lines, 'crux_session');
    if (token) {
      sessions.set(token, { xsrf: cookieOf(lines, 'XSRF-TOKEN') ?? '', accountName });
    }
  }

  function serveSpecResponse(
    response: ServerResponse,
    request: IncomingMessage,
    operation: SpecOperation,
    declared: SpecResponse,
    example: SpecExample | undefined,
    retryAfter?: number,
    accountName = 'operator',
  ): void {
    if (declared.status === 409) {
      busy409(response, request, operation);
      return;
    }
    const lines = example?.setCookie ?? declared.setCookie ?? [];
    startSessionFrom(lines, accountName);
    const seconds = declared.requiredHeaders.includes('retry-after')
      ? (retryAfter ?? declared.retryAfterExample ?? 1)
      : undefined;
    if (example === undefined) {
      for (const [name, value] of Object.entries(commonHeaders())) {
        response.setHeader(name, value);
      }
      finish(response, request, operation.id, declared.status, '', lines);
      return;
    }
    const extra: { retryAfter?: number; setCookie?: string[] } = { setCookie: lines };
    if (seconds !== undefined) {
      extra.retryAfter = seconds;
    }
    json(response, request, operation.id, declared.status, example.value, extra);
  }

  function serveExample(
    response: ServerResponse,
    request: IncomingMessage,
    operation: SpecOperation,
    status: number,
    name: string,
    retryAfter?: number,
    accountName?: string,
  ): void {
    const declared = operation.responses.get(status);
    const example = declared?.examples.get(name);
    if (!declared || !example) {
      throw new Error(`the specification has no example ${name} for ${operation.id} ${status}`);
    }
    serveSpecResponse(response, request, operation, declared, example, retryAfter, accountName);
  }

  function serveForced(
    response: ServerResponse,
    request: IncomingMessage,
    operation: SpecOperation,
    choice: ForcedResponse,
  ): void {
    const declared = operation.responses.get(choice.status);
    if (!declared) {
      throw new Error(`${operation.id} declares no ${choice.status}`);
    }
    const example = choice.example ? declared.examples.get(choice.example) : [...declared.examples.values()][0];
    if (choice.example && !example) {
      throw new Error(`the specification has no example ${choice.example} for ${operation.id} ${choice.status}`);
    }
    const token = requestCookie(request, 'crux_session');
    serveSpecResponse(response, request, operation, declared, example, choice.retryAfter, choice.account);
    if (operation.id === 'deleteSession' && choice.status === 200 && token) {
      sessions.delete(token);
    }
  }

  function currentSession(request: IncomingMessage): { token: string; session: Session; account: Account } | null {
    const token = requestCookie(request, 'crux_session');
    const session = token === undefined ? undefined : sessions.get(token);
    const account = session && accountTable.get(session.accountName);
    return token && session && account ? { token, session, account } : null;
  }

  function originOk(request: IncomingMessage): boolean {
    return request.headers.origin === `http://${request.headers.host ?? ''}`;
  }

  function may(account: Account, permission: string): boolean {
    return !account.mustChange && account.permissions.includes(permission);
  }

  function newAttempt(run: Attempt['run']): string {
    const id = randomBytes(16).toString('base64url');
    attempts.set(id, {
      run,
      pollsLeft: config.verifyPolls,
      delaySeconds: config.delaySeconds,
      busySeconds: config.attemptBusySeconds,
    });
    return id;
  }

  async function handleLogin(request: IncomingMessage, response: ServerResponse, operation: SpecOperation): Promise<void> {
    if (!originOk(request)) {
      error(response, request, operation, 403, 'origin_rejected');
      return;
    }
    let text: string;
    try {
      text = await readBody(request);
    } catch {
      error(response, request, operation, 413, 'payload_too_large');
      return;
    }
    const body = parseJsonObject(text);
    if (
      !body ||
      typeof body['user'] !== 'string' ||
      typeof body['password'] !== 'string' ||
      !hasOnly(body, ['user', 'password'])
    ) {
      error(response, request, operation, 400, 'bad_request');
      return;
    }
    if (config.loginBusySeconds !== null) {
      error(response, request, operation, 503, 'busy', { retryAfter: config.loginBusySeconds });
      return;
    }
    const user = body['user'];
    const password = body['password'];
    const id = newAttempt(() => {
      const account = accountTable.get(user);
      const ok = account !== undefined && !account.disabled && account.password === password;
      return { example: ok ? `login:${user}` : 'rejected' };
    });
    json(response, request, operation.id, 202, { id });
  }

  async function handlePassword(
    request: IncomingMessage,
    response: ServerResponse,
    operation: SpecOperation,
    token: string,
    account: Account,
  ): Promise<void> {
    let text: string;
    try {
      text = await readBody(request);
    } catch {
      error(response, request, operation, 413, 'payload_too_large');
      return;
    }
    const body = parseJsonObject(text);
    if (
      !body ||
      typeof body['current_password'] !== 'string' ||
      typeof body['new_password'] !== 'string' ||
      !hasOnly(body, ['current_password', 'new_password'])
    ) {
      error(response, request, operation, 400, 'bad_request');
      return;
    }
    const current = body['current_password'];
    const next = body['new_password'];
    const id = newAttempt(() => {
      if (!sessions.has(token)) {
        return { example: 'password_change_session_ended' };
      }
      if (account.password !== current) {
        return { example: 'wrong_current_password' };
      }
      const bytes = Buffer.byteLength(next, 'utf8');
      if (bytes < 12 || bytes > 128) {
        return { example: 'new_password_refused' };
      }
      account.password = next;
      delete account.mustChange;
      return { example: 'password_changed' };
    });
    json(response, request, operation.id, 202, { id });
  }

  function handlePoll(
    request: IncomingMessage,
    response: ServerResponse,
    operation: SpecOperation,
    id: string,
  ): void {
    const attempt = attempts.get(id);
    if (!attempt) {
      error(response, request, operation, 404, 'not_found');
      return;
    }
    if (attempt.delaySeconds !== null) {
      attempts.delete(id);
      error(response, request, operation, 429, 'retry_later', { retryAfter: attempt.delaySeconds });
      return;
    }
    if (attempt.busySeconds !== null) {
      attempts.delete(id);
      error(response, request, operation, 503, 'busy', { retryAfter: attempt.busySeconds });
      return;
    }
    if (attempt.pollsLeft > 0) {
      attempt.pollsLeft -= 1;
      serveExample(response, request, operation, 200, 'pending');
      return;
    }
    attempts.delete(id);
    const answer = attempt.run();
    if (answer.example.startsWith('login:')) {
      serveExample(response, request, operation, 200, 'success', undefined, answer.example.slice('login:'.length));
    } else {
      serveExample(response, request, operation, 200, answer.example);
    }
  }

  function networkState(): Record<string, unknown> {
    if (network.saving) {
      if (network.saving.pollsLeft > 0) {
        network.saving.pollsLeft -= 1;
      } else {
        if (network.saving.result === 'ok') {
          network.saved = network.saving.values;
          network.failed = false;
        } else {
          network.saved = undefined;
          network.failed = true;
        }
        network.saving = null;
      }
    }
    const state: Record<string, unknown> = {
      running: network.running,
      source: network.source,
      reboot_required:
        network.saved !== undefined && JSON.stringify(network.saved) !== JSON.stringify(network.running),
    };
    if (network.saved) {
      state['saved'] = network.saved;
    }
    if (network.saving) {
      state['save'] = { state: 'pending' };
    } else if (network.failed) {
      state['save'] = { state: 'failed' };
    }
    return state;
  }

  async function handlePutNetwork(request: IncomingMessage, response: ServerResponse, operation: SpecOperation): Promise<void> {
    let text: string;
    try {
      text = await readBody(request);
    } catch {
      error(response, request, operation, 413, 'payload_too_large');
      return;
    }
    if (!config.networkProvider) {
      error(response, request, operation, 501, 'not_supported');
      return;
    }
    const body = parseJsonObject(text);
    const address = body?.['address'];
    const prefix = body?.['prefix_length'];
    const gateway = body?.['gateway'];
    if (
      !body ||
      typeof address !== 'string' ||
      parseIpv4(address) === null ||
      typeof prefix !== 'number' ||
      !Number.isInteger(prefix) ||
      (gateway !== undefined && (typeof gateway !== 'string' || parseIpv4(gateway) === null)) ||
      !hasOnly(body, ['address', 'prefix_length', 'gateway'])
    ) {
      error(response, request, operation, 400, 'bad_request');
      return;
    }
    const { problems } = checkIpv4Settings({
      address,
      prefix_length: String(prefix),
      gateway: (gateway as string | undefined) ?? '',
    });
    if (problems.length > 0) {
      error(response, request, operation, 400, 'invalid_field', {
        fields: problems.map((problem) => ({ name: problem.name, code: problem.code })),
      });
      return;
    }
    if (network.saving) {
      error(response, request, operation, 503, 'busy', { retryAfter: 1 });
      return;
    }
    const values: Record<string, unknown> = { address, prefix_length: prefix };
    if (gateway !== undefined) {
      values['gateway'] = gateway;
    }
    network.saving = { pollsLeft: config.savePolls, values, result: config.saveResult };
    network.failed = false;
    json(response, request, operation.id, 202, { state: 'pending' });
  }

  function completeRestart(): void {
    reboot = { state: 'none', reason: '', pendingLeft: 0, away: false, awayForever: false, awayLeft: 0 };
    sessions.clear();
    attempts.clear();
    if (network.saved) {
      network.running = network.saved;
      network.source = 'saved';
    }
  }

  function dropConnection(request: IncomingMessage): void {
    request.socket.destroy();
  }

  function handleGetReboot(request: IncomingMessage, response: ServerResponse, operation: SpecOperation): void {
    if (reboot.state === 'pending') {
      if (reboot.pendingLeft > 0) {
        reboot.pendingLeft -= 1;
      } else {
        const outcome = config.rebootOutcome;
        if (outcome === 'fail') {
          reboot.state = 'failed';
        } else if (outcome === 'never') {
          // It stays pending.
        } else if (outcome === 'away_forever') {
          reboot.away = true;
          reboot.awayForever = true;
          dropConnection(request);
          return;
        } else if (outcome === 'restart') {
          reboot.away = true;
          reboot.awayLeft = config.rebootAwayPolls - 1;
          if (config.rebootAwayPolls > 0) {
            dropConnection(request);
            return;
          }
          completeRestart();
          error(response, request, operation, 401, 'unauthorized');
          return;
        } else {
          reboot.state = 'refused';
          reboot.reason = outcome.refuseLate;
        }
      }
    }
    const body: Record<string, unknown> = { state: reboot.state };
    if (reboot.state === 'refused') {
      body['reason'] = reboot.reason;
    }
    json(response, request, operation.id, 200, body);
  }

  function handlePostReboot(request: IncomingMessage, response: ServerResponse, operation: SpecOperation): void {
    if (reboot.state === 'pending') {
      json(response, request, operation.id, 202, { state: 'pending' });
      return;
    }
    if (config.rebootPolicy !== 'allow') {
      reboot.state = 'refused';
      reboot.reason = config.rebootPolicy.refuse;
      error(response, request, operation, 423, 'reboot_refused', { reason: config.rebootPolicy.refuse });
      return;
    }
    reboot.state = 'pending';
    reboot.pendingLeft = config.rebootPendingPolls;
    json(response, request, operation.id, 202, { state: 'pending' });
  }

  function statusItems(account: Account): unknown[] {
    if (account.mustChange) {
      return [];
    }
    return config.statusItems.filter((entry) => account.permissions.includes(entry.permission)).map((entry) => entry.item);
  }

  /** The operation's stateful behaviour. */
  async function handleOperation(
    request: IncomingMessage,
    response: ServerResponse,
    operation: SpecOperation,
    path: string,
  ): Promise<void> {
    if (operation.id === 'getSessionAttempt') {
      handlePoll(request, response, operation, path.slice(path.lastIndexOf('/') + 1));
      return;
    }
    if (operation.id === 'createSession') {
      await handleLogin(request, response, operation);
      return;
    }
    const current = currentSession(request);
    if (!current) {
      error(response, request, operation, 401, 'unauthorized');
      return;
    }
    const stateChanging = operation.method !== 'GET';
    if (stateChanging) {
      if (request.headers['x-xsrf-token'] !== current.session.xsrf) {
        error(response, request, operation, 403, 'csrf_rejected');
        return;
      }
      if (!originOk(request)) {
        error(response, request, operation, 403, 'origin_rejected');
        return;
      }
    }
    if (operation.access !== 'session' && !may(current.account, operation.access)) {
      error(response, request, operation, 403, 'permission_denied');
      return;
    }
    switch (operation.id) {
      case 'deleteSession': {
        sessions.delete(current.token);
        serveSpecResponse(response, request, operation, operation.responses.get(200) as SpecResponse, undefined);
        return;
      }
      case 'getCurrentUser':
        json(response, request, operation.id, 200, {
          user: current.session.accountName,
          role: current.account.role,
          must_change_password: current.account.mustChange === true,
        });
        return;
      case 'changeOwnPassword':
        await handlePassword(request, response, operation, current.token, current.account);
        return;
      case 'getStatus':
        json(response, request, operation.id, 200, { items: statusItems(current.account) });
        return;
      case 'getNetwork':
        json(response, request, operation.id, 200, networkState());
        return;
      case 'putNetwork':
        await handlePutNetwork(request, response, operation);
        return;
      case 'getRebootState':
        handleGetReboot(request, response, operation);
        return;
      case 'requestReboot':
        handlePostReboot(request, response, operation);
        return;
      default:
        throw new Error(`no behaviour for ${operation.id}`);
    }
  }

  async function handleApi(request: IncomingMessage, response: ServerResponse, path: string): Promise<void> {
    if (reboot.away) {
      if (reboot.awayForever || reboot.awayLeft > 0) {
        reboot.awayLeft -= 1;
        dropConnection(request);
        return;
      }
      completeRestart();
    }
    const candidates = operationList.filter((operation) => operation.pattern.test(path));
    if (candidates.length === 0) {
      if (operationList.some((operation) => path.startsWith(`${operation.path}/`))) {
        json(response, request, null, 404, { code: 'not_found' });
      } else {
        plain(response, request, 404);
      }
      return;
    }
    const operation = candidates.find((candidate) => candidate.method === request.method);
    if (!operation) {
      plain(response, request, 405);
      return;
    }
    if (operation.id === 'getSessionAttempt' && request.headers['x-crux-request'] !== '1') {
      error(response, request, operation, 403, 'csrf_rejected');
      return;
    }
    const queue = forced.get(operation.id);
    const choice = queue?.shift();
    if (choice) {
      // The body is not read for a forced answer, so the connection cannot be reused for it.
      request.resume();
      serveForced(response, request, operation, choice);
      return;
    }
    await handleOperation(request, response, operation, path);
  }

  async function handleControl(request: IncomingMessage, response: ServerResponse): Promise<void> {
    const body = parseJsonObject(await readBody(request)) ?? {};
    switch (body['action']) {
      case 'reset':
        resetState();
        break;
      case 'force':
        control.force(body['operation'] as string, ...(body['responses'] as ForcedResponse[]));
        break;
      case 'set':
        control.set(body['config'] as Partial<MockConfig>);
        break;
      case 'expire':
        control.expireSessions();
        break;
      default:
        response.statusCode = 400;
        response.end('unknown action');
        return;
    }
    response.statusCode = 200;
    response.end('ok');
  }

  async function handleStatic(request: IncomingMessage, response: ServerResponse, path: string): Promise<void> {
    if (!options.staticDir) {
      plain(response, request, 404);
      return;
    }
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      plain(response, request, 405);
      return;
    }
    let decoded: string;
    try {
      decoded = decodeURIComponent(path);
    } catch {
      plain(response, request, 404);
      return;
    }
    const relative = decoded === '/' ? 'index.html' : normalize(decoded).replace(/^[/\\]+/, '');
    if (relative.split(sep).includes('..') || decoded.includes('\0')) {
      plain(response, request, 404);
      return;
    }
    const file = join(options.staticDir, relative);
    try {
      if (!(await stat(file)).isFile()) {
        plain(response, request, 404);
        return;
      }
      const data = await readFile(file);
      for (const [name, value] of Object.entries(commonHeaders())) {
        response.setHeader(name, value);
      }
      response.setHeader('Content-Type', CONTENT_TYPES[extname(file)] ?? 'application/octet-stream');
      const hashed = relative.startsWith(`assets${sep}`) || /[.-][A-Za-z0-9_-]{8,}\.[a-z]+$/.test(relative);
      response.setHeader('Cache-Control', hashed ? 'public, max-age=31536000, immutable' : 'no-cache');
      response.end(request.method === 'HEAD' ? undefined : data);
    } catch {
      plain(response, request, 404);
    }
  }

  const server: Server = createServer((request, response) => {
    const path = (request.url ?? '/').split('?')[0] ?? '/';
    const headers: Record<string, string> = {};
    for (const [name, value] of Object.entries(request.headers)) {
      headers[name] = Array.isArray(value) ? value.join(', ') : (value ?? '');
    }
    const work = async (): Promise<void> => {
      if (path.startsWith('/api/')) {
        requests.push({ method: request.method ?? '', path, headers });
        await handleApi(request, response, path);
      } else if (options.httpControl && path === '/__mock/control' && request.method === 'POST') {
        await handleControl(request, response);
      } else {
        await handleStatic(request, response, path);
      }
    };
    work().catch((cause: unknown) => {
      process.stderr.write(`mock: ${String(cause)}\n`);
      if (!response.headersSent) {
        response.statusCode = 500;
      }
      response.end();
    });
  });

  const control: MockControl = {
    reset: resetState,
    force(operationId, ...responses) {
      if (!spec.operations.has(operationId)) {
        throw new Error(`unknown operation ${operationId}`);
      }
      forced.set(operationId, [...(forced.get(operationId) ?? []), ...responses]);
    },
    set(partial) {
      config = { ...config, ...partial };
    },
    expireSessions() {
      sessions.clear();
    },
    requests,
  };

  await new Promise<void>((resolve) => {
    server.listen(options.port ?? 0, '127.0.0.1', resolve);
  });
  const { port } = server.address() as AddressInfo;
  return {
    url: `http://127.0.0.1:${port}`,
    control,
    conformanceProblems: [],
    close: () =>
      new Promise<void>((resolve, reject) => {
        server.close((cause) => (cause ? reject(cause) : resolve()));
        server.closeAllConnections();
      }),
  };
}
