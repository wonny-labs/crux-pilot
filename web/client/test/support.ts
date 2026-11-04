// Test support: a scripted fetch and a clock that advances only when the client sleeps.
import { createClient } from '../src/index.ts';
import type { ClientOptions, CruxClient } from '../src/index.ts';

export interface Recorded {
  method: string;
  url: string;
  headers: Headers;
  body: string | null;
  init: RequestInit;
}

type Step = Response | Error | ((request: Recorded) => Response | Error);

export function json(status: number, body: unknown, headers: Record<string, string> = {}): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json', ...headers },
  });
}

export function plain(status: number, text: string): Response {
  return new Response(text, { status, headers: { 'Content-Type': 'text/plain' } });
}

export function empty(status: number, headers: Record<string, string> = {}): Response {
  return new Response(null, { status, headers });
}

export interface Harness {
  client: CruxClient;
  requests: Recorded[];
  sleeps: number[];
  clock: { now: number };
  sessionLost: { count: number };
}

export function harness(
  script: Step[] | ((request: Recorded, index: number) => Step),
  options: Partial<ClientOptions> & { cookies?: string } = {},
): Harness {
  const requests: Recorded[] = [];
  const sleeps: number[] = [];
  const clock = { now: 1_000_000 };
  const sessionLost = { count: 0 };
  const steps = typeof script === 'function' ? null : [...script];
  const fetchImpl = async (input: RequestInfo | URL, init: RequestInit = {}): Promise<Response> => {
    const request: Recorded = {
      method: init.method ?? 'GET',
      url: String(input),
      headers: new Headers(init.headers),
      body: typeof init.body === 'string' ? init.body : null,
      init,
    };
    const index = requests.length;
    requests.push(request);
    const step = steps ? steps.shift() : (script as (r: Recorded, i: number) => Step)(request, index);
    if (step === undefined) {
      throw new Error(`no scripted response for request ${index}: ${request.method} ${request.url}`);
    }
    const result = typeof step === 'function' ? step(request) : step;
    if (result instanceof Error) {
      throw result;
    }
    return result;
  };
  const { cookies, ...rest } = options;
  const client = createClient({
    fetch: fetchImpl as typeof fetch,
    readCookies: () => cookies ?? '',
    now: () => clock.now,
    random: () => 0,
    sleep: async (ms: number, signal?: AbortSignal) => {
      sleeps.push(ms);
      clock.now += ms;
      if (signal?.aborted) {
        throw new DOMException('aborted', 'AbortError');
      }
    },
    onSessionLost: () => {
      sessionLost.count += 1;
    },
    ...rest,
  });
  return { client, requests, sleeps, clock, sessionLost };
}
