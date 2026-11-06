// The management API through HttpClient, with the generated types. It covers what the screens need: the
// two-request attempts for sign-in and the password change, the required poll header, the retry of a held
// route, the plain answers, and the configuration calls with the wait for a save.
import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { firstValueFrom } from 'rxjs';
import type { Observable } from 'rxjs';
import type {
  AttemptAccepted,
  ConfigItem,
  ConfigList,
  ConfigSaveFailed,
  ConfigValue,
  CurrentUser,
  ErrorCode,
  FieldError,
  SaveQueued,
  SessionAttempt,
  StatusReport,
} from '../../../../../web/client/src/api-types.generated';

/** The end of a sign-in or a password change. Only the second kind of `rejected` has a code and fields. */
export type AttemptOutcome =
  | { kind: 'success' }
  | { kind: 'rejected'; code?: ErrorCode; fields?: FieldError[] }
  | { kind: 'delayed'; retryAfterSeconds: number }
  | { kind: 'busy'; retryAfterSeconds: number }
  | { kind: 'lost' }
  | { kind: 'failed'; code: ErrorCode | 'network' | 'unexpected' };

/** What a failed call comes to: the code of the device's error, or a stand-in, with what the form needs. */
export interface Failure {
  code: ErrorCode | 'network' | 'unexpected';
  status: number;
  fields: FieldError[];
  retryAfterSeconds: number | undefined;
}

export type SaveOutcome =
  | { kind: 'saved'; list: ConfigList }
  | { kind: 'failed'; code: ConfigSaveFailed['code']; list: ConfigList }
  | { kind: 'not_visible' }
  | { kind: 'not_confirmed' }
  | { kind: 'aborted' }
  | { kind: 'failed_request'; failure: Failure };

const SAVE_POLL_MS = 1000;
const SAVE_LIMIT_MS = 15_000;
/** The device's Retry-After is honoured up to this long between two reads. */
const SAVE_WAIT_MAX_MS = 5000;

const BUSY_DELAYS_MS = [300, 600, 1200];
const POLL_START_MS = 250;
const POLL_MAX_MS = 1000;
const ATTEMPT_LIMIT_MS = 30_000;

