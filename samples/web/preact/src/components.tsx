// Small shared pieces. Each keeps the accessible wiring in one place.
import type { ComponentChildren, RefObject } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';

export function Heading({ title }: { title: string }) {
  return (
    <h1 id="page-title" tabIndex={-1}>
      {title}
    </h1>
  );
}

/** An error or warning is announced at once, and anything else when the reader is free. */
export function Notice({ kind, children }: { kind: 'error' | 'warning' | 'info' | 'success'; children: ComponentChildren }) {
  return (
    <div class={`notice notice-${kind}`} role={kind === 'error' ? 'alert' : 'status'}>
      {children}
    </div>
  );
}

export interface FieldProps {
  id: string;
  label: string;
  value: string;
  onInput: (value: string) => void;
  type?: 'text' | 'password';
  autocomplete?: string;
  inputMode?: 'numeric' | 'decimal' | 'text';
  error?: string | undefined;
  hint?: string;
  required?: boolean;
  inputRef?: RefObject<HTMLInputElement | null>;
}

export function Field(props: FieldProps) {
  const errorId = `${props.id}-error`;
  const hintId = `${props.id}-hint`;
  const describedBy = [props.hint ? hintId : '', props.error ? errorId : ''].filter(Boolean).join(' ');
  // These attributes appear only when they apply, so nothing renders them as empty.
  const optional: Record<string, string> = { type: props.type ?? 'text', autocapitalize: 'off' };
  if (props.autocomplete) {
    optional['autocomplete'] = props.autocomplete;
  }
  if (props.inputMode) {
    optional['inputMode'] = props.inputMode;
  }
  if (props.required) {
    optional['aria-required'] = 'true';
  }
  if (props.error) {
    optional['aria-invalid'] = 'true';
  }
  if (describedBy !== '') {
    optional['aria-describedby'] = describedBy;
  }
  return (
    <div class={props.error ? 'field field-invalid' : 'field'}>
      <label for={props.id}>{props.label}</label>
      {props.hint ? (
        <p class="hint" id={hintId}>
          {props.hint}
        </p>
      ) : null}
      <input
        id={props.id}
        ref={props.inputRef ?? null}
        value={props.value}
        spellcheck={false}
        {...optional}
        onInput={(event) => props.onInput((event.currentTarget as HTMLInputElement).value)}
      />
      {props.error ? (
        <p class="field-error" id={errorId}>
          {props.error}
        </p>
      ) : null}
    </div>
  );
}

/**
 * A message about a wait. The message is announced once, with the length of the wait, and the running count
 * sits outside the live region, so that a screen reader does not repeat it every second.
 */
export function WaitNotice(props: { kind: 'error' | 'warning'; message: string; left: string }) {
  return (
    <>
      <Notice kind={props.kind}>{props.message}</Notice>
      <p class="hint">{props.left}</p>
    </>
  );
}

/** A count of seconds that runs down to zero. */
export function useCountdown(): [number, (seconds: number) => void] {
  const [left, setLeft] = useState(0);
  const timer = useRef<ReturnType<typeof setInterval> | undefined>(undefined);
  const stop = (): void => {
    if (timer.current !== undefined) {
      clearInterval(timer.current);
      timer.current = undefined;
    }
  };
  const start = (seconds: number): void => {
    stop();
    setLeft(seconds);
    timer.current = setInterval(() => {
      setLeft((current) => {
        if (current <= 1) {
          stop();
          return 0;
        }
        return current - 1;
      });
    }, 1000);
  };
  useEffect(() => stop, []);
  return [left, start];
}
