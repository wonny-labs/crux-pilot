// Network settings: running and saved values, and a save that takes effect at the next boot. The checks
// here are for usability, and the server remains the authority. A session that may not read can still
// submit, because a screen learns what it may do from each route's own answer.
import { useEffect, useRef, useState } from 'preact/hooks';
import { checkIpv4Settings } from '../../../client/src/index.ts';
import type { ApiError, FieldError, Ipv4Settings, NetworkState } from '../../../client/src/index.ts';
import { Field, Heading, Notice } from '../components.tsx';
import { useApp } from '../context.ts';
import type { Screen } from '../context.ts';
import { hrefFor } from '../router.ts';

interface FormValues {
  address: string;
  prefix_length: string;
  gateway: string;
}

type FieldName = keyof FormValues;
type FieldMessages = Partial<Record<FieldName, string>>;

type Result =
  | { kind: 'saved' }
  | { kind: 'failed' }
  | { kind: 'unconfirmed' }
  | { kind: 'unreadable' }
  | { kind: 'not_supported' }
  | { kind: 'denied' }
  | { kind: 'error'; error: ApiError };

const FIELD_ORDER: FieldName[] = ['address', 'prefix_length', 'gateway'];

function formFrom(settings: Ipv4Settings | undefined): FormValues {
  return {
    address: settings?.address ?? '',
    prefix_length: settings ? String(settings.prefix_length) : '',
    gateway: settings?.gateway ?? '',
  };
}

function Values(props: { settings: Ipv4Settings; labels: { address: string; gateway: string; none: string } }) {
  const { settings, labels } = props;
  return (
    <dl class="items">
      <div class="item">
        <dt>{labels.address}</dt>
        <dd>{`${settings.address}/${settings.prefix_length}`}</dd>
      </div>
      <div class="item">
        <dt>{labels.gateway}</dt>
        <dd>{settings.gateway ?? labels.none}</dd>
      </div>
    </dl>
  );
}

