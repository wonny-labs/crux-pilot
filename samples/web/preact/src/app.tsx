// The Preact interface: a small shell around the screens. A project reuses it through the options of
// mountPreactInterface, or replaces it entirely with any folder of built files.
import { render } from 'preact';
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'preact/hooks';
import { createClient } from '../../../../web/client/src/index.ts';
import type { ApiError, ClientOptions, CruxClient, CurrentUser } from '../../../../web/client/src/index.ts';
import { Heading, Notice } from './components.tsx';
import { AppContext } from './context.ts';
import type { AppValue, HomeItemDefinition, Screen, Timing } from './context.ts';
import { hrefFor, useHashPath } from './router.ts';
import { ChangePassword } from './screens/change-password.tsx';
import { SignIn } from './screens/sign-in.tsx';
import type { SignInNotice } from './screens/sign-in.tsx';
import { englishText, mergeText } from './text.ts';
import type { Text, TextOverrides } from './text.ts';

export interface PreactOptions {
  /** The screens after sign-in. The first one answers the root route. */
  screens: Screen[];
  /** Labels and formats for a project's own status items. An item with none is shown by its name. */
  homeItems?: HomeItemDefinition[];
  text?: TextOverrides;
  /** Client settings, such as a longer restart limit for a device that boots slowly. */
  client?: Partial<ClientOptions>;
  timing?: Partial<Timing>;
}

const defaultTiming: Timing = { statusPollMs: 5000 };

type Session =
  | { phase: 'checking' }
  | { phase: 'unavailable'; error: ApiError }
  | { phase: 'signed_out'; notice: SignInNotice | undefined }
  | { phase: 'signed_in'; user: CurrentUser };

/**
 * Keep the title and the keyboard focus with the screen. Focus moves to the heading when the screen changes,
 * but not on the first load, and not to sign-in, which focuses its first field. A layout effect moves focus in
 * the same step as the page change, so that a key pressed straight away cannot land on nothing.
 */
function useFocusOnChange(key: string, title: string, product: string): void {
  const previous = useRef('checking');
  useLayoutEffect(() => {
    document.title = `${title} – ${product}`;
    const arriving = previous.current === 'checking' || previous.current === 'unavailable';
    previous.current = key;
    if (!arriving && key !== 'sign-in') {
      document.getElementById('page-title')?.focus();
    }
  }, [key, title, product]);
}

function Shell(props: { options: PreactOptions; text: Text }) {
  const { options, text } = props;
  const [session, setSession] = useState<Session>({ phase: 'checking' });
  const [shellMessage, setShellMessage] = useState<string | null>(null);
  const path = useHashPath();
  const signingOut = useRef(false);

  const client: CruxClient = useMemo(
    () =>
      createClient({
        ...options.client,
        onSessionLost: () => {
          if (!signingOut.current) {
            setShellMessage(null);
            setSession({ phase: 'signed_out', notice: 'session_ended' });
          }
        },
      }),
    [],
  );

  async function loadUser(justSignedIn = false): Promise<boolean> {
    const result = await client.getCurrentUser({ quiet: true });
    if (result.ok) {
      setSession({ phase: 'signed_in', user: result.value });
      return !result.value.must_change_password;
    }
    if (result.error.kind === 'unauthorized') {
      // Straight after a successful login, the browser may have refused the cookie, through plain HTTP or
      // blocked cookies, or the device may have ended the session. Both give this 401, so the message names
      // both. Without one, the form would just appear to do nothing.
      setSession({ phase: 'signed_out', notice: justSignedIn ? 'session_not_kept' : undefined });
    } else {
      setSession({ phase: 'unavailable', error: result.error });
    }
    return false;
  }

  useEffect(() => {
    void loadUser();
  }, []);

  async function signOut(): Promise<void> {
    signingOut.current = true;
    const result = await client.logout();
    signingOut.current = false;
    if (result.ok) {
      setShellMessage(null);
      setSession({ phase: 'signed_out', notice: 'signed_out' });
    } else {
      setShellMessage(text.shell.signOutFailed);
    }
  }

  const screens = options.screens;
  const active = screens.find((screen) => screen.path === path) ?? (path === '/' ? screens[0] : undefined);

  let title: string;
  let focusKey: string;
  switch (session.phase) {
    case 'checking':
    case 'unavailable':
      title = text.product;
      focusKey = session.phase;
      break;
    case 'signed_out':
      title = text.signIn.title;
      focusKey = 'sign-in';
      break;
    case 'signed_in':
      if (session.user.must_change_password) {
        title = text.password.title;
        focusKey = 'password';
      } else {
        title = active ? active.title(text) : text.notFound.title;
        focusKey = `screen:${path}`;
      }
      break;
  }
  useFocusOnChange(focusKey, title, text.product);

  const value: AppValue | null =
    session.phase === 'signed_in'
      ? {
          client,
          text,
          user: session.user,
          homeItems: options.homeItems ?? [],
          timing: { ...defaultTiming, ...options.timing },
          restarted: () => setSession({ phase: 'signed_out', notice: 'restarted' }),
        }
      : null;

  const forced = session.phase === 'signed_in' && session.user.must_change_password;

  return (
    <div class="layout">
      <header class="top">
        <span class="product">{text.product}</span>
        {session.phase === 'signed_in' ? (
          <>
            {!forced ? (
              <nav aria-label={text.shell.navigation}>
                <ul>
                  {screens.map((screen) => (
                    <li key={screen.path}>
                      <a href={hrefFor(screen.path)} aria-current={screen === active ? 'page' : undefined}>
                        {screen.navigation(text)}
                      </a>
                    </li>
                  ))}
                </ul>
              </nav>
            ) : null}
            <div class="account">
              <span class="hint">{text.shell.signedInAs(session.user.user, session.user.role)}</span>
              <button type="button" onClick={() => void signOut()}>
                {text.shell.signOut}
              </button>
            </div>
          </>
        ) : null}
      </header>
      <main>
        {shellMessage !== null ? <Notice kind="error">{shellMessage}</Notice> : null}
        {session.phase === 'checking' ? <Notice kind="info">{text.loading}</Notice> : null}
        {session.phase === 'unavailable' ? (
          <>
            <Notice kind="error">{text.errors[session.error.kind]}</Notice>
            <button type="button" onClick={() => void loadUser()}>
              {text.shell.tryAgain}
            </button>
          </>
        ) : null}
        {session.phase === 'signed_out' ? (
          <SignIn
            client={client}
            text={text}
            notice={session.notice}
            onSignedIn={async () => {
              await loadUser(true);
            }}
          />
        ) : null}
        {forced ? <ChangePassword client={client} text={text} onChanged={() => loadUser()} /> : null}
        {value !== null && !forced ? (
          <AppContext.Provider value={value}>
            {active ? (
              <active.component key={active.path} />
            ) : (
              <div>
                <Heading title={text.notFound.title} />
                <p>{text.notFound.body}</p>
                <p>
                  <a href={hrefFor('/')}>{text.notFound.home}</a>
                </p>
              </div>
            )}
          </AppContext.Provider>
        ) : null}
      </main>
    </div>
  );
}

export function mountPreactInterface(root: Element, options: PreactOptions): void {
  const text = mergeText(englishText, options.text);
  render(<Shell options={options} text={text} />, root);
}
