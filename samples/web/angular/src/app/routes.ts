import { inject } from '@angular/core';
import { Router, type Routes } from '@angular/router';
import { ChangePassword } from './change-password';
import { Configuration } from './config';
import { Home } from './home';
import { Session } from './session';
import { SignIn } from './sign-in';

/** A signed-in session may use the screens, unless it must first change its password. */
async function signedIn() {
  const session = inject(Session);
  const router = inject(Router);
  await session.load();
  const user = session.user();
  if (user === null) {
    return router.createUrlTree(['/sign-in']);
  }
  return user.must_change_password ? router.createUrlTree(['/change-password']) : true;
}

/** The forced change is for a session that must change its password, and for no other. */
async function mustChange() {
  const session = inject(Session);
  const router = inject(Router);
  await session.load();
  const user = session.user();
  if (user === null) {
    return router.createUrlTree(['/sign-in']);
  }
  return user.must_change_password ? true : router.createUrlTree(['/']);
}

export const routes: Routes = [
  { path: 'sign-in', component: SignIn },
  { path: 'change-password', component: ChangePassword, canActivate: [mustChange] },
  { path: 'config', component: Configuration, canActivate: [signedIn] },
  { path: '', component: Home, canActivate: [signedIn] },
  { path: '**', redirectTo: '' },
];
