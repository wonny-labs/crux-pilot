// Configuration: the parameters that this session may see, one update at a time in the device's memory, and
// one save that writes them. The list never holds a value, so each readable value is read on its own, and a
// sensitive one is never read. The checks here are for usability, and the device decides what it accepts.
import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks';
import { parseConfigInput } from '../../../client/src/index.ts';
import type { ApiError, ConfigItem, ConfigSaveOutcome, ConfigValue } from '../../../client/src/index.ts';
import { Field, Heading, Notice } from '../components.tsx';
import { useApp } from '../context.ts';
import type { Screen } from '../context.ts';
import { lookup } from '../text.ts';
import type { Text } from '../text.ts';

type Shown = 'loading' | 'value' | 'hidden' | 'sensitive' | 'failed';

interface Row {
  item: ConfigItem;
  shown: Shown;
  value: ConfigValue['value'] | undefined;
  /** What the person typed, or the state of the switch. */
  draft: string | boolean;
  /** The person has typed since the value was read, so a later read must not replace the draft. */
  edited: boolean;
  error: string | undefined;
  working: boolean;
}

type Message = { kind: 'error' | 'warning' | 'success'; text: string };

function draftOf(item: ConfigItem, value: ConfigValue['value'] | undefined): string | boolean {
  if (item.type === 'bool') {
    return value === true;
  }
  return value === undefined ? '' : String(value);
}

function valueText(row: Row, t: Text['config']): string {
  if (typeof row.value === 'boolean') {
    return row.value ? t.on : t.off;
  }
  return row.value === '' ? t.empty : String(row.value);
}

function fresh(item: ConfigItem): Row {
  const shown: Shown = item.sensitive ? 'sensitive' : item.readable ? 'loading' : 'hidden';
  return { item, shown, value: undefined, draft: draftOf(item, undefined), edited: false, error: undefined, working: false };
}

