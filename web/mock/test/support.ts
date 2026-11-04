// Test support: a small cookie-keeping HTTP helper for the mock server.
import { startMock } from '../server.ts';
import type { MockHandle, MockOptions } from '../server.ts';
import { checkResponse } from '../conformance.ts';
import { loadSpec } from '../spec.ts';

export const spec = loadSpec();

export interface Reply {
  status: number;
  headers: Headers;
  text: string;
  body: unknown;
  setCookie: string[];
}

export interface Caller {
  mock: MockHandle;
  jar: Map<string, string>;
  call(method: string, path: string, options?: CallOptions): Promise<Reply>;
  login(user?: string, password?: string): Promise<Reply>;
  start(user: string, password: string): Promise<string>;
}

export interface CallOptions {
  body?: unknown;
  rawBody?: string;
  headers?: Record<string, string>;
  /** Leave out the Origin, the CSRF token or the poll header that a browser page would send. */
  noOrigin?: boolean;
  noToken?: boolean;
  noPollHeader?: boolean;
}

/** Start a mock whose every API answer is checked against the specification. */
export async function caller(options: Partial<MockOptions> = {}): Promise<Caller> {
  const problems: string[] = [];
  const mock = await startMock({
    ...options,
    onApiResponse: (record) => {
      for (const problem of checkResponse(spec, record)) {
        problems.push(`${record.operationId ?? record.path} ${record.status}: ${problem}`);
      }
    },
  });
  mock.conformanceProblems = problems;
  const jar = new Map<string, string>();
  const self: Caller = {
    mock,
    jar,
    async call(method, path, callOptions = {}) {
      const headers: Record<string, string> = { ...callOptions.headers };
      if (jar.size > 0 && headers['Cookie'] === undefined) {
        headers['Cookie'] = [...jar].map(([name, value]) => `${name}=${value}`).join('; ');
      }
      if (method !== 'GET' && !callOptions.noOrigin) {
        headers['Origin'] = mock.url;
      }
      if (method !== 'GET' && !callOptions.noToken && jar.has('XSRF-TOKEN')) {
        headers['X-XSRF-TOKEN'] = jar.get('XSRF-TOKEN') as string;
      }
      if (path.includes('/session/attempt/') && !callOptions.noPollHeader && !headers['X-Crux-Request']) {
        headers['X-Crux-Request'] = '1';
      }
      const init: RequestInit = { method, headers };
      if (callOptions.body !== undefined) {
        headers['Content-Type'] = 'application/json';
        init.body = JSON.stringify(callOptions.body);
      } else if (callOptions.rawBody !== undefined) {
        headers['Content-Type'] = 'application/json';
        init.body = callOptions.rawBody;
      }
      const response = await fetch(mock.url + path, init);
      const text = await response.text();
      const setCookie = response.headers.getSetCookie();
      for (const line of setCookie) {
        const first = line.split(';')[0] as string;
        const equals = first.indexOf('=');
        const name = first.slice(0, equals);
        const value = first.slice(equals + 1);
        if (value === '' || /max-age=0/i.test(line)) {
          jar.delete(name);
        } else {
          jar.set(name, value);
        }
      }
      let body: unknown;
      if ((response.headers.get('Content-Type') ?? '').includes('json')) {
        body = JSON.parse(text);
      }
      return { status: response.status, headers: response.headers, text, body, setCookie };
    },
    async start(user, password) {
      const reply = await self.call('POST', '/api/v1/session', { body: { user, password } });
      return (reply.body as { id: string }).id;
    },
    async login(user = 'operator', password = 'example-password-not-real') {
      const id = await self.start(user, password);
      for (;;) {
        const reply = await self.call('GET', `/api/v1/session/attempt/${id}`);
        if ((reply.body as { state?: string }).state !== 'pending') {
          return reply;
        }
      }
    },
  };
  return self;
}
