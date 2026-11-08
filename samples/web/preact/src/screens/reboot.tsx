// Reboot: a confirmation, then the wait. The running device also answers, so an answer is never success.
// Success is a session that is no longer valid, and a wait that sees no change says it was not confirmed.
import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks';
import type { ApiError, RebootUpdate } from '../../../../../web/client/src/index.ts';
import { Heading, Notice } from '../components.tsx';
import { useApp } from '../context.ts';
import type { Screen } from '../context.ts';

type View =
  | { kind: 'loading' }
  | { kind: 'denied' }
  | { kind: 'idle'; previous?: { failed: true } | { reason: string } }
  | { kind: 'confirm' }
  | { kind: 'requesting' }
  | { kind: 'waiting'; update: Pick<RebootUpdate, 'lastSeen' | 'overdue'> }
  | { kind: 'failed' }
  | { kind: 'refused'; reason: string }
  | { kind: 'not_confirmed'; lastSeen: RebootUpdate['lastSeen'] }
  | { kind: 'error'; error: ApiError };

function Reboot() {
  const { client, text, restarted } = useApp();
  const t = text.reboot;
  const [view, setView] = useState<View>({ kind: 'loading' });
  const [savedAddress, setSavedAddress] = useState<string | null>(null);
  const abort = useRef<AbortController | null>(null);
  const startRef = useRef<HTMLButtonElement>(null);
  const cancelRef = useRef<HTMLButtonElement>(null);
  const focusStartNext = useRef(false);

  function freshSignal(): AbortSignal {
    abort.current?.abort();
    const controller = new AbortController();
    abort.current = controller;
    return controller.signal;
  }

  async function watch(): Promise<void> {
    setView({ kind: 'waiting', update: { lastSeen: 'running', overdue: false } });
    const outcome = await client.watchReboot({
      signal: freshSignal(),
      onUpdate: ({ lastSeen, overdue }) => setView({ kind: 'waiting', update: { lastSeen, overdue } }),
    });
    switch (outcome.kind) {
      case 'restarted':
        restarted();
        break;
      case 'failed':
        setView({ kind: 'failed' });
        break;
      case 'refused':
        setView({ kind: 'refused', reason: outcome.reason });
        break;
      case 'not_confirmed':
        setView({ kind: 'not_confirmed', lastSeen: outcome.lastSeen });
        break;
      case 'failed_request':
        setView(outcome.error.kind === 'permission_denied' ? { kind: 'denied' } : { kind: 'error', error: outcome.error });
        break;
      case 'aborted':
        break;
    }
  }

  async function arrive(): Promise<void> {
    setView({ kind: 'loading' });
    const signal = freshSignal();
    const [state, network] = await Promise.all([client.getRebootState({ signal }), client.getNetwork({ signal })]);
    if (signal.aborted) {
      return;
    }
    if (network.ok && network.value.reboot_required && network.value.saved) {
      setSavedAddress(network.value.saved.address);
    }
    if (state.ok) {
      switch (state.value.state) {
        case 'pending':
          void watch();
          break;
        case 'failed':
          setView({ kind: 'idle', previous: { failed: true } });
          break;
        case 'refused':
          setView({ kind: 'idle', previous: { reason: state.value.reason } });
          break;
        case 'none':
          setView({ kind: 'idle' });
          break;
      }
    } else if (state.error.kind === 'permission_denied') {
      setView({ kind: 'denied' });
    } else if (state.error.kind !== 'unauthorized' && state.error.kind !== 'aborted') {
      setView({ kind: 'error', error: state.error });
    }
  }

  useEffect(() => {
    void arrive();
    return () => abort.current?.abort();
  }, []);

  useLayoutEffect(() => {
    if (view.kind === 'confirm') {
      cancelRef.current?.focus();
    } else if (view.kind === 'idle' && focusStartNext.current) {
      focusStartNext.current = false;
      startRef.current?.focus();
    }
  }, [view.kind]);

  async function request(): Promise<void> {
    setView({ kind: 'requesting' });
    const result = await client.requestReboot({ signal: freshSignal() });
    if (result.ok) {
      await watch();
      return;
    }
    switch (result.error.kind) {
      case 'reboot_refused':
        setView({ kind: 'refused', reason: result.error.reason ?? '' });
        break;
      case 'permission_denied':
        setView({ kind: 'denied' });
        break;
      case 'unauthorized':
      case 'aborted':
        break;
      default:
        setView({ kind: 'error', error: result.error });
    }
  }

  const startButton = (
    <p class="actions">
      <button type="button" class="primary" ref={startRef} onClick={() => setView({ kind: 'confirm' })}>
        {t.start}
      </button>
    </p>
  );

  return (
    <div>
      <Heading title={t.title} />
      {view.kind === 'denied' ? (
        <Notice kind="warning">{t.denied}</Notice>
      ) : (
        <>
          <p>{t.intro}</p>
          {savedAddress !== null ? <Notice kind="warning">{t.savedAddress(savedAddress)}</Notice> : null}
        </>
      )}
      {view.kind === 'idle' ? (
        <>
          {view.previous && 'failed' in view.previous ? <Notice kind="warning">{t.previousFailed}</Notice> : null}
          {view.previous && 'reason' in view.previous ? (
            <Notice kind="warning">{t.previousRefused(view.previous.reason)}</Notice>
          ) : null}
          {startButton}
        </>
      ) : null}
      {view.kind === 'confirm' ? (
        <section class="panel" aria-labelledby="reboot-confirm">
          <h2 id="reboot-confirm">{t.confirmTitle}</h2>
          <p>{t.confirmBody}</p>
          <p class="actions">
            <button
              type="button"
              ref={cancelRef}
              onClick={() => {
                focusStartNext.current = true;
                setView({ kind: 'idle' });
              }}
            >
              {t.cancel}
            </button>
            <button type="button" class="danger" onClick={() => void request()}>
              {t.confirm}
            </button>
          </p>
        </section>
      ) : null}
      {view.kind === 'waiting' ? (
        <Notice kind="info">
          <p>{view.update.lastSeen === 'running' ? t.waitingRunning : t.waitingAway}</p>
          {view.update.overdue ? <p>{t.overdue}</p> : null}
        </Notice>
      ) : null}
      {view.kind === 'failed' ? (
        <>
          <Notice kind="error">{t.failed}</Notice>
          {startButton}
        </>
      ) : null}
      {view.kind === 'refused' ? (
        <>
          <Notice kind="error">{t.refused(view.reason)}</Notice>
          {startButton}
        </>
      ) : null}
      {view.kind === 'not_confirmed' ? (
        <>
          <Notice kind="error">{view.lastSeen === 'running' ? t.notConfirmedRunning : t.notConfirmedAway}</Notice>
          <p class="actions">
            <button type="button" class="primary" onClick={() => void watch()}>
              {t.keepWaiting}
            </button>
            <button type="button" onClick={() => void arrive()}>
              {t.back}
            </button>
          </p>
        </>
      ) : null}
      {view.kind === 'error' ? (
        <>
          <Notice kind="error">{text.errors[view.error.kind]}</Notice>
          {startButton}
        </>
      ) : null}
    </div>
  );
}

export const rebootScreen: Screen = {
  path: '/reboot',
  title: (text) => text.reboot.title,
  navigation: (text) => text.reboot.navigation,
  component: Reboot,
};
