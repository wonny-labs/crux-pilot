// A framework-free client for the Crux management API. It holds no secret: the session token is an
// HttpOnly cookie that it cannot read, and it reads the CSRF token from its cookie for each request.
import { operations } from './api-types.generated.ts';
import type {
  ConfigItem,
  ConfigList,
  ConfigSaveFailed,
  ConfigValue,
  CurrentUser,
  FieldError,
  Ipv4Settings,
  NetworkState,
  OperationId,
  RebootAccepted,
  RebootState,
  SaveQueued,
  StatusReport,
} from './api-types.generated.ts';
import { errorFromResponse, readJson } from './errors.ts';
import type { ApiError, Result } from './errors.ts';

export interface ClientTiming {
  /** Retries of a 409, which the server sends while another client holds the route. */
  busyRetries: number;
  busyBaseDelayMs: number;
  busyMaxDelayMs: number;
  /** Automatic retries of a 503 busy, only when its Retry-After is within the wait below. */
  unavailableRetries: number;
  unavailableMaxWaitSeconds: number;
  /** Used when a 429 or 503 lacks a valid Retry-After. */
  defaultRetryAfterSeconds: number;
  attemptPollStartMs: number;
  attemptPollMaxMs: number;
  attemptLimitMs: number;
  /** Consecutive failed polls before an attempt is given up. */
  attemptNetworkErrors: number;
  rebootPollMs: number;
  /** After this long the wait is flagged as overdue, and it goes on. */
  rebootExpectedMs: number;
  /** After this long the restart is reported as not confirmed. */
  rebootLimitMs: number;
  savePollMs: number;
  saveLimitMs: number;
}

export const defaultClientTiming: ClientTiming = {
  busyRetries: 3,
  busyBaseDelayMs: 300,
  busyMaxDelayMs: 2000,
  unavailableRetries: 1,
  unavailableMaxWaitSeconds: 5,
  defaultRetryAfterSeconds: 5,
  attemptPollStartMs: 250,
  attemptPollMaxMs: 1000,
  attemptLimitMs: 30_000,
  attemptNetworkErrors: 3,
  rebootPollMs: 2000,
  rebootExpectedMs: 60_000,
  rebootLimitMs: 180_000,
  savePollMs: 1000,
  saveLimitMs: 15_000,
};

export interface ClientOptions {
  fetch?: typeof fetch;
  /** Prefix for every path. Same-origin use leaves it empty. */
  baseUrl?: string;
  /** Returns the text of document.cookie. */
  readCookies?: () => string;
  now?: () => number;
  random?: () => number;
  sleep?: (ms: number, signal?: AbortSignal) => Promise<void>;
  timing?: Partial<ClientTiming>;
  /** Called when a route answers 401, unless the call asked to be quiet. */
  onSessionLost?: () => void;
}

export interface Wait {
  reason: 'busy';
  delayMs: number;
}

export interface CallOptions {
  signal?: AbortSignal;
  /** Called when the client starts waiting to retry. */
  onWait?: (wait: Wait) => void;
}

export type AttemptProgress =
  | { phase: 'submitting' }
  | { phase: 'verifying' }
  | { phase: 'waiting'; reason: 'busy'; delayMs: number };

export interface AttemptOptions {
  signal?: AbortSignal;
  onProgress?: (progress: AttemptProgress) => void;
}

export type RejectionCode = 'invalid_field' | 'bad_request' | 'storage_failed' | 'not_ready' | 'unauthorized';

export type AttemptOutcome =
  | { kind: 'success' }
  | { kind: 'rejected'; code?: RejectionCode; fields?: FieldError[] }
  /** A 429: inside a delay window. Nothing was verified. */
  | { kind: 'delayed'; retryAfterSeconds: number }
  /** The route stayed held, or the verifier was busy and the attempt did not run. */
  | { kind: 'busy'; retryAfterSeconds?: number }
  /** A 404 while polling: the result cannot be recovered. */
  | { kind: 'lost' }
  | { kind: 'timed_out' }
  | { kind: 'aborted' }
  | { kind: 'failed'; error: ApiError };

