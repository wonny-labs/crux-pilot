import { ChangeDetectionStrategy, Component, inject } from '@angular/core';
import { RouterOutlet } from '@angular/router';
import { Session } from './session';

@Component({
  selector: 'crux-root',
  imports: [RouterOutlet],
  template: `
    @if (session.user(); as user) {
      <header>
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
