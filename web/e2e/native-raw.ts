// A raw HTTPS client for the cases that a browser cannot set up by itself (Issue #220): a second client that
// holds a route, fills the login table, holds every client slot or stalls a handshake. It verifies the test
// certificate as its trust anchor, for the name localhost, over TLS 1.3 only. Nothing turns verification off.
import { readFileSync } from 'node:fs';
import { connect as connectTcp } from 'node:net';
import type { Socket } from 'node:net';
import { connect as connectTls } from 'node:tls';
import type { TLSSocket } from 'node:tls';
import { DEVICE_HOST, DEVICE_PORT, DEVICE_URL, TEST_CERTIFICATE } from './device.ts';

export interface RawResponse {
  status: number;
  headers: Record<string, string>;
  setCookie: string[];
  body: string;
}

export function openTls(): Promise<TLSSocket> {
  return new Promise((resolve, reject) => {
    const socket = connectTls({
      host: '127.0.0.1',
      port: DEVICE_PORT,
      servername: DEVICE_HOST,
      ca: readFileSync(TEST_CERTIFICATE),
      minVersion: 'TLSv1.3',
    });
    socket.once('secureConnect', () => resolve(socket));
    socket.once('error', reject);
  });
}

/** A TCP connection that sends nothing: a handshake that never starts. */
export function openSilent(): Promise<Socket> {
  return new Promise((resolve, reject) => {
    const socket = connectTcp({ host: '127.0.0.1', port: DEVICE_PORT });
    socket.once('connect', () => resolve(socket));
    socket.once('error', reject);
  });
}

function head(method: string, path: string, headers: Record<string, string>, length?: number): string {
  const lines = [`${method} ${path} HTTP/1.1`, `Host: ${DEVICE_HOST}:${DEVICE_PORT}`];
  for (const [name, value] of Object.entries(headers)) {
    lines.push(`${name}: ${value}`);
  }
  if (length !== undefined) {
    lines.push(`Content-Length: ${length}`);
  }
  return `${lines.join('\r\n')}\r\n\r\n`;
}

function parse(buffer: Buffer): RawResponse | undefined {
  const text = buffer.toString('latin1');
  const end = text.indexOf('\r\n\r\n');
  if (end < 0) {
    return undefined;
  }
  const [statusLine = '', ...lines] = text.slice(0, end).split('\r\n');
  const headers: Record<string, string> = {};
  const setCookie: string[] = [];
  for (const line of lines) {
    const at = line.indexOf(':');
    const name = line.slice(0, at).trim().toLowerCase();
    const value = line.slice(at + 1).trim();
    if (name === 'set-cookie') {
      setCookie.push(value);
    } else {
      headers[name] = value;
    }
  }
  let body = text.slice(end + 4);
  if (headers['transfer-encoding'] === 'chunked') {
    let decoded = '';
    let rest = body;
    for (;;) {
      const at = rest.indexOf('\r\n');
      if (at < 0) {
        return undefined;
      }
      const size = parseInt(rest.slice(0, at), 16);
      if (size === 0) {
        break;
      }
      if (rest.length < at + 2 + size) {
        return undefined;
      }
      decoded += rest.slice(at + 2, at + 2 + size);
      rest = rest.slice(at + 2 + size + 2);
    }
    body = decoded;
  } else if (headers['content-length'] !== undefined && body.length < Number(headers['content-length'])) {
    return undefined;
  }
  return { status: Number(statusLine.split(' ')[1]), headers, setCookie, body };
}

/** One request on a new connection, closed after its answer. */
export async function request(
  method: string,
  path: string,
  headers: Record<string, string> = {},
  body?: string,
): Promise<RawResponse> {
  const socket = await openTls();
  return new Promise((resolve, reject) => {
    let buffer = Buffer.alloc(0);
    const done = (): void => {
      const response = parse(buffer);
      socket.destroy();
      if (response) {
        resolve(response);
      } else {
        reject(new Error(`no whole answer: ${buffer.toString('latin1').slice(0, 200)}`));
      }
    };
    socket.on('data', (data: Buffer) => {
      buffer = Buffer.concat([buffer, data]);
      if (parse(buffer)) {
        done();
      }
    });
    socket.once('end', done);
    socket.once('error', reject);
    socket.setTimeout(30_000, () => reject(new Error('no answer in 30 s')));
    const json = body === undefined ? undefined : Buffer.from(body);
    const all = json ? { 'Content-Type': 'application/json', Connection: 'close', ...headers } : { Connection: 'close', ...headers };
    socket.write(head(method, path, all, json?.length));
    if (json) {
      socket.write(json);
    }
  });
}

export interface RawSession {
  cookie: string;
  xsrf: string;
}

function cookieValue(lines: string[], name: string): string | undefined {
  for (const line of lines) {
    const match = new RegExp(`^${name}=([^;]*)`).exec(line);
    if (match) {
      return match[1];
    }
  }
  return undefined;
}

/** The two-request login, as the client does it. */
export async function login(user: string, password: string): Promise<RawSession> {
  const queued = await request('POST', '/api/v1/session', { Origin: DEVICE_URL }, JSON.stringify({ user, password }));
  if (queued.status !== 202) {
    throw new Error(`login of ${user}: ${queued.status} ${queued.body}`);
  }
  const id = (JSON.parse(queued.body) as { id: string }).id;
  for (let tries = 0; tries < 200; ++tries) {
    const poll = await request('GET', `/api/v1/session/attempt/${id}`, { 'X-Crux-Request': '1' });
    const state = (JSON.parse(poll.body) as { state: string }).state;
    if (state === 'success') {
      const session = cookieValue(poll.setCookie, 'crux_session');
      const xsrf = cookieValue(poll.setCookie, 'XSRF-TOKEN');
      if (session === undefined || xsrf === undefined) {
        throw new Error('no cookies on success');
      }
      return { cookie: `crux_session=${session}; XSRF-TOKEN=${xsrf}`, xsrf };
    }
    if (state !== 'pending') {
      throw new Error(`login of ${user}: ${state}`);
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`login of ${user}: still pending`);
}

/**
 * A client that holds a configuration parameter's route: the head of a PUT and part of its body, then
 * silence. The server holds the resource for this client until the body ends or the connection closes,
 * and answers another client's request for it with its own plain 409.
 */
export async function holdRoute(session: RawSession, path: string): Promise<{ release(): void }> {
  const socket = await openTls();
  const body = '{"value":7}';
  socket.write(
    head(
      'PUT',
      path,
      { Cookie: session.cookie, Origin: DEVICE_URL, 'X-XSRF-TOKEN': session.xsrf, 'Content-Type': 'application/json' },
      body.length,
    ),
  );
  socket.write(body.slice(0, 4));
  socket.on('error', () => undefined);
  return { release: () => socket.destroy() };
}

/** A connection that sends one keep-alive request, reads its answer and then stays idle in its client slot. */
export async function holdSlot(path = '/favicon.svg'): Promise<{ socket: TLSSocket; status: number }> {
  const socket = await openTls();
  return new Promise((resolve, reject) => {
    let buffer = Buffer.alloc(0);
    socket.on('data', (data: Buffer) => {
      buffer = Buffer.concat([buffer, data]);
      const response = parse(buffer);
      if (response) {
        socket.removeAllListeners('data');
        socket.on('data', () => undefined);
        resolve({ socket, status: response.status });
      }
    });
    socket.on('error', reject);
    socket.write(head('GET', path, {}));
  });
}
