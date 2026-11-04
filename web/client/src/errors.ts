// Mapping from HTTP responses to the client's error form. See web/api/openapi.yaml, "Common forms"
// and "Responses from the HTTP server".
import type { ErrorCode, FieldError } from './api-types.generated.ts';

export type ApiErrorKind =
  | Exclude<ErrorCode, 'storage_failed'>
  // The server's plain 404, for a path with no registered resource.
  | 'no_route'
  // The server's plain 405, for a method that a registered path does not support.
  | 'method_not_allowed'
  // No HTTP answer at all.
  | 'network'
  | 'aborted'
  // An answer that the specification does not define.
  | 'unexpected';

export interface ApiError {
  kind: ApiErrorKind;
  status?: number;
  fields?: FieldError[];
  reason?: string;
  retryAfterSeconds?: number;
}

export type Result<T> = { ok: true; value: T } | { ok: false; error: ApiError };

const KNOWN_CODES: ReadonlySet<string> = new Set<Exclude<ErrorCode, 'storage_failed'>>([
  'bad_request',
  'invalid_field',
  'unauthorized',
  'permission_denied',
  'csrf_rejected',
  'origin_rejected',
  'not_found',
  'payload_too_large',
  'reboot_refused',
  'retry_later',
  'not_supported',
  'busy',
  'not_ready',
]);

function isJson(response: Response): boolean {
  return (response.headers.get('Content-Type') ?? '').toLowerCase().includes('json');
}

/** The parsed body of a JSON response, or undefined for any other body. Never throws. */
export async function readJson(response: Response): Promise<unknown> {
  if (!isJson(response)) {
    return undefined;
  }
  try {
    return JSON.parse(await response.text());
  } catch {
    return undefined;
  }
}

function retryAfter(response: Response, fallbackSeconds: number): number {
  const header = response.headers.get('Retry-After');
  if (header !== null && /^[1-9][0-9]*$/.test(header)) {
    return Number(header);
  }
  return fallbackSeconds;
}

/** Map a response that is not a success. A 409 has no body, and it always means busy. */
export async function errorFromResponse(response: Response, fallbackRetryAfterSeconds: number): Promise<ApiError> {
  const status = response.status;
  if (status === 409) {
    return { kind: 'busy', status };
  }
  const body = (await readJson(response)) as { code?: unknown; fields?: unknown; reason?: unknown } | undefined;
  const code = body?.code;
  const error: ApiError = { kind: 'unexpected', status };
  if (typeof code === 'string' && KNOWN_CODES.has(code)) {
    error.kind = code as ApiErrorKind;
    if (Array.isArray(body?.fields)) {
      error.fields = body.fields as FieldError[];
    }
    if (typeof body?.reason === 'string') {
      error.reason = body.reason;
    }
  } else if (status === 404) {
    error.kind = 'no_route';
  } else if (status === 405) {
    error.kind = 'method_not_allowed';
  }
  if (status === 429 || status === 503) {
    error.retryAfterSeconds = retryAfter(response, fallbackRetryAfterSeconds);
  }
  return error;
}
