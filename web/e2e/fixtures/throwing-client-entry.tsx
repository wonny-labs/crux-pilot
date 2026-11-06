// The reference with a client whose cookie reader can be made to throw, to show that a call that rejects
// does not leave the configuration screen locked. The client itself never throws for an HTTP or network
// failure, so a request rejects only through a reader or a response of a project's own. A test sets
// `cruxTestThrows` to the number of cookie reads that should throw, and `cruxTestFetch` to make the next
// matching request answer with a response that throws when read.
import '../../reference/src/style.css';
import { mountReferenceInterface } from '../../reference/src/app.tsx';
import { configScreen } from '../../reference/src/screens/config.tsx';
import { homeScreen } from '../../reference/src/screens/home.tsx';

const hooks = globalThis as unknown as {
  cruxTestThrows?: number;
  /** `break-get`: the next read of one parameter. `hold-put`: the next update, held until `cruxTestRelease`. */
  cruxTestFetch?: 'break-get' | 'hold-put' | undefined;
  cruxTestRelease?: (() => void) | undefined;
};

/** Looks like a success, and throws when the client reads its headers. */
function broken(): Response {
  return {
    ok: true,
    status: 200,
    get headers(): Headers {
      throw new Error('the response is broken');
    },
  } as unknown as Response;
}

const root = document.getElementById('app');
if (root !== null) {
  mountReferenceInterface(root, {
    screens: [homeScreen, configScreen],
    client: {
      fetch: async (input, init) => {
        const method = init?.method ?? 'GET';
        if (hooks.cruxTestFetch === 'break-get' && method === 'GET' && String(input).startsWith('/api/v1/config/')) {
          hooks.cruxTestFetch = undefined;
          return broken();
        }
        if (hooks.cruxTestFetch === 'hold-put' && method === 'PUT') {
          hooks.cruxTestFetch = undefined;
          await new Promise<void>((resolve) => {
            hooks.cruxTestRelease = resolve;
          });
          return broken();
        }
        return globalThis.fetch(input, init);
      },
      readCookies: () => {
        if ((hooks.cruxTestThrows ?? 0) > 0) {
          hooks.cruxTestThrows = (hooks.cruxTestThrows ?? 0) - 1;
          throw new Error('the cookie reader failed');
        }
        return document.cookie;
      },
    },
  });
}
