import { ChangeDetectionStrategy, Component, inject } from '@angular/core';
import { RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router';
import { Session } from './session';

@Component({
  selector: 'crux-root',
  imports: [RouterOutlet, RouterLink, RouterLinkActive],
  template: `
    @if (session.user(); as user) {
      <header>
        @if (!user.must_change_password) {
          <nav aria-label="Main">
            <a routerLink="/" routerLinkActive="current" [routerLinkActiveOptions]="{ exact: true }" ariaCurrentWhenActive="page">Home</a>
            <a routerLink="/config" routerLinkActive="current" ariaCurrentWhenActive="page">Configuration</a>
            <a routerLink="/network" routerLinkActive="current" ariaCurrentWhenActive="page">Network</a>
            <a routerLink="/reboot" routerLinkActive="current" ariaCurrentWhenActive="page">Restart</a>
          </nav>
        }
        <span>Signed in as {{ user.user }} ({{ user.role }})</span>
        <button type="button" (click)="session.signOut()">Sign out</button>
      </header>
    }
    <main><router-outlet /></main>
  `,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class App {
  protected readonly session = inject(Session);
}
