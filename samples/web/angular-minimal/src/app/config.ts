// Configuration: the parameters that this session may see, one update at a time in the device's memory, and
// one save that writes them. The list never holds a value, so each readable value is read on its own, and a
// sensitive one is never read. The checks here are for usability, and the device decides what it accepts.
import { ChangeDetectionStrategy, Component, Injector, OnDestroy, OnInit, afterNextRender, inject, signal } from '@angular/core';
import type { ConfigItem, ConfigValue } from '../../../../../web/client/src/api-types.generated';
import { Api, failureOf } from './api';
import type { Failure, SaveOutcome } from './api';
import { Notice } from './notice';
import { Session } from './session';

type Shown = 'loading' | 'value' | 'hidden' | 'sensitive' | 'failed';
type Kind = 'error' | 'warning' | 'success';

interface Row {
  item: ConfigItem;
  shown: Shown;
  value: ConfigValue['value'] | undefined;
  /** What the person typed, or the state of the switch. */
  draft: string | boolean;
  /** The person has typed since the value was read, so a later read must not replace the draft. */
  edited: boolean;
  error: string | null;
  working: boolean;
}

interface Message {
  kind: Kind;
  text: string;
}

const TYPES: Record<string, string> = { bool: 'On or off', u32: 'Whole number', string: 'Text' };
const ACTIVATION: Record<string, string> = {
  application: 'The application applies a change when it chooses.',
  restart: 'A change applies after a restart.',
  safe_state: 'A change applies when the machine is in a safe state.',
};
const FIELD_ERRORS: Record<string, string> = {
  out_of_range: 'Enter a whole number from 0 to 4294967295.',
  rule_not_met: 'The device does not accept this value.',
  wrong_type: 'This is not the kind of value this parameter takes.',
};
const SAVE_FAILED: Record<string, string> = {
  storage_failed: 'The save failed. The device could not store the changes. Saves made earlier stay saved. Try again.',
  not_supported:
    'The save failed. This device cannot save, because it has no Configuration provider. The changes stay in memory until the next restart. Retrying will not help.',
  not_ready: 'The save failed. The configuration service is not ready. Try again shortly.',
  busy: 'The save failed. The device was busy. Try again.',
  unauthorized: 'The save failed. The session ended before the save ran.',
  permission_denied: 'The save failed. This account may not save.',
};
const FAILURES: Record<string, string> = {
  network: 'The device did not answer. Check the connection and try again.',
  busy: 'The device is busy. Try again in a moment.',
  not_ready: 'A service on the device is not ready. Try again shortly.',
  permission_denied: 'This account does not have permission to do this.',
  csrf_rejected: 'The device refused this request, because its security token did not match. Reload the page and try again.',
  origin_rejected: 'The device refused this request, because it did not come from the address of the device.',
  not_found: 'The device has no such item.',
  payload_too_large: 'This request is too large for the device.',
  not_supported: 'This device does not support this operation.',
};

function lookup(table: Record<string, string>, key: string): string | undefined {
  return Object.hasOwn(table, key) ? table[key] : undefined;
}

function seconds(count: number): string {
  return count === 1 ? '1 second' : `${count} seconds`;
}

function draftOf(item: ConfigItem, value: ConfigValue['value'] | undefined): string | boolean {
  if (item.type === 'bool') {
    return value === true;
  }
  return value === undefined ? '' : String(value);
}

function fresh(item: ConfigItem): Row {
  const shown: Shown = item.sensitive ? 'sensitive' : item.readable ? 'loading' : 'hidden';
  return { item, shown, value: undefined, draft: draftOf(item, undefined), edited: false, error: null, working: false };
}

/** The text of a field as a JSON value. Only the device knows the range, so a sign is sent on. */
function parse(item: ConfigItem, text: string): { value: number | string } | { problem: string } {
  if (item.type === 'string') {
    return { value: text };
  }
  const trimmed = text.trim();
  if (trimmed === '') {
    return { problem: 'Enter a value.' };
  }
  if (!/^-?[0-9]+$/.test(trimmed) || !Number.isSafeInteger(Number(trimmed))) {
    return { problem: 'Enter a whole number.' };
  }
  return { value: Number(trimmed) };
}