function Network() {
  const { client, text } = useApp();
  const t = text.network;
  const [state, setState] = useState<NetworkState | null>(null);
  const [readDenied, setReadDenied] = useState(false);
  const [loadError, setLoadError] = useState<ApiError | null>(null);
  const [form, setForm] = useState<FormValues>(formFrom(undefined));
  const [errors, setErrors] = useState<FieldMessages>({});
  const [pending, setPending] = useState<Ipv4Settings | null>(null);
  const [saving, setSaving] = useState(false);
  const [result, setResult] = useState<Result | null>(null);
  const abort = useRef<AbortController | null>(null);
  const edited = useRef(false);
  const refs = {
    address: useRef<HTMLInputElement>(null),
    prefix_length: useRef<HTMLInputElement>(null),
    gateway: useRef<HTMLInputElement>(null),
  };
  const reviewRef = useRef<HTMLButtonElement>(null);
  const cancelRef = useRef<HTMLButtonElement>(null);
  const focusField = useRef<FieldName | null>(null);
  const focusReview = useRef(false);

  function freshSignal(): AbortSignal {
    abort.current?.abort();
    const controller = new AbortController();
    abort.current = controller;
    return controller.signal;
  }

  async function load(): Promise<void> {
    const signal = freshSignal();
    const read = await client.getNetwork({ signal });
    if (signal.aborted) {
      return;
    }
    if (read.ok) {
      setState(read.value);
      setLoadError(null);
      if (!edited.current) {
        setForm(formFrom(read.value.saved ?? read.value.running));
      }
    } else if (read.error.kind === 'permission_denied') {
      setReadDenied(true);
    } else if (read.error.kind !== 'unauthorized' && read.error.kind !== 'aborted') {
      setLoadError(read.error);
    }
  }

  useEffect(() => {
    void load();
    return () => abort.current?.abort();
  }, []);

  useEffect(() => {
    if (pending !== null && !saving) {
      cancelRef.current?.focus();
    } else if (pending === null && focusReview.current) {
      focusReview.current = false;
      reviewRef.current?.focus();
    } else if (pending === null && focusField.current !== null) {
      refs[focusField.current].current?.focus();
      focusField.current = null;
    }
  }, [pending, saving, errors]);

  function setField(name: FieldName, value: string): void {
    edited.current = true;
    setForm((current) => ({ ...current, [name]: value }));
  }

  function showFieldErrors(found: { name: string; code: string }[]): void {
    const messages: FieldMessages = {};
    for (const { name, code } of found) {
      if (FIELD_ORDER.includes(name as FieldName)) {
        messages[name as FieldName] = t.fieldErrors[code] ?? t.fieldUnknown;
      }
    }
    focusField.current = FIELD_ORDER.find((name) => messages[name] !== undefined) ?? null;
    setErrors(messages);
  }

  function review(event: Event): void {
    event.preventDefault();
    setResult(null);
    const { problems, settings } = checkIpv4Settings(form);
    showFieldErrors(problems);
    if (settings) {
      setPending(settings);
    }
  }

  async function save(settings: Ipv4Settings): Promise<void> {
    setSaving(true);
    const signal = freshSignal();
    const accepted = await client.putNetwork(settings, { signal });
    if (signal.aborted) {
      return;
    }
    if (!accepted.ok) {
      setSaving(false);
      setPending(null);
      switch (accepted.error.kind) {
        case 'invalid_field':
          showFieldErrors((accepted.error.fields ?? []) as FieldError[]);
          break;
        case 'not_supported':
          setResult({ kind: 'not_supported' });
          break;
        case 'permission_denied':
          setResult({ kind: 'denied' });
          break;
        case 'unauthorized':
        case 'aborted':
          break;
        default:
          setResult({ kind: 'error', error: accepted.error });
      }
      return;
    }
    const outcome = await client.watchSave(settings, { signal });
    if (signal.aborted) {
      return;
    }
    setSaving(false);
    setPending(null);
    switch (outcome.kind) {
      case 'saved':
        setState(outcome.state);
        setResult({ kind: 'saved' });
        break;
      case 'failed':
        setState(outcome.state);
        setResult({ kind: 'failed' });
        break;
      case 'unreadable':
        setResult({ kind: 'unreadable' });
        break;
      case 'not_confirmed':
        setResult({ kind: 'unconfirmed' });
        void load();
        break;
      case 'failed_request':
        if (outcome.error.kind !== 'unauthorized') {
          setResult({ kind: 'error', error: outcome.error });
        }
        break;
      case 'aborted':
        break;
    }
  }

  const valueLabels = { address: t.address, gateway: t.gateway, none: t.noGateway };

  return (
    <div>
      <Heading title={t.title} />
      {readDenied ? <Notice kind="warning">{t.readDenied}</Notice> : null}
      {loadError !== null ? <Notice kind="error">{text.errors[loadError.kind]}</Notice> : null}
      {state !== null ? (
        <>
          <section aria-labelledby="network-running">
            <h2 id="network-running">{t.running}</h2>
            {state.running ? (
              <Values settings={state.running} labels={valueLabels} />
            ) : (
              <p>
                {t.noAddress} {t.noAddressAfterBoot}
              </p>
            )}
            <p class="hint">{state.source === 'saved' ? t.sourceSaved : t.sourceDefault}</p>
          </section>
          <section aria-labelledby="network-saved">
            <h2 id="network-saved">{t.saved}</h2>
            {state.saved ? <Values settings={state.saved} labels={valueLabels} /> : <p>{t.nothingSaved}</p>}
          </section>
          {state.reboot_required ? (
            <Notice kind="warning">
              <p>{t.rebootRequired}</p>
              <p>
                <a href={hrefFor('/reboot')}>{t.rebootLink}</a>
              </p>
            </Notice>
          ) : null}
          {state.save?.state === 'pending' && !saving ? <Notice kind="info">{t.savePending}</Notice> : null}
          {state.save?.state === 'failed' && result?.kind !== 'failed' ? (
            <Notice kind="warning">{t.saveFailedState}</Notice>
          ) : null}
        </>
      ) : null}

      {result?.kind === 'saved' ? <Notice kind="success">{t.saved_}</Notice> : null}
      {result?.kind === 'failed' ? <Notice kind="error">{t.saveFailed}</Notice> : null}
      {result?.kind === 'unconfirmed' ? <Notice kind="warning">{t.saveUnconfirmed}</Notice> : null}
      {result?.kind === 'unreadable' ? <Notice kind="warning">{t.saveUnreadable}</Notice> : null}
      {result?.kind === 'not_supported' ? <Notice kind="error">{t.notSupported}</Notice> : null}
      {result?.kind === 'denied' ? <Notice kind="error">{t.saveDenied}</Notice> : null}
      {result?.kind === 'error' ? <Notice kind="error">{text.errors[result.error.kind]}</Notice> : null}

      {pending === null ? (
        <section aria-labelledby="network-form">
          <h2 id="network-form">{t.formTitle}</h2>
          <p>{t.formIntro}</p>
          <form class="narrow" onSubmit={review} noValidate>
            <Field
              id="network-address"
              label={t.address}
              value={form.address}
              onInput={(value) => setField('address', value)}
              inputMode="decimal"
              autocomplete="off"
              required
              error={errors.address}
              inputRef={refs.address}
            />
            <Field
              id="network-prefix"
              label={t.prefix}
              value={form.prefix_length}
              onInput={(value) => setField('prefix_length', value)}
              inputMode="numeric"
              autocomplete="off"
              required
              error={errors.prefix_length}
              inputRef={refs.prefix_length}
            />
            <Field
              id="network-gateway"
              label={t.gateway}
              value={form.gateway}
              onInput={(value) => setField('gateway', value)}
              inputMode="decimal"
              autocomplete="off"
              hint={t.gatewayHint}
              error={errors.gateway}
              inputRef={refs.gateway}
            />
            <button type="submit" class="primary" ref={reviewRef}>
              {t.review}
            </button>
          </form>
        </section>
      ) : (
        <section class="panel" aria-labelledby="network-confirm">
          <h2 id="network-confirm">{t.confirmTitle}</h2>
          <p>{t.confirmSummary(pending.address, pending.prefix_length, pending.gateway ?? t.noGateway)}</p>
          <ul>
            <li>{t.confirmNext(pending.address)}</li>
            <li>{t.confirmAnyReset}</li>
            <li>{t.confirmRecovery}</li>
          </ul>
          {saving ? (
            <Notice kind="info">{t.saving}</Notice>
          ) : (
            <p class="actions">
              <button
                type="button"
                ref={cancelRef}
                onClick={() => {
                  focusReview.current = true;
                  setPending(null);
                }}
              >
                {t.cancel}
              </button>
              <button type="button" class="primary" onClick={() => void save(pending)}>
                {t.confirm}
              </button>
            </p>
          )}
        </section>
      )}
    </div>
  );
}

export const networkScreen: Screen = {
  path: '/network',
  title: (text) => text.network.title,
  navigation: (text) => text.network.navigation,
  component: Network,
};
