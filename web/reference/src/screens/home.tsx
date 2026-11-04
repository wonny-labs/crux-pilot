// Home: the status items that this session may read, refreshed at a low rate because the server has one
// thread. It takes what the device sends and hides nothing itself: the server filters.
import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks';
import type { ApiError, StatusItem } from '../../../client/src/index.ts';
import { Heading, Notice } from '../components.tsx';
import { useApp } from '../context.ts';
import type { HomeItemDefinition, Screen } from '../context.ts';
import { lookup } from '../text.ts';
import type { Text } from '../text.ts';

interface Row {
  name: string;
  label: string;
  value: string;
  failed: boolean;
}

function sectionOf(name: string, text: Text): string {
  const sections = text.home.sections;
  if (name.startsWith('version.')) {
    return sections.versions;
  }
  if (name.startsWith('net.')) {
    return sections.network;
  }
  if (name.startsWith('reset.')) {
    return sections.reset;
  }
  if (name.startsWith('system.')) {
    return sections.time;
  }
  return text.home.otherItems;
}

function uptimeText(value: string, text: Text): string {
  // BigInt throws on anything but digits, and one bad value must not take the page down.
  if (!/^[0-9]+$/.test(value)) {
    return value;
  }
  const ms = BigInt(value);
  const minutes = ms / 60_000n;
  return text.home.uptime(minutes / 1440n, (minutes / 60n) % 24n, minutes % 60n);
}

function valueText(item: StatusItem, definition: HomeItemDefinition | undefined, text: Text): string {
  if (item.status !== 'ok') {
    return lookup(text.home.itemStatus, item.status) ?? text.home.unavailable;
  }
  if (definition?.format) {
    return definition.format(item.value);
  }
  if (item.name === 'system.uptime_ms' && item.type === 'u64') {
    return uptimeText(item.value, text);
  }
  if (item.type === 'bool') {
    if (item.name === 'net.link') {
      return item.value ? text.home.up : text.home.down;
    }
    return item.value ? text.home.yes : text.home.no;
  }
  return String(item.value);
}

function group(items: StatusItem[], definitions: HomeItemDefinition[], text: Text): [string, Row[]][] {
  const known = new Map(definitions.map((definition) => [definition.name, definition]));
  const groups = new Map<string, Row[]>();
  for (const item of items) {
    const definition = known.get(item.name);
    const section = definition?.section ?? sectionOf(item.name, text);
    const row: Row = {
      name: item.name,
      label: definition?.label ?? lookup(text.home.items, item.name) ?? item.name,
      value: valueText(item, definition, text),
      failed: item.status !== 'ok',
    };
    groups.set(section, [...(groups.get(section) ?? []), row]);
  }
  return [...groups];
}

function Home() {
  const { client, text, homeItems, timing } = useApp();
  const t = text.home;
  const [items, setItems] = useState<StatusItem[] | null>(null);
  const [updated, setUpdated] = useState<string | null>(null);
  const [problem, setProblem] = useState<ApiError | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const abort = useRef<AbortController | null>(null);
  const running = useRef(false);

  async function read(): Promise<void> {
    if (running.current) {
      return;
    }
    running.current = true;
    clearTimeout(timer.current);
    const controller = new AbortController();
    abort.current = controller;
    const result = await client.getStatus({ signal: controller.signal });
    running.current = false;
    if (controller.signal.aborted) {
      return;
    }
    let delay = timing.statusPollMs;
    if (result.ok) {
      setItems(result.value.items);
      setUpdated(new Date().toLocaleTimeString());
      setProblem(null);
    } else {
      if (result.error.kind === 'unauthorized') {
        return;
      }
      setProblem(result.error);
      delay = Math.max(delay, (result.error.retryAfterSeconds ?? 0) * 1000);
    }
    schedule(delay);
  }

  function schedule(delay: number): void {
    clearTimeout(timer.current);
    timer.current = setTimeout(() => {
      // A hidden page does not need fresh values, and the device has one server thread.
      if (document.hidden) {
        schedule(delay);
      } else {
        void read();
      }
    }, delay);
  }

  useEffect(() => {
    void read();
  }, []);

  // Preact runs the cleanup of a passive effect after paint, when a poll could still fire. A layout effect's
  // cleanup runs as the screen goes, so that no read follows it.
  useLayoutEffect(
    () => () => {
      clearTimeout(timer.current);
      abort.current?.abort();
    },
    [],
  );

  const groups = items === null ? [] : group(items, homeItems, text);

  return (
    <div>
      <Heading title={t.title} />
      {problem !== null ? (
        <Notice kind="error">
          {text.errors[problem.kind]} {updated !== null ? t.stale(updated) : null}
        </Notice>
      ) : null}
      {items !== null && items.length === 0 ? <Notice kind="info">{t.nothing}</Notice> : null}
      {groups.map(([section, rows]) => (
        <section key={section}>
          <h2>{section}</h2>
          <dl class="items">
            {rows.map((row) => (
              <div key={row.name} class={row.failed ? 'item item-failed' : 'item'}>
                <dt>{row.label}</dt>
                <dd>{row.value}</dd>
              </div>
            ))}
          </dl>
        </section>
      ))}
      <p class="toolbar">
        <button type="button" onClick={() => void read()}>
          {t.refresh}
        </button>
        {updated !== null ? <span class="hint">{t.updatedAt(updated)}</span> : null}
      </p>
    </div>
  );
}

export const homeScreen: Screen = {
  path: '/',
  title: (text) => text.home.title,
  navigation: (text) => text.home.navigation,
  component: Home,
};
