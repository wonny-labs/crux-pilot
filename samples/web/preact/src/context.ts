// What every screen can reach: the client, the text and the session's actions.
import { createContext } from 'preact';
import type { ComponentType } from 'preact';
import { useContext } from 'preact/hooks';
import type { CruxClient, CurrentUser } from '../../../../web/client/src/index.ts';
import type { Text } from './text.ts';

export interface HomeItemDefinition {
  /** The status item's name, such as app.cycle_count. */
  name: string;
  label: string;
  /** A section title. Items with no section go under the other items. */
  section?: string;
  /** Turns the value into text. Without it the value is shown as it is. */
  format?: (value: string | number | boolean) => string;
}

export interface Timing {
  /** How often the home screen reads the status. */
  statusPollMs: number;
}

export interface AppValue {
  client: CruxClient;
  text: Text;
  user: CurrentUser;
  homeItems: HomeItemDefinition[];
  timing: Timing;
  /** Back to sign-in, after a restart that ended this session. */
  restarted: () => void;
}

export const AppContext = createContext<AppValue | null>(null);

export function useApp(): AppValue {
  const value = useContext(AppContext);
  if (value === null) {
    throw new Error('a screen needs the application context');
  }
  return value;
}

/** A screen on a route of its own, with a link in the navigation. */
export interface Screen {
  /** The route, such as /network. The first screen in the list also answers /. */
  path: string;
  title: (text: Text) => string;
  navigation: (text: Text) => string;
  component: ComponentType;
}