export interface RebootUpdate {
  /** Whether the latest poll got an answer from a running device. */
  lastSeen: 'running' | 'away';
  overdue: boolean;
  elapsedMs: number;
}

export interface WatchRebootOptions {
  signal?: AbortSignal;
  onUpdate?: (update: RebootUpdate) => void;
}

export type RebootOutcome =
  /** The session stopped being valid. Only this counts as a restart. */
  | { kind: 'restarted' }
  | { kind: 'failed' }
  | { kind: 'refused'; reason: string }
  /** The time limit passed with no change. */
  | { kind: 'not_confirmed'; lastSeen: 'running' | 'away' }
  | { kind: 'aborted' }
  | { kind: 'failed_request'; error: ApiError };

export type SaveOutcome =
  | { kind: 'saved'; state: NetworkState }
  | { kind: 'failed'; state: NetworkState }
  /** This session may write but not read, so it cannot see the result. */
  | { kind: 'unreadable' }
  | { kind: 'not_confirmed' }
  | { kind: 'aborted' }
  | { kind: 'failed_request'; error: ApiError };

export type ConfigSaveOutcome =
  /** The device reports the latest save as saved. The list is the one that said so. */
  | { kind: 'saved'; list: ConfigList }
  /** The device reports it as failed. The code names no parameter. */
  | { kind: 'failed'; code: ConfigSaveFailed['code']; list: ConfigList }
  /** The list has no `save` member, so this session cannot see how the save went. */
  | { kind: 'not_visible' }
  | { kind: 'not_confirmed' }
  | { kind: 'aborted' }
  | { kind: 'failed_request'; error: ApiError };

/** A value in the JSON type of its parameter. */
export type ConfigInput = boolean | number | string;

export interface QuietOptions extends CallOptions {
  /** Do not call onSessionLost on a 401. */
  quiet?: boolean;
}

export interface CruxClient {
  login(user: string, password: string, options?: AttemptOptions): Promise<AttemptOutcome>;
  logout(options?: CallOptions): Promise<Result<void>>;
  getCurrentUser(options?: QuietOptions): Promise<Result<CurrentUser>>;
  changePassword(currentPassword: string, newPassword: string, options?: AttemptOptions): Promise<AttemptOutcome>;
  getStatus(options?: CallOptions): Promise<Result<StatusReport>>;
  listConfig(options?: CallOptions): Promise<Result<ConfigList>>;
  getConfigItem(name: string, options?: CallOptions): Promise<Result<ConfigValue>>;
  putConfigItem(name: string, value: ConfigInput, options?: CallOptions): Promise<Result<ConfigItem>>;
  saveConfig(options?: CallOptions): Promise<Result<SaveQueued>>;
  watchConfigSave(options?: { signal?: AbortSignal }): Promise<ConfigSaveOutcome>;
  getNetwork(options?: CallOptions): Promise<Result<NetworkState>>;
  putNetwork(settings: Ipv4Settings, options?: CallOptions): Promise<Result<SaveQueued>>;
  watchSave(expected: Ipv4Settings, options?: { signal?: AbortSignal }): Promise<SaveOutcome>;
  getRebootState(options?: QuietOptions): Promise<Result<RebootState>>;
  requestReboot(options?: CallOptions): Promise<Result<RebootAccepted>>;
  watchReboot(options?: WatchRebootOptions): Promise<RebootOutcome>;
}

interface RequestSpec {
  body?: unknown;
  /** Send the CSRF token. */
  token?: boolean;
  /** Send the header that the attempt poll requires. */
  pollHeader?: boolean;
  /** The body holds nothing to read. */
  noBody?: boolean;
  quiet?: boolean;
  /** Retry a 409 and a short 503 busy. Off where the caller has its own loop. */
  retry?: boolean;
  /**
   * Retry a 503 busy. Off for the poll, where a 503 has ended the attempt, and for a request that holds a
   * password, which is sent again only after a 409, because the server did not process that one.
   */
  retryUnavailable?: boolean;
  id?: string;
  /** A configuration parameter's name, for the `{name}` of the path. */
  name?: string;
  signal?: AbortSignal;
  onWait?: (wait: Wait) => void;
}

