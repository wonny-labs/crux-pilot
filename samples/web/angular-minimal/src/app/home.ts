import { ChangeDetectionStrategy, Component, OnDestroy, OnInit, inject, signal } from '@angular/core';
import type { StatusItem } from '../../../../../web/client/src/api-types.generated';
import { Api, codeOf, isUnauthorized } from './api';
import { Notice } from './notice';
import { Session } from './session';

const POLL_MS = 5000;

const LABELS: Record<string, string> = {
  'version.platform': 'Platform version',
  'version.project': 'Project version',
  'net.link': 'Link',
  'net.admin_up': 'Interface enabled',
  'net.ipv4': 'IPv4 address',
  'reset.causes': 'Reset causes',
  'reset.supported': 'Reset causes reported',
  'system.uptime_ms': 'Time since boot',
  'system.clock': 'Clock',
  'system.watchdog_state': 'Watchdog',
};

@Component({
  selector: 'crux-home',
  imports: [Notice],
  template: `
    <h1>Home</h1>
    @if (session.user()?.must_change_password) {
      <crux-notice kind="error">
        This account must change its password. This sample has no such screen, so use the reference interface or the
        command line.
      </crux-notice>
    }
    @if (problem(); as text) {
      <crux-notice kind="error">{{ text }}</crux-notice>
    }
    @if (items(); as list) {
      @if (list.length === 0) {
        <crux-notice>There is nothing to show to this account.</crux-notice>
      }
      <dl>
        @for (item of list; track item.name) {
          <div>
            <dt>{{ label(item) }}</dt>
            <dd>{{ show(item) }}</dd>
          </div>
        }
      </dl>
    }
  `,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class Home implements OnInit, OnDestroy {
  protected readonly session = inject(Session);
  private readonly api = inject(Api);

  protected readonly items = signal<StatusItem[] | null>(null);
  protected readonly problem = signal<string | null>(null);
  private timer: ReturnType<typeof setTimeout> | undefined;
  private stopped = false;

  ngOnInit(): void {
    void this.read();
  }

  ngOnDestroy(): void {
    this.stopped = true;
    clearTimeout(this.timer);
  }

  protected label(item: StatusItem): string {
    return Object.hasOwn(LABELS, item.name) ? (LABELS[item.name] as string) : item.name;
  }

  protected show(item: StatusItem): string {
    if (item.status !== 'ok') {
      return `Not available (${item.status})`;
    }
    return typeof item.value === 'boolean' ? (item.value ? 'Yes' : 'No') : String(item.value);
  }

  private async read(): Promise<void> {
    try {
      const report = await this.api.status();
      this.items.set(report.items);
      this.problem.set(null);
    } catch (error) {
      if (isUnauthorized(error)) {
        this.session.ended();
        return;
      }
      this.problem.set(`The device could not answer (${codeOf(error)}).`);
    }
    if (!this.stopped) {
      this.timer = setTimeout(() => void this.read(), POLL_MS);
    }
  }
}
