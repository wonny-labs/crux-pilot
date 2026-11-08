// Sign-in, through the two-request login. The password stays in this form's state only, and it is dropped
// as soon as the device has judged it.
import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks';
import type { AttemptProgress, CruxClient } from '../../../../../web/client/src/index.ts';
import { Field, Heading, Notice, useCountdown, WaitNotice } from '../components.tsx';
import type { Text } from '../text.ts';

export type SignInNotice = 'session_ended' | 'restarted' | 'signed_out' | 'session_not_kept';

interface Props {
  client: CruxClient;
  text: Text;
  notice: SignInNotice | undefined;
  onSignedIn: () => Promise<void>;
}

type Hold = { kind: 'delayed' | 'busy'; seconds: number };

export function SignIn({ client, text, notice, onSignedIn }: Props) {
  const t = text.signIn;
  const [user, setUser] = useState('');
  const [password, setPassword] = useState('');
  const [working, setWorking] = useState(false);
  const [progress, setProgress] = useState<AttemptProgress['phase'] | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [hold, setHold] = useState<Hold | null>(null);
  const [fieldErrors, setFieldErrors] = useState<{ user?: string; password?: string }>({});
  const [waitLeft, startWait] = useCountdown();
  const userRef = useRef<HTMLInputElement>(null);
  const passwordRef = useRef<HTMLInputElement>(null);
  const abort = useRef<AbortController | null>(null);

  useLayoutEffect(() => {
    userRef.current?.focus();
  }, []);

  useEffect(() => () => abort.current?.abort(), []);

  useEffect(() => {
    if (hold !== null && waitLeft === 0) {
      setHold(null);
    }
  }, [hold, waitLeft]);

  const blocked = working || waitLeft > 0;

  async function submit(event: Event): Promise<void> {
    event.preventDefault();
    if (blocked) {
      return;
    }
    const errors: { user?: string; password?: string } = {};
    if (user === '') {
      errors.user = t.needUser;
    }
    if (password === '') {
      errors.password = t.needPassword;
    }
    setFieldErrors(errors);
    setMessage(null);
    if (errors.user !== undefined) {
      userRef.current?.focus();
      return;
    }
    if (errors.password !== undefined) {
      passwordRef.current?.focus();
      return;
    }
    setWorking(true);
    const controller = new AbortController();
    abort.current = controller;
    const outcome = await client.login(user, password, {
      signal: controller.signal,
      onProgress: (update) => setProgress(update.phase),
    });
    setProgress(null);
    switch (outcome.kind) {
      case 'success':
        setPassword('');
        await onSignedIn();
        break;
      case 'rejected':
        setPassword('');
        setMessage(t.rejected);
        passwordRef.current?.focus();
        break;
      case 'delayed':
        setHold({ kind: 'delayed', seconds: outcome.retryAfterSeconds });
        startWait(outcome.retryAfterSeconds);
        break;
      case 'busy':
        setHold({ kind: 'busy', seconds: outcome.retryAfterSeconds ?? 5 });
        startWait(outcome.retryAfterSeconds ?? 5);
        break;
      case 'lost':
        setMessage(t.lost);
        break;
      case 'timed_out':
        setMessage(t.timedOut);
        break;
      case 'failed':
        setMessage(text.errors[outcome.error.kind]);
        break;
      case 'aborted':
        return;
    }
    setWorking(false);
  }

  const noticeTexts: Record<SignInNotice, string> = {
    session_ended: t.sessionEnded,
    restarted: t.restarted,
    signed_out: t.signedOut,
    session_not_kept: t.sessionNotKept,
  };
  const noticeText = notice === undefined ? null : noticeTexts[notice];

  return (
    <div class="narrow">
      <Heading title={t.title} />
      {noticeText !== null && message === null && hold === null ? (
        <Notice kind={notice === 'session_not_kept' ? 'error' : 'info'}>{noticeText}</Notice>
      ) : null}
      <form onSubmit={(event) => void submit(event)} noValidate>
        <Field
          id="sign-in-user"
          label={t.user}
          value={user}
          onInput={setUser}
          autocomplete="username"
          required
          error={fieldErrors.user}
          inputRef={userRef}
        />
        <Field
          id="sign-in-password"
          label={t.password}
          type="password"
          value={password}
          onInput={setPassword}
          autocomplete="current-password"
          required
          error={fieldErrors.password}
          inputRef={passwordRef}
        />
        <button type="submit" class="primary" aria-disabled={blocked ? 'true' : undefined}>
          {t.submit}
        </button>
      </form>
      {progress !== null ? (
        <Notice kind="info">
          {progress === 'submitting' ? t.submitting : progress === 'verifying' ? t.verifying : t.waiting}
        </Notice>
      ) : null}
      {message !== null ? <Notice kind="error">{message}</Notice> : null}
      {hold !== null && waitLeft > 0 ? (
        <WaitNotice
          kind={hold.kind === 'delayed' ? 'error' : 'warning'}
          message={hold.kind === 'delayed' ? t.delayed(hold.seconds) : t.busy(hold.seconds)}
          left={text.shell.timeLeft(waitLeft)}
        />
      ) : null}
    </div>
  );
}
