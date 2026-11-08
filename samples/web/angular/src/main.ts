import { provideHttpClient } from '@angular/common/http';
import { bootstrapApplication } from '@angular/platform-browser';
import { provideRouter, withHashLocation } from '@angular/router';
import { App } from './app/app';
import { routes } from './app/routes';

// provideHttpClient keeps its default CSRF names: it reads the XSRF-TOKEN cookie and sends X-XSRF-TOKEN on
// state-changing, same-origin requests. Those are the names that the Crux API uses, so nothing is set here.
// withHashLocation means that the server sees only the root path and the assets, so a reload needs no fallback.
bootstrapApplication(App, {
  providers: [provideHttpClient(), provideRouter(routes, withHashLocation())],
}).catch((error: unknown) => {
  console.error(error);
});
