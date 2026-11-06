// The forced password change. The session can do nothing else until it completes, and the server enforces
// that: the routes only keep this session on the screen. A lost result is never taken as success on its own,
// and only the device's own answer about the session decides.
import { ChangeDetectionStrategy, Component, Injector, OnDestroy, afterNextRender, inject, signal } from '@angular/core';
import { Router } from '@angular/router';
import { Api } from './api';
import { Notice } from './notice';
import { Session } from './session';

const MESSAGES = {
  needCurrent: 'Enter your current password.',
  needNext: 'Enter a new password.',
  mismatch: 'The new passwords do not match.',
  incorrect: 'The current password is not correct.',
  incorrectAfterLost:
    'The current password was not accepted. An earlier attempt may have taken effect, so the new password may already be the current one. Enter it as the current password, or check with an administrator.',
  ruleNotMet: "The new password does not meet the device's password rule.",
  fieldUnknown: 'The device refused this value.',
  storageFailed:
    'The new password could not be saved. The running password is unchanged, and a later boot may use either password. Try again.',
  badRequest: 'The device refused this change. Check the values and try again.',
  notReady: 'The account service on the device is not ready. Try again shortly.',
  lost: 'The result was lost, so this change may or may not have taken effect. Enter the passwords again.',
  network: 'The device did not answer. Check the connection and try again.',
  unexpected: 'The device gave an answer that this interface does not understand.',
};

