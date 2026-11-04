import { inject } from '@angular/core';
import { Router, type Routes } from '@angular/router';
import { Home } from './home';
import { Session } from './session';
import { SignIn } from './sign-in';

async function signedIn() {
  const session = inject(Session);
  const router = inject(Router);
  await session.load();
  return session.user() !== null ? true : router.createUrlTree(['/sign-in']);
}

export const routes: Routes = [
  { path: 'sign-in', component: SignIn },
  { path: '', component: Home, canActivate: [signedIn] },
  { path: '**', redirectTo: '' },
];
