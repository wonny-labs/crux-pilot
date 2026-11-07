// Network settings: the running and the saved values, and a save that takes effect at the next boot. The
// checks here are for usability, and the device remains the authority. A session that may not read can still
// submit, because a screen learns what it may do from each route's own answer. The behaviour is that of the
// Preact example, so the shared browser cases hold for both.
import { ChangeDetectionStrategy, Component, Injector, OnDestroy, OnInit, afterNextRender, inject, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { checkIpv4Settings } from '../../../../../web/client/src/ipv4';
import type { Ipv4Settings, NetworkState } from '../../../../../web/client/src/api-types.generated';
import { Api, failureOf } from './api';
import type { Failure, NetworkSaveOutcome } from './api';
import { Notice } from './notice';
import { Session } from './session';
import { NETWORK as T, errorText, lookup } from './texts';

interface FormValues {
  address: string;
  prefix_length: string;
  gateway: string;
}

type FieldName = keyof FormValues;
type FieldMessages = Partial<Record<FieldName, string>>;

type Result =
  | { kind: 'saved' | 'failed' | 'unconfirmed' | 'unreadable' | 'not_supported' | 'denied' }
  | { kind: 'error'; text: string };

const FIELD_ORDER: FieldName[] = ['address', 'prefix_length', 'gateway'];
/** The ids that a person's assistive technology and the shared browser cases find the fields by. */
const FIELD_IDS: Record<FieldName, string> = {
  address: 'network-address',
  prefix_length: 'network-prefix',
  gateway: 'network-gateway',
};

function formFrom(settings: Ipv4Settings | undefined): FormValues {
  return {
    address: settings?.address ?? '',
    prefix_length: settings ? String(settings.prefix_length) : '',
    gateway: settings?.gateway ?? '',
  };
}

@Component({
  selector: 'crux-network',
  imports: [Notice, RouterLink],
  template: `
    <h1 id="page-title" tabindex="-1">{{ t.title }}</h1>
    @if (readDenied()) {
      <crux-notice kind="warning">{{ t.readDenied }}</crux-notice>
    }
    @if (loadError(); as text) {
      <crux-notice kind="error">{{ text }}</crux-notice>
    }
    @if (state(); as current) {
      <section aria-labelledby="network-running">
        <h2 id="network-running">{{ t.running }}</h2>
        @if (current.running; as settings) {
          <dl class="items">
            <div class="item">
              <dt>{{ t.address }}</dt>
              <dd>{{ settings.address }}/{{ settings.prefix_length }}</dd>
            </div>
            <div class="item">
              <dt>{{ t.gateway }}</dt>
              <dd>{{ settings.gateway ?? t.noGateway }}</dd>
            </div>
          </dl>
        } @else {
          <p>{{ t.noAddress }} {{ t.noAddressAfterBoot }}</p>
        }
        <p class="hint">{{ current.source === 'saved' ? t.sourceSaved : t.sourceDefault }}</p>
      </section>
      <section aria-labelledby="network-saved">
        <h2 id="network-saved">{{ t.saved }}</h2>
        @if (current.saved; as settings) {
          <dl class="items">
            <div class="item">
              <dt>{{ t.address }}</dt>
              <dd>{{ settings.address }}/{{ settings.prefix_length }}</dd>
            </div>
            <div class="item">
              <dt>{{ t.gateway }}</dt>
              <dd>{{ settings.gateway ?? t.noGateway }}</dd>
            </div>
          </dl>
        } @else {
          <p>{{ t.nothingSaved }}</p>
        }
      </section>
      @if (current.reboot_required) {
        <crux-notice kind="warning">
          <p>{{ t.rebootRequired }}</p>
          <p>
            <a routerLink="/reboot">{{ t.rebootLink }}</a>
          </p>
        </crux-notice>
      }
      @if (current.save?.state === 'pending' && !saving()) {
        <crux-notice>{{ t.savePending }}</crux-notice>
      }
      @if (current.save?.state === 'failed' && result()?.kind !== 'failed') {
        <crux-notice kind="warning">{{ t.saveFailedState }}</crux-notice>
      }
    }

    @switch (result()?.kind) {
      @case ('saved') {
        <crux-notice kind="success">{{ t.saved_ }}</crux-notice>
      }
      @case ('failed') {
        <crux-notice kind="error">{{ t.saveFailed }}</crux-notice>
      }
      @case ('unconfirmed') {
        <crux-notice kind="warning">{{ t.saveUnconfirmed }}</crux-notice>
      }
      @case ('unreadable') {
        <crux-notice kind="warning">{{ t.saveUnreadable }}</crux-notice>
      }
      @case ('not_supported') {
        <crux-notice kind="error">{{ t.notSupported }}</crux-notice>
      }
      @case ('denied') {
        <crux-notice kind="error">{{ t.saveDenied }}</crux-notice>
      }
      @case ('error') {
        <crux-notice kind="error">{{ errorMessage() }}</crux-notice>
      }
    }

    @if (pending(); as settings) {
      <section class="panel" aria-labelledby="network-confirm">
        <h2 id="network-confirm">{{ t.confirmTitle }}</h2>
        <p>{{ t.confirmSummary(settings.address, settings.prefix_length, settings.gateway ?? t.noGateway) }}</p>
        <ul>
          <li>{{ t.confirmNext(settings.address) }}</li>
          <li>{{ t.confirmAnyReset }}</li>
          <li>{{ t.confirmRecovery }}</li>
        </ul>
        @if (saving()) {
          <crux-notice>{{ t.saving }}</crux-notice>
        } @else {
          <p class="actions">
            <button type="button" id="network-cancel" (click)="cancel()">{{ t.cancel }}</button>
            <button type="button" class="primary" (click)="save(settings)">{{ t.confirm }}</button>
          </p>
        }
      </section>
    } @else {
      <section aria-labelledby="network-form">
        <h2 id="network-form">{{ t.formTitle }}</h2>
        <p>{{ t.formIntro }}</p>
        <form class="narrow" (submit)="review($event)" novalidate>
          <label for="network-address">{{ t.address }}</label>
          <input
            id="network-address"
            type="text"
            inputmode="decimal"
            autocomplete="off"
            autocapitalize="off"
            spellcheck="false"
            aria-required="true"
            [attr.aria-invalid]="errors().address ? 'true' : null"
            [attr.aria-describedby]="errors().address ? 'network-address-error' : null"
            [value]="form().address"
            (input)="setField('address', text($event))"
          />
          @if (errors().address; as problem) {
            <p class="field-error" id="network-address-error">{{ problem }}</p>
          }
          <label for="network-prefix">{{ t.prefix }}</label>
          <input
            id="network-prefix"
            type="text"
            inputmode="numeric"
            autocomplete="off"
            autocapitalize="off"
            spellcheck="false"
            aria-required="true"
            [attr.aria-invalid]="errors().prefix_length ? 'true' : null"
            [attr.aria-describedby]="errors().prefix_length ? 'network-prefix-error' : null"
            [value]="form().prefix_length"
            (input)="setField('prefix_length', text($event))"
          />
          @if (errors().prefix_length; as problem) {
            <p class="field-error" id="network-prefix-error">{{ problem }}</p>
          }
          <label for="network-gateway">{{ t.gateway }}</label>
          <p class="hint" id="network-gateway-hint">{{ t.gatewayHint }}</p>
          <input
            id="network-gateway"
            type="text"
            inputmode="decimal"
            autocomplete="off"
            autocapitalize="off"
            spellcheck="false"
            [attr.aria-invalid]="errors().gateway ? 'true' : null"
            [attr.aria-describedby]="errors().gateway ? 'network-gateway-hint network-gateway-error' : 'network-gateway-hint'"
            [value]="form().gateway"
            (input)="setField('gateway', text($event))"
          />
          @if (errors().gateway; as problem) {
            <p class="field-error" id="network-gateway-error">{{ problem }}</p>
          }
          <button type="submit" class="primary" id="network-review">{{ t.review }}</button>
        </form>
      </section>
    }
  `,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class Network implements OnInit, OnDestroy {
  private readonly api = inject(Api);
  private readonly session = inject(Session);
  private readonly injector = inject(Injector);

  protected readonly t = T;
  protected readonly state = signal<NetworkState | null>(null);
  protected readonly readDenied = signal(false);
  protected readonly loadError = signal<string | null>(null);
  protected readonly form = signal<FormValues>(formFrom(undefined));
  protected readonly errors = signal<FieldMessages>({});
  protected readonly pending = signal<Ipv4Settings | null>(null);
  protected readonly saving = signal(false);
  protected readonly result = signal<Result | null>(null);
  /** The person has typed since the values were read, so a later read must not replace the form. */
  private edited = false;
  /** A new load makes an older one stop, and leaving the screen stops them all. */
  private generation = 0;
  private stopped = false;
  private readonly leaving = new AbortController();

  ngOnInit(): void {
    void this.load();
  }

  ngOnDestroy(): void {
    this.stopped = true;
    this.leaving.abort();
  }

  protected text(event: Event): string {
    return (event.target as HTMLInputElement).value;
  }

  protected errorMessage(): string {
    const current = this.result();
    return current?.kind === 'error' ? current.text : '';
  }

  private focusId(id: string): void {
    afterNextRender(() => document.getElementById(id)?.focus(), { injector: this.injector });
  }

  /** A message for a failed call, or none when the session ended and sign-in takes over. */
  private failed(failure: Failure): Result | null {
    if (failure.code === 'unauthorized') {
      this.session.ended();
      return null;
    }
    return { kind: 'error', text: errorText(failure) };
  }

  protected async load(): Promise<void> {
    this.generation += 1;
    const generation = this.generation;
    try {
      const read = await this.api.network();
      if (this.stopped || generation !== this.generation) {
        return;
      }
      this.state.set(read);
      this.loadError.set(null);
      if (!this.edited) {
        this.form.set(formFrom(read.saved ?? read.running));
      }
    } catch (error) {
      const failure = failureOf(error);
      if (this.stopped || generation !== this.generation) {
        return;
      }
      if (failure.code === 'permission_denied') {
        this.readDenied.set(true);
      } else if (failure.code === 'unauthorized') {
        this.session.ended();
      } else {
        this.loadError.set(errorText(failure));
      }
    }
  }

  protected setField(name: FieldName, value: string): void {
    this.edited = true;
    this.form.update((current) => ({ ...current, [name]: value }));
  }

  private showFieldErrors(found: { name: string; code: string }[]): void {
    const messages: FieldMessages = {};
    for (const { name, code } of found) {
      if (FIELD_ORDER.includes(name as FieldName)) {
        messages[name as FieldName] = lookup(T.fieldErrors, code) ?? T.fieldUnknown;
      }
    }
    this.errors.set(messages);
    const first = FIELD_ORDER.find((name) => messages[name] !== undefined);
    if (first !== undefined) {
      this.focusId(FIELD_IDS[first]);
    }
  }

  protected review(event: Event): void {
    event.preventDefault();
    this.result.set(null);
    const { problems, settings } = checkIpv4Settings(this.form());
    this.showFieldErrors(problems);
    if (settings) {
      this.pending.set(settings);
      this.focusId('network-cancel');
    }
  }

  protected cancel(): void {
    this.pending.set(null);
    this.focusId('network-review');
  }

  private showSaveOutcome(outcome: NetworkSaveOutcome): void {
    switch (outcome.kind) {
      case 'saved':
        this.state.set(outcome.state);
        this.result.set({ kind: 'saved' });
        break;
      case 'failed':
        this.state.set(outcome.state);
        this.result.set({ kind: 'failed' });
        break;
      case 'unreadable':
        this.result.set({ kind: 'unreadable' });
        break;
      case 'not_confirmed':
        this.result.set({ kind: 'unconfirmed' });
        void this.load();
        break;
      case 'failed_request':
        this.result.set(this.failed(outcome.failure));
        break;
      case 'aborted':
        break;
    }
  }

  protected async save(settings: Ipv4Settings): Promise<void> {
    this.saving.set(true);
    this.generation += 1;
    try {
      await this.api.putNetwork(settings);
    } catch (error) {
      if (this.stopped) {
        return;
      }
      this.saving.set(false);
      this.pending.set(null);
      const failure = failureOf(error);
      switch (failure.code) {
        case 'invalid_field':
          this.showFieldErrors(failure.fields);
          break;
        case 'not_supported':
          this.result.set({ kind: 'not_supported' });
          break;
        case 'permission_denied':
          this.result.set({ kind: 'denied' });
          break;
        default:
          this.result.set(this.failed(failure));
      }
      return;
    }
    const outcome = await this.api.watchNetworkSave(settings, this.leaving.signal);
    if (this.stopped) {
      return;
    }
    this.saving.set(false);
    this.pending.set(null);
    this.showSaveOutcome(outcome);
  }
}