function Configuration() {
  const { client, text } = useApp();
  const t = text.config;
  const [rows, setRows] = useState<Row[] | null>(null);
  const [loadError, setLoadError] = useState<ApiError | null>(null);
  const [message, setMessage] = useState<Message | null>(null);
  const [saving, setSaving] = useState(false);
  const [saveMessage, setSaveMessage] = useState<Message | null>(null);
  // Reading the list and its values is one task, and an update or a save is another, so that one does not stop the other.
  const loading = useRef<AbortController | null>(null);
  const working = useRef<AbortController | null>(null);
  const focusName = useRef<string | null>(null);
  // One update or save at a time, answered at once, because a state change shows only after the next render.
  const inFlight = useRef(false);

  function freshSignal(task: { current: AbortController | null }): AbortSignal {
    task.current?.abort();
    const controller = new AbortController();
    task.current = controller;
    return controller.signal;
  }

  function patch(name: string, change: Partial<Row>): void {
    setRows((current) => current?.map((row) => (row.item.name === name ? { ...row, ...change } : row)) ?? null);
  }

  /** Read one value. A sensitive parameter is never asked for, whatever else happens. */
  async function readValue(item: ConfigItem, signal: AbortSignal): Promise<void> {
    if (item.sensitive || !item.readable) {
      return;
    }
    const read = await client.getConfigItem(item.name, { signal });
    if (signal.aborted) {
      return;
    }
    if (read.ok) {
      setRows(
        (current) =>
          current?.map((row) =>
            row.item.name === item.name
              ? {
                  ...row,
                  shown: 'value',
                  value: read.value.value,
                  draft: row.edited ? row.draft : draftOf(item, read.value.value),
                }
              : row,
          ) ?? null,
      );
    } else if (read.error.kind === 'permission_denied') {
      patch(item.name, { shown: 'hidden' });
    } else if (read.error.kind !== 'unauthorized' && read.error.kind !== 'aborted') {
      patch(item.name, { shown: 'failed' });
    }
  }

  async function load(): Promise<void> {
    const signal = freshSignal(loading);
    setLoadError(null);
    const listed = await client.listConfig({ signal });
    if (signal.aborted) {
      return;
    }
    if (!listed.ok) {
      if (listed.error.kind !== 'unauthorized' && listed.error.kind !== 'aborted') {
        setLoadError(listed.error);
      }
      return;
    }
    setRows(listed.value.items.map(fresh));
    // One at a time, because the server has one thread and other clients share it.
    for (const item of listed.value.items) {
      await readValue(item, signal);
      if (signal.aborted) {
        return;
      }
    }
  }

  useEffect(() => {
    void load();
    return () => {
      loading.current?.abort();
      working.current?.abort();
    };
  }, []);

  useLayoutEffect(() => {
    if (focusName.current !== null) {
      document.getElementById(`config-${focusName.current}`)?.focus();
      focusName.current = null;
    }
  }, [rows]);

  function failureMessage(error: ApiError, denied: string): Message | null {
    switch (error.kind) {
      case 'unauthorized':
      case 'aborted':
        // The client has reported a lost session, and sign-in takes over.
        return null;
      case 'permission_denied':
        return { kind: 'error', text: denied };
      case 'busy':
        return {
          kind: 'error',
          text: error.retryAfterSeconds === undefined ? text.errors.busy : t.busy(error.retryAfterSeconds),
        };
      default:
        return { kind: 'error', text: text.errors[error.kind] };
    }
  }

  async function update(row: Row): Promise<void> {
    const { item } = row;
    if (inFlight.current) {
      return;
    }
    setMessage(null);
    setSaveMessage(null);
    let value: ConfigValue['value'];
    if (item.type === 'bool') {
      value = row.draft === true;
    } else {
      const parsed = parseConfigInput(item.type, String(row.draft));
      if (!parsed.ok) {
        focusName.current = item.name;
        patch(item.name, { error: parsed.problem === 'empty' ? t.needValue : t.needInteger });
        return;
      }
      value = parsed.value;
    }
    inFlight.current = true;
    patch(item.name, { working: true, error: undefined });
    const signal = freshSignal(working);
    const result = await client.putConfigItem(item.name, value, { signal });
    if (signal.aborted) {
      inFlight.current = false;
      return;
    }
    if (result.ok) {
      // The typed value is now the device's, so the read-back may replace the draft.
      patch(item.name, { working: false, item: result.value, edited: false });
      setMessage({ kind: 'success', text: t.updated(item.name) });
      // The device's own value is what is shown, and a session that may not read it keeps what it had.
      await readValue(result.value, signal);
      inFlight.current = false;
      return;
    }
    inFlight.current = false;
    patch(item.name, { working: false });
    const { error } = result;
    if (error.kind === 'invalid_field') {
      const field = (error.fields ?? []).find((entry) => entry.name === 'value');
      focusName.current = item.name;
      patch(item.name, { error: (field && lookup(t.fieldErrors, field.code)) ?? t.fieldUnknown });
    } else if (error.kind === 'not_found') {
      setMessage({ kind: 'error', text: t.updateMissing(item.name) });
      void load();
    } else {
      setMessage(failureMessage(error, t.updateDenied(item.name)));
    }
  }

  function showSaveOutcome(outcome: ConfigSaveOutcome): void {
    switch (outcome.kind) {
      case 'saved':
      case 'failed':
        setRows((current) =>
          (current ?? []).map((row) => ({
            ...row,
            item: outcome.list.items.find((entry) => entry.name === row.item.name) ?? row.item,
          })),
        );
        setSaveMessage(
          outcome.kind === 'saved'
            ? { kind: 'success', text: t.saved }
            : { kind: 'error', text: lookup(t.saveFailed, outcome.code) ?? text.errors.unexpected },
        );
        break;
      case 'not_visible':
        setSaveMessage({ kind: 'warning', text: t.saveNotVisible });
        break;
      case 'not_confirmed':
        setSaveMessage({ kind: 'warning', text: t.saveNotConfirmed });
        void load();
        break;
      case 'failed_request':
        setSaveMessage(failureMessage(outcome.error, t.saveDenied));
        break;
      case 'aborted':
        break;
    }
  }

  async function save(): Promise<void> {
    if (inFlight.current) {
      return;
    }
    inFlight.current = true;
    setMessage(null);
    setSaveMessage(null);
    setSaving(true);
    const signal = freshSignal(working);
    const queued = await client.saveConfig({ signal });
    if (signal.aborted) {
      inFlight.current = false;
      return;
    }
    if (!queued.ok) {
      inFlight.current = false;
      setSaving(false);
      setSaveMessage(failureMessage(queued.error, t.saveDenied));
      return;
    }
    const outcome = await client.watchConfigSave({ signal });
    inFlight.current = false;
    if (signal.aborted) {
      return;
    }
    setSaving(false);
    showSaveOutcome(outcome);
  }

  const anyWritable = (rows ?? []).some((row) => row.item.writable);
  const busy = saving || (rows ?? []).some((row) => row.working);

  return (
    <div>
      <Heading title={t.title} />
      <p>{t.intro}</p>
      {loadError !== null ? (
        <>
          <Notice kind="error">{`${t.loadFailed} ${text.errors[loadError.kind]}`}</Notice>
          <button type="button" onClick={() => void load()}>
            {t.tryAgain}
          </button>
        </>
      ) : null}
      {message !== null ? <Notice kind={message.kind}>{message.text}</Notice> : null}
      {rows !== null && rows.length === 0 ? <p>{t.nothing}</p> : null}
      {rows !== null && rows.length > 0 ? (
        <ul class="params">
          {rows.map((row) => {
            const { item } = row;
            const id = `config-${item.name}`;
            return (
              <li key={item.name}>
                <form
                  onSubmit={(event) => {
                    event.preventDefault();
                    void update(row);
                  }}
                  noValidate
                >
                  <fieldset>
                    <legend>{item.name}</legend>
                    <p class="hint">{`${lookup(t.types, item.type) ?? item.type}. ${lookup(t.activation, item.activation) ?? ''}`}</p>
                    {row.shown === 'value' ? <p>{t.current(valueText(row, t))}</p> : null}
                    {row.shown === 'hidden' ? <p>{t.notShown}</p> : null}
                    {row.shown === 'sensitive' ? <p>{t.sensitive}</p> : null}
                    {row.shown === 'failed' ? <p>{t.readFailed}</p> : null}
                    {item.unsaved ? <p class="unsaved">{t.unsaved}</p> : null}
                    {!item.writable ? <p class="hint">{t.readOnly}</p> : null}
                    {item.writable && !item.sensitive ? (
                      <>
                        {item.type === 'bool' ? (
                          <label class="switch" for={id}>
                            <input
                              id={id}
                              type="checkbox"
                              checked={row.draft === true}
                              onChange={(event) => patch(item.name, { draft: (event.currentTarget as HTMLInputElement).checked, edited: true })}
                            />
                            {t.on}
                          </label>
                        ) : (
                          <Field
                            id={id}
                            label={t.newValue}
                            value={String(row.draft)}
                            onInput={(value) => patch(item.name, { draft: value, edited: true, error: undefined })}
                            inputMode={item.type === 'u32' ? 'numeric' : 'text'}
                            autocomplete="off"
                            error={row.error}
                          />
                        )}
                        <button type="submit" aria-disabled={busy ? 'true' : undefined}>
                          {t.update}
                        </button>
                        {row.working ? <p class="hint">{t.updating}</p> : null}
                      </>
                    ) : null}
                  </fieldset>
                </form>
              </li>
            );
          })}
        </ul>
      ) : null}
      {anyWritable ? (
        <section aria-labelledby="config-save-title">
          <h2 id="config-save-title">{t.saveTitle}</h2>
          <p>{t.saveIntro}</p>
          <button type="button" class="primary" aria-disabled={busy ? 'true' : undefined} onClick={() => void save()}>
            {t.save}
          </button>
          {saving ? <Notice kind="info">{t.saving}</Notice> : null}
          {saveMessage !== null ? <Notice kind={saveMessage.kind}>{saveMessage.text}</Notice> : null}
        </section>
      ) : null}
    </div>
  );
}

export const configScreen: Screen = {
  path: '/config',
  title: (text) => text.config.title,
  navigation: (text) => text.config.navigation,
  component: Configuration,
};
