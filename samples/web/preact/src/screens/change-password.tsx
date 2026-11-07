// The forced password change. The session can do nothing else until it completes, and the server enforces
// that. A lost result never counts as success on its own: only the device's own answer does.
import { useEffect, useRef, useState } from 'preact/hooks';
import type { AttemptProgress, CruxClient, FieldError } from '../../../../../web/client/src/index.ts';
import { Field, Heading, Notice, useCountdown, WaitNotice } from '../components.tsx';
import type { Text } from '../text.ts';

interface Props {
  client: CruxClient;
  text: Text;
  /** Reads the session again and leaves this screen once the change no longer applies. */
  onChanged: () => Promise<boolean>;
}

type FieldErrors = { current?: string; next?: string; repeat?: string };
type Hold = { kind: 'delayed' | 'busy'; seconds: number };

export function ChangePassword({ client, text, onChanged }: Props) {
  const t = text.password;
  const [current, setCurrent] = useState('');
  const [next, setNext] = useState('');
  const [repeat, setRepeat] = useState('');
  const [errors, setErrors] = useState<FieldErrors>({});
  const [message, setMessage] = useState<string | null>(null);
  const [working, setWorking] = useState(false);
  const [progress, setProgress] = useState<AttemptProgress['phase'] | null>(null);
  const [hold, setHold] = useState<Hold | null>(null);
  const [waitLeft, startWait] = useCountdown();
  // Set once the outcome of an attempt is unknown, because the meaning of "incorrect" then changes.
  const lostEarlier = useRef(false);
  const queued = useRef(false);
  const currentRef = useRef<HTMLInputElement>(null);
  const nextRef = useRef<HTMLInputElement>(null);
  const abort = useRef<AbortController | null>(null);

  useEffect(() => () => abort.current?.abort(), []);
  useEffect(() => {
    if (hold !== null && waitLeft === 0) {
      setHold(null);
    }
  }, [hold, waitLeft]);

  const blocked = working || waitLeft > 0;

  function applyFields(fields: FieldError[]): void {
    const found: FieldErrors = {};
    for (const field of fields) {
      if (field.name === 'current_password' && field.code === 'incorrect') {
        found.current = lostEarlier.current ? t.incorrectAfterLost : t.incorrect;
      } else if (field.name === 'new_password' && field.code === 'rule_not_met') {
        found.next = t.ruleNotMet;
      } else if (field.name === 'new_password') {
        found.next = t.fieldUnknown;
      } else {
        found.current = t.fieldUnknown;
      }
    }
    setErrors(found);
    if (found.current !== undefined) {
      setCurrent('');
      currentRef.current?.focus();
    } else {
      setNext('');
      setRepeat('');
      nextRef.current?.focus();
    }
  }

  function clearAll(): void {
    setCurrent('');
    setNext('');
    setRepeat('');
    currentRef.current?.focus();
  }

  async function submit(event: Event): Promise<void> {
    event.preventDefault();
    if (blocked) {
      return;
    }
    const found: FieldErrors = {};
    if (current === '') {
      found.current = t.needCurrent;
    }
    if (next === '') {
      found.next = t.needNext;
    }
    if (next !== repeat) {
      found.repeat = t.mismatch;
    }
    setErrors(found);
    setMessage(null);
    if (Object.keys(found).length > 0) {
      (found.current !== undefined ? currentRef : nextRef).current?.focus();
      return;
    }
    setWorking(true);
    queued.current = false;
    const controller = new AbortController();
    abort.current = controller;
    const outcome = await client.changePassword(current, next, {
      signal: controller.signal,
      onProgress: (update) => {
        setProgress(update.phase);
        if (update.phase === 'verifying') {
          queued.current = true;
        }
      },
    });
    setProgress(null);
    switch (outcome.kind) {
      case 'success':
        await onChanged();
        break;
      case 'rejected':
        switch (outcome.code) {
          case 'invalid_field':
            applyFields(outcome.fields ?? []);
            break;
          case 'storage_failed':
            setMessage(t.storageFailed);
            break;
          case 'bad_request':
            setMessage(t.badRequest);
            break;
          case 'not_ready':
            setMessage(t.notReady);
            break;
          case 'unauthorized':
            // The client has reported the lost session, and sign-in takes over.
            return;
          default:
            setMessage(text.errors.unexpected);
        }
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
        // During a forced change the session itself shows whether the earlier change succeeded.
        if (await onChanged()) {
          return;
        }
        lostEarlier.current = true;
        setMessage(t.lost);
        clearAll();
        break;
      case 'timed_out':
        lostEarlier.current = true;
        setMessage(t.timedOut);
        break;
      case 'failed':
        if (outcome.error.kind === 'unauthorized') {
          return;
        }
        // Once queued, or with no answer at all, the attempt may have run with its result never seen.
        if (queued.current || outcome.error.kind === 'network') {
          lostEarlier.current = true;
        }
        setMessage(text.errors[outcome.error.kind]);
        break;
      case 'aborted':
        return;
    }
    setWorking(false);
  }

  return (
    <div class="narrow">
      <Heading title={t.title} />
      <p>{t.intro}</p>
      <form onSubmit={(event) => void submit(event)} noValidate>
        <Field
          id="password-current"
          label={t.current}
          type="password"
          value={current}
          onInput={setCurrent}
          autocomplete="current-password"
          required
          error={errors.current}
          inputRef={currentRef}
        />
        <Field
          id="password-new"
          label={t.next}
          type="password"
          value={next}
          onInput={setNext}
          autocomplete="new-password"
          hint={t.rule}
          required
          error={errors.next}
          inputRef={nextRef}
        />
        <Field
          id="password-repeat"
          label={t.repeat}
          type="password"
          value={repeat}
          onInput={setRepeat}
          autocomplete="new-password"
          required
          error={errors.repeat}
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
