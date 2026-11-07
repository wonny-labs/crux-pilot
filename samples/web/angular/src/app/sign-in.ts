import { ChangeDetectionStrategy, Component, OnDestroy, inject, signal } from '@angular/core';
import { Router } from '@angular/router';
import { Api } from './api';
import { Notice } from './notice';
import { Session } from './session';

const REJECTED =
  'Sign-in failed. Check the user name and password. A device with no account yet must first be set up on its serial console.';

@Component({
  selector: 'crux-sign-in',
  imports: [Notice],
  template: `
    <h1>Sign in</h1>
    @if (session.notice(); as text) {
      @if (!message() && !waitText()) {
        <crux-notice>{{ text }}</crux-notice>
      }
    }
    <form (submit)="submit($event)" novalidate>
      <label for="user">User name</label>
      <input id="user" autocomplete="username" [value]="user()" (input)="user.set(value($event))" />
      <label for="password">Password</label>
      <input
        id="password"
        type="password"
        autocomplete="current-password"
        [value]="password()"
        (input)="password.set(value($event))"
      />
      <button type="submit" [attr.aria-disabled]="blocked() ? 'true' : null">Sign in</button>
    </form>
    @if (message(); as text) {
      <crux-notice kind="error">{{ text }}</crux-notice>
    }
    @if (waitText(); as text) {
      <crux-notice kind="error">{{ text }}</crux-notice>
    }
  `,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class SignIn implements OnDestroy {
  protected readonly session = inject(Session);
  private readonly api = inject(Api);
  private readonly router = inject(Router);

  protected readonly user = signal('');
  protected readonly password = signal('');
  protected readonly message = signal<string | null>(null);
  protected readonly waitText = signal<string | null>(null);
  protected readonly blocked = signal(false);
  private timer: ReturnType<typeof setInterval> | undefined;

  ngOnDestroy(): void {
    clearInterval(this.timer);
  }

  protected value(event: Event): string {
    return (event.target as HTMLInputElement).value;
  }

  private hold(seconds: number, text: (left: number) => string): void {
    let left = seconds;
    this.blocked.set(true);
    this.waitText.set(text(left));
    clearInterval(this.timer);
    this.timer = setInterval(() => {
      left -= 1;
      if (left <= 0) {
        clearInterval(this.timer);
        this.blocked.set(false);
        this.waitText.set(null);
      } else {
        this.waitText.set(text(left));
      }
    }, 1000);
  }

  protected async submit(event: Event): Promise<void> {
    event.preventDefault();
    if (this.blocked()) {
      return;
    }
    this.message.set(null);
    this.blocked.set(true);
    const outcome = await this.api.login(this.user(), this.password());
    this.blocked.set(false);
    switch (outcome.kind) {
      case 'success':
        this.password.set('');
        this.session.notice.set(null);
        await this.session.refresh();
        await this.router.navigateByUrl('/');
        break;
      case 'rejected':
        this.password.set('');
        this.message.set(REJECTED);
        break;
      case 'delayed':
        this.hold(outcome.retryAfterSeconds, (left) => `Too many attempts. Try again in ${left} seconds.`);
        break;
      case 'busy':
        this.hold(outcome.retryAfterSeconds, (left) => `The device is busy. Try again in ${left} seconds.`);
        break;
      case 'lost':
        this.message.set('The device lost the result of this sign-in. Sign in again.');
        break;
      case 'failed':
        this.message.set(`The device refused this sign-in (${outcome.code}).`);
        break;
    }
  }
}
