// The session as the screens see it: the user, and a notice for sign-in.
import { Injectable, inject, signal } from '@angular/core';
import { Router } from '@angular/router';
import type { CurrentUser } from '../../../../../web/client/src/api-types.generated';
import { Api, isUnauthorized } from './api';

@Injectable({ providedIn: 'root' })
export class Session {
  private readonly api = inject(Api);
  private readonly router = inject(Router);
  private loaded: Promise<void> | undefined;

  readonly user = signal<CurrentUser | null>(null);
  readonly notice = signal<string | null>(null);

  /** Read the session once, for the route guard. */
  load(): Promise<void> {
    this.loaded ??= this.refresh();
    return this.loaded;
  }

  async refresh(): Promise<void> {
    try {
      this.user.set(await this.api.currentUser());
    } catch {
      this.user.set(null);
    }
  }

  /** A 401 from any route ends the session here, and sign-in takes over. */
  ended(): void {
    this.user.set(null);
    this.notice.set('Your session has ended. Sign in again to continue.');
    void this.router.navigateByUrl('/sign-in');
  }

  async signOut(): Promise<void> {
    try {
      await this.api.logout();
    } catch (error) {
      if (!isUnauthorized(error)) {
        this.notice.set('Could not sign out, and the session is still active.');
        return;
      }
    }
    this.user.set(null);
    this.notice.set('You have signed out.');
    await this.router.navigateByUrl('/sign-in');
  }
}
