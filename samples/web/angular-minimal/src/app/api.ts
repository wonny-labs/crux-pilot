// The management API through HttpClient, with the generated types. It covers what sign-in and home need:
// the two-request login, the required poll header, the retry of a held route and the plain answers.
import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { firstValueFrom } from 'rxjs';
import type {
  AttemptAccepted,
  CurrentUser,
  ErrorCode,
  SessionAttempt,
  StatusReport,
} from '../../../../../web/client/src/api-types.generated';

export type LoginOutcome =
  | { kind: 'success' }
  | { kind: 'rejected' }
  | { kind: 'delayed'; retryAfterSeconds: number }
  | { kind: 'busy'; retryAfterSeconds: number }
  | { kind: 'lost' }
  | { kind: 'failed'; code: ErrorCode | 'network' | 'unexpected' };

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

  async login(user: string, password: string): Promise<LoginOutcome> {
    let accepted: AttemptAccepted;
    try {
      accepted = await this.retryBusy(() =>
        firstValueFrom(this.http.post<AttemptAccepted>('api/v1/session', { user, password })),
      );
    } catch (error) {
      return this.outcomeOf(error);
    }
    const start = Date.now();
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
          return { kind: 'rejected' };
        }
      } catch (error) {
        return this.outcomeOf(error);
      }
      if (Date.now() - start >= ATTEMPT_LIMIT_MS) {
        return { kind: 'failed', code: 'busy' };
      }
    }
  }

  private outcomeOf(error: unknown): LoginOutcome {
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