@Component({
  selector: 'crux-change-password',
  imports: [Notice],
  template: `
    <h1 id="page-title" tabindex="-1">Change your password</h1>
    <p>This account must change its password before it can do anything else.</p>
    <form (submit)="submit($event)" novalidate>
      <label for="password-current">Current password</label>
      <input
        id="password-current"
        type="password"
        autocomplete="current-password"
        aria-required="true"
        [attr.aria-invalid]="errorCurrent() ? 'true' : null"
        [attr.aria-describedby]="errorCurrent() ? 'password-current-error' : null"
        [value]="current()"
        (input)="current.set(value($event))"
      />
      @if (errorCurrent(); as text) {
        <p class="field-error" id="password-current-error">{{ text }}</p>
      }
      <label for="password-new">New password</label>
      <p class="hint" id="password-new-hint">
        The device checks the new password against its password rule. The reference rule is 12 to 128 bytes.
      </p>
      <input
        id="password-new"
        type="password"
        autocomplete="new-password"
        aria-required="true"
        [attr.aria-invalid]="errorNext() ? 'true' : null"
        [attr.aria-describedby]="errorNext() ? 'password-new-hint password-new-error' : 'password-new-hint'"
        [value]="next()"
        (input)="next.set(value($event))"
      />
      @if (errorNext(); as text) {
        <p class="field-error" id="password-new-error">{{ text }}</p>
      }
      <label for="password-repeat">Repeat the new password</label>
      <input
        id="password-repeat"
        type="password"
        autocomplete="new-password"
        aria-required="true"
        [attr.aria-invalid]="errorRepeat() ? 'true' : null"
        [attr.aria-describedby]="errorRepeat() ? 'password-repeat-error' : null"
        [value]="repeat()"
        (input)="repeat.set(value($event))"
      />
      @if (errorRepeat(); as text) {
        <p class="field-error" id="password-repeat-error">{{ text }}</p>
      }
      <button type="submit" [attr.aria-disabled]="blocked() ? 'true' : null">Change password</button>
    </form>
    @if (progress(); as text) {
      <crux-notice>{{ text }}</crux-notice>
    }
    @if (message(); as text) {
      <crux-notice kind="error">{{ text }}</crux-notice>
    }
    @if (waitText(); as text) {
      <crux-notice kind="error">{{ text }}</crux-notice>
    }
  `,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class ChangePassword implements OnDestroy {
  private readonly session = inject(Session);
  private readonly api = inject(Api);
  private readonly router = inject(Router);
  private readonly injector = inject(Injector);

  protected readonly current = signal('');
  protected readonly next = signal('');
  protected readonly repeat = signal('');
  protected readonly errorCurrent = signal<string | null>(null);
  protected readonly errorNext = signal<string | null>(null);
  protected readonly errorRepeat = signal<string | null>(null);
  protected readonly message = signal<string | null>(null);
  protected readonly progress = signal<string | null>(null);
  protected readonly waitText = signal<string | null>(null);
  protected readonly blocked = signal(false);
  /** Set once the outcome of an attempt is unknown, because the meaning of "incorrect" then changes. */
  private lostEarlier = false;
  private timer: ReturnType<typeof setInterval> | undefined;

  ngOnDestroy(): void {
    clearInterval(this.timer);
  }

  protected value(event: Event): string {
    return (event.target as HTMLInputElement).value;
  }

  private focus(id: string): void {
    afterNextRender(() => document.getElementById(id)?.focus(), { injector: this.injector });
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

  private clearAll(): void {
    this.clearPasswords();
    this.focus('password-current');
  }

  protected async submit(event: Event): Promise<void> {
    event.preventDefault();
    if (this.blocked()) {
      return;
    }
    this.message.set(null);
    this.errorCurrent.set(this.current() === '' ? MESSAGES.needCurrent : null);
    this.errorNext.set(this.next() === '' ? MESSAGES.needNext : null);
    this.errorRepeat.set(this.next() !== this.repeat() ? MESSAGES.mismatch : null);
    if (this.errorCurrent() !== null || this.errorNext() !== null || this.errorRepeat() !== null) {
      this.focus(this.errorCurrent() !== null ? 'password-current' : 'password-new');
      return;
    }
    this.blocked.set(true);
    this.progress.set('Sending the change to the device…');
    const outcome = await this.api.changePassword(this.current(), this.next());
    this.progress.set(null);
    this.blocked.set(false);
    switch (outcome.kind) {
      case 'success':
        await this.finish();
        break;
      case 'rejected':
        await this.rejected(outcome.code, outcome.fields ?? []);
        break;
      case 'delayed':
        this.hold(outcome.retryAfterSeconds, (left) => `Too many attempts. Try again in ${left} seconds.`);
        break;
      case 'busy':
        this.hold(outcome.retryAfterSeconds, (left) => `The device is busy. Try again in ${left} seconds.`);
        break;
      case 'lost':
        // During a forced change the session itself shows whether the earlier change succeeded.
        if (!(await this.finish())) {
          this.lostEarlier = true;
          this.message.set(MESSAGES.lost);
          this.clearAll();
        }
        break;
      case 'failed':
        if (outcome.code === 'unauthorized') {
          this.session.ended();
        } else {
          this.lostEarlier = this.lostEarlier || outcome.code === 'network';
          this.message.set(outcome.code === 'network' ? MESSAGES.network : MESSAGES.unexpected);
        }
        break;
    }
  }

  /** Read the session again, and leave this screen once the change no longer applies. */
  private async finish(): Promise<boolean> {
    this.clearPasswords();
    await this.session.refresh();
    const user = this.session.user();
    if (user === null) {
      await this.router.navigateByUrl('/sign-in');
      return true;
    }
    if (user.must_change_password) {
      return false;
    }
    await this.router.navigateByUrl('/');
    return true;
  }

  private clearPasswords(): void {
    this.current.set('');
    this.next.set('');
    this.repeat.set('');
  }

  private async rejected(code: string | undefined, fields: { name: string; code: string }[]): Promise<void> {
    switch (code) {
      case 'invalid_field': {
        let current: string | null = null;
        let next: string | null = null;
        for (const field of fields) {
          if (field.name === 'current_password' && field.code === 'incorrect') {
            current = this.lostEarlier ? MESSAGES.incorrectAfterLost : MESSAGES.incorrect;
          } else if (field.name === 'new_password') {
            next = field.code === 'rule_not_met' ? MESSAGES.ruleNotMet : MESSAGES.fieldUnknown;
          } else {
            current = MESSAGES.fieldUnknown;
          }
        }
        this.errorCurrent.set(current);
        this.errorNext.set(next);
        if (current !== null) {
          this.current.set('');
          this.focus('password-current');
        } else {
          this.next.set('');
          this.repeat.set('');
          this.focus('password-new');
        }
        break;
      }
      case 'storage_failed':
        this.message.set(MESSAGES.storageFailed);
        break;
      case 'bad_request':
        this.message.set(MESSAGES.badRequest);
        break;
      case 'not_ready':
        this.message.set(MESSAGES.notReady);
        break;
      case 'unauthorized':
        this.session.ended();
        break;
      default:
        this.message.set(MESSAGES.unexpected);
    }
  }
}
