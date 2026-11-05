// A component with its own styles, as most Angular applications have. Angular inserts these into the page at
// run time, which is why a project using it needs the inline style setting. See the README.
import { ChangeDetectionStrategy, Component, input } from '@angular/core';

@Component({
  selector: 'crux-notice',
  template: `<div class="notice" [attr.role]="kind() === 'error' ? 'alert' : 'status'"><ng-content /></div>`,
  styles: `
    .notice {
      margin: 1rem 0;
      padding: 0.75rem 1rem;
      border-left: 4px solid currentColor;
    }
  `,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class Notice {
  readonly kind = input<'error' | 'warning' | 'success' | 'info'>('info');
}