const ATTEMPT_ID = /^[A-Za-z0-9_-]{22,64}$/;
/** A parameter's key as the specification gives it. `save` is a fixed route and never a parameter. */
const CONFIG_NAME = /^[A-Za-z0-9_.-]{1,30}$/;

function usableConfigName(name: string): boolean {
  return CONFIG_NAME.test(name) && name !== '.' && name !== '..' && name !== 'save';
}

function defaultSleep(ms: number, signal?: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) {
      reject(new DOMException('aborted', 'AbortError'));
      return;
    }
    const timer = setTimeout(resolve, ms);
    signal?.addEventListener(
      'abort',
      () => {
        clearTimeout(timer);
        reject(new DOMException('aborted', 'AbortError'));
      },
      { once: true },
    );
  });
}

function readDocumentCookies(): string {
  return typeof document === 'undefined' ? '' : document.cookie;
}

/** The cookie's raw value. The token is opaque, so it is neither decoded nor changed. */
function cookieValue(cookies: string, name: string): string | undefined {
  for (const part of cookies.split(';')) {
    const trimmed = part.trim();
    const equals = trimmed.indexOf('=');
    if (equals > 0 && trimmed.slice(0, equals) === name) {
      return trimmed.slice(equals + 1);
    }
  }
  return undefined;
}

function sameSettings(a: Ipv4Settings | undefined, b: Ipv4Settings): boolean {
  return a !== undefined && a.address === b.address && a.prefix_length === b.prefix_length && a.gateway === b.gateway;
}

