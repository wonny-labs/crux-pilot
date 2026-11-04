// Check one HTTP answer against the specification. It returns a list of problems, empty when the
// answer conforms. The firmware adapter can be checked against the same function in M11.
import { Ajv } from 'ajv';
import type { ValidateFunction } from 'ajv';
import type { Spec, SpecResponse } from './spec.ts';

export interface ResponseRecord {
  path: string;
  method: string;
  /** The matched operation, or null for an answer outside any operation. */
  operationId: string | null;
  status: number;
  contentType: string | null;
  /** Lower-case names, leaving out Set-Cookie. */
  headers: Record<string, string>;
  setCookie: string[];
  bodyText: string;
}

const COMMON_HEADERS = ['content-security-policy', 'x-content-type-options', 'referrer-policy', 'cache-control'];

const ajv = new Ajv({ strict: false, allErrors: true });
ajv.addFormat('ipv4', /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/);
ajv.addFormat('password', true);

const compiled = new WeakMap<object, ValidateFunction>();

function validatorFor(spec: Spec, schema: Record<string, unknown>): ValidateFunction {
  let validate = compiled.get(schema);
  if (!validate) {
    // Component references use #/components/..., so the schema carries the components with it.
    validate = ajv.compile({ ...schema, components: spec.components });
    compiled.set(schema, validate);
  }
  return validate;
}

function checkCommonHeaders(record: ResponseRecord, problems: string[]): void {
  for (const header of COMMON_HEADERS) {
    if (record.headers[header] === undefined) {
      problems.push(`missing header ${header}`);
    }
  }
}

interface ParsedCookie {
  name: string;
  value: string;
  attributes: Map<string, string>;
}

function parseCookie(line: string): ParsedCookie {
  const [first = '', ...rest] = line.split(';').map((part) => part.trim());
  const equals = first.indexOf('=');
  const attributes = new Map<string, string>();
  for (const part of rest) {
    const at = part.indexOf('=');
    const name = (at < 0 ? part : part.slice(0, at)).toLowerCase();
    attributes.set(name, at < 0 ? '' : part.slice(at + 1));
  }
  return { name: first.slice(0, equals), value: first.slice(equals + 1), attributes };
}

/** Both cookies once each, with the attributes that the specification requires of each. */
function checkCookieAttributes(cookies: ParsedCookie[], problems: string[]): void {
  const names = cookies.map((cookie) => cookie.name).sort();
  if (names.join() !== 'XSRF-TOKEN,crux_session') {
    problems.push(`expected the crux_session and XSRF-TOKEN cookies, found ${names.join(', ') || 'none'}`);
  }
  for (const cookie of cookies) {
    if (!cookie.attributes.has('secure')) {
      problems.push(`${cookie.name} is not Secure`);
    }
    if (cookie.attributes.get('samesite') !== 'Strict') {
      problems.push(`${cookie.name} is not SameSite=Strict`);
    }
    if (cookie.attributes.get('path') !== '/') {
      problems.push(`${cookie.name} is not on Path=/`);
    }
    const httpOnly = cookie.attributes.has('httponly');
    if (cookie.name === 'crux_session' && !httpOnly) {
      problems.push('crux_session is not HttpOnly');
    }
    if (cookie.name === 'XSRF-TOKEN' && httpOnly) {
      problems.push('XSRF-TOKEN must be readable by page scripts, so it cannot be HttpOnly');
    }
  }
}

/** A login sets both cookies, with no persistent expiry and with values that a client can use. */
function checkLoginCookies(cookies: ParsedCookie[], problems: string[]): void {
  checkCookieAttributes(cookies, problems);
  for (const cookie of cookies) {
    if (cookie.value === '') {
      problems.push(`${cookie.name} is empty`);
    }
    if (cookie.attributes.has('max-age') || cookie.attributes.has('expires')) {
      problems.push(`${cookie.name} has a persistent expiry`);
    }
  }
  const session = cookies.find((cookie) => cookie.name === 'crux_session')?.value;
  const token = cookies.find((cookie) => cookie.name === 'XSRF-TOKEN')?.value;
  if (token !== undefined) {
    if (token === session) {
      problems.push('the XSRF-TOKEN value must be independent of the session token');
    }
    // Angular decodes the cookie before it sends the header, while the Crux client sends it raw (#186).
    if (token.includes('%') || /^".*"$/.test(token)) {
      problems.push('the XSRF-TOKEN value holds a percent sign or surrounding quotes');
    }
  }
}