@Component({
  selector: 'crux-config',
  imports: [Notice],
  template: `
    <h1 id="page-title" tabindex="-1">Configuration</h1>
    <p>
      A changed value is kept in the device's memory, and it applies as its parameter says. Save the changes to keep
      them after a restart.
    </p>
    @if (loadError(); as text) {
      <crux-notice kind="error">{{ text }}</crux-notice>
      <button type="button" (click)="load()">Try again</button>
    }
    @if (message(); as note) {
      <crux-notice [kind]="note.kind">{{ note.text }}</crux-notice>
    }
    @if (rows(); as list) {
      @if (list.length === 0) {
        <p>There is nothing to configure for this account.</p>
      } @else {
        <ul class="params">
          @for (row of list; track row.item.name) {
            <li>
              <form (submit)="update($event, row)" novalidate>
                <fieldset>
                  <legend>{{ row.item.name }}</legend>
                  <p class="hint">{{ description(row.item) }}</p>
                  @switch (row.shown) {
                    @case ('value') {
                      <p>Current value: {{ valueText(row) }}</p>
                    }
                    @case ('hidden') {
                      <p>The value is not shown to this account.</p>
                    }
                    @case ('sensitive') {
                      <p>A sensitive value is never shown.</p>
                    }
                    @case ('failed') {
                      <p>The value could not be read.</p>
                    }
                  }
                  @if (row.item.unsaved) {
                    <p class="unsaved">Changed, not yet saved</p>
                  }
                  @if (!row.item.writable) {
                    <p class="hint">Read only for this account.</p>
                  }
                  @if (row.item.writable && !row.item.sensitive) {
                    @if (row.item.type === 'bool') {
                      <label class="switch" [attr.for]="'config-' + row.item.name">
                        <input
                          type="checkbox"
                          [id]="'config-' + row.item.name"
                          [checked]="row.draft === true"
                          (change)="edit(row.item.name, checked($event))"
                        />
                        On
                      </label>
                    } @else {
                      <label [attr.for]="'config-' + row.item.name">New value</label>
                      <input
                        [id]="'config-' + row.item.name"
                        autocomplete="off"
                        [attr.inputmode]="row.item.type === 'u32' ? 'numeric' : 'text'"
                        [attr.aria-invalid]="row.error ? 'true' : null"
                        [attr.aria-describedby]="row.error ? 'config-' + row.item.name + '-error' : null"
                        [value]="row.draft"
                        (input)="edit(row.item.name, text($event))"
                      />
                      @if (row.error; as problem) {
                        <p class="field-error" [id]="'config-' + row.item.name + '-error'">{{ problem }}</p>
                      }
                    }
                    <button type="submit" [attr.aria-disabled]="busy() ? 'true' : null">Update</button>
                    @if (row.working) {
                      <p class="hint">Sending the value to the device…</p>
                    }
                  }
                </fieldset>
              </form>
            </li>
          }
        </ul>
      }
      @if (anyWritable()) {
        <section aria-labelledby="config-save-title">
          <h2 id="config-save-title">Save</h2>
          <p>Saving writes every changed parameter, including any that this account cannot see.</p>
          <button type="button" [attr.aria-disabled]="busy() ? 'true' : null" (click)="save()">Save changes</button>
          @if (saving()) {
            <crux-notice>Saving the changes on the device…</crux-notice>
          }
          @if (saveMessage(); as note) {
            <crux-notice [kind]="note.kind">{{ note.text }}</crux-notice>
          }
        </section>
      }
    }
  `,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class Configuration implements OnInit, OnDestroy {
  private readonly api = inject(Api);
  private readonly session = inject(Session);
  private readonly injector = inject(Injector);

  protected readonly rows = signal<Row[] | null>(null);
  protected readonly loadError = signal<string | null>(null);
  protected readonly message = signal<Message | null>(null);
  protected readonly saving = signal(false);
  protected readonly saveMessage = signal<Message | null>(null);
  /** A new load makes an older one stop, and leaving the screen stops them all. */
  private generation = 0;
  private stopped = false;

  ngOnInit(): void {
    void this.load();
  }

  ngOnDestroy(): void {
    this.stopped = true;
  }

  protected description(item: ConfigItem): string {
    return `${lookup(TYPES, item.type) ?? item.type}. ${lookup(ACTIVATION, item.activation) ?? ''}`;
  }

  protected valueText(row: Row): string {
    if (typeof row.value === 'boolean') {
      return row.value ? 'On' : 'Off';
    }
    return row.value === '' ? '(empty)' : String(row.value);
  }

  protected text(event: Event): string {
    return (event.target as HTMLInputElement).value;
  }

  protected checked(event: Event): boolean {
    return (event.target as HTMLInputElement).checked;
  }

  protected busy(): boolean {
    return this.saving() || (this.rows() ?? []).some((row) => row.working);
  }

  protected anyWritable(): boolean {
    return (this.rows() ?? []).some((row) => row.item.writable);
  }

  protected edit(name: string, draft: string | boolean): void {
    this.patch(name, { draft, edited: true, error: null });
  }

  private patch(name: string, change: Partial<Row>): void {
    this.rows.update((rows) => rows?.map((row) => (row.item.name === name ? { ...row, ...change } : row)) ?? null);
  }

  private focus(name: string): void {
    afterNextRender(() => document.getElementById(`config-${name}`)?.focus(), { injector: this.injector });
  }

  private failureText(failure: Failure): string {
    if (failure.code === 'busy' && failure.retryAfterSeconds !== undefined) {
      return `The device is busy. Try again in ${seconds(failure.retryAfterSeconds)}.`;
    }
    return lookup(FAILURES, failure.code) ?? 'The device gave an answer that this interface does not understand.';
  }

  /** A message for a failed call, or none when the session ended and sign-in takes over. */
  private failed(failure: Failure, denied: string): Message | null {
    if (failure.code === 'unauthorized') {
      this.session.ended();
      return null;
    }
    return { kind: 'error', text: failure.code === 'permission_denied' ? denied : this.failureText(failure) };
  }

  /** Read one value. A sensitive parameter is never asked for, whatever else happens. */
  private async readValue(item: ConfigItem, generation: number): Promise<void> {
    if (item.sensitive || !item.readable) {
      return;
    }
    try {
      const read = await this.api.configValue(item.name);
      if (this.stopped || generation !== this.generation) {
        return;
      }
      this.rows.update(
        (rows) =>
          rows?.map((row) =>
            row.item.name === item.name
              ? { ...row, shown: 'value', value: read.value, draft: row.edited ? row.draft : draftOf(item, read.value) }
              : row,
          ) ?? null,
      );
    } catch (error) {
      const failure = failureOf(error);
      if (failure.code === 'unauthorized') {
        this.session.ended();
      } else if (failure.code === 'permission_denied') {
        this.patch(item.name, { shown: 'hidden' });
      } else {
        this.patch(item.name, { shown: 'failed' });
      }
    }
  }

  protected async load(): Promise<void> {
    this.generation += 1;
    const generation = this.generation;
    this.loadError.set(null);
    let items: ConfigItem[];
    try {
      items = (await this.api.listConfig()).items;
    } catch (error) {
      const failure = failureOf(error);
      if (failure.code === 'unauthorized') {
        this.session.ended();
      } else {
        this.loadError.set(`The parameters could not be read. ${this.failureText(failure)}`);
      }
      return;
    }
    if (this.stopped || generation !== this.generation) {
      return;
    }
    this.rows.set(items.map(fresh));
    // One at a time, because the server has one thread and other clients share it.
    for (const item of items) {
      await this.readValue(item, generation);
      if (this.stopped || generation !== this.generation) {
        return;
      }
    }
  }

  protected async update(event: Event, row: Row): Promise<void> {
    event.preventDefault();
    const { item } = row;
    if (row.working || this.busy()) {
      return;
    }
    this.message.set(null);
    this.saveMessage.set(null);
    let value: ConfigValue['value'];
    if (item.type === 'bool') {
      value = row.draft === true;
    } else {
      const parsed = parse(item, String(row.draft));
      if ('problem' in parsed) {
        this.patch(item.name, { error: parsed.problem });
        this.focus(item.name);
        return;
      }
      value = parsed.value;
    }
    this.patch(item.name, { working: true, error: null });
    try {
      const accepted = await this.api.updateConfig(item.name, value);
      if (this.stopped) {
        return;
      }
      // The typed value is now the device's, so the read-back may replace the draft.
      this.patch(item.name, { working: false, item: accepted, edited: false });
      this.message.set({ kind: 'success', text: `Updated ${item.name}. Save the changes to keep them after a restart.` });
      await this.readValue(accepted, this.generation);
    } catch (error) {
      this.patch(item.name, { working: false });
      const failure = failureOf(error);
      if (failure.code === 'invalid_field') {
        const field = failure.fields.find((entry) => entry.name === 'value');
        this.patch(item.name, {
          error: (field && lookup(FIELD_ERRORS, field.code)) ?? 'The device refused this value.',
        });
        this.focus(item.name);
      } else if (failure.code === 'not_found') {
        this.message.set({ kind: 'error', text: `The device has no parameter ${item.name}. The list was read again.` });
        void this.load();
      } else {
        this.message.set(this.failed(failure, `This account may not change ${item.name}.`));
      }
    }
  }

  private showSaveOutcome(outcome: SaveOutcome): void {
    switch (outcome.kind) {
      case 'saved':
      case 'failed': {
        const list = outcome.list;
        this.rows.update(
          (rows) =>
            rows?.map((row) => ({ ...row, item: list.items.find((entry) => entry.name === row.item.name) ?? row.item })) ??
            null,
        );
        this.saveMessage.set(
          outcome.kind === 'saved'
            ? { kind: 'success', text: 'The changes are saved.' }
            : {
                kind: 'error',
                text: lookup(SAVE_FAILED, outcome.code) ?? 'The device gave an answer that this interface does not understand.',
              },
        );
        break;
      }
      case 'not_visible':
        this.saveMessage.set({ kind: 'warning', text: 'The device accepted the save. This account cannot see its result.' });
        break;
      case 'not_confirmed':
        this.saveMessage.set({
          kind: 'warning',
          text: 'The device accepted the save, but it was not confirmed in time. The list was read again.',
        });
        void this.load();
        break;
      case 'failed_request':
        this.saveMessage.set(this.failed(outcome.failure, 'This account may not save changes.'));
        break;
    }
  }

  protected async save(): Promise<void> {
    if (this.busy()) {
      return;
    }
    this.message.set(null);
    this.saveMessage.set(null);
    this.saving.set(true);
    try {
      await this.api.saveConfig();
    } catch (error) {
      this.saving.set(false);
      this.saveMessage.set(this.failed(failureOf(error), 'This account may not save changes.'));
      return;
    }
    const outcome = await this.api.watchSave();
    if (this.stopped) {
      return;
    }
    this.saving.set(false);
    this.showSaveOutcome(outcome);
  }
}