function wait(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function retryAfter(error: HttpErrorResponse): number {
  const header = error.headers.get('Retry-After') ?? '';
  return /^[1-9][0-9]*$/.test(header) ? Number(header) : 5;
}

/** The code from a JSON error, or a stand-in for the server's plain answers and for no answer at all. */
export function codeOf(error: unknown): ErrorCode | 'network' | 'unexpected' {
  if (!(error instanceof HttpErrorResponse)) {
    return 'unexpected';
  }
  if (error.status === 0) {
    return 'network';
  }
  const body = error.error as { code?: ErrorCode } | null;
  return typeof body === 'object' && body !== null && typeof body.code === 'string' ? body.code : 'unexpected';
}

export function failureOf(error: unknown): Failure {
  const status = error instanceof HttpErrorResponse ? error.status : 0;
  const body = error instanceof HttpErrorResponse ? (error.error as { fields?: FieldError[] } | null) : null;
  const header = error instanceof HttpErrorResponse ? (error.headers.get('Retry-After') ?? '') : '';
  return {
    code: codeOf(error),
    status,
    fields: Array.isArray(body?.fields) ? body.fields : [],
    retryAfterSeconds: /^[1-9][0-9]*$/.test(header) ? Number(header) : undefined,
  };
}

export function isUnauthorized(error: unknown): boolean {
  return error instanceof HttpErrorResponse && error.status === 401;
}

@Injectable({ providedIn: 'root' })
export class Api {
  private readonly http = inject(HttpClient);

  /** A 409 has no body, and it means that another client holds the route, so the request is sent again. */
  private async retryBusy<T>(send: () => Promise<T>): Promise<T> {
    for (const delay of BUSY_DELAYS_MS) {
      try {
        return await send();
      } catch (error) {
        if (!(error instanceof HttpErrorResponse) || error.status !== 409) {
          throw error;
        }
        await wait(delay);
      }
    }
    return send();
  }

  currentUser(): Promise<CurrentUser> {
    return this.retryBusy(() => firstValueFrom(this.http.get<CurrentUser>('api/v1/session/me')));
  }

  status(): Promise<StatusReport> {
    return this.retryBusy(() => firstValueFrom(this.http.get<StatusReport>('api/v1/status')));
  }

  /** The body of a logout is empty, so it is read as text and never parsed. */
  logout(): Promise<string> {
    return this.retryBusy(() => firstValueFrom(this.http.delete('api/v1/session', { responseType: 'text' })));
  }

  login(user: string, password: string): Promise<AttemptOutcome> {
    return this.attempt(() => this.http.post<AttemptAccepted>('api/v1/session', { user, password }));
  }

  /** The token header is added by HttpClient. */
  changePassword(currentPassword: string, newPassword: string): Promise<AttemptOutcome> {
    return this.attempt(() =>
      this.http.post<AttemptAccepted>('api/v1/session/me/password', {
        current_password: currentPassword,
        new_password: newPassword,
      }),
    );
  }

  listConfig(): Promise<ConfigList> {
    return this.retryBusy(() => firstValueFrom(this.http.get<ConfigList>('api/v1/config')));
  }

  configValue(name: string): Promise<ConfigValue> {
    return this.retryBusy(() => firstValueFrom(this.http.get<ConfigValue>(`api/v1/config/${encodeURIComponent(name)}`)));
  }

  updateConfig(name: string, value: boolean | number | string): Promise<ConfigItem> {
    return this.retryBusy(() =>
      firstValueFrom(this.http.put<ConfigItem>(`api/v1/config/${encodeURIComponent(name)}`, { value })),
    );
  }

  saveConfig(): Promise<SaveQueued> {
    return this.retryBusy(() => firstValueFrom(this.http.post<SaveQueued>('api/v1/config/save', null)));
  }

  /** Read the list until the latest save has ended. Busy means that the worker is still inside storage. */
  async watchSave(signal: AbortSignal): Promise<SaveOutcome> {
    const start = Date.now();
    for (;;) {
      if (signal.aborted) {
        return { kind: 'aborted' };
      }
      let delay = SAVE_POLL_MS;
      try {
        const list = await this.listConfig();
        if (list.save === undefined) {
          return { kind: 'not_visible' };
        }
        if (list.save.state === 'saved') {
          return { kind: 'saved', list };
        }
        if (list.save.state === 'failed') {
          return { kind: 'failed', code: list.save.code, list };
        }
      } catch (error) {
        const failure = failureOf(error);
        if (failure.code === 'busy' || failure.code === 'not_ready') {
          delay = Math.max(delay, (failure.retryAfterSeconds ?? 0) * 1000);
        } else if (failure.code !== 'network' && failure.code !== 'unexpected') {
          return { kind: 'failed_request', failure };
        }
      }
      const left = SAVE_LIMIT_MS - (Date.now() - start);
      if (left <= 0) {
        return { kind: 'not_confirmed' };
      }
      await wait(Math.min(delay, SAVE_WAIT_MAX_MS, left));
    }
  }

  /** Start an attempt, then poll it. A 202 only queues it. */
  private async attempt(start: () => Observable<AttemptAccepted>): Promise<AttemptOutcome> {
    let accepted: AttemptAccepted;
    try {
      accepted = await this.retryBusy(() => firstValueFrom(start()));
    } catch (error) {
      return this.outcomeOf(error);
    }
    const begun = Date.now();
    let delay = POLL_START_MS;
    for (;;) {
      await wait(delay);
      delay = Math.min(POLL_MAX_MS, Math.floor(delay * 1.5));
      try {
        const attempt = await this.retryBusy(() =>
          firstValueFrom(
            this.http.get<SessionAttempt>(`api/v1/session/attempt/${encodeURIComponent(accepted.id)}`, {
              headers: { 'X-Crux-Request': '1' },
            }),
          ),
        );
        if (attempt.state === 'success') {
          return { kind: 'success' };
        }
        if (attempt.state === 'rejected') {
          const outcome: AttemptOutcome = { kind: 'rejected' };
          if ('code' in attempt) {
            outcome.code = attempt.code;
          }
          if ('fields' in attempt) {
            outcome.fields = attempt.fields;
          }
          return outcome;
        }
      } catch (error) {
        return this.outcomeOf(error);
      }
      if (Date.now() - begun >= ATTEMPT_LIMIT_MS) {
        return { kind: 'failed', code: 'busy' };
      }
    }
  }

  private outcomeOf(error: unknown): AttemptOutcome {
    if (error instanceof HttpErrorResponse) {
      if (error.status === 429) {
        return { kind: 'delayed', retryAfterSeconds: retryAfter(error) };
      }
      if (error.status === 503 || error.status === 409) {
        return { kind: 'busy', retryAfterSeconds: retryAfter(error) };
      }
      if (error.status === 404) {
        return { kind: 'lost' };
      }
    }
    return { kind: 'failed', code: codeOf(error) };
  }
}