/** A logout clears both cookies. */
function checkClearingCookies(cookies: ParsedCookie[], problems: string[]): void {
  checkCookieAttributes(cookies, problems);
  for (const cookie of cookies) {
    if (cookie.value !== '') {
      problems.push(`${cookie.name} is not cleared`);
    }
    if (cookie.attributes.get('max-age') !== '0') {
      problems.push(`${cookie.name} is not expired with Max-Age=0`);
    }
  }
}

function checkCookies(record: ResponseRecord, declared: SpecResponse | undefined, problems: string[]): void {
  const cookies = record.setCookie.map(parseCookie);
  if (!declared?.declaresCookies) {
    if (cookies.length > 0) {
      problems.push('this response declares no Set-Cookie');
    }
    return;
  }
  if (declared.setCookie) {
    checkClearingCookies(cookies, problems);
    return;
  }
  if (cookies.length === 0) {
    // A successful password change sets none, and neither does any other state of the poll.
    return;
  }
  let state: unknown;
  try {
    state = (JSON.parse(record.bodyText) as { state?: unknown }).state;
  } catch {
    state = undefined;
  }
  if (state !== 'success') {
    problems.push(`a poll in state ${String(state)} must set no cookie`);
    return;
  }
  checkLoginCookies(cookies, problems);
}

function checkOutsideOperation(record: ResponseRecord, problems: string[]): void {
  if (record.contentType?.includes('json')) {
    // The JSON 404 for a path under a registered route.
    if (record.status !== 404 || record.bodyText !== '{"code":"not_found"}') {
      problems.push(`unexpected JSON answer outside the operations: ${record.bodyText}`);
    }
    checkCommonHeaders(record, problems);
    return;
  }
  const plain: Record<number, string> = { 404: 'Not Found', 405: 'Method Not Allowed' };
  if (plain[record.status] !== record.bodyText) {
    problems.push(`not one of the server's plain answers: ${record.status} ${record.bodyText}`);
  }
}

export function checkResponse(spec: Spec, record: ResponseRecord): string[] {
  const problems: string[] = [];
  if (record.operationId === null) {
    checkOutsideOperation(record, problems);
    checkCookies(record, undefined, problems);
    return problems;
  }
  const operation = spec.operations.get(record.operationId);
  if (!operation) {
    return [`unknown operation ${record.operationId}`];
  }
  const response = operation.responses.get(record.status);
  if (!response) {
    return [`status ${record.status} is not declared`];
  }
  checkCookies(record, response, problems);
  if (record.status === 409) {
    // The server's own answer: no body and none of the API's headers.
    if (record.bodyText !== '') {
      problems.push('a 409 must have no body');
    }
    return problems;
  }
  checkCommonHeaders(record, problems);
  for (const header of response.requiredHeaders) {
    const value = record.headers[header];
    if (value === undefined) {
      problems.push(`missing required header ${header}`);
    } else if (header === 'retry-after' && !/^[1-9][0-9]*$/.test(value)) {
      problems.push(`Retry-After is not whole seconds: ${value}`);
    }
  }
  if (!response.schema) {
    if (record.bodyText !== '') {
      problems.push('this response has no body in the specification');
    }
    return problems;
  }
  if (!record.contentType?.includes('application/json')) {
    problems.push(`content type is ${record.contentType ?? 'absent'}, not JSON`);
    return problems;
  }
  let body: unknown;
  try {
    body = JSON.parse(record.bodyText);
  } catch {
    problems.push('the body is not JSON');
    return problems;
  }
  const validate = validatorFor(spec, response.schema);
  if (!validate(body)) {
    for (const error of validate.errors ?? []) {
      problems.push(`${error.instancePath || '/'} ${error.message ?? 'is invalid'}`);
    }
  }
  return problems;
}
