// Restart: a confirmation, then the wait. The running device also answers, so an answer is never success.
// Success is a session that is no longer valid, and a wait that sees no change says it was not confirmed.
// The behaviour is that of the Preact example, so the shared browser cases hold for both.
import { NgTemplateOutlet } from '@angular/common';
import { ChangeDetectionStrategy, Component, Injector, OnDestroy, OnInit, afterNextRender, inject, signal } from '@angular/core';
import { Api, failureOf } from './api';
import type { Failure, RebootOutcome, RebootUpdate } from './api';
import { Notice } from './notice';
import { Session } from './session';
import { REBOOT as T, errorText } from './texts';

type Phase =
  | 'loading'
  | 'denied'
  | 'idle'
  | 'confirm'
  | 'requesting'
  | 'waiting'
  | 'failed'
  | 'refused'
  | 'not_confirmed'
  | 'error';

@Component({
  selector: 'crux-reboot',
  imports: [Notice, NgTemplateOutlet],
  template: `
    <h1 id="page-title" tabindex="-1">{{ t.title }}</h1>
    @if (phase() === 'denied') {
      <crux-notice kind="warning">{{ t.denied }}</crux-notice>
    } @else {
      <p>{{ t.intro }}</p>
      @if (savedAddress(); as address) {
        <crux-notice kind="warning">{{ t.savedAddress(address) }}</crux-notice>
      }
    }
    @switch (phase()) {
      @case ('idle') {
        @if (previousFailed()) {
          <crux-notice kind="warning">{{ t.previousFailed }}</crux-notice>
        }
        @if (previousReason(); as earlier) {
          <crux-notice kind="warning">{{ t.previousRefused(earlier) }}</crux-notice>
        }
        <ng-container [ngTemplateOutlet]="start" />
      }
      @case ('confirm') {
        <section class="panel" aria-labelledby="reboot-confirm">
          <h2 id="reboot-confirm">{{ t.confirmTitle }}</h2>
          <p>{{ t.confirmBody }}</p>
          <p class="actions">
            <button type="button" id="reboot-cancel" (click)="cancel()">{{ t.cancel }}</button>
            <button type="button" class="danger" (click)="request()">{{ t.confirm }}</button>
          </p>
        </section>
      }
      @case ('waiting') {
        <crux-notice>
          <p>{{ update().lastSeen === 'running' ? t.waitingRunning : t.waitingAway }}</p>
          @if (update().overdue) {
            <p>{{ t.overdue }}</p>
          }
        </crux-notice>
      }
      @case ('failed') {
        <crux-notice kind="error">{{ t.failed }}</crux-notice>
        <ng-container [ngTemplateOutlet]="start" />
      }
      @case ('refused') {
        <crux-notice kind="error">{{ t.refused(reason()) }}</crux-notice>
        <ng-container [ngTemplateOutlet]="start" />
      }
      @case ('not_confirmed') {
        <crux-notice kind="error">{{ lastSeen() === 'running' ? t.notConfirmedRunning : t.notConfirmedAway }}</crux-notice>
        <p class="actions">
          <button type="button" class="primary" (click)="watch()">{{ t.keepWaiting }}</button>
          <button type="button" (click)="arrive()">{{ t.back }}</button>
        </p>
      }
      @case ('error') {
        <crux-notice kind="error">{{ errorMessage() }}</crux-notice>
        <ng-container [ngTemplateOutlet]="start" />
      }
    }
    <ng-template #start>
      <p class="actions">
        <button type="button" class="primary" id="reboot-start" (click)="ask()">{{ t.start }}</button>
      </p>
    </ng-template>
  `,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class Reboot implements OnInit, OnDestroy {
  private readonly api = inject(Api);
  private readonly session = inject(Session);
  private readonly injector = inject(Injector);

  protected readonly t = T;
  protected readonly phase = signal<Phase>('loading');
  /** What an earlier request left, which the screen shows on arrival. */
  protected readonly previousFailed = signal(false);
  protected readonly previousReason = signal<string | null>(null);
  protected readonly savedAddress = signal<string | null>(null);
  protected readonly update = signal<RebootUpdate>({ lastSeen: 'running', overdue: false });
  protected readonly reason = signal('');
  protected readonly lastSeen = signal<RebootUpdate['lastSeen']>('running');
  protected readonly errorMessage = signal('');
  /** A new step makes an older one stop, and leaving the screen stops them all. */
  private step = 0;
  private stopped = false;
  private controller = new AbortController();

  ngOnInit(): void {
    void this.arrive();
  }

  ngOnDestroy(): void {
    this.stopped = true;
    this.controller.abort();
  }

  /** Start a step: the older one stops, and the wait of the older one with it. */
  private begin(): { ticket: number; signal: AbortSignal } {
    this.controller.abort();
    this.controller = new AbortController();
    this.step += 1;
    return { ticket: this.step, signal: this.controller.signal };
  }

  private current(ticket: number): boolean {
    return !this.stopped && ticket === this.step;
  }

  private focusId(id: string): void {
    afterNextRender(() => document.getElementById(id)?.focus(), { injector: this.injector });
  }

  private showError(failure: Failure): void {
    if (failure.code === 'permission_denied') {
      this.phase.set('denied');
    } else if (failure.code === 'unauthorized') {
      this.session.ended();
    } else {
      this.errorMessage.set(errorText(failure));
      this.phase.set('error');
    }
  }

  protected async watch(): Promise<void> {
    const { ticket, signal } = this.begin();
    this.update.set({ lastSeen: 'running', overdue: false });
    this.phase.set('waiting');
    const outcome = await this.api.watchReboot(signal, (update) => {
      if (this.current(ticket)) {
        this.update.set(update);
      }
    });
    if (!this.current(ticket)) {
      return;
    }
    this.showOutcome(outcome);
  }

  private showOutcome(outcome: RebootOutcome): void {
    switch (outcome.kind) {
      case 'restarted':
        this.session.restarted();
        break;
      case 'failed':
        this.phase.set('failed');
        break;
      case 'refused':
        this.reason.set(outcome.reason);
        this.phase.set('refused');
        break;
      case 'not_confirmed':
        this.lastSeen.set(outcome.lastSeen);
        this.phase.set('not_confirmed');
        break;
      case 'failed_request':
        this.showError(outcome.failure);
        break;
      case 'aborted':
        break;
    }
  }

  protected async arrive(): Promise<void> {
    const { ticket } = this.begin();
    this.phase.set('loading');
    const [state, network] = await Promise.allSettled([this.api.rebootState(), this.api.network()]);
    if (!this.current(ticket)) {
      return;
    }
    if (network.status === 'fulfilled' && network.value.reboot_required && network.value.saved) {
      this.savedAddress.set(network.value.saved.address);
    }
    if (state.status === 'rejected') {
      this.showError(failureOf(state.reason));
      return;
    }
    if (network.status === 'rejected' && failureOf(network.reason).code === 'unauthorized') {
      this.session.ended();
      return;
    }
    switch (state.value.state) {
      case 'pending':
        void this.watch();
        break;
      case 'failed':
        this.previousFailed.set(true);
        this.previousReason.set(null);
        this.phase.set('idle');
        break;
      case 'refused':
        this.previousFailed.set(false);
        this.previousReason.set(state.value.reason);
        this.phase.set('idle');
        break;
      case 'none':
        this.previousFailed.set(false);
        this.previousReason.set(null);
        this.phase.set('idle');
        break;
    }
  }

  protected ask(): void {
    this.phase.set('confirm');
    this.focusId('reboot-cancel');
  }

  protected cancel(): void {
    // As in the Preact example, what an earlier request left is shown on arrival only.
    this.previousFailed.set(false);
    this.previousReason.set(null);
    this.phase.set('idle');
    this.focusId('reboot-start');
  }

  protected async request(): Promise<void> {
    const { ticket } = this.begin();
    this.phase.set('requesting');
    try {
      await this.api.requestReboot();
    } catch (error) {
      if (!this.current(ticket)) {
        return;
      }
      const failure = failureOf(error);
      if (failure.code === 'reboot_refused') {
        this.reason.set(failure.reason ?? '');
        this.phase.set('refused');
      } else {
        this.showError(failure);
      }
      return;
    }
    if (!this.current(ticket)) {
      return;
    }
    await this.watch();
  }
}