export function createClient(options: ClientOptions = {}): CruxClient {
  const fetchImpl = options.fetch ?? globalThis.fetch.bind(globalThis);
  const baseUrl = options.baseUrl ?? '';
  const readCookies = options.readCookies ?? readDocumentCookies;
  // Read at call time, so that a test's replacement clock also applies to a client that already exists.
  const now = options.now ?? (() => Date.now());
  const random = options.random ?? Math.random;
  const sleep = options.sleep ?? defaultSleep;
  const timing: ClientTiming = { ...defaultClientTiming, ...options.timing };
  const sessionLost = options.onSessionLost ?? (() => undefined);

  /** Wait, and report false if the wait was aborted. */
  async function pause(ms: number, signal?: AbortSignal): Promise<boolean> {
    try {
      await sleep(ms, signal);
      return true;
    } catch {
      return false;
    }
  }

  function busyDelay(retry: number): number {
    const base = Math.min(timing.busyMaxDelayMs, timing.busyBaseDelayMs * 2 ** retry);
    return Math.floor(base * (1 + random() / 4));
  }

  async function request<T>(operation: OperationId, spec: RequestSpec = {}): Promise<Result<T>> {
    const { method, path } = operations[operation];
    let target: string = path;
    if (spec.id !== undefined) {
      target = target.replace('{id}', spec.id);
    }
    if (spec.name !== undefined) {
      target = target.replace('{name}', spec.name);
    }
    const url = baseUrl + target;
    const headers: Record<string, string> = { Accept: 'application/json' };
    const init: RequestInit = {
      method,
      headers,
      credentials: 'same-origin',
      redirect: 'error',
      cache: 'no-store',
    };
    if (spec.signal) {
      init.signal = spec.signal;
    }
    if (spec.body !== undefined) {
      headers['Content-Type'] = 'application/json';
      init.body = JSON.stringify(spec.body);
    }
    if (spec.pollHeader) {
      headers['X-Crux-Request'] = '1';
    }
    const retry = spec.retry ?? true;
    const retryUnavailable = retry && (spec.retryUnavailable ?? true);
    let busyTries = 0;
    let unavailableTries = 0;
    for (;;) {
      if (spec.token) {
        // Read it for each try: a newer session may have replaced the cookie.
        const token = cookieValue(readCookies(), 'XSRF-TOKEN');
        if (token === undefined) {
          delete headers['X-XSRF-TOKEN'];
        } else {
          headers['X-XSRF-TOKEN'] = token;
        }
      }
      let response: Response;
      try {
        response = await fetchImpl(url, init);
      } catch (cause) {
        const aborted = spec.signal?.aborted === true || (cause as { name?: string }).name === 'AbortError';
        return { ok: false, error: { kind: aborted ? 'aborted' : 'network' } };
      }
      if (response.ok) {
        if (spec.noBody) {
          return { ok: true, value: undefined as T };
        }
        const value = await readJson(response);
        if (typeof value !== 'object' || value === null) {
          return { ok: false, error: { kind: 'unexpected', status: response.status } };
        }
        return { ok: true, value: value as T };
      }
      const error = await errorFromResponse(response, timing.defaultRetryAfterSeconds);
      let delayMs: number | undefined;
      if (retry && response.status === 409 && busyTries < timing.busyRetries) {
        delayMs = busyDelay(busyTries);
        busyTries += 1;
      } else if (
        retryUnavailable &&
        response.status === 503 &&
        error.kind === 'busy' &&
        unavailableTries < timing.unavailableRetries &&
        (error.retryAfterSeconds ?? Number.POSITIVE_INFINITY) <= timing.unavailableMaxWaitSeconds
      ) {
        delayMs = (error.retryAfterSeconds ?? timing.defaultRetryAfterSeconds) * 1000;
        unavailableTries += 1;
      }
      if (delayMs !== undefined) {
        spec.onWait?.({ reason: 'busy', delayMs });
        if (!(await pause(delayMs, spec.signal))) {
          return { ok: false, error: { kind: 'aborted' } };
        }
        continue;
      }
      if (error.kind === 'unauthorized' && !spec.quiet) {
        sessionLost();
      }
      return { ok: false, error };
    }
  }

  function outcomeFromRouteError(error: ApiError): AttemptOutcome {
    switch (error.kind) {
      case 'busy':
        return error.retryAfterSeconds === undefined
          ? { kind: 'busy' }
          : { kind: 'busy', retryAfterSeconds: error.retryAfterSeconds };
      case 'aborted':
        return { kind: 'aborted' };
      default:
        return { kind: 'failed', error };
    }
  }

  async function pollAttempt(id: string, attempt: AttemptOptions): Promise<AttemptOutcome> {
    const start = now();
    let delay = timing.attemptPollStartMs;
    let networkErrors = 0;
    for (;;) {
      if (!(await pause(delay, attempt.signal))) {
        return { kind: 'aborted' };
      }
      delay = Math.min(timing.attemptPollMaxMs, Math.floor(delay * 1.5));
      const spec: RequestSpec = { id, pollHeader: true, retryUnavailable: false };
      if (attempt.signal) {
        spec.signal = attempt.signal;
      }
      const result = await request<{ state?: string; code?: RejectionCode; fields?: FieldError[] }>(
        'getSessionAttempt',
        spec,
      );
      if (result.ok) {
        networkErrors = 0;
        const { state, code, fields } = result.value;
        if (state === 'success') {
          return { kind: 'success' };
        }
        if (state === 'rejected') {
          const outcome: AttemptOutcome = { kind: 'rejected' };
          if (code !== undefined) {
            outcome.code = code;
          }
          if (fields !== undefined) {
            outcome.fields = fields;
          }
          return outcome;
        }
        if (state !== 'pending') {
          return { kind: 'failed', error: { kind: 'unexpected', status: 200 } };
        }
      } else {
        const { error } = result;
        if (error.kind === 'not_found') {
          return { kind: 'lost' };
        }
        if (error.kind === 'retry_later') {
          return { kind: 'delayed', retryAfterSeconds: error.retryAfterSeconds ?? timing.defaultRetryAfterSeconds };
        }
        if (error.kind === 'network') {
          networkErrors += 1;
          if (networkErrors >= timing.attemptNetworkErrors) {
            return { kind: 'failed', error };
          }
        } else if (!(error.kind === 'busy' && error.status === 409)) {
          // A 409 that outlasted its retries leaves the attempt as it was, so polling goes on.
          return outcomeFromRouteError(error);
        }
      }
      if (now() - start >= timing.attemptLimitMs) {
        return { kind: 'timed_out' };
      }
    }
  }

  async function startAttempt(
    operation: 'createSession' | 'changeOwnPassword',
    body: unknown,
    token: boolean,
    attempt: AttemptOptions,
  ): Promise<AttemptOutcome> {
    attempt.onProgress?.({ phase: 'submitting' });
    const spec: RequestSpec = {
      body,
      token,
      retryUnavailable: false,
      onWait: (wait) => attempt.onProgress?.({ phase: 'waiting', reason: wait.reason, delayMs: wait.delayMs }),
    };
    if (attempt.signal) {
      spec.signal = attempt.signal;
    }
    const accepted = await request<{ id?: unknown }>(operation, spec);
    if (!accepted.ok) {
      return outcomeFromRouteError(accepted.error);
    }
    // A 202 means that the attempt is queued, even if the identifier then turns out to be unusable.
    attempt.onProgress?.({ phase: 'verifying' });
    const id = accepted.value.id;
    if (typeof id !== 'string' || !ATTEMPT_ID.test(id)) {
      return { kind: 'failed', error: { kind: 'unexpected', status: 202 } };
    }
    return pollAttempt(id, attempt);
  }

  return {
    login(user, password, attempt = {}) {
      return startAttempt('createSession', { user, password }, false, attempt);
    },

    async logout(call = {}) {
      const result = await request<void>('deleteSession', { ...call, token: true, noBody: true, quiet: true });
      if (!result.ok && result.error.kind === 'unauthorized') {
        return { ok: true, value: undefined };
      }
      return result;
    },

    getCurrentUser(call = {}) {
      return request<CurrentUser>('getCurrentUser', call);
    },

    async changePassword(currentPassword, newPassword, attempt = {}) {
      const outcome = await startAttempt(
        'changeOwnPassword',
        { current_password: currentPassword, new_password: newPassword },
        true,
        attempt,
      );
      if (outcome.kind === 'rejected' && outcome.code === 'unauthorized') {
        sessionLost();
      }
      return outcome;
    },

    getStatus(call = {}) {
      return request<StatusReport>('getStatus', call);
    },

    listConfig(call = {}) {
      return request<ConfigList>('listConfig', call);
    },

    async getConfigItem(name, call = {}) {
      if (!usableConfigName(name)) {
        return { ok: false, error: { kind: 'bad_request' } };
      }
      return request<ConfigValue>('getConfigItem', { ...call, name });
    },

    async putConfigItem(name, value, call = {}) {
      if (!usableConfigName(name)) {
        return { ok: false, error: { kind: 'bad_request' } };
      }
      return request<ConfigItem>('putConfigItem', { ...call, name, body: { value }, token: true });
    },

    saveConfig(call = {}) {
      return request<SaveQueued>('saveConfig', { ...call, token: true });
    },

    async watchConfigSave(watch = {}) {
      const start = now();
      for (;;) {
        let delay = timing.savePollMs;
        // The service answers busy while the worker is inside storage, so busy is the save still running.
        const spec: RequestSpec = { retryUnavailable: false };
        if (watch.signal) {
          spec.signal = watch.signal;
        }
        const result = await request<ConfigList>('listConfig', spec);
        if (result.ok) {
          const save = result.value.save;
          if (save === undefined) {
            return { kind: 'not_visible' };
          }
          if (save.state === 'saved') {
            return { kind: 'saved', list: result.value };
          }
          if (save.state === 'failed') {
            return { kind: 'failed', code: save.code, list: result.value };
          }
        } else {
          const { error } = result;
          switch (error.kind) {
            case 'aborted':
              return { kind: 'aborted' };
            case 'busy':
            case 'not_ready':
              delay = Math.max(delay, (error.retryAfterSeconds ?? 0) * 1000);
              break;
            case 'network':
            case 'unexpected':
              break;
            default:
              return { kind: 'failed_request', error };
          }
        }
        if (now() - start >= timing.saveLimitMs) {
          return { kind: 'not_confirmed' };
        }
        if (!(await pause(delay, watch.signal))) {
          return { kind: 'aborted' };
        }
      }
    },

    getNetwork(call = {}) {
      return request<NetworkState>('getNetwork', call);
    },

    putNetwork(settings, call = {}) {
      return request<SaveQueued>('putNetwork', { ...call, body: settings, token: true });
    },

    async watchSave(expected, watch = {}) {
      const start = now();
      for (;;) {
        const spec: RequestSpec = {};
        if (watch.signal) {
          spec.signal = watch.signal;
        }
        const result = await request<NetworkState>('getNetwork', spec);
        if (result.ok) {
          const state = result.value;
          if (state.save?.state === 'failed') {
            return { kind: 'failed', state };
          }
          if (state.save === undefined && sameSettings(state.saved, expected)) {
            return { kind: 'saved', state };
          }
        } else {
          switch (result.error.kind) {
            case 'permission_denied':
              return { kind: 'unreadable' };
            case 'aborted':
              return { kind: 'aborted' };
            case 'unauthorized':
              return { kind: 'failed_request', error: result.error };
            default:
              break;
          }
        }
        if (now() - start >= timing.saveLimitMs) {
          return { kind: 'not_confirmed' };
        }
        if (!(await pause(timing.savePollMs, watch.signal))) {
          return { kind: 'aborted' };
        }
      }
    },

    getRebootState(call = {}) {
      return request<RebootState>('getRebootState', call);
    },

    requestReboot(call = {}) {
      return request<RebootAccepted>('requestReboot', { ...call, token: true });
    },

    async watchReboot(watch = {}) {
      const start = now();
      let lastSeen: RebootUpdate['lastSeen'] = 'running';
      for (;;) {
        let delay = timing.rebootPollMs;
        // A 401 here is the expected end of the wait, so it does not report a lost session.
        const spec: RequestSpec = { quiet: true, retry: false };
        if (watch.signal) {
          spec.signal = watch.signal;
        }
        const result = await request<RebootState>('getRebootState', spec);
        if (result.ok) {
          lastSeen = 'running';
          const state = result.value;
          if (state.state === 'failed') {
            return { kind: 'failed' };
          }
          if (state.state === 'refused') {
            return { kind: 'refused', reason: state.reason };
          }
        } else {
          const { error } = result;
          switch (error.kind) {
            case 'unauthorized':
              return { kind: 'restarted' };
            case 'aborted':
              return { kind: 'aborted' };
            case 'network':
              lastSeen = 'away';
              break;
            case 'busy':
            case 'not_ready':
              lastSeen = 'running';
              delay = Math.max(delay, (error.retryAfterSeconds ?? 0) * 1000);
              break;
            case 'no_route':
            case 'method_not_allowed':
            case 'unexpected':
              // The device answered, even if not as the specification says, so it is running.
              lastSeen = 'running';
              break;
            default:
              return { kind: 'failed_request', error };
          }
        }
        const elapsedMs = now() - start;
        if (elapsedMs >= timing.rebootLimitMs) {
          return { kind: 'not_confirmed', lastSeen };
        }
        watch.onUpdate?.({ lastSeen, overdue: elapsedMs >= timing.rebootExpectedMs, elapsedMs });
        if (!(await pause(delay, watch.signal))) {
          return { kind: 'aborted' };
        }
      }
    },
  };
}
